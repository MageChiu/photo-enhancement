#pragma once
#include "platform_io.hpp"
#include <fstream>
#include <vector>
#include <ft2build.h>
#include FT_FREETYPE_H

namespace pe {
inline FT_Error open_font(FT_Library library, const char* path,
    std::vector<unsigned char>& storage, FT_Face* face) {
#ifdef _WIN32
  // FreeType's narrow filename API does not accept UTF-8 Windows paths.
  std::ifstream file(io::path(path), std::ios::binary | std::ios::ate);
  if (!file) return FT_Err_Cannot_Open_Resource;
  const auto size = file.tellg();
  if (size <= 0 || size > 64 * 1024 * 1024) return FT_Err_Invalid_Argument;
  storage.resize(static_cast<size_t>(size)); file.seekg(0);
  file.read(reinterpret_cast<char*>(storage.data()), size);
  if (!file) return FT_Err_Cannot_Open_Resource;
  return FT_New_Memory_Face(library, storage.data(), static_cast<FT_Long>(storage.size()), 0, face);
#else
  (void)storage;
  return FT_New_Face(library, path, 0, face);
#endif
}
}
