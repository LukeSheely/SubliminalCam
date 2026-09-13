#pragma once

#include <filesystem>
#include <string>

namespace subliminalcam {

struct AppSettings {
  std::wstring camera_id;
  std::wstring message{L"Take a breath"};
  std::wstring image_path;
  bool mirror{true};
  bool message_repeat{false};
  int message_interval_centiseconds{500};
  int message_duration_centiseconds{10};
  bool image_repeat{false};
  int image_interval_centiseconds{500};
  int image_duration_centiseconds{10};
};

std::filesystem::path settings_path();
bool save_settings(const AppSettings& settings, const std::filesystem::path& path);
AppSettings load_settings(const std::filesystem::path& path);

}  // namespace subliminalcam
