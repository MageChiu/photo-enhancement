#include "internal.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <cstring>

namespace pe {
namespace {
std::vector<uint32_t> codepoints(const std::string& text) {
  std::vector<uint32_t> result;
  for (size_t i = 0; i < text.size();) {
    uint8_t first = uint8_t(text[i++]);
    uint32_t cp; int remaining;
    if (first < 0x80) { cp = first; remaining = 0; }
    else if ((first & 0xe0) == 0xc0) { cp = first & 31; remaining = 1; }
    else if ((first & 0xf0) == 0xe0) { cp = first & 15; remaining = 2; }
    else if ((first & 0xf8) == 0xf0) { cp = first & 7; remaining = 3; }
    else throw Error(PE_INVALID_ARGUMENT, "Caption is not valid UTF-8");
    for (int n = 0; n < remaining; ++n) {
      require(i < text.size() && (uint8_t(text[i]) & 0xc0) == 0x80, "Caption is not valid UTF-8");
      cp = (cp << 6) | (uint8_t(text[i++]) & 63);
    }
    require(cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff) &&
        !(remaining == 1 && cp < 128) && !(remaining == 2 && cp < 2048) &&
        !(remaining == 3 && cp < 65536), "Caption is not valid UTF-8");
    require(cp >= 32, "Caption must be a single printable line");
    result.push_back(cp);
  }
  return result;
}
struct Font {
  FT_Library library = nullptr; FT_Face face = nullptr;
  ~Font() { if (face) FT_Done_Face(face); if (library) FT_Done_FreeType(library); }
};
}
std::unique_ptr<pe_image> add_border(const pe_image& src, const pe_border& b) {
  require(std::isfinite(b.width) && b.width >= 0 && b.width <= .25 &&
      std::isfinite(b.bottom) && b.bottom >= 0 && b.bottom <= .4, "Invalid border dimensions");
  require(std::memchr(b.text, 0, sizeof(b.text)) && std::memchr(b.font_path, 0, sizeof(b.font_path)), "Border strings must be null-terminated");
  uint32_t short_edge = std::min(src.info.width, src.info.height);
  uint32_t pad = uint32_t(std::lround(short_edge * b.width));
  uint32_t bottom = uint32_t(std::lround(short_edge * b.bottom));
  if (!pad && !bottom) return std::make_unique<pe_image>(src);
  uint32_t w = src.info.width + pad * 2, h = src.info.height + pad * 2 + bottom;
  auto out = image(w, h);
  out->info = src.info; out->info.width = w; out->info.height = h;
  uint16_t color[]{quantize(linear(((b.color >> 16) & 255) / 255.0)),
                   quantize(linear(((b.color >> 8) & 255) / 255.0)),
                   quantize(linear((b.color & 255) / 255.0)), 65535};
  for (size_t i = 0; i < out->pixels.size(); i += 4) std::copy_n(color, 4, out->pixels.data() + i);
  for (uint32_t y = 0; y < src.info.height; ++y)
    std::copy_n(src.pixels.data() + size_t(y) * src.info.width * 4, size_t(src.info.width) * 4,
                out->pixels.data() + (size_t(y + pad) * w + pad) * 4);
  std::string text = b.text[0] ? b.text : caption(src.info);
  if (!bottom || text.empty()) return out;
  require(b.font_path[0], "A font path is required for an EXIF/text border");
  auto chars = codepoints(text);
  Font font;
  require(FT_Init_FreeType(&font.library) == 0 && FT_New_Face(font.library, b.font_path, 0, &font.face) == 0,
      "Cannot load the caption font", PE_IO_ERROR);
  uint32_t size = std::max(1U, uint32_t(bottom * .23));
  int64_t text_width = 0;
  for (;;) {
    require(FT_Set_Pixel_Sizes(font.face, 0, size) == 0, "Cannot set font size");
    text_width = 0;
    for (uint32_t cp : chars) {
      require(FT_Get_Char_Index(font.face, cp) != 0, "Caption font does not contain all characters");
      require(FT_Load_Char(font.face, cp, FT_LOAD_DEFAULT) == 0, "Cannot measure caption glyph");
      text_width += font.face->glyph->advance.x >> 6;
    }
    if (text_width <= int64_t(w) - 2 * std::max(pad, 2U) || size <= 1) break;
    --size;
  }
  require(text_width <= int64_t(w) - 2 * std::max(pad, 2U), "Caption does not fit this image size");
  int pen = int((int64_t(w) - text_width) / 2);
  int ascent = int(font.face->size->metrics.ascender >> 6);
  int descent = int(font.face->size->metrics.descender >> 6);
  int baseline = int(pad + src.info.height) + int(bottom) / 2 + (ascent + descent) / 2;
  double ink = (.2126 * color[0] + .7152 * color[1] + .0722 * color[2]) > 24000 ? .025 : .94;
  for (uint32_t cp : chars) {
    require(FT_Load_Char(font.face, cp, FT_LOAD_RENDER) == 0, "Cannot rasterize caption glyph");
    auto glyph = font.face->glyph;
    require(!glyph->bitmap.rows || (glyph->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY &&
        std::abs(glyph->bitmap.pitch) >= int(glyph->bitmap.width)),
        "Caption font must produce grayscale glyphs", PE_UNSUPPORTED);
    for (unsigned y = 0; y < glyph->bitmap.rows; ++y) for (unsigned x = 0; x < glyph->bitmap.width; ++x) {
      int dx = pen + glyph->bitmap_left + int(x), dy = baseline - glyph->bitmap_top + int(y);
      if (dx < 0 || dy < int(pad + src.info.height) || dx >= int(w) || dy >= int(h - pad)) continue;
      int pitch = glyph->bitmap.pitch;
      size_t row = pitch >= 0 ? y * size_t(pitch) : (glyph->bitmap.rows - 1 - y) * size_t(-pitch);
      double coverage = glyph->bitmap.buffer[row + x] / 255.0;
      size_t p = (size_t(dy) * w + size_t(dx)) * 4;
      for (int c = 0; c < 3; ++c) out->pixels[p + c] = quantize(out->pixels[p + c] / 65535.0 * (1 - coverage) + ink * coverage);
    }
    pen += int(glyph->advance.x >> 6);
  }
  return out;
}
}
