#include "pe/pe.h"
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
void jpeg_fixture(const fs::path& path, int orientation, bool icc) {
  FILE* file = std::fopen(path.c_str(), "wb"); expect(file);
  jpeg_compress_struct c{}; jpeg_error_mgr error{}; c.err = jpeg_std_error(&error);
  jpeg_create_compress(&c); jpeg_stdio_dest(&c, file);
  c.image_width = 12; c.image_height = 8; c.input_components = 3; c.in_color_space = JCS_RGB;
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
  uint8_t row[36];
  while (c.next_scanline < c.image_height) {
    for (int x = 0; x < 12; ++x) { row[x * 3] = uint8_t(x * 20); row[x * 3 + 1] = uint8_t(c.next_scanline * 30); row[x * 3 + 2] = 100; }
    JSAMPROW p = row; jpeg_write_scanlines(&c, &p, 1);
  }
  jpeg_finish_compress(&c); jpeg_destroy_compress(&c); std::fclose(file);
}
}

int main() {
  dir = fs::temp_directory_path() / ("photo-core-tests-" + std::to_string(getpid()));
  fs::create_directories(dir);
  std::vector<std::pair<std::string, std::function<void()>>> tests{
    {"ABI and defaults", [] {
      expect(pe_abi_version() == 1); pe_decode_options o; pe_default_decode_options(&o);
      expect(o.max_edge == 1600 && o.max_pixels == 80000000); expect(std::strstr(pe_capabilities(), "raw"));
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
      expect(pe_image_create(10000, 10000, pixel, &out) == PE_LIMIT_EXCEEDED);
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
