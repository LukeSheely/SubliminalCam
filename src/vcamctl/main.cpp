#include <windows.h>

#include <ks.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfvirtualcamera.h>
#include <wrl/client.h>

#include <filesystem>
#include <iostream>
#include <string>

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

void usage() {
  std::wcout << L"SubliminalCamVcamCtl\n\n"
             << L"  SubliminalCamVcamCtl install [path-to-VirtualCameraMediaSource.dll]\n"
             << L"  SubliminalCamVcamCtl remove\n\n"
             << L"Run from an elevated terminal. Installation creates a current-user camera; "
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
    std::wcout << (command == L"install" ? L"Virtual camera installed.\n"
                                           : L"Virtual camera removed.\n");
  }

  MFShutdown();
  CoUninitialize();
  return SUCCEEDED(hr) ? 0 : 1;
}
