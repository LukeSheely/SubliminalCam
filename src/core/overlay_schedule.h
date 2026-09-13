#pragma once

#include <cstdint>

namespace subliminalcam {

// Timing values are stored as integer hundredths of a second. The first pulse
// begins after one interval, then subsequent pulses repeat start to start.
bool scheduled_overlay_visible(bool enabled, std::int64_t elapsed_ms,
                               int interval_centiseconds,
                               int duration_centiseconds);

}  // namespace subliminalcam
