#include "core/prompt_scheduler.h"

namespace subliminalcam {

void PromptScheduler::configure(PromptSettings settings, std::int64_t now_ms) {
  AppSettings wrapper;
  wrapper.prompts = std::move(settings);
  settings_ = sanitize(std::move(wrapper)).prompts;
  next_index_ = 0;
  visible_until_ms_ = 0;
  visible_message_.clear();
  next_automatic_ms_ = now_ms + settings_.interval_ms;
}

void PromptScheduler::show_now(std::int64_t now_ms) {
  if (!settings_.enabled || settings_.messages.empty()) return;
  visible_message_ = settings_.messages[next_index_++ % settings_.messages.size()];
  visible_until_ms_ = now_ms + settings_.duration_ms;
  next_automatic_ms_ = std::max(next_automatic_ms_, now_ms + 30000);
}

PromptState PromptScheduler::tick(std::int64_t now_ms) {
  PromptState result;
  result.disclosure_visible = settings_.enabled;
  if (!settings_.enabled || settings_.messages.empty()) return result;

  if (now_ms >= next_automatic_ms_) {
    visible_message_ = settings_.messages[next_index_++ % settings_.messages.size()];
    visible_until_ms_ = now_ms + settings_.duration_ms;
    next_automatic_ms_ = now_ms + settings_.interval_ms;
  }
  if (now_ms < visible_until_ms_) {
    result.prompt_visible = true;
    result.message = visible_message_;
  }
  return result;
}

}  // namespace subliminalcam
