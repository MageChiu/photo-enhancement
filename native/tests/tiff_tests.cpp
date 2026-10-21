#include "pe/pe.h"
#include <tiffio.h>
#include <png.h>
#include <lcms2.h>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <unistd.h>
#include <pthread.h>
#include <fstream>

namespace fs=std::filesystem;
using Image=std::unique_ptr<pe_image,decltype(&pe_image_free)>;
void expect(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void ok(pe_status status){if(status!=PE_OK)throw std::runtime_error(pe_last_error());}
Image decode(const fs::path& file,uint32_t edge=0){
  pe_decode_options options;pe_default_decode_options(&options);options.max_edge=edge;
  pe_image* out=nullptr;ok(pe_decode(file.c_str(),&options,&out));return Image(out,pe_image_free);
}
pe_image_info info(const Image& image){pe_image_info out{};ok(pe_image_get_info(image.get(),&out));return out;}
std::vector<uint8_t> pixels(const Image& image){
  const auto i=info(image);std::vector<uint8_t> out(size_t(i.width)*i.height*4);
  ok(pe_image_copy_rgba8(image.get(),out.data(),out.size()));return out;
}
void fixture(const fs::path& path,uint16_t bits,bool tiled,bool separate,uint16_t orientation,
    uint16_t compression,bool grey=false,bool associated=false,bool white=false,bool icc=false,bool big=false,bool dng=false){
  std::unique_ptr<TIFF,decltype(&TIFFClose)> file(TIFFOpen(path.c_str(),big?"w8b":"wb"),TIFFClose);
  expect(bool(file),"Cannot create TIFF fixture");auto* t=file.get();
  uint64_t exif_offset=0;
  TIFFCreateEXIFDirectory(t);
  TIFFSetField(t,EXIFTAG_FNUMBER,2.8);TIFFSetField(t,EXIFTAG_EXPOSURETIME,1.0/125);
  TIFFSetField(t,EXIFTAG_FOCALLENGTH,50.0);
  const uint16_t iso=200;TIFFSetField(t,EXIFTAG_ISOSPEEDRATINGS,1,&iso);
  TIFFSetField(t,EXIFTAG_LENSMAKE,"Lens Maker");TIFFSetField(t,EXIFTAG_LENSMODEL,"50mm Lens");
  expect(TIFFWriteCustomDirectory(t,&exif_offset),"Cannot write TIFF EXIF");
  TIFFCreateDirectory(t);
  TIFFSetField(t,TIFFTAG_EXIFIFD,exif_offset);
  constexpr uint32_t width=19,height=23;const uint16_t samples=grey?1:4;
  TIFFSetField(t,TIFFTAG_IMAGEWIDTH,width);TIFFSetField(t,TIFFTAG_IMAGELENGTH,height);
  TIFFSetField(t,TIFFTAG_BITSPERSAMPLE,bits);TIFFSetField(t,TIFFTAG_SAMPLESPERPIXEL,samples);
  TIFFSetField(t,TIFFTAG_PHOTOMETRIC,grey?(white?PHOTOMETRIC_MINISWHITE:PHOTOMETRIC_MINISBLACK):PHOTOMETRIC_RGB);
  TIFFSetField(t,TIFFTAG_PLANARCONFIG,separate?PLANARCONFIG_SEPARATE:PLANARCONFIG_CONTIG);
  TIFFSetField(t,TIFFTAG_ORIENTATION,orientation);TIFFSetField(t,TIFFTAG_COMPRESSION,compression);
  TIFFSetField(t,TIFFTAG_MAKE,"Camera");TIFFSetField(t,TIFFTAG_MODEL,"TIFF Test");
  if(!grey){uint16_t alpha=associated?EXTRASAMPLE_ASSOCALPHA:EXTRASAMPLE_UNASSALPHA;TIFFSetField(t,TIFFTAG_EXTRASAMPLES,1,&alpha);}
  if(dng){const uint8_t version[]{1,4,0,0};TIFFSetField(t,TIFFTAG_DNGVERSION,version);}
  if(icc){
    cmsToneCurve* gamma=cmsBuildGamma(nullptr,2.2);cmsCIExyY wp;cmsWhitePointFromTemp(&wp,6504);
    cmsHPROFILE profile=grey?cmsCreateGrayProfile(&wp,gamma):cmsCreate_sRGBProfile();cmsFreeToneCurve(gamma);
    cmsUInt32Number bytes=0;cmsSaveProfileToMem(profile,nullptr,&bytes);std::vector<uint8_t> data(bytes);
    cmsSaveProfileToMem(profile,data.data(),&bytes);cmsCloseProfile(profile);
    TIFFSetField(t,TIFFTAG_ICCPROFILE,bytes,data.data());
  }
  const uint32_t bw=tiled?16:width,bh=tiled?16:5;
  if(tiled){TIFFSetField(t,TIFFTAG_TILEWIDTH,bw);TIFFSetField(t,TIFFTAG_TILELENGTH,bh);}
  else TIFFSetField(t,TIFFTAG_ROWSPERSTRIP,bh);
  for(uint16_t plane=0;plane<(separate?samples:1);++plane)
    for(uint32_t y=0;y<height;y+=bh)for(uint32_t x=0;x<width;x+=bw){
      const uint32_t rows=tiled?bh:std::min(bh,height-y),components=separate?1:samples;
      std::vector<uint16_t> values(size_t(bw)*rows*components);
      for(uint32_t dy=0;dy<rows;++dy)for(uint32_t dx=0;dx<bw;++dx)for(uint16_t c=0;c<components;++c){
        const uint16_t channel=separate?plane:c;
        uint16_t value=channel==0?uint16_t(10000+std::min(x+dx,width-1)*40):
            channel==1?uint16_t(12000+std::min(y+dy,height-1)*1000):channel==2?40000:32768;
        if(associated && channel<3)value=uint16_t(value/2);
        if(grey && white)value=uint16_t(65535-value);
        values[(size_t(dy)*bw+dx)*components+c]=value;
      }
      std::vector<uint8_t> bytes(values.size()*(bits/8));
      for(size_t i=0;i<values.size();++i){
        if(bits==8)bytes[i]=uint8_t((values[i]+128)/257);else std::memcpy(bytes.data()+i*2,&values[i],2);
      }
      const auto result=tiled?TIFFWriteEncodedTile(t,TIFFComputeTile(t,x,y,0,plane),bytes.data(),tmsize_t(bytes.size())):
          TIFFWriteEncodedStrip(t,TIFFComputeStrip(t,y,plane),bytes.data(),tmsize_t(bytes.size()));
      expect(result>0,"Cannot write TIFF fixture block");
    }
}
std::vector<uint16_t> png16(const fs::path& path){
  FILE* file=std::fopen(path.c_str(),"rb");expect(file,"Cannot read PNG16");
  auto* png=png_create_read_struct(PNG_LIBPNG_VER_STRING,nullptr,nullptr,nullptr);
  auto* meta=png_create_info_struct(png);expect(png && meta,"PNG allocation failed");
  if(setjmp(png_jmpbuf(png))){png_destroy_read_struct(&png,&meta,nullptr);std::fclose(file);throw std::runtime_error("PNG read failed");}
  png_init_io(png,file);png_read_info(png,meta);expect(png_get_bit_depth(png,meta)==16,"Lost TIFF output bit depth");
  const size_t row=png_get_rowbytes(png,meta),height=png_get_image_height(png,meta);
  std::vector<uint8_t> data(row*height);std::vector<png_bytep> rows(height);
  for(size_t y=0;y<height;++y)rows[y]=data.data()+row*y;png_read_image(png,rows.data());
  png_destroy_read_struct(&png,&meta,nullptr);std::fclose(file);
  std::vector<uint16_t> values(data.size()/2);
  for(size_t i=0;i<values.size();++i)values[i]=uint16_t(data[i*2])<<8|data[i*2+1];return values;
}
// Match the constrained stack of an FFI worker, rather than only testing from
// the large process main stack. Stack overflow terminates this test executable.
struct WorkerResult {
  std::string path;
  bool lens=false;
  pe_status status=PE_OK;
  pe_image* image=nullptr;
  char error[512]{};
};
WorkerResult worker_call(const fs::path& path,bool lens=false){
  WorkerResult result;result.path=path.string();result.lens=lens;
  pthread_attr_t attributes;expect(pthread_attr_init(&attributes)==0,"Cannot initialize worker attributes");
  expect(pthread_attr_setstacksize(&attributes,512*1024)==0,"Cannot set worker stack size");
  pthread_t thread;
  const int started=pthread_create(&thread,&attributes,[](void* value)->void*{
    auto& task=*static_cast<WorkerResult*>(value);
    if(task.lens){
      pe_lens_info lens{};task.status=pe_read_lens_info(task.path.c_str(),512ULL*1024*1024,&lens);
    }else{
      pe_decode_options options;pe_default_decode_options(&options);options.max_edge=8;
      task.status=pe_decode(task.path.c_str(),&options,&task.image);
    }
    std::snprintf(task.error,sizeof(task.error),"%s",pe_last_error());return nullptr;
  },&result);
  pthread_attr_destroy(&attributes);expect(started==0,"Cannot start small-stack worker");
  expect(pthread_join(thread,nullptr)==0,"Cannot join small-stack worker");return result;
}
int main(){
  const auto dir=fs::temp_directory_path()/("photo-tiff-tests-"+std::to_string(getpid()));fs::create_directories(dir);
  try{
    expect(std::strstr(pe_capabilities(),"tiff"),"Missing TIFF capability");
    int serial=0;
    for(uint16_t bits:{8,16})for(bool tiled:{false,true})for(bool separate:{false,true}){
      const auto input=dir/("image-"+std::to_string(serial++)+".tif");
      fixture(input,bits,tiled,separate,6,tiled?COMPRESSION_ADOBE_DEFLATE:COMPRESSION_LZW,false,false,false,true);
      auto source=decode(input);expect(info(source).width==23 && info(source).height==19,"TIFF rotation mismatch");
      expect(!info(source).is_raw && std::string(info(source).model)=="TIFF Test","Ordinary TIFF was treated as RAW");
      expect(info(source).iso==200 && std::abs(info(source).focal_length-50)<.01,
          "TIFF exposure metadata was lost");
      pe_lens_info lens{};ok(pe_read_lens_info(input.c_str(),512ULL*1024*1024,&lens));
      expect(std::string(lens.model)=="50mm Lens","TIFF lens metadata was lost");
      auto small=decode(input,8);expect(info(small).width==8 && info(small).height<=8,"TIFF preview was not scaled");
      const auto output=dir/("stream-"+std::to_string(serial)+".png"),full=dir/("full-"+std::to_string(serial)+".png");
      pe_decode_options options;pe_default_decode_options(&options);pe_params params;pe_default_params(&params);
      pe_effects effects;pe_default_effects(&effects);pe_border border;pe_default_border(&border);
      ok(pe_process_file(input.c_str(),output.c_str(),&options,&params,&effects,nullptr,1,&border,PE_PNG,95,1,64*1024*1024));
      ok(pe_encode(source.get(),full.c_str(),PE_PNG,95,1));
      expect(png16(output)==png16(full),"TIFF disk strips and full decode differ");
      auto rejected=dir/"rejected.png";
      expect(pe_process_file(input.c_str(),rejected.c_str(),&options,&params,&effects,nullptr,1,&border,PE_PNG,95,1,1024)==PE_LIMIT_EXCEEDED && !fs::exists(rejected),"TIFF budget refusal left output");
      expect(pe_process_file(input.c_str(),input.c_str(),&options,&params,&effects,nullptr,1,&border,PE_PNG,95,1,0)==PE_IO_ERROR,"TIFF input overwrite allowed");
    }
    const auto input=dir/"precision.tiff";fixture(input,16,false,false,1,COMPRESSION_PACKBITS);
    auto worker=worker_call(input);
    expect(worker.status==PE_OK,worker.error);Image worker_image(worker.image,pe_image_free);
    expect(info(worker_image).width==7 && info(worker_image).height==8,"Small-stack TIFF preview failed");
    const auto invalid=dir/"not-a-raw.bin";{std::ofstream file(invalid);file<<"not a RAW image";}
    auto invalid_decode=worker_call(invalid);
    expect(invalid_decode.status!=PE_OK && !invalid_decode.image,"Invalid RAW unexpectedly decoded");
    auto invalid_probe=worker_call(invalid,true);
    expect(invalid_probe.status!=PE_OK,"Invalid RAW metadata probe unexpectedly succeeded");
    auto image=decode(input);const auto encoded=dir/"precision.png";ok(pe_encode(image.get(),encoded.c_str(),PE_PNG,95,0));
    const auto precision=png16(encoded);
    expect(precision[4]>precision[0]+20 && precision[4]<precision[0]+60,"16-bit TIFF was reduced to 8-bit");
    for(bool white:{false,true}){
      auto path=dir/(white?"white.tif":"black.tif");fixture(path,16,true,false,1,COMPRESSION_ADOBE_DEFLATE,true,false,white,true,true);
      auto gray=decode(path);const auto rgba=pixels(gray);
      for(size_t i=0;i<rgba.size();i+=4)expect(std::abs(int(rgba[i])-rgba[i+1])<=1 && std::abs(int(rgba[i+1])-rgba[i+2])<=1,"Gray ICC acquired a colour cast");
    }
    const auto straight=dir/"straight.tif",associated=dir/"associated.tif";
    fixture(straight,16,false,false,1,COMPRESSION_NONE);fixture(associated,16,false,false,1,COMPRESSION_NONE,false,true);
    const auto a=pixels(decode(straight)),b=pixels(decode(associated));
    for(size_t i=0;i<a.size();++i)expect(std::abs(int(a[i])-b[i])<=1,"Associated TIFF alpha was not unpremultiplied");
    const auto dng=dir/"dng-container.tif";fixture(dng,16,false,false,1,COMPRESSION_NONE,false,false,false,false,false,true);
    pe_decode_options options;pe_default_decode_options(&options);pe_image* raw=nullptr;
    const auto status=pe_decode(dng.c_str(),&options,&raw);
    if(status==PE_OK){Image result(raw,pe_image_free);expect(info(result).is_raw,"DNG tag fell through to ordinary raster");}
    else expect(!raw,"Failed DNG decode returned an image");
    const auto unsupported=dir/"unsupported.tif";fixture(unsupported,16,false,false,1,COMPRESSION_NONE);
    {std::unique_ptr<TIFF,decltype(&TIFFClose)> t(TIFFOpen(unsupported.c_str(),"r+"),TIFFClose);
      TIFFSetField(t.get(),TIFFTAG_PHOTOMETRIC,PHOTOMETRIC_SEPARATED);TIFFRewriteDirectory(t.get());}
    expect(pe_decode(unsupported.c_str(),&options,&raw)==PE_UNSUPPORTED && !raw,
        "Unsupported TIFF layout must give an explicit error");
    fs::resize_file(input,12);expect(pe_decode(input.c_str(),&options,&raw)!=PE_OK && !raw,"Truncated TIFF succeeded");
    std::cout<<"PASS TIFF integer depth, ICC, alpha, orientation, strips/tiles, planar, BigTIFF, export, limits and RAW routing\n";
    fs::remove_all(dir);return 0;
  }catch(const std::exception& e){std::cerr<<"FAIL TIFF: "<<e.what()<<"; fixtures: "<<dir<<'\n';return 1;}
}
