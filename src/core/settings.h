#pragma once

#include "core/frame.h"

#include <filesystem>
#include <string>
#include <vector>

namespace subliminalcam {

enum class BackgroundMode { none, blur, solid, gradient, image };

struct PromptSettings {
  bool enabled{false};
  int duration_ms{1500};
  int interval_ms{60000};
  std::vector<std::wstring> messages{L"Take a breath"};
};

struct AppSettings {
  std::wstring camera_id;
  std::wstring image_path;
  BackgroundMode background_mode{BackgroundMode::none};
  Pixel color_a{38, 31, 22, 255};
  Pixel color_b{87, 63, 41, 255};
  int blur_radius{12};
  bool mirror{true};
  PromptSettings prompts;
};

AppSettings sanitize(AppSettings settings);
std::filesystem::path settings_path();
bool save_settings(const AppSettings& settings, const std::filesystem::path& path);
AppSettings load_settings(const std::filesystem::path& path);

}  // namespace subliminalcam
