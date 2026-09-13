#include <windows.h>

#include "core/diagnostics.h"
#include "shared/frame_transport.h"
#include "app/camera_capture.h"
#include "app/camera_devices.h"

#include <ks.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mfvirtualcamera.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;

// {D0B7E14A-ED7D-4C2F-9B34-517D5AA37190}
constexpr GUID kMediaSourceClsid{
    0xd0b7e14a, 0xed7d, 0x4c2f, {0x9b, 0x34, 0x51, 0x7d, 0x5a, 0xa3, 0x71, 0x90}};
constexpr wchar_t kMediaSourceClsidString[] = L"{D0B7E14A-ED7D-4C2F-9B34-517D5AA37190}";
constexpr wchar_t kFriendlyName[] = L"SubliminalCam";

// Activation attribute understood by the Microsoft-derived media source.
// Synthetic=0 causes the source to expose generated frames until shared-frame transport is enabled.
constexpr GUID kVirtualCameraKind{
    0xc7f7c57b, 0xdf30, 0x41d0, {0xaf, 0xfc, 0x15, 0x20, 0x1c, 0xdf, 0x92, 0x0d}};

std::wstring error_message(HRESULT hr) {
  wchar_t* text = nullptr;
  FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                     FORMAT_MESSAGE_IGNORE_INSERTS,
                 nullptr, static_cast<DWORD>(hr), 0,
                 reinterpret_cast<wchar_t*>(&text), 0, nullptr);
  std::wstring result = text ? text : L"Unknown error";
  LocalFree(text);
  return result;
}

std::filesystem::path executable_directory() {
  std::wstring buffer(32768, L'\0');
  const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  buffer.resize(length);
  return std::filesystem::path(buffer).parent_path();
}

HRESULT register_com_server(const std::filesystem::path& dll_path) {
  if (!std::filesystem::exists(dll_path)) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
  const std::wstring key_path = std::wstring(L"Software\\Classes\\CLSID\\") +
                                kMediaSourceClsidString + L"\\InprocServer32";
  HKEY key = nullptr;
  const auto status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, key_path.c_str(), 0, nullptr, 0,
                                      KEY_SET_VALUE, nullptr, &key, nullptr);
  if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
  const auto value = dll_path.wstring();
  auto write_status = RegSetValueExW(key, nullptr, 0, REG_SZ,
      reinterpret_cast<const BYTE*>(value.c_str()),
      static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
  if (write_status == ERROR_SUCCESS) {
    constexpr wchar_t threading[] = L"Both";
    write_status = RegSetValueExW(key, L"ThreadingModel", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(threading), sizeof(threading));
  }
  RegCloseKey(key);
  return HRESULT_FROM_WIN32(write_status);
}

HRESULT unregister_com_server() {
  const std::wstring key_path = std::wstring(L"Software\\Classes\\CLSID\\") +
                                kMediaSourceClsidString;
  const auto status = RegDeleteTreeW(HKEY_LOCAL_MACHINE, key_path.c_str());
  return status == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(status);
}

HRESULT create_camera(ComPtr<IMFVirtualCamera>& camera) {
  HRESULT hr = MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource,
                                     MFVirtualCameraLifetime_System,
                                     MFVirtualCameraAccess_CurrentUser,
                                     kFriendlyName,
                                     kMediaSourceClsidString,
                                     nullptr, 0, &camera);
  if (SUCCEEDED(hr)) hr = camera->SetUINT32(kVirtualCameraKind, 0);
  return hr;
}

HRESULT install_camera(const std::filesystem::path& dll_path) {
  HRESULT hr = register_com_server(dll_path);
  if (FAILED(hr)) return hr;
  ComPtr<IMFVirtualCamera> camera;
  hr = create_camera(camera);
  if (SUCCEEDED(hr)) hr = camera->Start(nullptr);
  return hr;
}

HRESULT remove_camera() {
  ComPtr<IMFVirtualCamera> camera;
  HRESULT hr = create_camera(camera);
  if (SUCCEEDED(hr)) hr = camera->Remove();
  const auto registry_hr = unregister_com_server();
  return FAILED(hr) ? hr : registry_hr;
}

