#pragma once

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace subliminalcam {

struct Pixel {
  std::uint8_t b{};
  std::uint8_t g{};
  std::uint8_t r{};
  std::uint8_t a{255};
};

struct Frame {
  int width{};
  int height{};
  std::vector<Pixel> pixels;

  Frame() = default;
  Frame(int w, int h) : width(w), height(h), pixels(static_cast<std::size_t>(w) * h) {
    if (w <= 0 || h <= 0) throw std::invalid_argument("frame dimensions must be positive");
  }

  Pixel& at(int x, int y) { return pixels.at(static_cast<std::size_t>(y) * width + x); }
  const Pixel& at(int x, int y) const { return pixels.at(static_cast<std::size_t>(y) * width + x); }
};

inline std::uint8_t mix_channel(std::uint8_t foreground, std::uint8_t background, std::uint8_t alpha) {
  const auto a = static_cast<unsigned>(alpha);
  return static_cast<std::uint8_t>((static_cast<unsigned>(foreground) * a +
                                    static_cast<unsigned>(background) * (255u - a) + 127u) / 255u);
}

}  // namespace subliminalcam
