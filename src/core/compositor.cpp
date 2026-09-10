#include "core/compositor.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace subliminalcam {

Frame solid_background(int width, int height, Pixel color) {
  Frame output(width, height);
  std::fill(output.pixels.begin(), output.pixels.end(), color);
  return output;
}

Frame gradient_background(int width, int height, Pixel top, Pixel bottom) {
  Frame output(width, height);
  for (int y = 0; y < height; ++y) {
    const auto alpha = height == 1 ? 0u : static_cast<unsigned>((255ull * y) / (height - 1));
    Pixel color{
        mix_channel(bottom.b, top.b, static_cast<std::uint8_t>(alpha)),
        mix_channel(bottom.g, top.g, static_cast<std::uint8_t>(alpha)),
        mix_channel(bottom.r, top.r, static_cast<std::uint8_t>(alpha)), 255};
    std::fill_n(output.pixels.begin() + static_cast<std::size_t>(y) * width, width, color);
  }
  return output;
}

Frame box_blur(const Frame& source, int radius) {
  if (radius <= 0) return source;
  radius = std::min(radius, 48);
  Frame horizontal(source.width, source.height);
  Frame output(source.width, source.height);

  for (int y = 0; y < source.height; ++y) {
    std::uint64_t sb = 0, sg = 0, sr = 0;
    for (int x = -radius; x <= radius; ++x) {
      const auto& p = source.at(std::clamp(x, 0, source.width - 1), y);
      sb += p.b; sg += p.g; sr += p.r;
    }
    const auto count = static_cast<unsigned>(radius * 2 + 1);
    for (int x = 0; x < source.width; ++x) {
      horizontal.at(x, y) = Pixel{static_cast<std::uint8_t>(sb / count),
                                  static_cast<std::uint8_t>(sg / count),
                                  static_cast<std::uint8_t>(sr / count), 255};
      const auto& remove = source.at(std::clamp(x - radius, 0, source.width - 1), y);
      const auto& add = source.at(std::clamp(x + radius + 1, 0, source.width - 1), y);
      sb += add.b - remove.b; sg += add.g - remove.g; sr += add.r - remove.r;
    }
  }

  for (int x = 0; x < source.width; ++x) {
    std::uint64_t sb = 0, sg = 0, sr = 0;
    for (int y = -radius; y <= radius; ++y) {
      const auto& p = horizontal.at(x, std::clamp(y, 0, source.height - 1));
      sb += p.b; sg += p.g; sr += p.r;
    }
    const auto count = static_cast<unsigned>(radius * 2 + 1);
    for (int y = 0; y < source.height; ++y) {
      output.at(x, y) = Pixel{static_cast<std::uint8_t>(sb / count),
                              static_cast<std::uint8_t>(sg / count),
                              static_cast<std::uint8_t>(sr / count), 255};
      const auto& remove = horizontal.at(x, std::clamp(y - radius, 0, source.height - 1));
      const auto& add = horizontal.at(x, std::clamp(y + radius + 1, 0, source.height - 1));
      sb += add.b - remove.b; sg += add.g - remove.g; sr += add.r - remove.r;
    }
  }
  return output;
}

Frame composite_portrait(const Frame& foreground, const Frame& background,
                         std::span<const std::uint8_t> person_mask) {
  if (foreground.width != background.width || foreground.height != background.height ||
      person_mask.size() != foreground.pixels.size()) {
    throw std::invalid_argument("composite inputs must have matching dimensions");
  }
  Frame output(foreground.width, foreground.height);
  for (std::size_t i = 0; i < output.pixels.size(); ++i) {
    const auto alpha = person_mask[i];
    output.pixels[i] = Pixel{mix_channel(foreground.pixels[i].b, background.pixels[i].b, alpha),
                             mix_channel(foreground.pixels[i].g, background.pixels[i].g, alpha),
                             mix_channel(foreground.pixels[i].r, background.pixels[i].r, alpha), 255};
  }
  return output;
}

void mirror_horizontal(Frame& frame) {
  for (int y = 0; y < frame.height; ++y) {
    for (int x = 0; x < frame.width / 2; ++x) {
      std::swap(frame.at(x, y), frame.at(frame.width - x - 1, y));
    }
  }
}

}  // namespace subliminalcam