std::wstring registry_string(HKEY root, const wchar_t* path, const wchar_t* value = nullptr) {
  DWORD bytes = 0;
  if (RegGetValueW(root, path, value, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS ||
      bytes < sizeof(wchar_t)) return {};
  std::wstring result(bytes / sizeof(wchar_t), L'\0');
  if (RegGetValueW(root, path, value, RRF_RT_REG_SZ, nullptr, result.data(), &bytes) != ERROR_SUCCESS)
    return {};
  while (!result.empty() && result.back() == L'\0') result.pop_back();
  return result;
}

std::wstring guid_text(const GUID& value) {
  if (value == MFVideoFormat_MJPG) return L"MJPG";
  if (value == MFVideoFormat_YUY2) return L"YUY2";
  if (value == MFVideoFormat_NV12) return L"NV12";
  if (value == MFVideoFormat_RGB32) return L"RGB32";
  wchar_t text[64]{};
  StringFromGUID2(value, text, static_cast<int>(std::size(text)));
  return text;
}

std::wstring selected_camera_id() {
  wchar_t* local_app_data = nullptr;
  std::size_t length = 0;
  if (_wdupenv_s(&local_app_data, &length, L"LOCALAPPDATA") != 0 || !local_app_data) return {};
  const auto path = std::filesystem::path(local_app_data) / L"SubliminalCam" / L"settings.ini";
  std::free(local_app_data);
  std::wifstream input(path);
  std::wstring line;
  while (std::getline(input, line)) {
    constexpr wchar_t prefix[] = L"camera_id=";
    if (line.starts_with(prefix)) return line.substr(std::size(prefix) - 1);
  }
  return {};
}

void print_native_types(IMFActivate* activation, std::wostream& output) {
  ComPtr<IMFMediaSource> source;
  HRESULT hr = activation->ActivateObject(IID_PPV_ARGS(&source));
  if (FAILED(hr)) {
    output << L"    OPEN FAILED: " << subliminalcam::hresult_message(hr)
           << L" (camera may be busy)\n";
    return;
  }
  ComPtr<IMFSourceReader> reader;
  hr = MFCreateSourceReaderFromMediaSource(source.Get(), nullptr, &reader);
  if (FAILED(hr)) {
    output << L"    SOURCE READER FAILED: " << subliminalcam::hresult_message(hr) << L"\n";
    source->Shutdown();
    return;
  }
  output << L"    Native modes:\n";
  for (DWORD index = 0; index < 64; ++index) {
    ComPtr<IMFMediaType> type;
    hr = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &type);
    if (hr == MF_E_NO_MORE_TYPES) break;
    if (FAILED(hr)) {
      output << L"      enumeration failed: " << subliminalcam::hresult_message(hr) << L"\n";
      break;
    }
    GUID subtype{};
    UINT32 width = 0, height = 0, rate_numerator = 0, rate_denominator = 0;
    type->GetGUID(MF_MT_SUBTYPE, &subtype);
    MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height);
    MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &rate_numerator, &rate_denominator);
    output << L"      [" << index << L"] " << width << L"x" << height << L" @ ";
    if (rate_denominator) output << std::fixed << std::setprecision(2)
                                 << static_cast<double>(rate_numerator) / rate_denominator;
    else output << L"unknown";
    output << L" " << guid_text(subtype) << L"\n";
  }
  source->Shutdown();
  activation->ShutdownObject();
}

void print_transport(std::wostream& output) {
  HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, subliminalcam::kFrameMappingName);
  if (!mapping) {
    output << L"Shared frame transport: NOT OPEN (controller has not published)\n";
    return;
  }
  const auto* view = static_cast<const std::byte*>(MapViewOfFile(
      mapping, FILE_MAP_READ, 0, 0, subliminalcam::kFrameMappingBytes));
  if (!view) {
    output << L"Shared frame transport: MAP FAILED — " << GetLastError() << L"\n";
    CloseHandle(mapping);
    return;
  }
  const auto* header = reinterpret_cast<const subliminalcam::SharedFrameHeader*>(view);
  const LONG before = header->sequence;
  MemoryBarrier();
  const auto magic = header->magic;
  const auto version = header->version;
  const auto width = header->width;
  const auto height = header->height;
  const auto bytes = header->data_bytes;
  const auto timestamp = header->timestamp_100ns;
  MemoryBarrier();
  const LONG after = header->sequence;
  output << L"Shared frame transport: "
         << ((before == after && (before & 1) == 0 && magic == subliminalcam::kFrameMagic)
                 ? L"VALID" : L"INVALID OR MID-WRITE") << L"\n"
         << L"  mapping=" << subliminalcam::kFrameMappingName << L"\n"
         << L"  version=" << version << L", sequence=" << after << L", frame="
         << width << L"x" << height << L", bytes=" << bytes << L"\n";
  const auto now_100ns = GetTickCount64() * 10000ull;
  if (timestamp && timestamp <= now_100ns)
    output << L"  last publish age=" << (now_100ns - timestamp) / 10000ull << L" ms\n";
  UnmapViewOfFile(view);
  CloseHandle(mapping);
}

