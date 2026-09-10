#pragma once

#include "core/settings.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace subliminalcam {

struct PromptState {
  bool disclosure_visible{false};
  bool prompt_visible{false};
  std::wstring message;
};

class PromptScheduler {
 public:
  void configure(PromptSettings settings, std::int64_t now_ms);
  void show_now(std::int64_t now_ms);
  PromptState tick(std::int64_t now_ms);

 private:
  PromptSettings settings_{};
  std::size_t next_index_{};
  std::int64_t next_automatic_ms_{};
  std::int64_t visible_until_ms_{};
  std::wstring visible_message_;
};

}  // namespace subliminalcam
