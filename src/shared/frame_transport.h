#pragma once

#include "core/frame.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace subliminalcam {

inline constexpr wchar_t kFrameMappingName[] = L"Global\\SubliminalCam.Frame.v1";
inline constexpr std::uint32_t kFrameMagic = 0x53434652;  // SCFR
inline constexpr std::uint32_t kFrameVersion = 1;
inline constexpr int kMaxFrameWidth = 1920;
inline constexpr int kMaxFrameHeight = 1080;
inline constexpr std::size_t kMaxFrameBytes =
    static_cast<std::size_t>(kMaxFrameWidth) * kMaxFrameHeight * sizeof(Pixel);

#pragma pack(push, 8)
struct SharedFrameHeader {
  std::uint32_t magic;
  std::uint32_t version;
  volatile LONG sequence;
  std::uint32_t width;
  std::uint32_t height;
  std::uint32_t stride;
  std::uint32_t data_bytes;
  std::uint64_t timestamp_100ns;
};
#pragma pack(pop)

inline constexpr std::size_t kFrameMappingBytes = sizeof(SharedFrameHeader) + kMaxFrameBytes;

class SharedFrameWriter {
 public:
  SharedFrameWriter() = default;
  ~SharedFrameWriter();
  SharedFrameWriter(const SharedFrameWriter&) = delete;
  SharedFrameWriter& operator=(const SharedFrameWriter&) = delete;

  bool write(const Frame& frame, std::uint64_t timestamp_100ns);
  void close();

 private:
  bool ensure_open();
  HANDLE mapping_{};
  std::byte* view_{};
};

}  // namespace subliminalcam
