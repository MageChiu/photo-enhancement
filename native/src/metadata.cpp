#include "internal.hpp"
#include <cstring>
#include <iomanip>
#include <sstream>

namespace pe {
namespace {
struct Tiff {
  const uint8_t* data; size_t size; bool little;
  bool contains(size_t off, size_t n) const { return off <= size && n <= size - off; }
  uint16_t u16(size_t off) const {
    if (!contains(off, 2)) return 0;
    return little ? data[off] | uint16_t(data[off + 1]) << 8 : uint16_t(data[off]) << 8 | data[off + 1];
  }
  uint32_t u32(size_t off) const {
    if (!contains(off, 4)) return 0;
    return little ? u16(off) | uint32_t(u16(off + 2)) << 16 : uint32_t(u16(off)) << 16 | u16(off + 2);
  }
  void ifd(uint32_t off, Metadata& result, bool nested) const {
    if (!contains(off, 2)) return;
    uint16_t count = u16(off);
    if (count > 512 || !contains(size_t(off) + 2, size_t(count) * 12)) return;
    for (uint16_t i = 0; i < count; ++i) {
      size_t entry = size_t(off) + 2 + size_t(i) * 12;
      uint16_t tag = u16(entry), type = u16(entry + 2);
      uint32_t n = u32(entry + 4);
      size_t unit = type == 2 || type == 1 || type == 7 ? 1 : type == 3 ? 2 : type == 4 ? 4 : type == 5 ? 8 : 0;
      if (!unit || n > 65536) continue;
      size_t bytes = unit * n, value = bytes <= 4 ? entry + 8 : u32(entry + 8);
      if (!contains(value, bytes)) continue;
      auto text = [&](char* target, size_t capacity) {
        if (type != 2 || n == 0) return;
        size_t len = std::min(size_t(n), capacity - 1);
        std::memcpy(target, data + value, len); target[len] = 0;
        for (size_t c = 0; c < len; ++c) if (uint8_t(target[c]) < 32 && target[c]) target[c] = ' ';
      };
      double number = n == 0 ? 0 : type == 3 ? u16(value) : type == 4 ? u32(value) :
          type == 5 && u32(value + 4) ? double(u32(value)) / u32(value + 4) : 0;
      switch (tag) {
        case 0x010f: text(result.info.make, sizeof(result.info.make)); break;
        case 0x0110: text(result.info.model, sizeof(result.info.model)); break;
        case 0x0112: if (number >= 1 && number <= 8) result.orientation = uint16_t(number); break;
        case 0x829a: result.info.shutter = number; break;
        case 0x829d: result.info.aperture = number; break;
        case 0x8827: result.info.iso = uint32_t(number); break;
        case 0x9003: text(result.info.captured_at, sizeof(result.info.captured_at)); break;
        case 0x920a: result.info.focal_length = number; break;
        case 0xa433: text(result.lens.make, sizeof(result.lens.make)); break;
        case 0xa434: text(result.lens.model, sizeof(result.lens.model)); break;
        case 0x8769: if (!nested && type == 4 && n == 1) ifd(uint32_t(number), result, true); break;
        default: break;
      }
    }
  }
};
void put16(std::vector<uint8_t>& v, size_t p, uint16_t x) { v[p] = uint8_t(x); v[p + 1] = uint8_t(x >> 8); }
void put32(std::vector<uint8_t>& v, size_t p, uint32_t x) { put16(v, p, uint16_t(x)); put16(v, p + 2, uint16_t(x >> 16)); }
struct Entry { uint16_t tag, type; uint32_t count; std::vector<uint8_t> value; };
Entry string_entry(uint16_t tag, const char* str) {
  std::vector<uint8_t> value(str, str + std::strlen(str) + 1);
  return {tag, 2, uint32_t(value.size()), std::move(value)};
}
Entry integer_entry(uint16_t tag, uint32_t number, bool short_value = false) {
  std::vector<uint8_t> value(short_value ? 2 : 4);
  if (short_value) put16(value, 0, uint16_t(number)); else put32(value, 0, number);
  return {tag, uint16_t(short_value ? 3 : 4), 1, std::move(value)};
}
Entry rational_entry(uint16_t tag, double number) {
  std::vector<uint8_t> value(8);
  constexpr uint32_t denominator = 100000;
  double scaled = std::clamp(number * denominator, 0.0, double(UINT32_MAX));
  put32(value, 0, uint32_t(std::lround(scaled))); put32(value, 4, denominator);
  return {tag, 5, 1, std::move(value)};
}
uint32_t append_ifd(std::vector<uint8_t>& out, std::vector<Entry> entries) {
  std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.tag < b.tag; });
  uint32_t offset = uint32_t(out.size());
  out.resize(out.size() + 2 + entries.size() * 12 + 4);
  put16(out, offset, uint16_t(entries.size()));
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& e = entries[i]; size_t p = offset + 2 + i * 12;
    put16(out, p, e.tag); put16(out, p + 2, e.type); put32(out, p + 4, e.count);
    if (e.value.size() <= 4) std::copy(e.value.begin(), e.value.end(), out.begin() + p + 8);
    else {
      put32(out, p + 8, uint32_t(out.size()));
      out.insert(out.end(), e.value.begin(), e.value.end());
      if (out.size() % 2) out.push_back(0);
    }
  }
  return offset;
}
}

