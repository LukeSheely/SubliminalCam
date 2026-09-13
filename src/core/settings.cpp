#include "core/settings.h"

#include <cstdlib>
#include <fstream>

namespace subliminalcam {

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

bool save_settings(const AppSettings& settings, const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::wofstream out(path, std::ios::trunc);
  if (!out) return false;
  out << L"version=2\n"
      << L"camera_id=" << settings.camera_id << L"\n"
      << L"mirror=" << (settings.mirror ? 1 : 0) << L"\n"
      << L"message=" << settings.message << L"\n"
      << L"image_path=" << settings.image_path << L"\n";
  return static_cast<bool>(out);
}

AppSettings load_settings(const std::filesystem::path& path) {
  AppSettings settings;
  std::wifstream in(path);
  if (!in) return settings;
  std::wstring line;
  while (std::getline(in, line)) {
    const auto split = line.find(L'=');
    if (split == std::wstring::npos) continue;
    const auto key = line.substr(0, split);
    const auto value = line.substr(split + 1);
    try {
      if (key == L"camera_id") settings.camera_id = value;
      else if (key == L"mirror") settings.mirror = value == L"1";
      else if (key == L"message" || key == L"prompt") settings.message = value;
      else if (key == L"image_path") settings.image_path = value;
    } catch (...) {
      // Preserve defaults for malformed values.
    }
  }
  return settings;
}

}  // namespace subliminalcam
