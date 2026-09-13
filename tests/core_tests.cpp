#include "core/compositor.h"
#include "core/overlay_schedule.h"
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
  source.message_repeat = true;
  source.message_interval_centiseconds = 123;
  source.message_duration_centiseconds = 7;
  source.image_repeat = true;
  source.image_interval_centiseconds = 987;
  source.image_duration_centiseconds = 3;
  require(save_settings(source, path), "settings save");
  const auto loaded = load_settings(path);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  require(loaded.camera_id == source.camera_id, "camera setting");
  require(loaded.message == source.message, "message setting");
  require(loaded.image_path == source.image_path, "image path setting");
  require(!loaded.mirror, "mirror setting");
  require(loaded.message_repeat == source.message_repeat, "message repeat setting");
  require(loaded.message_interval_centiseconds == 123, "message interval setting");
  require(loaded.message_duration_centiseconds == 7, "message duration setting");
  require(loaded.image_repeat == source.image_repeat, "image repeat setting");
  require(loaded.image_interval_centiseconds == 987, "image interval setting");
  require(loaded.image_duration_centiseconds == 3, "image duration setting");
}

void test_overlay_schedule() {
  require(!scheduled_overlay_visible(false, 0, 100, 10), "disabled schedule");
  require(!scheduled_overlay_visible(true, 0, 100, 10), "schedule begins hidden");
  require(!scheduled_overlay_visible(true, 999, 100, 10), "hidden before repeat");
  require(scheduled_overlay_visible(true, 1000, 100, 10), "visible at first interval");
  require(scheduled_overlay_visible(true, 1099, 100, 10), "visible before duration boundary");
  require(!scheduled_overlay_visible(true, 1100, 100, 10), "hidden at duration boundary");
  require(scheduled_overlay_visible(true, 2000, 100, 10), "visible at repeat boundary");
  require(scheduled_overlay_visible(true, 5000, 1, 2), "overlapping schedule stays visible");
  require(!scheduled_overlay_visible(true, -1, 100, 10), "negative elapsed is before start");
}

int main() {
  try {
    test_mirror();
    test_settings();
    test_overlay_schedule();
    std::cout << "All SubliminalCam core tests passed.\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "Test failure: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
