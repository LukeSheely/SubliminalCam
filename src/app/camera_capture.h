#pragma once

#include "core/frame.h"

#include <windows.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace subliminalcam {

enum class CaptureStage { idle, initializing, opening, negotiating, streaming, stopped, failed };

struct CaptureStatus {
  CaptureStage stage{CaptureStage::idle};
  HRESULT result{S_OK};
  std::wstring detail;
};

struct CaptureSnapshot {
  CaptureStage stage{CaptureStage::idle};
  HRESULT last_result{S_OK};
  std::uint64_t frames_received{};
  std::uint64_t copy_failures{};
  std::uint64_t stream_ticks{};
  std::uint64_t media_type_changes{};
  std::uint64_t last_frame_tick_ms{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t frame_rate{};
};

const wchar_t* capture_stage_name(CaptureStage stage);

class CameraCapture {
 public:
  using FrameCallback = std::function<void(std::shared_ptr<Frame>)>;
  using StatusCallback = std::function<void(const CaptureStatus&)>;

  explicit CameraCapture(FrameCallback callback, StatusCallback status_callback = {});
  ~CameraCapture();
  CameraCapture(const CameraCapture&) = delete;
  CameraCapture& operator=(const CameraCapture&) = delete;

  void start(std::wstring symbolic_link);
  void stop();
  CaptureSnapshot snapshot() const;

 private:
  void run(std::wstring symbolic_link);
  void report(CaptureStage stage, HRESULT result, std::wstring detail);

  FrameCallback callback_;
  StatusCallback status_callback_;
  std::atomic_bool stopping_{false};
  std::atomic<CaptureStage> stage_{CaptureStage::idle};
  std::atomic_long last_result_{S_OK};
  std::atomic_uint64_t frames_received_{};
  std::atomic_uint64_t copy_failures_{};
  std::atomic_uint64_t stream_ticks_{};
  std::atomic_uint64_t media_type_changes_{};
  std::atomic_uint64_t last_frame_tick_ms_{};
  std::atomic_uint32_t width_{};
  std::atomic_uint32_t height_{};
  std::atomic_uint32_t frame_rate_{};
  std::thread worker_;
};

}  // namespace subliminalcam
