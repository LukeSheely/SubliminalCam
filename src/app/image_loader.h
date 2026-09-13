#pragma once

#include "core/frame.h"

#include <filesystem>
#include <optional>

namespace subliminalcam {

// Decodes PNG/JPEG/BMP through Windows Imaging Component. Very large images
// are reduced while preserving aspect ratio to keep per-frame cover scaling cheap.
std::optional<Frame> load_image(const std::filesystem::path& path, int max_dimension = 2560);

}  // namespace subliminalcam
