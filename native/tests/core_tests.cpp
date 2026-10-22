#include "pe/pe.h"
#include "../src/internal.hpp"
#include "../src/raw_support.hpp"
#include "../src/heif_support.hpp"
#include <cstddef>
#include <cstdio>
#include <jpeglib.h>
#include <lcms2.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
#include <unistd.h>

namespace fs = std::filesystem;
using Image = std::unique_ptr<pe_image, decltype(&pe_image_free)>;
namespace {
fs::path dir;
void expect(bool condition, const char* message = "Assertion failed") { if (!condition) throw std::runtime_error(message); }
void ok(pe_status s) { if (s != PE_OK) throw std::runtime_error(pe_last_error()); }
Image fixture(uint32_t w = 32, uint32_t h = 24) {
  std::vector<uint16_t> pixels(size_t(w) * h * 4);
  for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) {
    size_t p = (size_t(y) * w + x) * 4;
    pixels[p] = uint16_t(x * 60000 / w); pixels[p + 1] = uint16_t(y * 60000 / h);
    pixels[p + 2] = 10000; pixels[p + 3] = 65535;
  }
  pe_image* out = nullptr; ok(pe_image_create(w, h, pixels.data(), &out)); return Image(out, pe_image_free);
}
pe_image_info info(const Image& img) { pe_image_info i; ok(pe_image_get_info(img.get(), &i)); return i; }
std::vector<uint8_t> pixels(const Image& img) {
  auto i = info(img); std::vector<uint8_t> result(size_t(i.width) * i.height * 4);
  ok(pe_image_copy_rgba8(img.get(), result.data(), result.size())); return result;
}
Image decode(const fs::path& path, uint32_t edge = 0) {
  pe_decode_options options; pe_default_decode_options(&options); options.max_edge = edge;
  pe_image* result = nullptr; ok(pe_decode(path.c_str(), &options, &result)); return Image(result, pe_image_free);
}
void text(const fs::path& path, const std::string& content) { std::ofstream(path) << content; }
void lut_file(const fs::path& path, bool swap = false) {
  std::ofstream out(path); out << "TITLE \"test\"\nLUT_3D_SIZE 2\n";
  for (int b = 0; b < 2; ++b) for (int g = 0; g < 2; ++g) for (int r = 0; r < 2; ++r)
    out << (swap ? b : r) << ' ' << g << ' ' << (swap ? r : b) << '\n';
}
// Small real JPEG fixture with arbitrary EXIF orientation, ICC and metadata.
void jpeg_fixture(const fs::path& path, int orientation, bool icc, uint32_t width=12, uint32_t height=8) {
  FILE* file = std::fopen(path.c_str(), "wb"); expect(file);
  jpeg_compress_struct c{}; jpeg_error_mgr error{}; c.err = jpeg_std_error(&error);
  jpeg_create_compress(&c); jpeg_stdio_dest(&c, file);
  c.image_width = width; c.image_height = height; c.input_components = 3; c.in_color_space = JCS_RGB;
  jpeg_set_defaults(&c); jpeg_set_quality(&c, 100, TRUE); jpeg_start_compress(&c, TRUE);
  std::vector<uint8_t> exif{'E','x','i','f',0,0,'I','I',42,0,8,0,0,0,
    2,0, 0x12,1,3,0,1,0,0,0,uint8_t(orientation),0,0,0,
    0x10,1,2,0,6,0,0,0,38,0,0,0, 0,0,0,0,'C','A','M','E','R',0};
  jpeg_write_marker(&c, JPEG_APP0 + 1, exif.data(), static_cast<unsigned int>(exif.size()));
  if (icc) {
    cmsHPROFILE profile = cmsCreate_sRGBProfile(); cmsUInt32Number n = 0;
    cmsSaveProfileToMem(profile, nullptr, &n); std::vector<uint8_t> marker(14 + n);
    std::memcpy(marker.data(), "ICC_PROFILE\0", 12); marker[12] = 1; marker[13] = 1;
    cmsSaveProfileToMem(profile, marker.data() + 14, &n); cmsCloseProfile(profile);
    jpeg_write_marker(&c, JPEG_APP0 + 2, marker.data(), static_cast<unsigned int>(marker.size()));
  }
  std::vector<uint8_t> row(size_t(width)*3);
  while (c.next_scanline < c.image_height) {
    for (uint32_t x = 0; x < width; ++x) { row[x * 3] = uint8_t(x * 240 / width); row[x * 3 + 1] = uint8_t(c.next_scanline * 240 / height); row[x * 3 + 2] = 100; }
    JSAMPROW p = row.data(); jpeg_write_scanlines(&c, &p, 1);
  }
  jpeg_finish_compress(&c); jpeg_destroy_compress(&c); std::fclose(file);
}
}

