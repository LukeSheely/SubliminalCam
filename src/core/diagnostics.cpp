#include "core/diagnostics.h"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace subliminalcam {

namespace {

std::string utf8(const std::wstring& value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
      static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string result(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size, nullptr, nullptr);
  return result;
}

const wchar_t* level_name(DiagnosticLevel level) {
  switch (level) {
    case DiagnosticLevel::warning: return L"WARN";
    case DiagnosticLevel::error: return L"ERROR";
    default: return L"INFO";
  }
}

}  // namespace

std::filesystem::path diagnostic_log_path() {
  wchar_t* local_app_data = nullptr;
  std::size_t length = 0;
  if (_wdupenv_s(&local_app_data, &length, L"LOCALAPPDATA") != 0 || !local_app_data) {
    return L"SubliminalCam.log";
  }
  const auto result = std::filesystem::path(local_app_data) /
                      L"SubliminalCam" / L"SubliminalCam.log";
  std::free(local_app_data);
  return result;
}

std::wstring hresult_message(HRESULT result) {
  wchar_t* text = nullptr;
  const DWORD length = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, static_cast<DWORD>(result), 0,
      reinterpret_cast<wchar_t*>(&text), 0, nullptr);
  std::wstring message = length && text ? std::wstring(text, length) : L"No system description";
  LocalFree(text);
  while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' ||
                              message.back() == L' ')) {
    message.pop_back();
  }
  std::wostringstream output;
  output << L"0x" << std::uppercase << std::hex << std::setw(8) << std::setfill(L'0')
         << static_cast<unsigned long>(result) << L" (" << message << L")";
  return output.str();
}

DiagnosticLog& DiagnosticLog::instance() {
  static DiagnosticLog log;
  return log;
}

void DiagnosticLog::write(DiagnosticLevel level, const std::wstring& component,
                          const std::wstring& message, HRESULT result) {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  std::wostringstream output;
  output << std::setfill(L'0') << std::setw(4) << now.wYear << L'-'
         << std::setw(2) << now.wMonth << L'-' << std::setw(2) << now.wDay << L' '
         << std::setw(2) << now.wHour << L':' << std::setw(2) << now.wMinute << L':'
         << std::setw(2) << now.wSecond << L'.' << std::setw(3) << now.wMilliseconds
         << L" [" << level_name(level) << L"] [" << component << L"] " << message;
  if (FAILED(result)) output << L" — " << hresult_message(result);
  const auto line = output.str();

  std::scoped_lock lock(mutex_);
  recent_.push_back(line);
  if (recent_.size() > 300) recent_.erase(recent_.begin(), recent_.begin() + 50);
  append_file(line);
}

std::vector<std::wstring> DiagnosticLog::recent() const {
  std::scoped_lock lock(mutex_);
  return recent_;
}

void DiagnosticLog::append_file(const std::wstring& line) {
  try {
    const auto path = diagnostic_log_path();
    std::filesystem::create_directories(path.parent_path());
    std::error_code error;
    if (std::filesystem::exists(path, error) &&
        std::filesystem::file_size(path, error) > 2 * 1024 * 1024) {
      const auto previous = path.wstring() + L".1";
      std::filesystem::remove(previous, error);
      error.clear();
      std::filesystem::rename(path, previous, error);
    }
    std::ofstream file(path, std::ios::app | std::ios::binary);
    if (file) file << utf8(line) << "\r\n";
  } catch (...) {
    // Diagnostics must never take down the camera pipeline.
  }
}

}  // namespace subliminalcam
