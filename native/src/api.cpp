#include "internal.hpp"
#include <cstdio>
#include <cstring>

namespace {
thread_local std::array<char, 2048> last_error{};
template<class F> pe_status guard(F&& f) noexcept {
  last_error[0] = 0;
  try { f(); return PE_OK; }
  catch (const pe::Error& e) { std::snprintf(last_error.data(), last_error.size(), "%s", e.what()); return e.status; }
  catch (const std::bad_alloc&) { std::snprintf(last_error.data(), last_error.size(), "Not enough memory"); return PE_OUT_OF_MEMORY; }
  catch (const std::exception& e) { std::snprintf(last_error.data(), last_error.size(), "%s", e.what()); return PE_INTERNAL_ERROR; }
  catch (...) { std::snprintf(last_error.data(), last_error.size(), "Unknown native error"); return PE_INTERNAL_ERROR; }
}
template<class F> pe_status output(pe_image** out, F&& f) {
  if (out) *out = nullptr;
  return guard([&] { pe::require(out, "Output pointer is null"); *out = f().release(); });
}
}

extern "C" {
uint32_t pe_abi_version() { return 1; }
const char* pe_last_error() { return last_error.data(); }
const char* pe_capabilities() {
  return "jpeg,png,raw,icc,exif,lut,border"
#ifdef PE_HAVE_WEBP
    ",webp"
#endif
#ifdef PE_HAVE_HEIF
    ",heif"
#endif
    ;
}
void pe_default_decode_options(pe_decode_options* p) {
  if (p) *p = {1600, 80000000, 512ULL * 1024 * 1024};
}
void pe_default_params(pe_params* p) { if (p) *p = {}; }
void pe_default_border(pe_border* p) { if (p) { *p = {}; p->color = 0xffffff; } }
pe_status pe_decode(const char* path, const pe_decode_options* options, pe_image** out) {
  return output(out, [&] {
    pe::require(path && options, "Path or options are null");
    pe::require(options->max_pixels && options->max_pixels <= pe::pixel_limit &&
                options->max_file_bytes, "Invalid decode limits");
    return pe::decode(path, *options);
  });
}
pe_status pe_image_create(uint32_t w, uint32_t h, const uint16_t* rgba, pe_image** out) {
  return output(out, [&] {
    pe::require(rgba, "Pixel data is null");
    auto result = pe::image(w, h);
    std::copy(rgba, rgba + result->pixels.size(), result->pixels.begin());
    return result;
  });
}
pe_status pe_image_get_info(const pe_image* img, pe_image_info* info) {
  return guard([&] { pe::require(img && info, "Image or info is null"); *info = img->info; });
}
pe_status pe_image_copy_rgba8(const pe_image* img, uint8_t* buffer, uint64_t capacity) {
  return guard([&] {
    pe::require(img && buffer, "Image or buffer is null");
    pe::require(capacity >= img->pixels.size(), "Pixel output buffer is too small");
    const auto data = pe::rgba8(*img);
    std::copy(data.begin(), data.end(), buffer);
  });
}
pe_status pe_resize(const pe_image* img, uint32_t edge, pe_image** out) {
  return output(out, [&] { pe::require(img, "Image is null"); return pe::resize(*img, edge); });
}
pe_status pe_apply_params(const pe_image* img, const pe_params* p, pe_image** out) {
  return output(out, [&] { pe::require(img && p, "Image or params is null"); return pe::apply_params(*img, *p); });
}
pe_status pe_lut_load(const char* path, pe_lut** out) {
  if (out) *out = nullptr;
  return guard([&] { pe::require(path && out, "LUT path or output is null"); *out = pe::load_lut(path).release(); });
}
pe_status pe_apply_lut(const pe_image* img, const pe_lut* lut, double strength, pe_image** out) {
  return output(out, [&] { pe::require(img && lut, "Image or LUT is null"); return pe::apply_lut(*img, *lut, strength); });
}
pe_status pe_add_border(const pe_image* img, const pe_border* b, pe_image** out) {
  return output(out, [&] { pe::require(img && b, "Image or border is null"); return pe::add_border(*img, *b); });
}
pe_status pe_encode(const pe_image* img, const char* path, pe_format format,
                    uint32_t quality, uint32_t metadata) {
  return guard([&] {
    pe::require(img && path, "Image or destination is null");
    pe::require(quality >= 1 && quality <= 100, "Quality must be between 1 and 100");
    pe::encode(*img, path, format, quality, metadata != 0);
  });
}
void pe_image_free(pe_image* img) { delete img; }
void pe_lut_free(pe_lut* lut) { delete lut; }
}
