#pragma once
#include "pe/pe.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

struct pe_image {
  pe_image_info info{};
  pe_lens_info lens{};
  std::vector<uint16_t> pixels;
};
struct pe_lut {
  uint32_t size = 0;
  std::array<double, 3> low{0, 0, 0}, high{1, 1, 1};
  std::vector<std::array<double, 3>> values;
};
namespace pe {
// Structural safety boundary, independent of device memory or camera format.
constexpr uint64_t pixel_limit = 32768ULL * 32768;
uint64_t memory_budget(bool preview = false) noexcept;
void require_memory(uint64_t bytes, uint64_t budget, const std::string& stage);
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
  const std::string dimensions = std::to_string(w) + "x" + std::to_string(h);
  require(w && h && w <= 32768 && h <= 32768,
          "Image " + dimensions + " has invalid dimensions or exceeds the 32768px edge limit", PE_LIMIT_EXCEEDED);
  require(uint64_t(w) * h <= limit,
          "Image " + dimensions + " (" + std::to_string(uint64_t(w) * h) +
          " pixels) exceeds the configured limit of " + std::to_string(limit) + " pixels", PE_LIMIT_EXCEEDED);
  return size_t(w) * h;
}
inline std::unique_ptr<pe_image> image(uint32_t w, uint32_t h) {
  auto out = std::make_unique<pe_image>();
  require_memory(checked_pixels(w, h) * 8ULL, memory_budget(), "分配像素缓冲");
  out->pixels.resize(checked_pixels(w, h) * 4);
  out->info.width = w; out->info.height = h;
  return out;
}
std::unique_ptr<pe_image> decode(const std::string&, const pe_decode_options&);
struct Metadata {
  pe_image_info info{};
  pe_lens_info lens{};
  uint16_t orientation = 1;
};
struct ImageProbe {
  Metadata metadata;
  uint64_t file_bytes = 0, sensor_pixels = 0;
  std::string format = "RAW";
  bool jpeg = false, progressive = false, raw = false, tiff = false, raw_disk = false;
};
ImageProbe probe_image(const std::string&, uint64_t file_limit);
struct DiskImage {
  FILE* file = nullptr;
  Metadata metadata;
  ~DiskImage();
  std::unique_ptr<pe_image> strip(uint32_t y, uint32_t rows) const;
};
std::unique_ptr<DiskImage> jpeg_to_disk(const std::string&, const pe_decode_options&, uint64_t budget);
std::unique_ptr<DiskImage> raw_to_disk(const std::string&, const pe_decode_options&, uint64_t budget);
bool tiff_raster_file(const std::string&);
Metadata tiff_metadata(const std::string&);
std::unique_ptr<pe_image> tiff_decode(const std::string&, const pe_decode_options&);
std::unique_ptr<DiskImage> tiff_to_disk(const std::string&, const pe_decode_options&, uint64_t budget);
struct RowWriter {
  virtual ~RowWriter() = default;
  virtual void rows(const pe_image&, uint32_t begin, uint32_t count) = 0;
  virtual void finish() = 0;
};
std::unique_ptr<RowWriter> row_writer(FILE*, const pe_image_info&, pe_format, uint32_t quality, bool metadata, const pe_lens_info* lens = nullptr);
struct FrameCanvas { uint32_t width, height, x, y; };
FrameCanvas template_canvas(const pe_image_info&, const pe_border&, const pe_frame_template*);
void validate_template(const pe_frame_template&);
std::unique_ptr<pe_image> template_avatar(const pe_frame_template*);
std::unique_ptr<pe_image> template_strip(const pe_image_info&, const pe_border&, const pe_frame_template&,
    const pe_image*, uint32_t, uint32_t);
std::unique_ptr<pe_image> add_template(const pe_image&, const pe_frame&, const pe_frame_adjustments*, const pe_frame_template&);
void validate_frame(const pe_frame&);
void validate_frame_adjustments(const pe_frame_adjustments&);
std::unique_ptr<pe_image> combined_frame_logo(std::unique_ptr<pe_image>,const pe_image*);
std::unique_ptr<pe_image> frame_logo(const pe_frame&);
std::unique_ptr<pe_image> border_strip(const pe_image_info&, const pe_border&, uint32_t y, uint32_t rows,
    const pe_frame* frame = nullptr, const pe_image* logo = nullptr,
    const pe_frame_adjustments* adjustments = nullptr, const pe_image* accent = nullptr);
void process_file(const std::string&, const std::string&, const pe_decode_options&,
    const pe_params&, const pe_effects&, const char* lut_path, double strength,
    const pe_border&, pe_format, uint32_t quality, bool metadata, uint64_t budget,
    const pe_frame* frame = nullptr, const pe_look* look = nullptr,
    const pe_frame_adjustments* adjustments = nullptr, const pe_frame_template* decoration = nullptr);
void validate_look(const pe_look&);
pe_look_analysis analyze_look(const pe_image&);
pe_look_analysis analyze_look(const DiskImage&);
std::unique_ptr<pe_image> apply_look(const pe_image&, const pe_lut&, double,
    const pe_look&, const pe_look_analysis&, uint32_t full_width, uint32_t full_height,
    uint32_t origin_y);
std::unique_ptr<pe_image> resize(const pe_image&, uint32_t);
std::unique_ptr<pe_image> apply_params(const pe_image&, const pe_params&);
std::unique_ptr<pe_image> apply_effects(const pe_image&, const pe_effects&);
std::unique_ptr<pe_lut> load_lut(const std::string&);
std::unique_ptr<pe_image> apply_lut(const pe_image&, const pe_lut&, double);
std::unique_ptr<pe_image> add_border(const pe_image&, const pe_border&, const pe_frame* frame = nullptr, const pe_image* logo = nullptr,
    const pe_frame_adjustments* adjustments = nullptr, const pe_image* accent = nullptr);
void encode(const pe_image&, const std::string&, pe_format, uint32_t, bool);
std::vector<uint8_t> rgba8(const pe_image&, bool flatten = false);
void from_display(pe_image&, const uint16_t* rgba, const std::vector<uint8_t>& icc);
Metadata read_exif(const uint8_t* data, size_t size);
std::vector<uint8_t> write_exif(const pe_image_info&,const pe_lens_info* lens = nullptr);
void orient(pe_image&, uint16_t);
std::string caption(const pe_image_info&);
}
