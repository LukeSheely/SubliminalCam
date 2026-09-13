#include "core/compositor.h"
#include "core/settings.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace subliminalcam;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void test_mirror() {
  Frame frame(2, 1);
  frame.at(0, 0) = Pixel{1, 2, 3, 255};
  frame.at(1, 0) = Pixel{4, 5, 6, 255};
  mirror_horizontal(frame);
  require(frame.at(0, 0).r == 6 && frame.at(1, 0).r == 3, "horizontal mirror");
}

void test_settings() {
  const auto path = std::filesystem::temp_directory_path() / "subliminalcam-settings-test.ini";
  AppSettings source;
  source.camera_id = L"camera-id";
  source.message = L"Visible message";
  source.image_path = L"C:\\background.png";
  source.mirror = false;
  require(save_settings(source, path), "settings save");
  const auto loaded = load_settings(path);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  require(loaded.camera_id == source.camera_id, "camera setting");
  require(loaded.message == source.message, "message setting");
  require(loaded.image_path == source.image_path, "image path setting");
  require(!loaded.mirror, "mirror setting");
}

int main() {
  try {
    test_mirror();
    test_settings();
    std::cout << "All SubliminalCam core tests passed.\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "Test failure: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
