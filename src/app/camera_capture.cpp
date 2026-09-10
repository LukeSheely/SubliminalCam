#include "app/camera_capture.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <objbase.h>
#include <wrl/client.h>

#include <chrono>
#include <cstring>
#include <utility>

namespace subliminalcam {

using Microsoft::WRL::ComPtr;

CameraCapture::CameraCapture(FrameCallback callback) : callback_(std::move(callback)) {}

CameraCapture::~CameraCapture() { stop(); }

void CameraCapture::start(std::wstring symbolic_link) {
  stop();
  stopping_ = false;
  worker_ = std::thread([this, link = std::move(symbolic_link)] { run(link); });
}

void CameraCapture::stop() {
  stopping_ = true;
  if (worker_.joinable()) worker_.join();
}

void CameraCapture::run(std::wstring symbolic_link) {
  const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(com) && com != RPC_E_CHANGED_MODE) return;

  ComPtr<IMFAttributes> source_attributes;
  ComPtr<IMFMediaSource> source;
  ComPtr<IMFAttributes> reader_attributes;
  ComPtr<IMFSourceReader> reader;
  ComPtr<IMFMediaType> desired_type;

  HRESULT hr = MFCreateAttributes(&source_attributes, 2);
  if (SUCCEEDED(hr)) hr = source_attributes->SetGUID(
      MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  if (SUCCEEDED(hr)) hr = source_attributes->SetString(
      MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, symbolic_link.c_str());
  if (SUCCEEDED(hr)) hr = MFCreateDeviceSource(source_attributes.Get(), &source);
  if (SUCCEEDED(hr)) hr = MFCreateAttributes(&reader_attributes, 3);
  if (SUCCEEDED(hr)) hr = reader_attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
  if (SUCCEEDED(hr)) hr = reader_attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
  if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromMediaSource(source.Get(), reader_attributes.Get(), &reader);
  if (SUCCEEDED(hr)) hr = MFCreateMediaType(&desired_type);
  if (SUCCEEDED(hr)) hr = desired_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  if (SUCCEEDED(hr)) hr = desired_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
  if (SUCCEEDED(hr)) hr = MFSetAttributeSize(desired_type.Get(), MF_MT_FRAME_SIZE, 1280, 720);
  if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(desired_type.Get(), MF_MT_FRAME_RATE, 30, 1);
  if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                      nullptr, desired_type.Get());

  while (SUCCEEDED(hr) && !stopping_) {
    DWORD flags = 0;
    ComPtr<IMFSample> sample;
    hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, nullptr, &sample);
    if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) break;
    if (!sample) continue;

    ComPtr<IMFMediaBuffer> buffer;
    hr = sample->ConvertToContiguousBuffer(&buffer);
    if (FAILED(hr)) break;
    BYTE* bytes = nullptr;
    DWORD current_length = 0;
    hr = buffer->Lock(&bytes, nullptr, &current_length);
    if (FAILED(hr)) break;
    constexpr int width = 1280;
    constexpr int height = 720;
    constexpr std::size_t needed = static_cast<std::size_t>(width) * height * 4;
    if (current_length >= needed) {
      Frame frame(width, height);
      std::memcpy(frame.pixels.data(), bytes, needed);
      callback_(std::move(frame));
    }
    buffer->Unlock();
  }

  if (source) source->Shutdown();
  if (SUCCEEDED(com)) CoUninitialize();
}

}  // namespace subliminalcam
