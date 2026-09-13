#include "app/camera_capture.h"
#include "core/diagnostics.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <objbase.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <utility>

namespace subliminalcam {

using Microsoft::WRL::ComPtr;

namespace {

struct CaptureFormat {
  UINT32 width;
  UINT32 height;
  UINT32 frame_rate;
  GUID subtype;
};

std::wstring subtype_name(const GUID& subtype) {
  if (subtype == MFVideoFormat_MJPG) return L"MJPG";
  if (subtype == MFVideoFormat_YUY2) return L"YUY2";
  if (subtype == MFVideoFormat_NV12) return L"NV12";
  if (subtype == MFVideoFormat_RGB32) return L"RGB32";
  wchar_t text[64]{};
  StringFromGUID2(subtype, text, static_cast<int>(std::size(text)));
  return text;
}

void log_native_modes(IMFSourceReader* reader) {
  for (DWORD index = 0; index < 64; ++index) {
    ComPtr<IMFMediaType> type;
    const HRESULT hr = reader->GetNativeMediaType(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &type);
    if (hr == MF_E_NO_MORE_TYPES) return;
    if (FAILED(hr)) {
      DiagnosticLog::instance().write(DiagnosticLevel::warning, L"Capabilities",
                                       L"Native media-type enumeration stopped", hr);
      return;
    }
    GUID subtype{};
    UINT32 width = 0, height = 0, numerator = 0, denominator = 0;
    type->GetGUID(MF_MT_SUBTYPE, &subtype);
    MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height);
    MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &numerator, &denominator);
    std::wostringstream mode;
    mode << L"[" << index << L"] " << width << L"x" << height << L" @ ";
    if (denominator) mode << static_cast<double>(numerator) / denominator;
    else mode << L"unknown";
    mode << L" FPS " << subtype_name(subtype);
    DiagnosticLog::instance().write(DiagnosticLevel::info, L"Capabilities", mode.str());
  }
}

std::uint8_t clamp_byte(int value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

void convert_yuy2_row(const BYTE* source, Pixel* destination, int width) {
  for (int x = 0; x + 1 < width; x += 2) {
    const int y0 = std::max(0, static_cast<int>(source[0]) - 16);
    const int u = static_cast<int>(source[1]) - 128;
    const int y1 = std::max(0, static_cast<int>(source[2]) - 16);
    const int v = static_cast<int>(source[3]) - 128;
    const auto write_pixel = [u, v](int y, Pixel& pixel) {
      pixel.r = clamp_byte((298 * y + 409 * v + 128) >> 8);
      pixel.g = clamp_byte((298 * y - 100 * u - 208 * v + 128) >> 8);
      pixel.b = clamp_byte((298 * y + 516 * u + 128) >> 8);
      pixel.a = 255;
    };
    write_pixel(y0, destination[x]);
    write_pixel(y1, destination[x + 1]);
    source += 4;
  }
}

HRESULT copy_sample(IMFSample* sample, Frame& frame, LONG fallback_stride,
                    const GUID& subtype) {
  const bool is_yuy2 = subtype == MFVideoFormat_YUY2;
  const LONG source_row_bytes = frame.width * (is_yuy2 ? 2 : static_cast<LONG>(sizeof(Pixel)));
  const auto copy_row = [&](Pixel* destination, const BYTE* source) {
    if (is_yuy2) convert_yuy2_row(source, destination, frame.width);
    else std::memcpy(destination, source, static_cast<std::size_t>(source_row_bytes));
  };
  ComPtr<IMFMediaBuffer> buffer;
  HRESULT hr = sample->GetBufferByIndex(0, &buffer);
  if (FAILED(hr)) return hr;

  ComPtr<IMF2DBuffer2> buffer_2d;
  if (SUCCEEDED(buffer.As(&buffer_2d))) {
    BYTE* scanline_zero = nullptr;
    BYTE* buffer_start = nullptr;
    LONG pitch = 0;
    DWORD buffer_length = 0;
    hr = buffer_2d->Lock2DSize(MF2DBuffer_LockFlags_Read, &scanline_zero, &pitch,
                               &buffer_start, &buffer_length);
    if (FAILED(hr)) return hr;
    const bool valid = scanline_zero && std::abs(pitch) >= source_row_bytes &&
        buffer_length >= static_cast<DWORD>(std::abs(pitch)) * frame.height;
    if (valid) {
      for (int y = 0; y < frame.height; ++y) {
        copy_row(frame.pixels.data() + static_cast<std::size_t>(y) * frame.width,
                 scanline_zero + static_cast<std::ptrdiff_t>(y) * pitch);
      }
    }
    const HRESULT unlock_result = buffer_2d->Unlock2D();
    if (!valid) return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    return unlock_result;
  }

  buffer.Reset();
  hr = sample->ConvertToContiguousBuffer(&buffer);
  if (FAILED(hr)) return hr;
  BYTE* bytes = nullptr;
  DWORD current_length = 0;
  hr = buffer->Lock(&bytes, nullptr, &current_length);
  if (FAILED(hr)) return hr;
  if (fallback_stride == 0) fallback_stride = source_row_bytes;
  const auto absolute_stride = std::abs(fallback_stride);
  const std::size_t needed = static_cast<std::size_t>(absolute_stride) * frame.height;
  const bool valid = current_length >= needed;
  if (valid) {
    const BYTE* first_row = fallback_stride < 0
        ? bytes + static_cast<std::size_t>(frame.height - 1) * absolute_stride : bytes;
    for (int y = 0; y < frame.height; ++y) {
      copy_row(frame.pixels.data() + static_cast<std::size_t>(y) * frame.width,
               first_row + static_cast<std::ptrdiff_t>(y) * fallback_stride);
    }
  }
  const HRESULT unlock_result = buffer->Unlock();
  if (!valid) return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
  return unlock_result;
}

}  // namespace

