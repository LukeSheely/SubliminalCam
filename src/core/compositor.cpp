#include "core/compositor.h"

#include <algorithm>
#include <cstddef>

namespace subliminalcam {

void mirror_horizontal(Frame& frame) {
  for (int y = 0; y < frame.height; ++y) {
    auto* row = frame.pixels.data() + static_cast<std::size_t>(y) * frame.width;
    for (int x = 0; x < frame.width / 2; ++x) {
      std::swap(row[x], row[frame.width - x - 1]);
    }
  }
}

}  // namespace subliminalcam
