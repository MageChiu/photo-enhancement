#pragma once
#include <algorithm>
#include <cstdint>

namespace pe {
// Only for integer, single-plane CFA RAW with confirmed half_size processing.
// This is a conservative stage estimate, not a promise about a decoder's RSS.
// Dimensions are structurally checked by the caller before using this helper.
struct RawPreviewMemory {
  uint64_t unpack, process, output;
  uint64_t peak() const { return std::max({unpack, process, output}); }
};
inline RawPreviewMemory raw_preview_memory(uint32_t sensor_width, uint32_t sensor_height,
                                          uint32_t width, uint32_t height) {
  const uint64_t sensor = uint64_t(sensor_width) * sensor_height;
  const uint64_t work = ((uint64_t(width) + 1) / 2) * ((uint64_t(height) + 1) / 2);
  constexpr uint64_t reserve = 64ULL * 1024 * 1024;
  return {
    // Full sensor samples + decoder scratch; half_size never avoids unpack().
    sensor * 16 + reserve,
    // Retained sensor/correction buffers + half-size RGBA and processing scratch.
    sensor * 4 + work * 32 + reserve,
    // LibRaw's half-size image + our RGBA16 output, without an RGB16 bitmap copy.
    sensor * 4 + work * 16 + reserve,
  };
}

// copy_mem_image writes packed RGB/RGBG into a row with RGBA16 capacity.
// Expand backwards so no unread packed components are overwritten.
// Preconditions: colors is 3 or 4, row has at least width*4 components.
inline void expand_raw_row(uint16_t* row, uint32_t width, uint32_t colors) {
  for (uint32_t x = width; x-- > 0;) {
    const uint16_t r = row[uint64_t(x) * colors], g = row[uint64_t(x) * colors + 1],
        b = row[uint64_t(x) * colors + 2];
    row[uint64_t(x) * 4] = r; row[uint64_t(x) * 4 + 1] = g;
    row[uint64_t(x) * 4 + 2] = b; row[uint64_t(x) * 4 + 3] = 65535;
  }
}
}
