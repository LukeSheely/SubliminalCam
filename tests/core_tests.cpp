#include "core/compositor.h"
#include "core/prompt_scheduler.h"
#include "core/settings.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace subliminalcam;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void test_guardrails() {
  AppSettings input;
  input.prompts.enabled = true;
  input.prompts.duration_ms = 1;
  input.prompts.interval_ms = 2;
  input.blur_radius = 1000;
  const auto clean = sanitize(input);
  require(clean.prompts.duration_ms == 1000, "duration guardrail");
  require(clean.prompts.interval_ms == 30000, "interval guardrail");
  require(clean.blur_radius == 48, "blur clamp");
}

void test_scheduler() {
  PromptScheduler scheduler;
  PromptSettings settings;
  settings.enabled = true;
  settings.duration_ms = 1000;
  settings.interval_ms = 30000;
  settings.messages = {L"Hello", L"World"};
  scheduler.configure(settings, 100);
  require(scheduler.tick(100).disclosure_visible, "disclosure should be persistent");
  scheduler.show_now(200);
  const auto visible = scheduler.tick(200);
  require(visible.prompt_visible && visible.message == L"Hello", "manual prompt");
  require(!scheduler.tick(1200).prompt_visible, "manual prompt expiration");
  const auto automatic = scheduler.tick(30200);
  require(automatic.prompt_visible && automatic.message == L"World", "automatic rotation");
}

void test_compositor() {
  const Pixel top{0, 0, 0, 255};
  const Pixel bottom{255, 255, 255, 255};
  auto gradient = gradient_background(2, 2, top, bottom);
  require(gradient.at(0, 0).r == 0 && gradient.at(0, 1).r == 255, "gradient endpoints");

  Frame foreground = solid_background(2, 1, Pixel{10, 20, 30, 255});
  Frame background = solid_background(2, 1, Pixel{100, 110, 120, 255});
  std::vector<std::uint8_t> mask{255, 0};
  auto output = composite_portrait(foreground, background, mask);
  require(output.at(0, 0).r == 30 && output.at(1, 0).r == 120, "mask composition");
  mirror_horizontal(output);
  require(output.at(0, 0).r == 120 && output.at(1, 0).r == 30, "mirror");

  Frame impulse = solid_background(3, 1, Pixel{0, 0, 0, 255});
  impulse.at(1, 0) = Pixel{255, 255, 255, 255};
  auto blurred = box_blur(impulse, 1);
  require(blurred.at(1, 0).r > 0 && blurred.at(1, 0).r < 255, "blur");
}

int main() {
  try {
    test_guardrails();
    test_scheduler();
    test_compositor();
    std::cout << "All SubliminalCam core tests passed.\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "Test failure: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
