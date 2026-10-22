#include "internal.hpp"
#include <libraw/libraw.h>
#include <tiffio.h>
#include <lcms2.h>
#include <cstdarg>
#include <cstring>
#include <functional>

namespace pe {
namespace {
constexpr uint64_t MiB = 1024ULL * 1024;
int diagnostic(TIFF*, void* user, const char*, const char* format, va_list args) {
  if (user) std::vsnprintf(static_cast<char*>(user), 512, format, args);
  return 1;
}
struct Reader {
  char error[512]{};
  std::unique_ptr<TIFF, decltype(&TIFFClose)> file{nullptr, TIFFClose};
  Metadata meta;
  uint32_t width=0,height=0;
  uint16_t bits=0,samples=0,photo=0,planar=0,alpha=0;
  uint64_t budget;
  std::vector<uint8_t> icc;
  cmsHTRANSFORM transform=nullptr;
  bool grey=false;
  explicit Reader(const std::string& path,uint64_t limit):budget(limit) {
    std::unique_ptr<TIFFOpenOptions,decltype(&TIFFOpenOptionsFree)>
        options(TIFFOpenOptionsAlloc(),TIFFOpenOptionsFree);
    require(bool(options),"Cannot allocate TIFF options",PE_OUT_OF_MEMORY);
    TIFFOpenOptionsSetMaxSingleMemAlloc(options.get(),tmsize_t(budget/4));
    TIFFOpenOptionsSetMaxCumulatedMemAlloc(options.get(),tmsize_t(budget/4));
    TIFFOpenOptionsSetErrorHandlerExtR(options.get(),diagnostic,error);
    TIFFOpenOptionsSetWarningHandlerExtR(options.get(),diagnostic,nullptr);
    // No full-file mmap; respect the same explicit memory policy as JPEG.
#ifdef _WIN32
    file.reset(TIFFOpenWExt(io::path(path).c_str(),"rm",options.get()));
#else
    file.reset(TIFFOpenExt(path.c_str(),"rm",options.get()));
#endif
    require(bool(file),std::string("TIFF: ")+error,PE_DECODE_ERROR);
    auto* t=file.get();
    require(TIFFGetField(t,TIFFTAG_IMAGEWIDTH,&width) &&
        TIFFGetField(t,TIFFTAG_IMAGELENGTH,&height),"TIFF 缺少图像尺寸",PE_DECODE_ERROR);
    checked_pixels(width,height);
    TIFFGetFieldDefaulted(t,TIFFTAG_BITSPERSAMPLE,&bits);
    TIFFGetFieldDefaulted(t,TIFFTAG_SAMPLESPERPIXEL,&samples);
    TIFFGetFieldDefaulted(t,TIFFTAG_PLANARCONFIG,&planar);
    require(TIFFGetField(t,TIFFTAG_PHOTOMETRIC,&photo),"TIFF 缺少颜色解释",PE_DECODE_ERROR);
    uint16_t sample_format=SAMPLEFORMAT_UINT;
    TIFFGetFieldDefaulted(t,TIFFTAG_SAMPLEFORMAT,&sample_format);
    require((bits==8 || bits==16) && sample_format==SAMPLEFORMAT_UINT,
        "TIFF 当前支持 8/16 位无符号整数照片；浮点/HDR 或其他位深尚不支持",PE_UNSUPPORTED);
    grey=photo==PHOTOMETRIC_MINISBLACK || photo==PHOTOMETRIC_MINISWHITE;
    require(photo==PHOTOMETRIC_RGB || grey,
        "TIFF 当前支持 RGB/灰度；CMYK、Lab、YCbCr、调色板和传感器数据尚不支持",PE_UNSUPPORTED);
    require(planar==PLANARCONFIG_CONTIG || planar==PLANARCONFIG_SEPARATE,
        "Unsupported TIFF planar layout",PE_UNSUPPORTED);
    const uint16_t channels=grey?1:3;
    uint16_t extras=0;uint16_t* kinds=nullptr;
    TIFFGetFieldDefaulted(t,TIFFTAG_EXTRASAMPLES,&extras,&kinds);
    require((samples==channels && extras==0) || (samples==channels+1 && extras==1 && kinds &&
        (kinds[0]==EXTRASAMPLE_ASSOCALPHA || kinds[0]==EXTRASAMPLE_UNASSALPHA)),
        "TIFF 存在未支持的额外通道或未标明的 alpha",PE_UNSUPPORTED);
    if(samples>channels)alpha=kinds[0];
    require(!(photo==PHOTOMETRIC_MINISWHITE && alpha==EXTRASAMPLE_ASSOCALPHA),
        "White-is-zero TIFF with associated alpha is not supported",PE_UNSUPPORTED);
    uint16_t compression=COMPRESSION_NONE;
    TIFFGetFieldDefaulted(t,TIFFTAG_COMPRESSION,&compression);
    require(TIFFIsCODECConfigured(compression),"此 TIFF 压缩方式未包含在当前构建中",PE_UNSUPPORTED);
    uint8_t* dng=nullptr;
    require(!TIFFGetField(t,TIFFTAG_DNGVERSION,&dng),
        "DNG 应由 RAW 解码器读取；此 DNG 未被 LibRaw 识别",PE_UNSUPPORTED);
    meta.info.width=width;meta.info.height=height;
    TIFFGetFieldDefaulted(t,TIFFTAG_ORIENTATION,&meta.orientation);
    require(meta.orientation>=1 && meta.orientation<=8,"Invalid TIFF orientation",PE_DECODE_ERROR);
    auto text=[&](uint32_t tag,char* target,size_t size){
      char* value=nullptr;if(TIFFGetField(t,tag,&value) && value)std::snprintf(target,size,"%s",value);
    };
    text(TIFFTAG_MAKE,meta.info.make,sizeof(meta.info.make));
    text(TIFFTAG_MODEL,meta.info.model,sizeof(meta.info.model));
    text(TIFFTAG_DATETIME,meta.info.captured_at,sizeof(meta.info.captured_at));
    uint32_t count=0;void* profile=nullptr;
    if(TIFFGetField(t,TIFFTAG_ICCPROFILE,&count,&profile)){
      require(count<=16*MiB && count<=budget/8,"TIFF ICC profile exceeds memory limit",PE_LIMIT_EXCEEDED);
      require(profile && count,"Invalid TIFF ICC profile",PE_DECODE_ERROR);
      icc.assign(static_cast<uint8_t*>(profile),static_cast<uint8_t*>(profile)+count);
    }
    uint64_t exif=0;
    if(TIFFGetField(t,TIFFTAG_EXIFIFD,&exif)){
      const auto directory=TIFFCurrentDirOffset(t);
      require(TIFFReadEXIFDirectory(t,exif),"Invalid TIFF EXIF directory",PE_DECODE_ERROR);
      float value=0;
      if(TIFFGetField(t,EXIFTAG_FNUMBER,&value))meta.info.aperture=value;
      if(TIFFGetField(t,EXIFTAG_EXPOSURETIME,&value))meta.info.shutter=value;
      if(TIFFGetField(t,EXIFTAG_FOCALLENGTH,&value))meta.info.focal_length=value;
      uint16_t n=0;uint16_t* iso=nullptr;
      if(TIFFGetField(t,EXIFTAG_ISOSPEEDRATINGS,&n,&iso) && n && iso)meta.info.iso=iso[0];
      text(EXIFTAG_DATETIMEORIGINAL,meta.info.captured_at,sizeof(meta.info.captured_at));
      text(EXIFTAG_LENSMAKE,meta.lens.make,sizeof(meta.lens.make));
      text(EXIFTAG_LENSMODEL,meta.lens.model,sizeof(meta.lens.model));
      require(TIFFSetSubDirectory(t,directory),"Cannot restore TIFF directory",PE_DECODE_ERROR);
    }
  }
  ~Reader(){if(transform)cmsDeleteTransform(transform);}
  void prepare_color(){
    if(icc.empty())return;
    std::unique_ptr<void,decltype(&cmsCloseProfile)> input(
        cmsOpenProfileFromMem(icc.data(),cmsUInt32Number(icc.size())),cmsCloseProfile);
    require(bool(input),"Invalid TIFF ICC profile",PE_DECODE_ERROR);
    require(cmsGetColorSpace(input.get())==(grey?cmsSigGrayData:cmsSigRgbData),
        "TIFF ICC profile does not match RGB/grey pixels",PE_UNSUPPORTED);
    cmsCIExyY white;cmsWhitePointFromTemp(&white,6504);
    cmsCIExyYTRIPLE primaries{{.64,.33,1},{.30,.60,1},{.15,.06,1}};
    cmsToneCurve* curve=cmsBuildGamma(nullptr,1);
    require(curve,"Cannot allocate TIFF tone curve",PE_OUT_OF_MEMORY);
    cmsToneCurve* curves[]{curve,curve,curve};
    std::unique_ptr<void,decltype(&cmsCloseProfile)> target(
        cmsCreateRGBProfile(&white,&primaries,curves),cmsCloseProfile);
    cmsFreeToneCurve(curve);
    require(bool(target),"Cannot allocate TIFF colour space",PE_OUT_OF_MEMORY);
    transform=cmsCreateTransform(input.get(),grey?TYPE_GRAY_16:TYPE_RGBA_16,
        target.get(),TYPE_RGBA_16,INTENT_RELATIVE_COLORIMETRIC,
        cmsFLAGS_BLACKPOINTCOMPENSATION|(grey?0:cmsFLAGS_COPY_ALPHA));
    require(transform,"Cannot transform TIFF ICC profile",PE_DECODE_ERROR);
  }
  void convert(pe_image& image){
    if(!transform){from_display(image,image.pixels.data(),{});return;}
    if(!grey){cmsDoTransform(transform,image.pixels.data(),image.pixels.data(),
        cmsUInt32Number(image.pixels.size()/4));return;}
    std::vector<uint16_t> values(image.pixels.size()/4),alphas(values.size());
    for(size_t i=0;i<values.size();++i){values[i]=image.pixels[i*4];alphas[i]=image.pixels[i*4+3];}
    cmsDoTransform(transform,values.data(),image.pixels.data(),cmsUInt32Number(values.size()));
    for(size_t i=0;i<values.size();++i)image.pixels[i*4+3]=alphas[i];
  }
  using Planes=std::vector<std::vector<uint8_t>>;
  uint16_t sample(const Planes& planes,size_t pixel,uint16_t channel)const{
    const auto& plane=planes[planar==PLANARCONFIG_SEPARATE?channel:0];
    const size_t position=(planar==PLANARCONFIG_SEPARATE?pixel:pixel*samples+channel)*(bits/8);
    if(bits==8)return uint16_t(plane[position])*257;
    uint16_t value;std::memcpy(&value,plane.data()+position,2);return value;
  }
  void pixel(const Planes& planes,size_t index,uint16_t* target)const{
    const uint16_t a=alpha?sample(planes,index,samples-1):65535;
    for(uint16_t c=0;c<3;++c){
      double value=sample(planes,index,grey?0:c)/65535.0;
      if(grey && photo==PHOTOMETRIC_MINISWHITE)value=1-value;
      if(alpha==EXTRASAMPLE_ASSOCALPHA)value=a?clamp(value*65535/a):0;
      target[c]=quantize(value);
    }
    target[3]=a;
  }
  void blocks(uint64_t resident,const std::function<void(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,const Planes&)>& visit){
    auto* t=file.get();const bool tiles=TIFFIsTiled(t);
    uint32_t bw=width,bh=0;
    if(tiles){TIFFGetField(t,TIFFTAG_TILEWIDTH,&bw);TIFFGetField(t,TIFFTAG_TILELENGTH,&bh);}
    else {TIFFGetFieldDefaulted(t,TIFFTAG_ROWSPERSTRIP,&bh);bh=std::min(bh,height);}
    require(bw && bh && bw<=32768 && bh<=32768,"Invalid TIFF strip/tile dimensions",PE_LIMIT_EXCEEDED);
    const uint64_t bytes=tiles?TIFFTileSize64(t):TIFFStripSize64(t);
    const uint16_t planes=planar==PLANARCONFIG_SEPARATE?samples:1;
    const uint64_t expected=uint64_t(bw)*bh*(bits/8)*(planar==PLANARCONFIG_SEPARATE?1:samples);
    require(bytes && bytes>=expected && bytes<=uint64_t(std::numeric_limits<tmsize_t>::max()),
        "Invalid TIFF block size",PE_DECODE_ERROR);
    require_memory(resident+16*MiB,budget,"TIFF 解码常驻缓冲");
    require_memory(bytes,(budget-resident-16*MiB)/(uint64_t(planes)*8),"TIFF 单条带/图块缓冲");
    Planes data(planes,std::vector<uint8_t>(size_t(bytes),0));
    for(uint32_t y=0;y<height;y+=bh)for(uint32_t x=0;x<width;x+=bw){
      const uint32_t w=std::min(bw,width-x),h=std::min(bh,height-y);
      const uint64_t needed=uint64_t(bw)*h*(bits/8)*(planar==PLANARCONFIG_SEPARATE?1:samples);
      for(uint16_t p=0;p<planes;++p){
        const auto read=tiles?TIFFReadEncodedTile(t,TIFFComputeTile(t,x,y,0,p),data[p].data(),tmsize_t(bytes)):
            TIFFReadEncodedStrip(t,TIFFComputeStrip(t,y,p),data[p].data(),tmsize_t(bytes));
        require(read>=0 && uint64_t(read)>=needed,std::string("TIFF 解码失败或数据截断：")+error,PE_DECODE_ERROR);
      }
      visit(x,y,w,h,bw,data);
    }
  }
};
}

bool tiff_raster_file(const std::string& path){
  uint8_t h[4]{};std::ifstream f(io::path(path),std::ios::binary);f.read(reinterpret_cast<char*>(h),4);
  const bool signature=(h[0]=='I' && h[1]=='I' && (h[2]==42 || h[2]==43) && h[3]==0) ||
      (h[0]=='M' && h[1]=='M' && h[2]==0 && (h[3]==42 || h[3]==43));
  if(!signature)return false;
  // RAW containers may expose an ordinary RGB thumbnail as their first IFD.
  // Let LibRaw positively identify those before interpreting raster samples.
  // The LibRaw object itself is too large for some Dart worker thread stacks.
  auto raw=std::make_unique<LibRaw>();
  return raw->open_file(io::path(path).c_str())!=LIBRAW_SUCCESS || !raw->imgdata.idata.raw_count;
}
Metadata tiff_metadata(const std::string& path){return Reader(path,memory_budget()).meta;}

std::unique_ptr<pe_image> tiff_decode(const std::string& path,const pe_decode_options& options){
  Reader reader(path,memory_budget(options.max_edge!=0));
  checked_pixels(reader.width,reader.height,options.max_pixels);
  const double scale=options.max_edge?std::min(1.0,double(options.max_edge)/std::max(reader.width,reader.height)):1;
  const uint32_t width=std::max(1U,uint32_t(std::lround(reader.width*scale))),
      height=std::max(1U,uint32_t(std::lround(reader.height*scale)));
  require_memory(uint64_t(width)*height*20+16*MiB,reader.budget,"TIFF 预览/像素转换");
  auto out=image(width,height);reader.prepare_color();
  reader.blocks(uint64_t(width)*height*20,[&](uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint32_t stride,const Reader::Planes& data){
    const auto ceil_ratio=[](uint64_t a,uint32_t b){return uint32_t((a+b-1)/b);};
    const uint32_t first_x=ceil_ratio(uint64_t(x)*width,reader.width),last_x=ceil_ratio(uint64_t(x+w)*width,reader.width);
    const uint32_t first_y=ceil_ratio(uint64_t(y)*height,reader.height),last_y=ceil_ratio(uint64_t(y+h)*height,reader.height);
    for(uint32_t dy=first_y;dy<last_y;++dy)for(uint32_t dx=first_x;dx<last_x;++dx){
      const auto sx=uint32_t(uint64_t(dx)*reader.width/width),sy=uint32_t(uint64_t(dy)*reader.height/height);
      reader.pixel(data,size_t(sy-y)*stride+sx-x,out->pixels.data()+(size_t(dy)*width+dx)*4);
    }
  });
  reader.convert(*out);out->info=reader.meta.info;out->info.width=width;out->info.height=height;out->lens=reader.meta.lens;
  orient(*out,reader.meta.orientation);return out;
}

std::unique_ptr<DiskImage> tiff_to_disk(const std::string& path,const pe_decode_options& options,uint64_t budget){
  Reader reader(path,std::min(budget,memory_budget()));
  const uint64_t size=checked_pixels(reader.width,reader.height,options.max_pixels)*8ULL;
  const auto directory=std::filesystem::temp_directory_path();
  require(std::filesystem::space(directory).available>=size+64*MiB,"TIFF 临时像素缓存磁盘空间不足",PE_IO_ERROR);
  auto disk=std::make_unique<DiskImage>();disk->file=io::temporary();
  require(disk->file,"Cannot create TIFF pixel storage",PE_IO_ERROR);
  require(io::resize(disk->file,size),"Cannot size TIFF pixel storage",PE_IO_ERROR);
  disk->metadata=reader.meta;reader.prepare_color();
  reader.blocks(0,[&](uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint32_t stride,const Reader::Planes& data){
    auto chunk=image(w,h);
    for(uint32_t dy=0;dy<h;++dy)for(uint32_t dx=0;dx<w;++dx)
      reader.pixel(data,size_t(dy)*stride+dx,chunk->pixels.data()+(size_t(dy)*w+dx)*4);
    reader.convert(*chunk);
    for(uint32_t dy=0;dy<h;++dy){
      const size_t bytes=size_t(w)*8;const uint64_t offset=(uint64_t(y+dy)*reader.width+x)*8;
      require(io::write_at(disk->file,chunk->pixels.data()+size_t(dy)*w*4,bytes,offset),"Cannot write TIFF pixel storage",PE_IO_ERROR);
    }
  });
  return disk;
}
}
