#include "core/settings.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace subliminalcam {

AppSettings sanitize(AppSettings settings) {
  settings.blur_radius = std::clamp(settings.blur_radius, 0, 48);
  settings.prompts.duration_ms = std::clamp(settings.prompts.duration_ms, 1000, 10000);
  settings.prompts.interval_ms = std::clamp(settings.prompts.interval_ms, 30000, 3600000);
  settings.prompts.messages.erase(
      std::remove_if(settings.prompts.messages.begin(), settings.prompts.messages.end(),
                     [](const std::wstring& value) { return value.empty(); }),
      settings.prompts.messages.end());
  if (settings.prompts.messages.empty()) settings.prompts.enabled = false;
  return settings;
}

std::filesystem::path settings_path() {
  wchar_t* local_app_data = nullptr;
  std::size_t length = 0;
  if (_wdupenv_s(&local_app_data, &length, L"LOCALAPPDATA") != 0 || !local_app_data) {
    return L"settings.ini";
  }
  std::filesystem::path result = std::filesystem::path(local_app_data) / L"SubliminalCam" / L"settings.ini";
  std::free(local_app_data);
  return result;
}

bool save_settings(const AppSettings& raw, const std::filesystem::path& path) {
  const auto settings = sanitize(raw);
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::wofstream out(path, std::ios::trunc);
  if (!out) return false;
  out << L"version=1\n"
      << L"background=" << static_cast<int>(settings.background_mode) << L"\n"
      << L"blur=" << settings.blur_radius << L"\n"
      << L"mirror=" << (settings.mirror ? 1 : 0) << L"\n"
      << L"image_path=" << settings.image_path << L"\n"
      << L"prompts_enabled=" << (settings.prompts.enabled ? 1 : 0) << L"\n"
      << L"prompt_duration_ms=" << settings.prompts.duration_ms << L"\n"
      << L"prompt_interval_ms=" << settings.prompts.interval_ms << L"\n";
  for (const auto& message : settings.prompts.messages) out << L"prompt=" << message << L"\n";
  return static_cast<bool>(out);
}

AppSettings load_settings(const std::filesystem::path& path) {
  AppSettings settings;
  std::wifstream in(path);
  if (!in) return settings;
  std::wstring line;
  settings.prompts.messages.clear();
  while (std::getline(in, line)) {
    const auto split = line.find(L'=');
    if (split == std::wstring::npos) continue;
    const auto key = line.substr(0, split);
    const auto value = line.substr(split + 1);
    try {
      if (key == L"background") settings.background_mode = static_cast<BackgroundMode>(std::stoi(value));
      else if (key == L"blur") settings.blur_radius = std::stoi(value);
      else if (key == L"mirror") settings.mirror = value == L"1";
      else if (key == L"image_path") settings.image_path = value;
      else if (key == L"prompts_enabled") settings.prompts.enabled = value == L"1";
      else if (key == L"prompt_duration_ms") settings.prompts.duration_ms = std::stoi(value);
      else if (key == L"prompt_interval_ms") settings.prompts.interval_ms = std::stoi(value);
      else if (key == L"prompt") settings.prompts.messages.push_back(value);
    } catch (...) {
      // Preserve defaults for malformed values.
    }
  }
  return sanitize(std::move(settings));
}

}  // namespace subliminalcam
