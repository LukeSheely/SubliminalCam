#pragma once

#include "core/frame.h"

#include <cstdint>
#include <span>

namespace subliminalcam {

Frame solid_background(int width, int height, Pixel color);
Frame gradient_background(int width, int height, Pixel top, Pixel bottom);
Frame box_blur(const Frame& source, int radius);
Frame composite_portrait(const Frame& foreground, const Frame& background,
                         std::span<const std::uint8_t> person_mask);
void mirror_horizontal(Frame& frame);

}  // namespace subliminalcam
