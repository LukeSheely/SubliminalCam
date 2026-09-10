#pragma once

#include "core/frame.h"

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace subliminalcam {

class CameraCapture {
 public:
  using FrameCallback = std::function<void(Frame)>;

  explicit CameraCapture(FrameCallback callback);
  ~CameraCapture();
  CameraCapture(const CameraCapture&) = delete;
  CameraCapture& operator=(const CameraCapture&) = delete;

  void start(std::wstring symbolic_link);
  void stop();

 private:
  void run(std::wstring symbolic_link);

  FrameCallback callback_;
  std::atomic_bool stopping_{false};
  std::thread worker_;
};

}  // namespace subliminalcam
