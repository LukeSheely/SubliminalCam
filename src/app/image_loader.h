#pragma once

#include "core/frame.h"

#include <filesystem>
#include <optional>

namespace subliminalcam {

std::optional<Frame> load_image(const std::filesystem::path& path, int width, int height);

}  // namespace subliminalcam
