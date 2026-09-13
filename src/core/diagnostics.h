#pragma once

#include <windows.h>

#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace subliminalcam {

enum class DiagnosticLevel { info, warning, error };

std::filesystem::path diagnostic_log_path();
std::wstring hresult_message(HRESULT result);

class DiagnosticLog {
 public:
  static DiagnosticLog& instance();

  void write(DiagnosticLevel level, const std::wstring& component,
             const std::wstring& message, HRESULT result = S_OK);
  std::vector<std::wstring> recent() const;

 private:
  DiagnosticLog() = default;
  void append_file(const std::wstring& line);

  mutable std::mutex mutex_;
  std::vector<std::wstring> recent_;
};

}  // namespace subliminalcam
