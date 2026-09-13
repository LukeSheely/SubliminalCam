#pragma once

#include <filesystem>
#include <string>

namespace subliminalcam {

struct AppSettings {
  std::wstring camera_id;
  std::wstring message{L"Take a breath"};
  std::wstring image_path;
  bool mirror{true};
};

std::filesystem::path settings_path();
bool save_settings(const AppSettings& settings, const std::filesystem::path& path);
AppSettings load_settings(const std::filesystem::path& path);

}  // namespace subliminalcam