Metadata read_exif(const uint8_t* data, size_t size) {
  Metadata result;
  if (size >= 6 && std::memcmp(data, "Exif\0\0", 6) == 0) { data += 6; size -= 6; }
  if (size < 8 || !((data[0] == 'I' && data[1] == 'I') || (data[0] == 'M' && data[1] == 'M'))) return result;
  Tiff t{data, size, data[0] == 'I'};
  if (t.u16(2) == 42) t.ifd(t.u32(4), result, false);
  return result;
}

std::vector<uint8_t> write_exif(const pe_image_info& info,const pe_lens_info* lens) {
  std::vector<uint8_t> out{'I', 'I', 42, 0, 8, 0, 0, 0};
  std::vector<Entry> root{integer_entry(0x0112, 1, true),
      string_entry(0x0131, "Photo Enhancement 0.1"), integer_entry(0x8769, 0)};
  if (info.make[0]) root.push_back(string_entry(0x010f, info.make));
  if (info.model[0]) root.push_back(string_entry(0x0110, info.model));
  append_ifd(out, root);
  std::vector<Entry> exif{integer_entry(0xa001, 1, true), integer_entry(0xa002, info.width), integer_entry(0xa003, info.height)};
  if (info.iso) exif.push_back(integer_entry(0x8827, std::min(info.iso, 65535U), true));
  if (info.aperture > 0) exif.push_back(rational_entry(0x829d, info.aperture));
  if (info.shutter > 0) exif.push_back(rational_entry(0x829a, info.shutter));
  if (info.focal_length > 0) exif.push_back(rational_entry(0x920a, info.focal_length));
  if (info.captured_at[0]) exif.push_back(string_entry(0x9003, info.captured_at));
  if(lens && lens->make[0])exif.push_back(string_entry(0xa433,lens->make));
  if(lens && lens->model[0])exif.push_back(string_entry(0xa434,lens->model));
  uint32_t offset = append_ifd(out, exif);
  for (uint16_t i = 0; i < root.size(); ++i) {
    size_t p = 10 + size_t(i) * 12;
    if ((out[p] | uint16_t(out[p + 1]) << 8) == 0x8769) put32(out, p + 8, offset);
  }
  return out;
}

void orient(pe_image& img, uint16_t orientation) {
  if (orientation <= 1 || orientation > 8) return;
  uint32_t w = img.info.width, h = img.info.height;
  bool swap = orientation >= 5;
  uint32_t ow = swap ? h : w, oh = swap ? w : h;
  std::vector<uint16_t> pixels(img.pixels.size());
  for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) {
    uint32_t dx = x, dy = y;
    switch (orientation) {
      case 2: dx = w - 1 - x; break;
      case 3: dx = w - 1 - x; dy = h - 1 - y; break;
      case 4: dy = h - 1 - y; break;
      case 5: dx = y; dy = x; break;
      case 6: dx = h - 1 - y; dy = x; break;
      case 7: dx = h - 1 - y; dy = w - 1 - x; break;
      case 8: dx = y; dy = w - 1 - x; break;
      default: break;
    }
    size_t from = (size_t(y) * w + x) * 4, to = (size_t(dy) * ow + dx) * 4;
    std::copy_n(img.pixels.data() + from, 4, pixels.data() + to);
  }
  img.pixels = std::move(pixels); img.info.width = ow; img.info.height = oh;
}

std::string caption(const pe_image_info& i) {
  std::ostringstream out;
  if (i.model[0]) out << i.model;
  auto gap = [&] { if (out.tellp() > 0) out << "   "; };
  if (i.focal_length > 0) { gap(); out << std::setprecision(4) << i.focal_length << "mm"; }
  if (i.aperture > 0) { gap(); out << "f/" << std::setprecision(2) << i.aperture; }
  if (i.shutter > 0) {
    gap();
    if (i.shutter < 1) out << "1/" << std::lround(1 / i.shutter) << "s";
    else out << std::setprecision(3) << i.shutter << "s";
  }
  if (i.iso) { gap(); out << "ISO " << i.iso; }
  return out.str();
}
}