const wchar_t* capture_stage_name(CaptureStage stage) {
  switch (stage) {
    case CaptureStage::initializing: return L"Initializing";
    case CaptureStage::opening: return L"Opening camera";
    case CaptureStage::negotiating: return L"Negotiating camera format";
    case CaptureStage::streaming: return L"Streaming";
    case CaptureStage::stopped: return L"Stopped";
    case CaptureStage::failed: return L"Failed";
    default: return L"Idle";
  }
}

CameraCapture::CameraCapture(FrameCallback callback, StatusCallback status_callback)
    : callback_(std::move(callback)), status_callback_(std::move(status_callback)) {}

CameraCapture::~CameraCapture() { stop(); }

void CameraCapture::start(std::wstring symbolic_link) {
  stop();
  stopping_ = false;
  frames_received_ = 0;
  copy_failures_ = 0;
  stream_ticks_ = 0;
  media_type_changes_ = 0;
  last_frame_tick_ms_ = 0;
  width_ = 0;
  height_ = 0;
  frame_rate_ = 0;
  worker_ = std::thread([this, link = std::move(symbolic_link)] { run(link); });
}

void CameraCapture::stop() {
  stopping_ = true;
  if (worker_.joinable()) worker_.join();
}

CaptureSnapshot CameraCapture::snapshot() const {
  return {stage_.load(), static_cast<HRESULT>(last_result_.load()), frames_received_.load(),
          copy_failures_.load(), stream_ticks_.load(), media_type_changes_.load(),
          last_frame_tick_ms_.load(), width_.load(), height_.load(), frame_rate_.load()};
}

void CameraCapture::report(CaptureStage stage, HRESULT result, std::wstring detail) {
  stage_ = stage;
  last_result_ = result;
  const auto level = FAILED(result) ? DiagnosticLevel::error :
      (stage == CaptureStage::stopped ? DiagnosticLevel::warning : DiagnosticLevel::info);
  DiagnosticLog::instance().write(level, L"Capture",
      std::wstring(capture_stage_name(stage)) + L": " + detail, result);
  if (status_callback_) status_callback_({stage, result, std::move(detail)});
}

