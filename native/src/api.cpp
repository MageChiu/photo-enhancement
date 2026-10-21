#include "internal.hpp"
#ifdef PE_HAVE_HEIF
#include <libheif/heif.h>
#endif
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
uint64_t pe_memory_budget(uint32_t preview) { return pe::memory_budget(preview != 0); }
const char* pe_last_error() { return last_error.data(); }
const char* pe_capabilities() {
#define PE_BASE_CAPABILITIES "jpeg,png,raw,icc,exif,lut,border,frame,hsl,curve,split-tone,sharpen,denoise"
#ifdef PE_HAVE_WEBP
#define PE_CODEC_CAPABILITIES PE_BASE_CAPABILITIES ",webp"
#else
#define PE_CODEC_CAPABILITIES PE_BASE_CAPABILITIES
#endif
#ifdef PE_HAVE_HEIF
  return heif_have_encoder_for_format(heif_compression_HEVC)
    ? PE_CODEC_CAPABILITIES ",heif,heif-encode" : PE_CODEC_CAPABILITIES ",heif";
#else
  return PE_CODEC_CAPABILITIES;
#endif
#undef PE_CODEC_CAPABILITIES
#undef PE_BASE_CAPABILITIES
}
void pe_default_decode_options(pe_decode_options* p) {
  if (p) *p = {1600, uint32_t(pe::pixel_limit), 512ULL * 1024 * 1024};
}
void pe_default_params(pe_params* p) { if (p) *p = {}; }
void pe_default_border(pe_border* p) { if (p) { *p = {}; p->color = 0xffffff; } }
void pe_default_effects(pe_effects* p) {
  if (p) { *p = {}; for (int i = 0; i < 5; ++i) p->curve[i] = i / 4.0; p->radius = 1; }
}
pe_status pe_apply_effects(const pe_image* img, const pe_effects* p, pe_image** out) {
  return output(out, [&] {
    pe::require(img && p, "Image or effects are null");
    return pe::apply_effects(*img, *p);
  });
}
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
pe_status pe_process_file(const char* source,const char* destination,const pe_decode_options* options,
    const pe_params* params,const pe_effects* effects,const char* lut_path,double strength,
    const pe_border* border,pe_format format,uint32_t quality,uint32_t metadata,uint64_t budget) {
  return guard([&]{
    pe::require(source&&destination&&options&&params&&effects&&border,"Missing export arguments");
    pe::require(options->max_pixels&&options->max_pixels<=pe::pixel_limit&&options->max_file_bytes,"Invalid decode limits");
    pe::require(quality>=1&&quality<=100,"Quality must be between 1 and 100");
    pe::require(format==PE_JPEG||format==PE_PNG||format==PE_HEIF,"Unsupported output format",PE_UNSUPPORTED);
    pe::process_file(source,destination,*options,*params,*effects,lut_path,strength,*border,format,quality,metadata!=0,budget);
  });
}
pe_status pe_add_frame(const pe_image* img,const pe_frame* frame,pe_image** out) {
  return output(out,[&]{
    pe::require(img&&frame,"Missing frame arguments");pe::validate_frame(*frame);
    auto logo=pe::frame_logo(*frame);
    return pe::add_border(*img,frame->border,frame,logo.get());
  });
}
pe_status pe_read_lens_info(const char* path,uint64_t limit,pe_lens_info* out) {
  if(out)*out={};
  return guard([&]{pe::require(path&&out&&limit,"Missing lens metadata arguments");*out=pe::probe_image(path,limit).metadata.lens;});
}
pe_status pe_add_frame_advanced(const pe_image* img,const pe_frame* frame,const pe_frame_adjustments* a,pe_image** out) {
  return output(out,[&]{
    pe::require(img&&frame&&a,"Missing advanced frame arguments");pe::validate_frame(*frame);pe::validate_frame_adjustments(*a);
    auto logo=pe::frame_logo(*frame);pe_frame other{};std::memcpy(other.logo_path,a->accent_path,sizeof(other.logo_path));
    auto accent=pe::frame_logo(other);
    if(!a->enabled)logo=pe::combined_frame_logo(std::move(logo),accent.get());
    return pe::add_border(*img,frame->border,frame,logo.get(),a,accent.get());
  });
}
pe_status pe_analyze_look(const pe_image* image,pe_look_analysis* analysis){
  if(analysis)*analysis={};
  return guard([&]{pe::require(image&&analysis,"Missing look analysis arguments");*analysis=pe::analyze_look(*image);});
}
pe_status pe_apply_look(const pe_image* image,const pe_lut* lut,double strength,
    const pe_look* look,const pe_look_analysis* analysis,uint32_t width,uint32_t height,uint32_t origin,pe_image** out){
  return output(out,[&]{
    pe::require(image&&lut&&look&&analysis,"Missing look arguments");
    return pe::apply_look(*image,*lut,strength,*look,*analysis,width,height,origin);
  });
}
pe_status pe_process_file_look(const char* source,const char* destination,const pe_decode_options* options,
    const pe_params* params,const pe_effects* effects,const char* lut_path,double strength,
    const pe_frame* frame,const pe_look* look,pe_format format,uint32_t quality,uint32_t metadata,uint64_t budget){
  return guard([&]{
    pe::require(source&&destination&&options&&params&&effects&&frame&&look,"Missing export arguments");
    pe::require(options->max_pixels&&options->max_pixels<=pe::pixel_limit&&options->max_file_bytes,"Invalid decode limits");
    pe::require(quality>=1&&quality<=100,"Quality must be between 1 and 100");
    pe::require(format==PE_JPEG||format==PE_PNG||format==PE_HEIF,"Unsupported output format",PE_UNSUPPORTED);
    pe::validate_frame(*frame);pe::validate_look(*look);
    pe::process_file(source,destination,*options,*params,*effects,lut_path,strength,frame->border,format,quality,metadata!=0,budget,frame,look);
  });
}
pe_status pe_process_file_advanced(const char* source,const char* destination,const pe_decode_options* options,
    const pe_params* params,const pe_effects* effects,const char* lut_path,double strength,
    const pe_frame* frame,const pe_look* look,const pe_frame_adjustments* a,pe_format format,uint32_t quality,uint32_t metadata,uint64_t budget){
  return guard([&]{
    pe::require(source&&destination&&options&&params&&effects&&frame&&look&&a,"Missing advanced export arguments");
    pe::require(options->max_pixels&&options->max_pixels<=pe::pixel_limit&&options->max_file_bytes,"Invalid decode limits");
    pe::require(quality>=1&&quality<=100,"Quality must be between 1 and 100");
    pe::require(format==PE_JPEG||format==PE_PNG||format==PE_HEIF,"Unsupported output format",PE_UNSUPPORTED);
    pe::validate_frame(*frame);pe::validate_look(*look);pe::validate_frame_adjustments(*a);
    pe::process_file(source,destination,*options,*params,*effects,lut_path,strength,frame->border,format,quality,metadata!=0,budget,frame,look,a);
  });
}
pe_status pe_process_file_frame(const char* source,const char* destination,const pe_decode_options* options,
    const pe_params* params,const pe_effects* effects,const char* lut_path,double strength,
    const pe_frame* frame,pe_format format,uint32_t quality,uint32_t metadata,uint64_t budget) {
  return guard([&]{
    pe::require(source&&destination&&options&&params&&effects&&frame,"Missing export arguments");
    pe::require(options->max_pixels&&options->max_pixels<=pe::pixel_limit&&options->max_file_bytes,"Invalid decode limits");
    pe::require(quality>=1&&quality<=100,"Quality must be between 1 and 100");
    pe::require(format==PE_JPEG||format==PE_PNG||format==PE_HEIF,"Unsupported output format",PE_UNSUPPORTED);
    pe::validate_frame(*frame);
    pe::process_file(source,destination,*options,*params,*effects,lut_path,strength,frame->border,format,quality,metadata!=0,budget,frame);
  });
}

}
