#include "shared/frame_transport.h"

#include <sddl.h>

#include <cstring>

namespace subliminalcam {

SharedFrameWriter::~SharedFrameWriter() { close(); }

bool SharedFrameWriter::ensure_open() {
  if (view_) return true;
  mapping_ = OpenFileMappingW(FILE_MAP_WRITE, FALSE, kFrameMappingName);
  if (!mapping_) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    // Owner, Local Service, and System may read/write the frame exchange.
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GRGW;;;OW)(A;;GRGW;;;LS)(A;;GRGW;;;SY)", SDDL_REVISION_1,
            &descriptor, nullptr)) {
      return false;
    }
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE,
                                  static_cast<DWORD>(kFrameMappingBytes >> 32),
                                  static_cast<DWORD>(kFrameMappingBytes & 0xffffffffu),
                                  kFrameMappingName);
    LocalFree(descriptor);
  }
  if (!mapping_) return false;
  view_ = static_cast<std::byte*>(MapViewOfFile(mapping_, FILE_MAP_WRITE, 0, 0, kFrameMappingBytes));
  if (!view_) {
    CloseHandle(mapping_);
    mapping_ = nullptr;
    return false;
  }
  return true;
}

bool SharedFrameWriter::write(const Frame& frame, std::uint64_t timestamp_100ns) {
  if (frame.width <= 0 || frame.height <= 0 || frame.width > kMaxFrameWidth ||
      frame.height > kMaxFrameHeight || !ensure_open()) return false;
  const auto bytes = frame.pixels.size() * sizeof(Pixel);
  if (bytes > kMaxFrameBytes) return false;
  auto* header = reinterpret_cast<SharedFrameHeader*>(view_);
  InterlockedIncrement(&header->sequence);  // odd: write in progress
  header->magic = kFrameMagic;
  header->version = kFrameVersion;
  header->width = static_cast<std::uint32_t>(frame.width);
  header->height = static_cast<std::uint32_t>(frame.height);
  header->stride = static_cast<std::uint32_t>(frame.width * sizeof(Pixel));
  header->data_bytes = static_cast<std::uint32_t>(bytes);
  header->timestamp_100ns = timestamp_100ns;
  std::memcpy(view_ + sizeof(SharedFrameHeader), frame.pixels.data(), bytes);
  MemoryBarrier();
  InterlockedIncrement(&header->sequence);  // even: stable snapshot
  return true;
}

void SharedFrameWriter::close() {
  if (view_) UnmapViewOfFile(view_);
  if (mapping_) CloseHandle(mapping_);
  view_ = nullptr;
  mapping_ = nullptr;
}

}  // namespace subliminalcam