int main() {
  dir = fs::temp_directory_path() / ("photo-core-tests-" + std::to_string(getpid()));
  fs::create_directories(dir);
  std::vector<std::pair<std::string, std::function<void()>>> tests{
    {"lens metadata survives JPEG PNG edits and excludes unknown fields", [] {
      auto source=fixture(480,360);
      std::strcpy(source->info.make,"SONY");std::strcpy(source->info.model,"ILCE-7RM5");
      std::strcpy(source->lens.make,"SIGMA");std::strcpy(source->lens.model,"24-70mm F2.8 DG DN | Art");
      pe_lens_info lens{};
      for(auto format:{PE_JPEG,PE_PNG}){
        const auto input=dir/(format==PE_JPEG?"lens.jpg":"lens.png");
        ok(pe_encode(source.get(),input.c_str(),format,95,1));
        ok(pe_read_lens_info(input.c_str(),16*1024*1024,&lens));
        expect(std::string(lens.make)=="SIGMA" && std::string(lens.model)==source->lens.model);
        auto decoded=decode(input);pe_params params;pe_default_params(&params);params.exposure=.1;
        pe_image* raw=nullptr;ok(pe_apply_params(decoded.get(),&params,&raw));Image edited(raw,pe_image_free);
        const auto output=dir/(format==PE_JPEG?"lens-edited.jpg":"lens-edited.png");
        ok(pe_encode(edited.get(),output.c_str(),format,95,1));
        ok(pe_read_lens_info(output.c_str(),16*1024*1024,&lens));expect(std::string(lens.make)=="SIGMA");
        const auto stripped=dir/(format==PE_JPEG?"lens-private.jpg":"lens-private.png");
        ok(pe_encode(edited.get(),stripped.c_str(),format,95,0));
        ok(pe_read_lens_info(stripped.c_str(),16*1024*1024,&lens));expect(!lens.make[0] && !lens.model[0]);
      }
      expect(pe_read_lens_info(nullptr,1,&lens)==PE_INVALID_ARGUMENT && !lens.make[0]);
    }},
    {"advanced frame independent artwork matches strip export and validates bounds", [] {
      auto source=fixture(480,360);std::strcpy(source->info.model,"Camera");source->info.iso=100;
      const auto input=dir/"advanced-source.jpg";ok(pe_encode(source.get(),input.c_str(),PE_JPEG,95,1));source=decode(input);
      pe_frame frame{};pe_default_border(&frame.border);frame.border.width=.04;frame.border.bottom=.3;frame.fields=17|64;
      const auto font=(fs::path(PE_SOURCE_DIR)/"app/assets/frame_fonts/sans.ttf").string();std::strcpy(frame.border.font_path,font.c_str());
      std::strcpy(frame.border.text,"My photograph");
      const auto logo=(fs::path(PE_SOURCE_DIR)/"app/assets/frame_marks/hasselblad-h-2.png").string();std::strcpy(frame.logo_path,logo.c_str());
      pe_frame_adjustments a{};a.enabled=1;a.info_align=2;a.caption_align=1;
      const double positions[]{0,0,.45,1,1,.3,1,.35,.18,.25,.5,.85,.13};std::copy_n(positions,13,a.positions);
      const auto accent=(fs::path(PE_SOURCE_DIR)/"app/assets/frame_marks/sony-gm-2.png").string();std::strcpy(a.accent_path,accent.c_str());
      pe_image* raw=nullptr;ok(pe_add_frame_advanced(source.get(),&frame,&a,&raw));Image expected(raw,pe_image_free);
      const auto expected_path=dir/"advanced-full.png";ok(pe_encode(expected.get(),expected_path.c_str(),PE_PNG,95,1));
      pe_params params;pe_default_params(&params);pe_effects effects;pe_default_effects(&effects);pe_look look{};
      pe_decode_options options;pe_default_decode_options(&options);
      const auto output=dir/"advanced-strip.png";
      ok(pe_process_file_advanced(input.c_str(),output.c_str(),&options,&params,&effects,nullptr,1,&frame,&look,&a,PE_PNG,95,1,0));
      expect(pixels(decode(output))==pixels(decode(expected_path)),"Advanced strip and full output differ");
      const auto before=pixels(expected);a.positions[0]=1;ok(pe_add_frame_advanced(source.get(),&frame,&a,&raw));Image moved(raw,pe_image_free);
      expect(before!=pixels(moved),"Primary artwork did not move");
      a.positions[0]=std::numeric_limits<double>::quiet_NaN();expect(pe_add_frame_advanced(source.get(),&frame,&a,&raw)==PE_INVALID_ARGUMENT && !raw);
    }},
    {"HEIF grid memory plan bounds codec pixels without ignoring compressed tiles", [] {
      constexpr uint64_t MiB=1024ULL*1024;
      expect(!pe::heif_grid(1024,1024) && pe::heif_grid(1025,1) && pe::heif_grid(1,1025));
      const auto memory=pe::heif_export_memory(11648,8736);
      expect(memory<2297*MiB && memory>uint64_t(11648)*8736*8);
      expect(pe::heif_export_memory(14000,11000)>memory);
      expect(pe::heif_export_memory(32768,32768)>2297*MiB,
          "Compressed tile storage must still reject unreasonable outputs");
    }},
    {"HEIF grid preserves odd dimensions orientation metadata and strip boundaries", [] {
      if(!std::strstr(pe_capabilities(),"heif-encode")) {
        std::cout<<"SKIP HEIF grid encode: no HEVC encoder available\n";return;
      }
      const auto input=dir/"grid-source.jpg";
      jpeg_fixture(input,6,true,1051,2053);
      auto expected=decode(input);pe_image* out=nullptr;
      pe_params params;pe_default_params(&params);params.exposure=.1;
      pe_effects effects;pe_default_effects(&effects);effects.sharpen=.3;effects.radius=2;
      pe_border border;pe_default_border(&border);border.width=.01;border.bottom=.02;
      ok(pe_apply_params(expected.get(),&params,&out));expected.reset(out);
      ok(pe_apply_effects(expected.get(),&effects,&out));expected.reset(out);
      ok(pe_add_border(expected.get(),&border,&out));expected.reset(out);
      const auto full=dir/"grid-full.heic",strip=dir/"grid-strip.heic";
      ok(pe_encode(expected.get(),full.c_str(),PE_HEIF,95,1));
      pe_decode_options options;pe_default_decode_options(&options);
      ok(pe_process_file(input.c_str(),strip.c_str(),&options,&params,&effects,nullptr,1,
          &border,PE_HEIF,95,1,256ULL*1024*1024));
      auto actual=decode(strip),reference=decode(full);
      expect(info(actual).width==info(expected).width && info(actual).height==info(expected).height);
      expect(std::string(info(actual).model)=="CAMER","Grid lost main-image EXIF");
      const auto actual_pixels=pixels(actual),reference_pixels=pixels(reference),original=pixels(expected);
      expect(actual_pixels==reference_pixels,"HEIF grid strip boundary or orientation mismatch");
      uint64_t difference=0;
      for(size_t p=0;p<original.size();p+=4)for(size_t c=0;c<3;++c)
        difference+=uint64_t(std::abs(int(actual_pixels[p+c])-int(original[p+c])));
      expect(double(difference)/(original.size()/4*3)<4,"Grid colour profile or crop mismatch");
      const auto rejected=dir/"grid-rejected.heic";
      expect(pe_process_file(input.c_str(),rejected.c_str(),&options,&params,&effects,nullptr,1,
          &border,PE_HEIF,95,1,64ULL*1024*1024)==PE_LIMIT_EXCEEDED);
      expect(std::strstr(pe_last_error(),"HEIF") && !fs::exists(rejected));
    }},
    {"RAW half preview stages retain sensor cost without full RGB multiplier", [] {
      constexpr uint64_t MiB=1024ULL*1024;
      const auto preview=pe::raw_preview_memory(7360,5000,7360,4912);
      expect(uint64_t(7360)*5000*64>2176*MiB);
      expect(preview.peak()<2176*MiB,"Normal RAW preview still charged full RGB cost");
      expect(preview.unpack>=uint64_t(7360)*5000*2,"Full sensor unpack disappeared from estimate");
      expect(pe::raw_preview_memory(16000,12000,16000,12000).peak()>2176*MiB,
          "Oversized RAW preview must still exceed a limited budget");
      const auto odd=pe::raw_preview_memory(6001,4001,6001,4001);
      const auto even=pe::raw_preview_memory(6000,4000,6000,4000);
      expect(odd.process>even.process && odd.output>even.output,"Odd dimensions must round up");
    }},
    {"RAW packed RGB expands safely inside RGBA rows", [] {
      for(uint32_t colors:{3U,4U})for(uint32_t width:{1U,2U,3U,7U}){
        std::vector<uint16_t> row(width*4+8,54321);
        for(uint32_t x=0;x<width;++x)for(uint32_t c=0;c<colors;++c)
          row[x*colors+c]=uint16_t(x*100+c+1);
        const auto packed=row;
        pe::expand_raw_row(row.data(),width,colors);
        for(uint32_t x=0;x<width;++x){
          for(uint32_t c=0;c<3;++c)expect(row[x*4+c]==packed[x*colors+c]);
          expect(row[x*4+3]==65535);
        }
        for(size_t i=width*4;i<row.size();++i)expect(row[i]==54321,"RAW row expansion overflow");
      }
    }},
    {"Hanjun analysis is bounded and does not guess colour from saturated subjects", [] {
      expect(sizeof(pe_look)==40 && sizeof(pe_look_analysis)==32);
      std::vector<uint16_t> warm(128*128*4);
      for(size_t i=0;i<warm.size()/4;++i){
        const uint16_t grey=uint16_t(6000+(i%128)*240);
        warm[i*4]=uint16_t(grey*1.12);warm[i*4+1]=grey;warm[i*4+2]=uint16_t(grey*.9);warm[i*4+3]=65535;
      }
      pe_image* raw=nullptr;ok(pe_image_create(128,128,warm.data(),&raw));Image source(raw,pe_image_free);
      pe_look_analysis analysis{};ok(pe_analyze_look(source.get(),&analysis));
      expect(analysis.temperature<0 && analysis.temperature>=-.20 && analysis.confidence>.2);
      expect(analysis.exposure>=-.35 && analysis.exposure<=.45);
      for(size_t i=0;i<warm.size();i+=4){warm[i]=40000;warm[i+1]=1000;warm[i+2]=1000;}
      ok(pe_image_create(128,128,warm.data(),&raw));Image red(raw,pe_image_free);
      ok(pe_analyze_look(red.get(),&analysis));expect(analysis.confidence==0 && analysis.temperature==0);
      const auto lut_path=fs::path(PE_SOURCE_DIR)/"app/assets/luts/hanjun.cube";
      pe_lut* handle=nullptr;ok(pe_lut_load(lut_path.c_str(),&handle));
      std::unique_ptr<pe_lut,decltype(&pe_lut_free)> lut(handle,pe_lut_free);
      pe_look look{1,1,.7,.35,.25,.2};
      ok(pe_apply_look(source.get(),lut.get(),0,&look,&analysis,128,128,0,&raw));Image zero(raw,pe_image_free);
      expect(pixels(zero)==pixels(source),"Zero strength must remove adaptation and spatial effects");
      analysis.exposure=std::numeric_limits<double>::quiet_NaN();
      expect(pe_apply_look(source.get(),lut.get(),1,&look,&analysis,128,128,0,&raw)==PE_INVALID_ARGUMENT && !raw);
    }},
    {"Hanjun full and striped rendering share analysis halo and global coordinates", [] {
      auto source=fixture(400,360);const auto input=dir/"hanjun-source.jpg";
      ok(pe_encode(source.get(),input.c_str(),PE_JPEG,98,1));source=decode(input);
      pe_look_analysis analysis{};ok(pe_analyze_look(source.get(),&analysis));
      pe_params params;pe_default_params(&params);params.exposure=.2;
      pe_effects effects;pe_default_effects(&effects);effects.sharpen=.4;effects.radius=3;
      pe_frame frame{};pe_default_border(&frame.border);
      pe_look look{1,1,.7,.6,.4,.3};
      const auto lut_path=fs::path(PE_SOURCE_DIR)/"app/assets/luts/hanjun.cube";
      pe_lut* handle=nullptr;ok(pe_lut_load(lut_path.c_str(),&handle));
      std::unique_ptr<pe_lut,decltype(&pe_lut_free)> lut(handle,pe_lut_free);
      pe_image* raw=nullptr;ok(pe_apply_params(source.get(),&params,&raw));source.reset(raw);
      ok(pe_apply_effects(source.get(),&effects,&raw));source.reset(raw);
      ok(pe_apply_look(source.get(),lut.get(),.85,&look,&analysis,400,360,0,&raw));Image expected(raw,pe_image_free);
      const auto expected_path=dir/"hanjun-full.png";ok(pe_encode(expected.get(),expected_path.c_str(),PE_PNG,95,1));
      pe_decode_options options;pe_default_decode_options(&options);
      const auto output=dir/"hanjun-stream.png";
      ok(pe_process_file_look(input.c_str(),output.c_str(),&options,&params,&effects,lut_path.c_str(),.85,&frame,&look,PE_PNG,95,1,0));
      auto actual=decode(output),reference=decode(expected_path);
      expect(pixels(actual)==pixels(reference),"Hanjun strip seam or repeated grain/vignette detected");
      const auto missing=dir/"hanjun-no-lut.png";
      expect(pe_process_file_look(input.c_str(),missing.c_str(),&options,&params,&effects,nullptr,.85,&frame,&look,PE_PNG,95,1,0)==PE_INVALID_ARGUMENT);
      expect(!fs::exists(missing));
    }},
    {"ABI and defaults", [] {
      expect(pe_abi_version() == 1); pe_decode_options o; pe_default_decode_options(&o);
      expect(o.max_edge == 1600 && o.max_pixels == 32768U*32768U); expect(std::strstr(pe_capabilities(), "raw"));
      expect(pe_memory_budget(1)<=pe_memory_budget(0) && pe_memory_budget(1)<=128ULL*1024*1024);
    }},
    {"C ABI null safety", [] {
      expect(pe_decode(nullptr, nullptr, nullptr) == PE_INVALID_ARGUMENT);
      expect(pe_image_get_info(nullptr, nullptr) == PE_INVALID_ARGUMENT);
      pe_image* out = reinterpret_cast<pe_image*>(1);
      expect(pe_image_create(0, 1, nullptr, &out) == PE_INVALID_ARGUMENT && out == nullptr);
      pe_image_free(nullptr); pe_lut_free(nullptr);
    }},
    {"dimension and allocation limits", [] {
      uint16_t pixel[4]{}; pe_image* out = nullptr;
      expect(pe_image_create(UINT32_MAX, UINT32_MAX, pixel, &out) == PE_LIMIT_EXCEEDED);
      expect(pe_image_create(32769, 1, pixel, &out) == PE_LIMIT_EXCEEDED);
    }},
    {"RGBA conversion and buffer bounds", [] {
      uint16_t pixel[]{0, 32768, 65535, 32768}; pe_image* handle = nullptr;
      ok(pe_image_create(1, 1, pixel, &handle)); Image img(handle, pe_image_free); auto p = pixels(img);
      expect(p[0] == 0 && std::abs(int(p[1]) - 188) <= 1 && p[2] == 255 && p[3] == 128);
      expect(pe_image_copy_rgba8(img.get(), p.data(), 3) == PE_INVALID_ARGUMENT);
    }},
    {"identity preserves pixels and source lifetime", [] {
      auto src = fixture(); auto before = pixels(src); pe_params p; pe_default_params(&p); pe_image* out = nullptr;
      ok(pe_apply_params(src.get(), &p, &out)); Image result(out, pe_image_free); src.reset(); expect(pixels(result) == before);
    }},
    {"all ten controls and NaN rejection", [] {
      auto src = fixture(); auto before = pixels(src); pe_image* out = nullptr;
      for (int c = 0; c < 10; ++c) {
        pe_params p{}; double* field = nullptr;
        switch(c) { case 0: field=&p.exposure; break; case 1: field=&p.contrast; break;
          case 2: field=&p.highlights; break; case 3: field=&p.shadows; break; case 4: field=&p.whites; break;
          case 5: field=&p.blacks; break; case 6: field=&p.temperature; break; case 7: field=&p.tint; break;
          case 8: field=&p.saturation; break; default: field=&p.vibrance; }
        *field = .5; ok(pe_apply_params(src.get(), &p, &out)); Image changed(out, pe_image_free);
        expect(pixels(changed) != before); expect(pixels(src) == before);
        *field = std::numeric_limits<double>::quiet_NaN();
        expect(pe_apply_params(src.get(), &p, &out) == PE_INVALID_ARGUMENT && out == nullptr);
      }
    }},
    {"resize aspect and no upscale", [] {
      auto src = fixture(); pe_image* out = nullptr; ok(pe_resize(src.get(), 16, &out)); Image small(out, pe_image_free);
      expect(info(small).width == 16 && info(small).height == 12);
      ok(pe_resize(src.get(), 100, &out)); Image same(out, pe_image_free); expect(pixels(same) == pixels(src));
    }},
    {"advanced defaults, ownership, alpha and validation", [] {
      expect(sizeof(pe_effects)==304);
      auto src=fixture(); auto before=pixels(src); pe_effects e; pe_default_effects(&e); pe_image* raw=nullptr;
      ok(pe_apply_effects(src.get(),&e,&raw)); Image identity(raw,pe_image_free); expect(pixels(identity)==before);
      e.curve[2]=.7; ok(pe_apply_effects(src.get(),&e,&raw)); Image changed(raw,pe_image_free);
      expect(pixels(changed)!=before && pixels(src)==before);
      auto after=pixels(changed); for(size_t i=3;i<before.size();i+=4) expect(after[i]==before[i]);
      e.radius=std::numeric_limits<double>::quiet_NaN();
      expect(pe_apply_effects(src.get(),&e,&raw)==PE_INVALID_ARGUMENT && raw==nullptr);
    }},
    {"HSL and split tone preserve alpha and change color", [] {
      uint16_t value[]{50000,10000,6000,32768};pe_image* raw=nullptr;ok(pe_image_create(1,1,value,&raw));Image src(raw,pe_image_free);
      auto before=pixels(src); pe_effects e; pe_default_effects(&e);e.hue[0]=1;
      ok(pe_apply_effects(src.get(),&e,&raw));Image hsl(raw,pe_image_free);expect(pixels(hsl)!=before && pixels(hsl)[3]==before[3]);
      pe_default_effects(&e);e.shadow_hue=.6;e.shadow_strength=1;
      ok(pe_apply_effects(src.get(),&e,&raw));Image tone(raw,pe_image_free);expect(pixels(tone)!=before && pixels(tone)[3]==before[3]);
    }},
    {"denoise reduces flat-field noise and sharpening preserves flat fields", [] {
      std::vector<uint16_t> value(9*9*4,65535);
      for(size_t p=0;p<value.size();p+=4)for(int c=0;c<3;++c)value[p+c]=uint16_t(28000+((p/4)%2?700:-700));
      pe_image* raw=nullptr;ok(pe_image_create(9,9,value.data(),&raw));Image src(raw,pe_image_free);
      pe_effects e;pe_default_effects(&e);e.denoise_luma=1;e.denoise_chroma=1;
      ok(pe_apply_effects(src.get(),&e,&raw));Image clean(raw,pe_image_free);
      auto variance=[](const std::vector<uint8_t>& rgb) {
        double mean=0;for(size_t p=0;p<rgb.size();p+=4)mean+=rgb[p];mean/=rgb.size()/4;
        double sum=0;for(size_t p=0;p<rgb.size();p+=4)sum+=std::pow(rgb[p]-mean,2);return sum;
      };
      expect(variance(pixels(clean))<variance(pixels(src))*.5);
      std::fill(value.begin(),value.end(),30000);ok(pe_image_create(9,9,value.data(),&raw));Image flat(raw,pe_image_free);
      pe_default_effects(&e);e.sharpen=2;ok(pe_apply_effects(flat.get(),&e,&raw));Image sharp(raw,pe_image_free);expect(pixels(sharp)==pixels(flat));
    }},
    {"premultiplied-alpha resize prevents halos", [] {
      uint16_t p[]{65535,0,0,65535, 0,65535,0,0}; pe_image* handle = nullptr;
      ok(pe_image_create(2,1,p,&handle)); Image src(handle,pe_image_free);
      ok(pe_resize(src.get(),1,&handle)); Image small(handle,pe_image_free); auto v=pixels(small);
      expect(v[0]==255 && v[1]==0 && v[2]==0 && v[3]==128);
    }},
    {"LUT identity, interpolation, axis order and alpha", [] {
      lut_file(dir/"identity.cube"); lut_file(dir/"swap.cube",true); auto src=fixture();
      for (bool swap : {false,true}) {
        pe_lut* raw=nullptr; ok(pe_lut_load((dir/(swap ? "swap.cube":"identity.cube")).c_str(),&raw));
        std::unique_ptr<pe_lut,decltype(&pe_lut_free)> lut(raw,pe_lut_free); pe_image* out=nullptr;
        ok(pe_apply_lut(src.get(),lut.get(),1,&out)); Image result(out,pe_image_free); auto a=pixels(src), b=pixels(result);
        for(size_t i=0;i<a.size();i+=4) {
          expect(std::abs(int(a[i])-b[i+(swap?2:0)])<=1); expect(std::abs(int(a[i+2])-b[i+(swap?0:2)])<=1);
          expect(a[i+1]==b[i+1] && a[i+3]==b[i+3]);
        }
        ok(pe_apply_lut(src.get(),lut.get(),0,&out)); Image zero(out,pe_image_free); expect(pixels(zero)==a);
        expect(pe_apply_lut(src.get(),lut.get(),2,&out)==PE_INVALID_ARGUMENT);
      }
    }},
    {"malformed LUT validation", [] {
      for (const auto& content : {"LUT_3D_SIZE 1\n", "LUT_3D_SIZE 999999\n", "LUT_1D_SIZE 2\n", "LUT_3D_SIZE 2\n0 0 0\n", "DOMAIN_MIN nan 0 0\n"}) {
        text(dir/"bad.cube",content); pe_lut* lut=nullptr; expect(pe_lut_load((dir/"bad.cube").c_str(),&lut)!=PE_OK && !lut);
      }
    }},
    {"damaged LUT reports filename and exact row without skipping data", [] {
      const auto path=dir/"damaged.cube";
      text(path,std::string("LUT_3D_SIZE 2\n0.0381801 ")+std::string(16,'\0')+"\n");
      pe_lut* lut=nullptr;
      expect(pe_lut_load(path.c_str(),&lut)==PE_INVALID_ARGUMENT && !lut);
      expect(std::strstr(pe_last_error(),"damaged.cube") && std::strstr(pe_last_error(),"第 2 行"));
      text(path,"LUT_3D_SIZE 2\n0.0381801\n");
      expect(pe_lut_load(path.c_str(),&lut)==PE_INVALID_ARGUMENT && !lut);
      expect(std::strstr(pe_last_error(),"damaged.cube") && std::strstr(pe_last_error(),"第 2 行"));
    }},
    {"border dimensions, pixels and invalid caption", [] {
      auto src=fixture(100,80); pe_border b; pe_default_border(&b); b.width=.1; b.color=0xff0000; pe_image* out=nullptr;
      ok(pe_add_border(src.get(),&b,&out)); Image framed(out,pe_image_free); auto i=info(framed); auto p=pixels(framed);
      expect(i.width==116 && i.height==96 && p[0]==255 && p[1]==0 && p[3]==255);
      b.bottom=.2; std::strcpy(b.text,"caption"); expect(pe_add_border(src.get(),&b,&out)==PE_INVALID_ARGUMENT);
      b.width=1; expect(pe_add_border(src.get(),&b,&out)==PE_INVALID_ARGUMENT);
    }},
    {"FreeType caption rasterization", [] {
      const char* font="/System/Library/Fonts/Supplemental/Arial.ttf";
      if (!fs::exists(font)) { std::cout<<"  (system font unavailable; skip font fixture)\n"; return; }
      auto src=fixture(600,400); pe_border b; pe_default_border(&b); b.width=.04; b.bottom=.18;
      std::strcpy(b.text,"CAMERA   f/2.8   1/250s   ISO 100"); std::strcpy(b.font_path,font); pe_image* out=nullptr;
      ok(pe_add_border(src.get(),&b,&out)); Image framed(out,pe_image_free); expect(info(framed).height==504);
      ok(pe_encode(framed.get(),(dir/"caption.png").c_str(),PE_PNG,95,1));
    }},
    {"PNG16 roundtrip and content-based recognition", [] {
      auto src=fixture(); ok(pe_encode(src.get(),(dir/"photo.wrong-extension").c_str(),PE_PNG,95,1));
      auto result=decode(dir/"photo.wrong-extension"); auto a=pixels(src), b=pixels(result);
      expect(info(result).width==32 && info(result).height==24);
      for(size_t i=0;i<a.size();++i) expect(std::abs(int(a[i])-b[i])<=1);
    }},
    {"JPEG roundtrip, preview size and alpha flatten", [] {
      auto src=fixture(320,240); ok(pe_encode(src.get(),(dir/"photo.jpg").c_str(),PE_JPEG,95,0));
      auto result=decode(dir/"photo.jpg",80); expect(info(result).width==80 && info(result).height==60);
      uint16_t p[]{0,0,0,0}; pe_image* handle=nullptr; ok(pe_image_create(1,1,p,&handle)); Image transparent(handle,pe_image_free);
      ok(pe_encode(transparent.get(),(dir/"alpha.jpg").c_str(),PE_JPEG,100,0)); auto white=decode(dir/"alpha.jpg");
      expect(pixels(white)[0]>=254);
    }},
    {"JPEG preview limits apply to scaled decode, not original MP", [] {
      pe_decode_options options;pe_default_decode_options(&options);options.max_edge=80;options.max_pixels=5000;
      pe_image* handle=nullptr;ok(pe_decode((dir/"photo.jpg").c_str(),&options,&handle));Image img(handle,pe_image_free);
      expect(info(img).width==80 && info(img).height==60);
      options.max_edge=0;expect(pe_decode((dir/"photo.jpg").c_str(),&options,&handle)==PE_LIMIT_EXCEEDED && !handle);
    }},
    {"strip export matches full effects LUT border and caption across boundaries", [] {
      auto fixture_image=fixture(400,360);auto input=dir/"strip-source.jpg";
      ok(pe_encode(fixture_image.get(),input.c_str(),PE_JPEG,98,1));
      pe_params params;pe_default_params(&params);params.exposure=.3;params.temperature=.2;
      pe_effects effects;pe_default_effects(&effects);effects.sharpen=.8;effects.radius=3;
      effects.denoise_luma=.4;effects.denoise_chroma=.5;effects.curve[2]=.6;effects.hue[0]=.2;
      pe_border border;pe_default_border(&border);border.width=.05;border.bottom=.18;
      const char* font="/System/Library/Fonts/Supplemental/Arial.ttf";
      if(fs::exists(font)){std::strcpy(border.text,"CAMERA ISO 100");std::strcpy(border.font_path,font);}
      auto lut_path=dir/"strip.cube";lut_file(lut_path,true);pe_lut* raw_lut=nullptr;
      ok(pe_lut_load(lut_path.c_str(),&raw_lut));std::unique_ptr<pe_lut,decltype(&pe_lut_free)> lut(raw_lut,pe_lut_free);
      auto expected=decode(input);pe_image* out=nullptr;
      ok(pe_apply_params(expected.get(),&params,&out));expected.reset(out);
      ok(pe_apply_effects(expected.get(),&effects,&out));expected.reset(out);
      ok(pe_apply_lut(expected.get(),lut.get(),.7,&out));expected.reset(out);
      ok(pe_add_border(expected.get(),&border,&out));expected.reset(out);
      pe_decode_options options;pe_default_decode_options(&options);
      std::vector<pe_format> formats{PE_PNG,PE_JPEG};
      if(std::strstr(pe_capabilities(),"heif-encode"))formats.push_back(PE_HEIF);
      for(auto format:formats){
        const std::string extension=format==PE_PNG?"png":format==PE_JPEG?"jpg":"heic";
        auto actual_path=dir/("strip."+extension);
        auto expected_path=dir/("strip-expected."+extension);
        ok(pe_process_file(input.c_str(),actual_path.c_str(),&options,&params,&effects,lut_path.c_str(),.7,&border,format,95,1,64*1024*1024));
        ok(pe_encode(expected.get(),expected_path.c_str(),format,95,1));
        auto actual=decode(actual_path),reference=decode(expected_path);
        expect(info(actual).width==info(reference).width && info(actual).height==info(reference).height);
        expect(pixels(actual)==pixels(reference),"Strip boundary, effects or caption mismatch");
      }
      auto heif_rejected=dir/"strip-budget.heic";
      expect(pe_process_file(input.c_str(),heif_rejected.c_str(),&options,&params,&effects,
          nullptr,1,&border,PE_HEIF,95,1,1024)==PE_LIMIT_EXCEEDED);
      expect(std::strstr(pe_last_error(),"JPEG 400x360") &&
          std::strstr(pe_last_error(),"HEIF") && !fs::exists(heif_rejected));
      auto rejected=dir/"strip-budget.png";
      expect(pe_process_file(input.c_str(),rejected.c_str(),&options,&params,&effects,nullptr,1,&border,PE_PNG,95,1,1024)==PE_LIMIT_EXCEEDED);
      expect(std::strstr(pe_last_error(),"MiB") && !fs::exists(rejected));
      expect(pe_process_file(input.c_str(),input.c_str(),&options,&params,&effects,nullptr,1,&border,PE_PNG,95,1,0)==PE_IO_ERROR);
      auto broken=dir/"broken-strip.jpg";jpeg_fixture(broken,1,false);fs::resize_file(broken,180);
      auto failed=dir/"broken-strip.png";
      expect(pe_process_file(broken.c_str(),failed.c_str(),&options,&params,&effects,nullptr,1,&border,PE_PNG,95,1,0)!=PE_OK);
      expect(!fs::exists(failed));
    }},
    {"film and social templates preserve photo and match full versus strip export", [] {
      expect(sizeof(pe_frame_template)==3084,"Template ABI size changed");
      const std::string font=std::string(PE_SOURCE_DIR)+"/app/assets/frame_fonts/cjk.ttf";
      for(const auto dimensions: {std::pair<uint32_t,uint32_t>{480,320},{320,480}}){
        auto source=fixture(dimensions.first,dimensions.second);auto input=dir/("template-source-"+std::to_string(dimensions.first)+".jpg");
        ok(pe_encode(source.get(),input.c_str(),PE_JPEG,95,0));
        pe_decode_options options;pe_default_decode_options(&options);pe_image* raw=nullptr;
        ok(pe_decode(input.c_str(),&options,&raw));source.reset(raw);
        auto avatar=fixture(60,40);const auto avatar_path=dir/"template-avatar.png";if(!fs::exists(avatar_path))ok(pe_encode(avatar.get(),avatar_path.c_str(),PE_PNG,95,0));
        pe_params params{};pe_effects effects;pe_default_effects(&effects);pe_look look{};
        pe_frame frame{};pe_default_border(&frame.border);std::strcpy(frame.border.font_path,font.c_str());
        pe_frame_adjustments adjustments{};
        for(uint32_t kind=1;kind<=2;++kind)for(uint32_t variant=0;variant<(kind==1?2U:4U);++variant){
          pe_frame_template t{};t.kind=kind;t.variant=variant;t.flags=7;
          std::strcpy(t.title,kind==1?"FILM 400":"光屿摄影");std::strcpy(t.subtitle,kind==1?"07":"@photo");
          std::strcpy(t.body,"午后的海边，留住风和光。A quiet afternoon by the sea. A longer caption wraps around the card.\n第二段配文。");
          std::strcpy(t.location,"Seaside");std::strcpy(t.timestamp,"2026-10-21");std::strcpy(t.stats,"128 likes");std::strcpy(t.avatar_path,avatar_path.c_str());
          ok(pe_add_frame_template(source.get(),&frame,&adjustments,&t,&raw));Image expected(raw,pe_image_free);
          expect(info(expected).width>dimensions.first&&info(expected).height>dimensions.second);
          const auto label=std::to_string(dimensions.first)+"-"+std::to_string(kind)+"-"+std::to_string(variant);
          auto streamed=dir/("template-stream-"+label+".png"),reference=dir/("template-full-"+label+".png");
          ok(pe_encode(expected.get(),reference.c_str(),PE_PNG,95,0));
          ok(pe_process_file_template(input.c_str(),streamed.c_str(),&options,&params,&effects,nullptr,1,&frame,&look,&adjustments,&t,PE_PNG,95,0,64*1024*1024));
          ok(pe_decode(streamed.c_str(),&options,&raw));Image actual(raw,pe_image_free);
          ok(pe_decode(reference.c_str(),&options,&raw));Image decoded_reference(raw,pe_image_free);
          expect(pixels(actual)==pixels(decoded_reference),"Template strip output differs");
          // Turning optional decoration off must work with no avatar resource.
          t.flags=0;std::strcpy(t.avatar_path,"/missing/hidden-avatar.png");
          ok(pe_add_frame_template(source.get(),&frame,&adjustments,&t,&raw));pe_image_free(raw);
        }
        pe_frame_template invalid{};invalid.kind=3;
        expect(pe_add_frame_template(source.get(),&frame,&adjustments,&invalid,&raw)==PE_INVALID_ARGUMENT&&!raw);
        invalid.kind=2;std::memset(invalid.body,'a',sizeof(invalid.body));
        expect(pe_add_frame_template(source.get(),&frame,&adjustments,&invalid,&raw)==PE_INVALID_ARGUMENT&&!raw);
      }
    }},
    {"frame layouts fields logo and strip export agree", [] {
      expect(sizeof(pe_frame)==2600);
      auto source=fixture(400,360);auto input=dir/"frame-source.jpg";
      ok(pe_encode(source.get(),input.c_str(),PE_JPEG,95,1));source=decode(input);
      std::vector<uint16_t> logo_pixels(32*16*4,0);
      for(size_t p=0;p<logo_pixels.size();p+=4){logo_pixels[p+2]=65535;logo_pixels[p+3]=32768;}
      pe_image* raw=nullptr;ok(pe_image_create(32,16,logo_pixels.data(),&raw));Image logo(raw,pe_image_free);
      const auto logo_path=dir/"frame-logo.png";ok(pe_encode(logo.get(),logo_path.c_str(),PE_PNG,95,0));
      pe_params params;pe_default_params(&params);pe_effects effects;pe_default_effects(&effects);
      pe_decode_options options;pe_default_decode_options(&options);
      const char* font="/System/Library/Fonts/Supplemental/Arial.ttf";
      for(uint32_t layout=0;layout<=2;++layout){
        pe_frame frame{};pe_default_border(&frame.border);frame.border.width=.04;frame.border.bottom=.2;
        frame.layout=layout;frame.fields=0;
        std::strcpy(frame.logo_path,logo_path.c_str());
        if(fs::exists(font)){frame.fields=64;frame.brand=2;std::strcpy(frame.border.text,"Custom text");std::strcpy(frame.border.font_path,font);}
        ok(pe_add_frame(source.get(),&frame,&raw));Image expected(raw,pe_image_free);
        const auto expected_path=dir/("frame-expected-"+std::to_string(layout)+".png");
        ok(pe_encode(expected.get(),expected_path.c_str(),PE_PNG,95,1));
        const auto actual_path=dir/("frame-stream-"+std::to_string(layout)+".png");
        ok(pe_process_file_frame(input.c_str(),actual_path.c_str(),&options,&params,&effects,nullptr,1,&frame,PE_PNG,95,1,0));
        auto actual=decode(actual_path),reference=decode(expected_path);
        expect(pixels(actual)==pixels(reference),"Frame strip output differs");
        frame.fields=128;expect(pe_add_frame(source.get(),&frame,&raw)==PE_INVALID_ARGUMENT && !raw);
      }
      pe_frame frame{};pe_default_border(&frame.border);frame.border.bottom=.15;
      frame.fields=0;std::strcpy(frame.border.text,"Hidden custom text");
      ok(pe_add_frame(source.get(),&frame,&raw));Image hidden(raw,pe_image_free);
      frame.fields=64;expect(pe_add_frame(source.get(),&frame,&raw)==PE_INVALID_ARGUMENT && !raw);
    }},
    {"all eight EXIF orientations and metadata rewrite", [] {
      jpeg_fixture(dir/"orientation-reference.jpg",1,false);
      auto reference=decode(dir/"orientation-reference.jpg"); auto reference_pixels=pixels(reference);
      for(int o=1;o<=8;++o) {
        auto path=dir/("orientation-"+std::to_string(o)+".jpg"); jpeg_fixture(path,o,false); auto src=decode(path);
        expect(info(src).width==uint32_t(o>=5?8:12) && info(src).height==uint32_t(o>=5?12:8));
        expect(std::string(info(src).model)=="CAMER");
        auto oriented_pixels=pixels(src);
        for(uint32_t y=0;y<8;++y)for(uint32_t x=0;x<12;++x) {
          uint32_t dx=x,dy=y;
          switch(o) {case 2:dx=11-x;break;case 3:dx=11-x;dy=7-y;break;case 4:dy=7-y;break;
            case 5:dx=y;dy=x;break;case 6:dx=7-y;dy=x;break;case 7:dx=7-y;dy=11-x;break;case 8:dx=y;dy=11-x;break;}
          for(size_t c=0;c<4;++c)
            expect(oriented_pixels[(size_t(dy)*info(src).width+dx)*4+c]==reference_pixels[(size_t(y)*12+x)*4+c]);
        }
        auto streamed=dir/("streamed-orientation-"+std::to_string(o)+".png");
        pe_decode_options options;pe_default_decode_options(&options);
        pe_params params;pe_default_params(&params);pe_effects effects;pe_default_effects(&effects);
        pe_border border;pe_default_border(&border);
        ok(pe_process_file(path.c_str(),streamed.c_str(),&options,&params,&effects,nullptr,1,&border,PE_PNG,95,1,0));
        auto streamed_image=decode(streamed);expect(pixels(streamed_image)==pixels(src),"Strip EXIF orientation differs");
        auto out=dir/("oriented-"+std::to_string(o)+".jpg"); ok(pe_encode(src.get(),out.c_str(),PE_JPEG,100,1));
        auto rewritten=decode(out); expect(info(rewritten).width==info(src).width && info(rewritten).height==info(src).height);
        expect(std::string(info(rewritten).model)=="CAMER");
      }
    }},
    {"embedded RGB ICC conversion", [] {
      jpeg_fixture(dir/"icc.jpg",1,true); jpeg_fixture(dir/"no-icc.jpg",1,false);
      auto a=decode(dir/"icc.jpg"),b=decode(dir/"no-icc.jpg"); auto pa=pixels(a),pb=pixels(b);
      for(size_t i=0;i<pa.size();++i) expect(std::abs(int(pa[i])-pb[i])<=2);
    }},
    {"full-frame export budget error identifies actual input and output", [] {
      auto source=fixture(32,24);const auto input=dir/"budget-source.png";
      ok(pe_encode(source.get(),input.c_str(),PE_PNG,95,0));
      pe_decode_options options;pe_default_decode_options(&options);
      pe_params params;pe_default_params(&params);pe_effects effects;pe_default_effects(&effects);
      pe_border border;pe_default_border(&border);
      const auto output=dir/"budget-target.jpg";
      expect(pe_process_file(input.c_str(),output.c_str(),&options,&params,&effects,nullptr,1,&border,PE_JPEG,95,0,1024)==PE_LIMIT_EXCEEDED);
      expect(std::strstr(pe_last_error(),"PNG 32x24") && std::strstr(pe_last_error(),"JPEG"));
      expect(!fs::exists(output));
    }},
    {"no overwrite including input and failed export cleanup", [] {
      auto src=fixture(); text(dir/"existing.jpg","original");
      expect(pe_encode(src.get(),(dir/"existing.jpg").c_str(),PE_JPEG,95,1)==PE_IO_ERROR);
      std::ifstream file(dir/"existing.jpg"); std::string value; file>>value; expect(value=="original");
      expect(pe_encode(src.get(),(dir/"bad.out").c_str(),pe_format(99),95,1)==PE_UNSUPPORTED);
      expect(!fs::exists(dir/"bad.out"));
    }},
    {"malformed files and decode resource limits", [] {
      text(dir/"fake.jpg","not an image"); pe_decode_options o; pe_default_decode_options(&o); pe_image* out=nullptr;
      expect(pe_decode((dir/"fake.jpg").c_str(),&o,&out)!=PE_OK && !out);
      o.max_pixels=10; expect(pe_decode((dir/"photo.jpg").c_str(),&o,&out)==PE_LIMIT_EXCEEDED);
      expect(std::strstr(pe_last_error(), "320x240") && std::strstr(pe_last_error(), "10 pixels"));
      o.max_file_bytes=1; expect(pe_decode((dir/"photo.jpg").c_str(),&o,&out)==PE_LIMIT_EXCEEDED);
      text(dir/"truncated.png",std::string("\x89PNG\r\n\x1a\n",8)); pe_default_decode_options(&o);
      expect(pe_decode((dir/"truncated.png").c_str(),&o,&out)==PE_DECODE_ERROR);
    }},
    {"independent concurrent calls and thread-local errors", [] {
      auto src=fixture(); std::vector<std::thread> workers; std::vector<int> passed(4);
      for(int n=0;n<4;++n) workers.emplace_back([&,n] {
        pe_params p{};p.exposure=.2*n;pe_image* out=nullptr;
        if(pe_apply_params(src.get(),&p,&out)==PE_OK) { Image img(out,pe_image_free); passed[n]=pixels(img).size()==32*24*4; }
        pe_image_get_info(nullptr,nullptr); if(!std::strlen(pe_last_error())) passed[n]=0;
      });
      for(auto& w:workers)w.join();for(int pass:passed)expect(pass);
    }}
  };
  unsigned failed=0;
  for(const auto& test:tests) try {test.second();std::cout<<"PASS "<<test.first<<'\n';}
    catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<test.first<<": "<<e.what()<<'\n';}
  std::cout<<tests.size()-failed<<"/"<<tests.size()<<" tests passed. Fixtures: "<<dir<<'\n';
  if(!failed)fs::remove_all(dir);
  return failed?1:0;
}
