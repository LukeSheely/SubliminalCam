#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace subliminalcam {

struct CameraDevice {
  std::wstring name;
  std::wstring symbolic_link;
};

struct CameraEnumerationResult {
  std::vector<CameraDevice> devices;
  HRESULT result{S_OK};
  std::uint32_t excluded_virtual_devices{};
};

CameraEnumerationResult enumerate_cameras_detailed();
std::vector<CameraDevice> enumerate_cameras();

}  // namespace subliminalcam