HRESULT diagnose(std::wostream& output) {
  output << L"SubliminalCam standalone diagnostics\n"
         << L"Log: " << subliminalcam::diagnostic_log_path().wstring() << L"\n";
  const auto build = registry_string(HKEY_LOCAL_MACHINE,
      L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber");
  output << L"Windows build: " << (build.empty() ? L"unknown" : build) << L"\n";
  const auto privacy = registry_string(HKEY_LOCAL_MACHINE,
      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\webcam",
      L"Value");
  output << L"Machine camera privacy: " << (privacy.empty() ? L"not explicitly set" : privacy) << L"\n";

  const auto registered_dll = registry_string(HKEY_LOCAL_MACHINE,
      L"SOFTWARE\\Classes\\CLSID\\{D0B7E14A-ED7D-4C2F-9B34-517D5AA37190}\\InprocServer32");
  output << L"Virtual-camera COM registration: "
         << (registered_dll.empty() ? L"MISSING" : registered_dll) << L"\n";
  if (!registered_dll.empty())
    output << L"Registered DLL exists: " << (std::filesystem::exists(registered_dll) ? L"yes" : L"NO") << L"\n";
  print_transport(output);

  ComPtr<IMFAttributes> attributes;
  HRESULT hr = MFCreateAttributes(&attributes, 1);
  if (SUCCEEDED(hr)) hr = attributes->SetGUID(
      MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  IMFActivate** devices = nullptr;
  UINT32 count = 0;
  if (SUCCEEDED(hr)) hr = MFEnumDeviceSources(attributes.Get(), &devices, &count);
  if (FAILED(hr)) {
    output << L"Camera enumeration FAILED: " << subliminalcam::hresult_message(hr) << L"\n";
    return hr;
  }
  output << L"Media Foundation cameras: " << count << L"\n";
  const auto selected = selected_camera_id();
  bool selected_found = false;
  for (UINT32 index = 0; index < count; ++index) {
    wchar_t* name = nullptr;
    wchar_t* link = nullptr;
    UINT32 ignored = 0;
    devices[index]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &ignored);
    devices[index]->GetAllocatedString(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link, &ignored);
    const std::wstring device_name = name ? name : L"<unnamed>";
    const std::wstring device_link = link ? link : L"";
    selected_found = selected_found || (!selected.empty() && device_link == selected);
    output << (device_link == selected ? L"  * " : L"    ") << L"[" << index << L"] "
           << device_name << L"\n      " << device_link << L"\n";
    if (device_name != kFriendlyName) print_native_types(devices[index], output);
    CoTaskMemFree(name);
    CoTaskMemFree(link);
    devices[index]->Release();
  }
  CoTaskMemFree(devices);
  output << L"Selected physical camera found: "
         << (selected.empty() ? L"no saved selection" : (selected_found ? L"yes" : L"NO")) << L"\n";
  return count ? S_OK : MF_E_NOT_FOUND;
}

HRESULT probe_camera(std::wostream& output) {
  const auto enumeration = subliminalcam::enumerate_cameras_detailed();
  if (FAILED(enumeration.result)) {
    output << L"Camera enumeration failed: "
           << subliminalcam::hresult_message(enumeration.result) << L"\n";
    return enumeration.result;
  }
  if (enumeration.devices.empty()) {
    output << L"No physical camera is available.\n";
    return MF_E_NOT_FOUND;
  }
  const auto selected_id = selected_camera_id();
  const auto selected = std::find_if(enumeration.devices.begin(), enumeration.devices.end(),
      [&selected_id](const auto& device) { return device.symbolic_link == selected_id; });
  const auto& device = selected == enumeration.devices.end() ? enumeration.devices.front() : *selected;
  output << L"Probing: " << device.name << L"\n";

  std::mutex mutex;
  std::condition_variable changed;
  std::atomic_uint64_t delivered{};
  subliminalcam::CaptureStatus last_status;
  subliminalcam::CameraCapture capture(
      [&](std::shared_ptr<subliminalcam::Frame>) {
        ++delivered;
        changed.notify_all();
      },
      [&](const subliminalcam::CaptureStatus& status) {
        {
          std::scoped_lock lock(mutex);
          last_status = status;
        }
        output << L"  " << subliminalcam::capture_stage_name(status.stage)
               << L": " << status.detail;
        if (FAILED(status.result))
          output << L" — " << subliminalcam::hresult_message(status.result);
        output << L"\n";
        changed.notify_all();
      });
  capture.start(device.symbolic_link);
  {
    std::unique_lock lock(mutex);
    changed.wait_for(lock, std::chrono::seconds(6), [&] {
      return delivered.load() >= 10 || last_status.stage == subliminalcam::CaptureStage::failed;
    });
  }
  const auto snapshot = capture.snapshot();
  capture.stop();
  output << L"Probe summary: " << delivered.load() << L" frames, "
         << snapshot.copy_failures << L" copy failures, " << snapshot.stream_ticks
         << L" empty stream ticks, negotiated " << snapshot.width << L"x" << snapshot.height
         << L" @ " << snapshot.frame_rate << L" FPS.\n";
  if (delivered.load() == 0) {
    output << L"Probe FAILED at " << subliminalcam::capture_stage_name(snapshot.stage)
           << L": " << subliminalcam::hresult_message(snapshot.last_result) << L"\n";
    return FAILED(snapshot.last_result) ? snapshot.last_result : E_FAIL;
  }
  output << L"Probe PASSED: physical frames reached the BGRA capture pipeline.\n";
  return S_OK;
}

void usage() {
  std::wcout << L"SubliminalCamVcamCtl\n\n"
             << L"  SubliminalCamVcamCtl install [path-to-VirtualCameraMediaSource.dll]\n"
             << L"  SubliminalCamVcamCtl remove\n"
             << L"  SubliminalCamVcamCtl diagnose [optional-report-path]\n"
             << L"  SubliminalCamVcamCtl probe\n\n"
             << L"Install/remove require an elevated terminal. Diagnose is read-only. "
                L"Installation creates a current-user camera; "
                L"elevation is needed only to register the COM media-source DLL.\n";
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) {
    usage();
    return 2;
  }
  const auto com_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(com_hr)) return 3;
  const auto mf_hr = MFStartup(MF_VERSION);
  if (FAILED(mf_hr)) {
    CoUninitialize();
    return 4;
  }

  HRESULT hr = E_INVALIDARG;
  const std::wstring command = argv[1];
  if (command == L"install") {
    const auto dll = argc >= 3 ? std::filesystem::path(argv[2])
                               : executable_directory() / L"VirtualCameraMediaSource.dll";
    hr = install_camera(std::filesystem::absolute(dll));
  } else if (command == L"remove") {
    hr = remove_camera();
  } else if (command == L"diagnose") {
    std::wostringstream report;
    hr = diagnose(report);
    std::wcout << report.str();
    if (argc >= 3) {
      std::wofstream file(argv[2], std::ios::trunc);
      if (file) file << report.str();
      else hr = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
    }
  } else if (command == L"probe") {
    hr = probe_camera(std::wcout);
  } else {
    usage();
  }

  if (FAILED(hr)) {
    std::wcerr << L"Operation failed (0x" << std::hex << static_cast<unsigned long>(hr)
               << L"): " << error_message(hr) << L"\n";
    if (HRESULT_CODE(hr) == ERROR_ACCESS_DENIED) {
      std::wcerr << L"Open Terminal as administrator and retry.\n";
    }
  } else {
    if (command == L"install") std::wcout << L"Virtual camera installed.\n";
    else if (command == L"remove") std::wcout << L"Virtual camera removed.\n";
  }

  MFShutdown();
  CoUninitialize();
  return SUCCEEDED(hr) ? 0 : 1;
}
