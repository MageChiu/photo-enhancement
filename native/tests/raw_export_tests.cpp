#include "pe/pe.h"
#include "../src/internal.hpp"
#include "../src/raw_support.hpp"
#include <tiffio.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>
#include <unistd.h>
#include <sys/resource.h>

namespace fs=std::filesystem;
using Image=std::unique_ptr<pe_image,decltype(&pe_image_free)>;
void expect(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void ok(pe_status status){if(status!=PE_OK)throw std::runtime_error(pe_last_error());}
Image decode(const fs::path& path){
  pe_decode_options options;pe_default_decode_options(&options);options.max_edge=0;
  pe_image* out=nullptr;ok(pe_decode(path.c_str(),&options,&out));return Image(out,pe_image_free);
}
void dng(const fs::path& path,uint32_t width,uint32_t height,uint16_t orientation){
  std::unique_ptr<TIFF,decltype(&TIFFClose)> file(TIFFOpen(path.c_str(),"w"),TIFFClose);
  auto* t=file.get();expect(t,"Cannot create DNG fixture");
  TIFFSetField(t,TIFFTAG_IMAGEWIDTH,width);TIFFSetField(t,TIFFTAG_IMAGELENGTH,height);
  TIFFSetField(t,TIFFTAG_BITSPERSAMPLE,16);TIFFSetField(t,TIFFTAG_SAMPLESPERPIXEL,1);
  TIFFSetField(t,TIFFTAG_PHOTOMETRIC,PHOTOMETRIC_CFA);TIFFSetField(t,TIFFTAG_PLANARCONFIG,PLANARCONFIG_CONTIG);
  TIFFSetField(t,TIFFTAG_COMPRESSION,COMPRESSION_NONE);TIFFSetField(t,TIFFTAG_ROWSPERSTRIP,32);
  TIFFSetField(t,TIFFTAG_ORIENTATION,orientation);
  TIFFSetField(t,TIFFTAG_MAKE,"Test Camera");TIFFSetField(t,TIFFTAG_MODEL,"Bayer RAW");
  TIFFSetField(t,TIFFTAG_UNIQUECAMERAMODEL,"Photo Core Test Bayer");
  const uint8_t version[]{1,4,0,0},backward[]{1,1,0,0};
  TIFFSetField(t,TIFFTAG_DNGVERSION,version);TIFFSetField(t,TIFFTAG_DNGBACKWARDVERSION,backward);
  const uint16_t repeat[]{2,2};const uint8_t pattern[]{0,1,1,2};
  TIFFSetField(t,TIFFTAG_CFAREPEATPATTERNDIM,repeat);TIFFSetField(t,TIFFTAG_CFAPATTERN,4,pattern);
  const uint32_t white=65535;TIFFSetField(t,TIFFTAG_WHITELEVEL,1,&white);
  const float matrix[]{1,0,0,0,1,0,0,0,1},neutral[]{1,1,1};
  TIFFSetField(t,TIFFTAG_COLORMATRIX1,9,matrix);TIFFSetField(t,TIFFTAG_ASSHOTNEUTRAL,3,neutral);
  TIFFSetField(t,TIFFTAG_CALIBRATIONILLUMINANT1,21);
  std::vector<uint16_t> row(width);
  for(uint32_t y=0;y<height;++y){
    for(uint32_t x=0;x<width;++x)row[x]=uint16_t(1000+(x*43+y*71+(x%17)*193)%53000);
    expect(TIFFWriteScanline(t,row.data(),y,0)>=0,"Cannot write DNG fixture");
  }
}
int main(int argc,char** argv){
  const auto dir=fs::temp_directory_path()/("photo-raw-tests-"+std::to_string(getpid()));fs::create_directories(dir);
  try{
    constexpr uint64_t MiB=1024*1024;
    const auto conservative=pe::raw_preview_memory(11776,8944,11664,8750);
    const auto bounded=pe::raw_preview_memory(11776,8944,11664,8750,true);
    expect(conservative.unpack>1668*MiB && bounded.peak()<1668*MiB,
        "Audited 100 MP preview still hits the reported memory boundary");
    expect(conservative.unpack>conservative.process && conservative.process<1668*MiB,
        "Completed unpack peak must not be charged to preview processing");
    expect(bounded.unpack>=uint64_t(11776)*8944*2,
        "Bounded decoder must retain the full sensor allocation");
    expect(pe::raw_preview_memory(16000,12000,16000,12000,true).peak()>1668*MiB,
        "Oversized bounded RAW preview lost its memory protection");
    expect(pe::raw_disk_memory(uint64_t(11664)*8750,uint64_t(11664)*8750)<2023*MiB,
        "100 MP Bayer export still exceeds reported budget");
    expect(pe::raw_disk_memory(uint64_t(16000)*12000,uint64_t(16000)*12000)>2023*MiB,
        "RAW export lost its memory guard");
    pe_decode_options options;pe_default_decode_options(&options);
    if(argc>1 && (std::string(argv[1])=="--large" || std::string(argv[1])=="--large-preview")){
      const auto input=dir/"100mp.dng";dng(input,11664,8750,1);
      if(std::string(argv[1])=="--large-preview"){
        options.max_edge=1600;pe_image* ptr=nullptr;
        ok(pe_decode(input.c_str(),&options,&ptr));Image preview(ptr,pe_image_free);
        expect(preview->info.width==1600 && preview->info.height==1200,"100 MP preview dimensions differ");
        expect(preview->info.is_raw && std::string(preview->info.make)=="Test Camera","RAW preview metadata was lost");
        struct rusage usage{};getrusage(RUSAGE_SELF,&usage);
        std::cout<<"PASS 11664x8750 RAW preview 1600x1200; peak RSS bytes="<<usage.ru_maxrss<<std::endl;
        fs::remove_all(dir);return 0;
      }
      pe_params p;pe_default_params(&p);pe_effects e;pe_default_effects(&e);pe_border b;pe_default_border(&b);
      for(auto format:{PE_JPEG,PE_HEIF}){
        if(format==PE_HEIF && !std::strstr(pe_capabilities(),"heif-encode"))continue;
        const auto output=dir/(format==PE_JPEG?"100mp.jpg":"100mp.heic");
        ok(pe_process_file(input.c_str(),output.c_str(),&options,&p,&e,nullptr,1,&b,format,90,1,2023*MiB));
        struct rusage usage{};getrusage(RUSAGE_SELF,&usage);
        std::cout<<"PASS 11664x8750 RAW -> "<<(format==PE_JPEG?"JPEG":"HEIF")
            <<"; peak RSS bytes="<<usage.ru_maxrss<<"; output bytes="<<fs::file_size(output)<<std::endl;
      }
      fs::remove_all(dir);return 0;
    }
    pe_params params;pe_default_params(&params);params.exposure=.15;
    pe_effects effects;pe_default_effects(&effects);effects.sharpen=.2;
    pe_border border;pe_default_border(&border);border.width=.03;border.bottom=.07;
    const auto font=(fs::path(PE_SOURCE_DIR)/"app/assets/frame_fonts/sans.ttf").string();
    std::snprintf(border.font_path,sizeof(border.font_path),"%s",font.c_str());
    for(uint16_t orientation=1;orientation<=8;++orientation){
      const auto input=dir/(std::to_string(orientation)+".dng");
      dng(input,orientation==1?2048:192,orientation==1?1536:160,orientation);
      auto original=decode(input);expect(original->info.is_raw,"DNG was not decoded as RAW");
      auto preview_options=options;preview_options.max_edge=64;
      pe_image* preview_ptr=nullptr;ok(pe_decode(input.c_str(),&preview_options,&preview_ptr));
      Image preview(preview_ptr,pe_image_free);
      expect(std::max(preview->info.width,preview->info.height)==64 && preview->info.is_raw,
          "Half-size RAW preview dimensions or metadata differ");
      pe_image* ptr=nullptr;ok(pe_apply_params(original.get(),&params,&ptr));Image edited(ptr,pe_image_free);
      ok(pe_apply_effects(edited.get(),&effects,&ptr));edited.reset(ptr);
      ok(pe_add_border(edited.get(),&border,&ptr));Image framed(ptr,pe_image_free);
      const auto reference=dir/(std::to_string(orientation)+"-reference.png");
      ok(pe_encode(framed.get(),reference.c_str(),PE_PNG,95,1));auto expected=decode(reference);
      const auto output=dir/(std::to_string(orientation)+"-actual.png");
      ok(pe_process_file(input.c_str(),output.c_str(),&options,&params,&effects,nullptr,1,&border,PE_PNG,95,1,128*MiB));
      auto actual=decode(output);
      expect(actual->info.width==expected->info.width && actual->info.height==expected->info.height,"RAW export orientation or frame dimensions differ");
      expect(actual->pixels==expected->pixels,"RAW disk export pixels differ from full decode");
      expect(std::string(actual->info.make)=="Test Camera","RAW camera metadata was lost");
      expect(pe_process_file(input.c_str(),output.c_str(),&options,&params,&effects,nullptr,1,&border,PE_PNG,95,1,128*MiB)==PE_IO_ERROR,"RAW export overwrites files");
      const auto rejected=dir/(std::to_string(orientation)+"-rejected.png");
      expect(pe_process_file(input.c_str(),rejected.c_str(),&options,&params,&effects,nullptr,1,&border,PE_PNG,95,1,32*MiB)==PE_LIMIT_EXCEEDED,"RAW ignores explicit memory limit");
      expect(!fs::exists(rejected),"Failed RAW export leaves output behind");
      const auto jpeg=dir/(std::to_string(orientation)+".jpg");
      ok(pe_process_file(input.c_str(),jpeg.c_str(),&options,&params,&effects,nullptr,1,&border,PE_JPEG,95,1,128*MiB));
      auto jpg=decode(jpeg);expect(jpg->info.width==expected->info.width && jpg->info.height==expected->info.height,"RAW JPEG export dimensions differ");
      if(orientation==6 && std::strstr(pe_capabilities(),"heif-encode")){
        const auto heif=dir/"raw.heic",full=dir/"raw-reference.heic";
        ok(pe_encode(framed.get(),full.c_str(),PE_HEIF,95,1));
        ok(pe_process_file(input.c_str(),heif.c_str(),&options,&params,&effects,nullptr,1,&border,PE_HEIF,95,1,256*MiB));
        expect(decode(heif)->pixels==decode(full)->pixels,"RAW HEIF differs from full decode");
      }
      std::cout<<"PASS Bayer RAW orientation "<<orientation<<'\n';
    }
    fs::remove_all(dir);return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<" (fixtures: "<<dir<<")\n";return 1;}
}