void CameraCapture::run(std::wstring symbolic_link) {
  report(CaptureStage::initializing, S_OK, L"Starting capture worker");
  const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
    report(CaptureStage::failed, com, L"CoInitializeEx failed");
    return;
  }

  ComPtr<IMFAttributes> source_attributes;
  ComPtr<IMFMediaSource> source;
  ComPtr<IMFAttributes> reader_attributes;
  ComPtr<IMFSourceReader> reader;
  ComPtr<IMFMediaType> desired_type;

  HRESULT hr = MFCreateAttributes(&source_attributes, 2);
  std::wstring operation = L"Create camera activation attributes";
  if (SUCCEEDED(hr)) {
    operation = L"Set video-capture source type";
    hr = source_attributes->SetGUID(
      MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  }
  if (SUCCEEDED(hr)) {
    operation = L"Select camera symbolic link";
    hr = source_attributes->SetString(
      MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, symbolic_link.c_str());
  }
  if (SUCCEEDED(hr)) {
    report(CaptureStage::opening, S_OK, L"Opening selected Media Foundation source");
    operation = L"Open selected camera";
    hr = MFCreateDeviceSource(source_attributes.Get(), &source);
  }
  if (SUCCEEDED(hr)) {
    operation = L"Create source-reader attributes";
    hr = MFCreateAttributes(&reader_attributes, 3);
  }
  if (SUCCEEDED(hr)) {
    operation = L"Enable source-reader video processing";
    hr = reader_attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
  }
  if (SUCCEEDED(hr)) {
    operation = L"Enable hardware transforms";
    hr = reader_attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
  }
  if (SUCCEEDED(hr)) {
    operation = L"Configure source shutdown";
    hr = reader_attributes->SetUINT32(
      MF_SOURCE_READER_DISCONNECT_MEDIASOURCE_ON_SHUTDOWN, TRUE);
  }
  if (SUCCEEDED(hr)) {
    operation = L"Create camera source reader";
    hr = MFCreateSourceReaderFromMediaSource(source.Get(), reader_attributes.Get(), &reader);
  }
  if (SUCCEEDED(hr)) log_native_modes(reader.Get());

  UINT32 capture_width = 0;
  UINT32 capture_height = 0;
  UINT32 capture_rate = 0;
  LONG capture_stride = 0;
  GUID capture_subtype = MFVideoFormat_RGB32;
  if (SUCCEEDED(hr)) {
    report(CaptureStage::negotiating, S_OK,
           L"Trying compatible capture modes (native YUY2 480p30 preferred)");
    const std::array candidates{
        CaptureFormat{640, 480, 30, MFVideoFormat_YUY2},
        CaptureFormat{640, 360, 30, MFVideoFormat_YUY2},
        CaptureFormat{640, 480, 30, MFVideoFormat_RGB32},
        CaptureFormat{640, 360, 30, MFVideoFormat_RGB32},
        CaptureFormat{1280, 720, 30, MFVideoFormat_RGB32},
        CaptureFormat{1280, 720, 15, MFVideoFormat_RGB32},
        CaptureFormat{640, 480, 15, MFVideoFormat_YUY2}};
    operation = L"Negotiate every supported fallback capture mode";
    HRESULT negotiation_result = MF_E_INVALIDMEDIATYPE;
    for (const auto candidate : candidates) {
      ComPtr<IMFMediaType> candidate_type;
      negotiation_result = MFCreateMediaType(&candidate_type);
      if (SUCCEEDED(negotiation_result))
        negotiation_result = candidate_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
      if (SUCCEEDED(negotiation_result))
        negotiation_result = candidate_type->SetGUID(MF_MT_SUBTYPE, candidate.subtype);
      if (SUCCEEDED(negotiation_result))
        negotiation_result = MFSetAttributeSize(candidate_type.Get(), MF_MT_FRAME_SIZE,
                                                 candidate.width, candidate.height);
      if (SUCCEEDED(negotiation_result))
        negotiation_result = MFSetAttributeRatio(candidate_type.Get(), MF_MT_FRAME_RATE,
                                                  candidate.frame_rate, 1);
      if (SUCCEEDED(negotiation_result))
        negotiation_result = reader->SetCurrentMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, candidate_type.Get());

      std::wostringstream attempt;
      attempt << subtype_name(candidate.subtype) << L" " << candidate.width << L"x"
              << candidate.height << L" @ " << candidate.frame_rate << L" FPS";
      if (FAILED(negotiation_result)) {
        DiagnosticLog::instance().write(DiagnosticLevel::warning, L"Negotiation",
                                         L"Rejected " + attempt.str(), negotiation_result);
        continue;
      }

      desired_type = candidate_type;
      capture_width = candidate.width;
      capture_height = candidate.height;
      capture_rate = candidate.frame_rate;
      capture_subtype = candidate.subtype;
      ComPtr<IMFMediaType> active_type;
      if (SUCCEEDED(reader->GetCurrentMediaType(
              MF_SOURCE_READER_FIRST_VIDEO_STREAM, &active_type))) {
        MFGetAttributeSize(active_type.Get(), MF_MT_FRAME_SIZE, &capture_width, &capture_height);
        UINT32 rate_numerator = capture_rate;
        UINT32 rate_denominator = 1;
        if (SUCCEEDED(MFGetAttributeRatio(active_type.Get(), MF_MT_FRAME_RATE,
                                          &rate_numerator, &rate_denominator)) && rate_denominator) {
          capture_rate = std::max(1u, rate_numerator / rate_denominator);
        }
        active_type->GetGUID(MF_MT_SUBTYPE, &capture_subtype);
        const UINT32 default_stride = capture_width *
            (capture_subtype == MFVideoFormat_YUY2 ? 2u : static_cast<UINT32>(sizeof(Pixel)));
        capture_stride = static_cast<LONG>(MFGetAttributeUINT32(
            active_type.Get(), MF_MT_DEFAULT_STRIDE, default_stride));
      }
      DiagnosticLog::instance().write(DiagnosticLevel::info, L"Negotiation",
                                       L"Accepted " + attempt.str());
      break;
    }
    hr = negotiation_result;
  }

  if (FAILED(hr)) {
    report(CaptureStage::failed, hr, operation);
    if (source) source->Shutdown();
    if (SUCCEEDED(com)) CoUninitialize();
    return;
  }

  width_ = capture_width;
  height_ = capture_height;
  frame_rate_ = capture_rate;
  report(CaptureStage::negotiating, S_OK,
         L"Using " + subtype_name(capture_subtype) + L" " + std::to_wstring(capture_width) + L"x" +
             std::to_wstring(capture_height) + L" @ " + std::to_wstring(capture_rate) + L" FPS");
  std::array<std::shared_ptr<Frame>, 3> frame_pool{
      std::make_shared<Frame>(capture_width, capture_height),
      std::make_shared<Frame>(capture_width, capture_height),
      std::make_shared<Frame>(capture_width, capture_height)};
  std::size_t next_frame = 0;
  bool reported_streaming = false;

  while (SUCCEEDED(hr) && !stopping_) {
    DWORD flags = 0;
    ComPtr<IMFSample> sample;
    hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, nullptr, &sample);
    if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) break;
    if (flags & MF_SOURCE_READERF_STREAMTICK) ++stream_ticks_;
    if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
      ++media_type_changes_;
      DiagnosticLog::instance().write(DiagnosticLevel::warning, L"Capture",
                                       L"Camera changed its current media type; renegotiating");
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
    if (!frame) frame = std::make_shared<Frame>(capture_width, capture_height);
    const HRESULT copy_result = copy_sample(
        sample.Get(), *frame, capture_stride, capture_subtype);
    if (SUCCEEDED(copy_result)) {
      ++frames_received_;
      last_frame_tick_ms_ = GetTickCount64();
      if (!reported_streaming || stage_.load() != CaptureStage::streaming) {
        reported_streaming = true;
        report(CaptureStage::streaming, S_OK,
               frames_received_.load() == 1 ? L"First complete frame received"
                                            : L"Frame delivery recovered");
      }
      callback_(std::move(frame));
    } else {
      const auto failures = ++copy_failures_;
      if (failures == 1 || failures % 60 == 0) {
        reported_streaming = false;
        report(CaptureStage::failed, copy_result,
               L"Copy camera sample into the negotiated BGRA frame");
      }
    }
  }

  if (source) source->Shutdown();
  if (stopping_) {
    report(CaptureStage::stopped, S_OK, L"Capture stopped by controller");
  } else if (FAILED(hr)) {
    report(CaptureStage::failed, hr, L"ReadSample stopped with an error");
  } else {
    report(CaptureStage::failed, MF_E_END_OF_STREAM, L"Camera stream ended unexpectedly");
  }
  if (SUCCEEDED(com)) CoUninitialize();
}

}  // namespace subliminalcam
