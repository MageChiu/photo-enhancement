#pragma once
#include "pe/pe.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

struct pe_image {
  pe_image_info info{};
  std::vector<uint16_t> pixels;
};
struct pe_lut {
  uint32_t size = 0;
  std::array<double, 3> low{0, 0, 0}, high{1, 1, 1};
  std::vector<std::array<double, 3>> values;
};
namespace pe {
constexpr uint64_t pixel_limit = 80000000;
struct Error : std::runtime_error {
  pe_status status;
  Error(pe_status s, const std::string& message) : std::runtime_error(message), status(s) {}
};
inline void require(bool condition, const std::string& message,
                    pe_status status = PE_INVALID_ARGUMENT) {
  if (!condition) throw Error(status, message);
}
inline double clamp(double v) { return std::clamp(v, 0.0, 1.0); }
inline uint16_t quantize(double v) { return static_cast<uint16_t>(std::lround(clamp(v) * 65535)); }
inline double linear(double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); }
inline double display(double v) { return v <= .0031308 ? 12.92 * v : 1.055 * std::pow(v, 1 / 2.4) - .055; }
inline size_t checked_pixels(uint32_t w, uint32_t h, uint64_t limit = pixel_limit) {
  require(w && h && w <= 32768 && h <= 32768 && uint64_t(w) * h <= limit,
          "Image dimensions exceed the configured pixel limit", PE_LIMIT_EXCEEDED);
  return size_t(w) * h;
}
inline std::unique_ptr<pe_image> image(uint32_t w, uint32_t h) {
  auto out = std::make_unique<pe_image>();
  out->pixels.resize(checked_pixels(w, h) * 4);
  out->info.width = w; out->info.height = h;
  return out;
}
std::unique_ptr<pe_image> decode(const std::string&, const pe_decode_options&);
std::unique_ptr<pe_image> resize(const pe_image&, uint32_t);
std::unique_ptr<pe_image> apply_params(const pe_image&, const pe_params&);
std::unique_ptr<pe_lut> load_lut(const std::string&);
std::unique_ptr<pe_image> apply_lut(const pe_image&, const pe_lut&, double);
std::unique_ptr<pe_image> add_border(const pe_image&, const pe_border&);
void encode(const pe_image&, const std::string&, pe_format, uint32_t, bool);
std::vector<uint8_t> rgba8(const pe_image&, bool flatten = false);
void from_display(pe_image&, const uint16_t* rgba, const std::vector<uint8_t>& icc);
struct Metadata {
  pe_image_info info{};
  uint16_t orientation = 1;
};
Metadata read_exif(const uint8_t* data, size_t size);
std::vector<uint8_t> write_exif(const pe_image_info&);
void orient(pe_image&, uint16_t);
std::string caption(const pe_image_info&);
}
