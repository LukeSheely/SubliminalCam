#include "app/camera_capture.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <objbase.h>
#include <wrl/client.h>

#include <chrono>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

namespace subliminalcam {

using Microsoft::WRL::ComPtr;

namespace {

constexpr int kCaptureWidth = 1280;
constexpr int kCaptureHeight = 720;
constexpr LONG kCaptureStride = kCaptureWidth * static_cast<LONG>(sizeof(Pixel));

bool copy_sample(IMFSample* sample, Frame& frame) {
  ComPtr<IMFMediaBuffer> buffer;
  if (FAILED(sample->GetBufferByIndex(0, &buffer))) return false;

  ComPtr<IMF2DBuffer2> buffer_2d;
  if (SUCCEEDED(buffer.As(&buffer_2d))) {
    BYTE* scanline_zero = nullptr;
    BYTE* buffer_start = nullptr;
    LONG pitch = 0;
    DWORD buffer_length = 0;
    if (FAILED(buffer_2d->Lock2DSize(MF2DBuffer_LockFlags_Read, &scanline_zero, &pitch,
                                     &buffer_start, &buffer_length))) {
      return false;
    }
    const bool valid = scanline_zero && std::abs(pitch) >= kCaptureStride &&
        buffer_length >= static_cast<DWORD>(std::abs(pitch)) * kCaptureHeight;
    if (valid) {
      for (int y = 0; y < kCaptureHeight; ++y) {
        std::memcpy(frame.pixels.data() + static_cast<std::size_t>(y) * kCaptureWidth,
                    scanline_zero + static_cast<std::ptrdiff_t>(y) * pitch,
                    static_cast<std::size_t>(kCaptureStride));
      }
    }
    buffer_2d->Unlock2D();
    return valid;
  }

  buffer.Reset();
  if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) return false;
  BYTE* bytes = nullptr;
  DWORD current_length = 0;
  if (FAILED(buffer->Lock(&bytes, nullptr, &current_length))) return false;
  const std::size_t needed = static_cast<std::size_t>(kCaptureStride) * kCaptureHeight;
  const bool valid = current_length >= needed;
  if (valid) std::memcpy(frame.pixels.data(), bytes, needed);
  buffer->Unlock();
  return valid;
}

}  // namespace

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
  if (SUCCEEDED(hr)) hr = reader_attributes->SetUINT32(
      MF_SOURCE_READER_DISCONNECT_MEDIASOURCE_ON_SHUTDOWN, TRUE);
  if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromMediaSource(source.Get(), reader_attributes.Get(), &reader);
  if (SUCCEEDED(hr)) hr = MFCreateMediaType(&desired_type);
  if (SUCCEEDED(hr)) hr = desired_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  if (SUCCEEDED(hr)) hr = desired_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
  if (SUCCEEDED(hr)) hr = MFSetAttributeSize(desired_type.Get(), MF_MT_FRAME_SIZE,
                                              kCaptureWidth, kCaptureHeight);
  if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(desired_type.Get(), MF_MT_FRAME_RATE, 30, 1);
  if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                      nullptr, desired_type.Get());

  std::array<std::shared_ptr<Frame>, 3> frame_pool{
      std::make_shared<Frame>(kCaptureWidth, kCaptureHeight),
      std::make_shared<Frame>(kCaptureWidth, kCaptureHeight),
      std::make_shared<Frame>(kCaptureWidth, kCaptureHeight)};
  std::size_t next_frame = 0;

  while (SUCCEEDED(hr) && !stopping_) {
    DWORD flags = 0;
    ComPtr<IMFSample> sample;
    hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, nullptr, &sample);
    if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) break;
    if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
      hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                       nullptr, desired_type.Get());
      continue;
    }
    if (!sample) continue;

    std::shared_ptr<Frame> frame;
    for (std::size_t offset = 0; offset < frame_pool.size(); ++offset) {
      const auto index = (next_frame + offset) % frame_pool.size();
      if (frame_pool[index].use_count() == 1) {
        frame = frame_pool[index];
        next_frame = (index + 1) % frame_pool.size();
        break;
      }
    }
    // A slow preview can temporarily retain all pooled frames. Grow only in that
    // exceptional case instead of blocking the physical-camera reader.
    if (!frame) frame = std::make_shared<Frame>(kCaptureWidth, kCaptureHeight);
    if (copy_sample(sample.Get(), *frame)) callback_(std::move(frame));
  }

  if (source) source->Shutdown();
  if (SUCCEEDED(com)) CoUninitialize();
}

}  // namespace subliminalcam
