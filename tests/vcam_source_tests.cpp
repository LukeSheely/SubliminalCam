#include <windows.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <iostream>

namespace {

using Microsoft::WRL::ComPtr;
constexpr GUID kMediaSourceClsid{
    0xd0b7e14a, 0xed7d, 0x4c2f, {0x9b, 0x34, 0x51, 0x7d, 0x5a, 0xa3, 0x71, 0x90}};
constexpr GUID kVirtualCameraKind{
    0xc7f7c57b, 0xdf30, 0x41d0, {0xaf, 0xfc, 0x15, 0x20, 0x1c, 0xdf, 0x92, 0x0d}};
using DllGetClassObjectFn = HRESULT(__stdcall*)(REFCLSID, REFIID, void**);

int fail(const wchar_t* operation, HRESULT hr) {
  std::wcerr << operation << L" failed: 0x" << std::hex << static_cast<unsigned long>(hr) << L"\n";
  return 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc != 2) return 2;
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr)) return fail(L"CoInitializeEx", hr);
  hr = MFStartup(MF_VERSION);
  if (FAILED(hr)) return fail(L"MFStartup", hr);

  const auto module = LoadLibraryW(argv[1]);
  if (!module) return fail(L"LoadLibrary", HRESULT_FROM_WIN32(GetLastError()));
  const auto get_class_object = reinterpret_cast<DllGetClassObjectFn>(
      GetProcAddress(module, "DllGetClassObject"));
  if (!get_class_object) return fail(L"GetProcAddress", HRESULT_FROM_WIN32(GetLastError()));

  ComPtr<IClassFactory> factory;
  hr = get_class_object(kMediaSourceClsid, IID_PPV_ARGS(&factory));
  if (FAILED(hr)) return fail(L"DllGetClassObject", hr);
  ComPtr<IMFActivate> activation;
  hr = factory->CreateInstance(nullptr, IID_PPV_ARGS(&activation));
  if (FAILED(hr)) return fail(L"CreateInstance", hr);
  hr = activation->SetUINT32(kVirtualCameraKind, 0);
  if (FAILED(hr)) return fail(L"SetUINT32", hr);
  ComPtr<IMFMediaSource> source;
  hr = activation->ActivateObject(IID_PPV_ARGS(&source));
  if (FAILED(hr)) return fail(L"ActivateObject", hr);

  ComPtr<IMFPresentationDescriptor> presentation;
  hr = source->CreatePresentationDescriptor(&presentation);
  if (FAILED(hr)) return fail(L"CreatePresentationDescriptor", hr);
  BOOL selected = FALSE;
  ComPtr<IMFStreamDescriptor> stream_descriptor;
  hr = presentation->GetStreamDescriptorByIndex(0, &selected, &stream_descriptor);
  if (FAILED(hr)) return fail(L"GetStreamDescriptorByIndex", hr);
  DWORD stream_id = 0;
  hr = stream_descriptor->GetStreamIdentifier(&stream_id);
  if (FAILED(hr)) return fail(L"GetStreamIdentifier", hr);
  ComPtr<IMFSampleAllocatorControl> allocator_control;
  hr = source.As(&allocator_control);
  if (FAILED(hr)) return fail(L"IMFSampleAllocatorControl", hr);
  DWORD input_stream_id = 0;
  MFSampleAllocatorUsage usage{};
  hr = allocator_control->GetAllocatorUsage(stream_id, &input_stream_id, &usage);
  if (FAILED(hr)) return fail(L"GetAllocatorUsage", hr);
  if (usage == MFSampleAllocatorUsage_UsesProvidedAllocator) {
    ComPtr<IMFVideoSampleAllocator> allocator;
    hr = MFCreateVideoSampleAllocatorEx(IID_PPV_ARGS(&allocator));
    if (FAILED(hr)) return fail(L"MFCreateVideoSampleAllocatorEx", hr);
    hr = allocator_control->SetDefaultAllocator(stream_id, allocator.Get());
    if (FAILED(hr)) return fail(L"SetDefaultAllocator", hr);
  }
  ComPtr<IMFSourceReader> reader;
  hr = MFCreateSourceReaderFromMediaSource(source.Get(), nullptr, &reader);
  if (FAILED(hr)) return fail(L"MFCreateSourceReaderFromMediaSource", hr);
  hr = reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
  if (FAILED(hr)) return fail(L"SetStreamSelection", hr);
  ComPtr<IMFMediaType> media_type;
  hr = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &media_type);
  if (FAILED(hr)) return fail(L"GetNativeMediaType", hr);
  hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, media_type.Get());
  if (FAILED(hr)) return fail(L"SetCurrentMediaType", hr);

  ComPtr<IMFSample> sample;
  DWORD actual_stream = 0;
  DWORD flags = 0;
  LONGLONG timestamp = 0;
  hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &actual_stream, &flags,
                          &timestamp, &sample);
  if (FAILED(hr) || !sample) return fail(L"ReadSample", FAILED(hr) ? hr : E_UNEXPECTED);
  sample.Reset();
  media_type.Reset();
  reader.Reset();
  allocator_control.Reset();
  stream_descriptor.Reset();
  presentation.Reset();
  source->Shutdown();
  source.Reset();
  activation->ShutdownObject();
  activation.Reset();
  factory.Reset();
  FreeLibrary(module);
  MFShutdown();
  CoUninitialize();
  std::wcout << L"Virtual camera media source produced a valid sample.\n";
  return 0;
}
