#pragma once

#include <string>
#include <vector>

namespace subliminalcam {

struct CameraDevice {
  std::wstring name;
  std::wstring symbolic_link;
};

std::vector<CameraDevice> enumerate_cameras();

}  // namespace subliminalcam
