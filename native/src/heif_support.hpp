#pragma once
#include <cstdint>

namespace pe {
constexpr uint32_t heif_tile_edge = 1024;
inline bool heif_grid(uint32_t width, uint32_t height) {
  return width > heif_tile_edge || height > heif_tile_edge;
}
// Encoder/plugin allocations are estimates, not hard RSS limits. libheif keeps
// compressed tiles in its context until writing: reserve 8 bytes/output pixel
// for that storage as well as one band, one tile and bounded encoder scratch.
inline uint64_t heif_export_memory(uint32_t width, uint32_t height) {
  const uint64_t pixels = uint64_t(width) * height;
  if (!heif_grid(width, height)) return pixels * 32 + 16ULL * 1024 * 1024;
  return pixels * 8 + uint64_t(width) * heif_tile_edge * 4 +
      uint64_t(heif_tile_edge) * heif_tile_edge * 128 + 64ULL * 1024 * 1024;
}
}
