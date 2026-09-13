#include "core/overlay_schedule.h"

#include <algorithm>

namespace subliminalcam {

bool scheduled_overlay_visible(bool enabled, std::int64_t elapsed_ms,
                               int interval_centiseconds,
                               int duration_centiseconds) {
  if (!enabled) return false;
  const auto interval_ms = static_cast<std::int64_t>(
      std::max(1, interval_centiseconds)) * 10;
  const auto duration_ms = static_cast<std::int64_t>(
      std::max(1, duration_centiseconds)) * 10;
  if (elapsed_ms < interval_ms) return false;
  if (duration_ms >= interval_ms) return true;
  return (elapsed_ms - interval_ms) % interval_ms < duration_ms;
}

}  // namespace subliminalcam
