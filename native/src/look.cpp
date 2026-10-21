#include "internal.hpp"
#include <unistd.h>

namespace pe {
namespace {
struct Statistics {
  std::array<uint32_t,256> light{}, neutral{};
  uint32_t count=0, candidates=0;
  double red=0, blue=0;
  void add(const uint16_t* pixel) {
    if(pixel[3]<58981)return;
    const double r=display(pixel[0]/65535.0),g=display(pixel[1]/65535.0),b=display(pixel[2]/65535.0);
    const double l=.2126*r+.7152*g+.0722*b;
    const auto bin=std::min(255U,uint32_t(l*255));++light[bin];++count;
    const double maximum=std::max({r,g,b}),minimum=std::min({r,g,b});
    // Near-neutral midtones only. Coloured subjects and clipped whites must not
    // be treated as a grey card. This is deliberately a weak colour heuristic.
    if(l>.15 && l<.85 && maximum-minimum<maximum*.28){
      ++neutral[bin];++candidates;
      red+=pixel[0]/65535.0;blue+=pixel[2]/65535.0;
    }
  }
  static double quantile(const std::array<uint32_t,256>& histogram,uint32_t n,double q){
    if(!n)return 0;
    const uint32_t target=std::max(1U,uint32_t(std::ceil(n*q)));
    uint32_t total=0;
    for(uint32_t i=0;i<256;++i){total+=histogram[i];if(total>=target)return i/255.0;}
    return 1;
  }
  pe_look_analysis finish() const {
    pe_look_analysis result{};
    if(!count)return result;
    result.median=quantile(light,count,.5);
    // Small exposure assistance, never full automatic exposure normalization.
    result.exposure=std::clamp(std::log2(.42/std::max(.02,result.median))*.22,-.35,.45);
    if(candidates>=64 && red>0 && blue>0){
      const double coverage=std::min(1.0,double(candidates)/count/.08);
      const double spread=quantile(neutral,candidates,.9)-quantile(neutral,candidates,.1);
      result.confidence=coverage*clamp((spread-.05)/.20);
      result.temperature=std::clamp(-std::log2(red/blue)/.7,-.20,.20)*result.confidence;
    }
    return result;
  }
};
uint32_t sample_at(uint32_t i,uint32_t size,uint32_t samples){
  return uint32_t((uint64_t(i)*2+1)*size/(uint64_t(samples)*2));
}
double noise(uint32_t x,uint32_t y){
  uint32_t h=x*0x9e3779b1U+y*0x85ebca77U+0x37a1b5d3U;
  h^=h>>16;h*=0x7feb352dU;h^=h>>15;h*=0x846ca68bU;h^=h>>16;
  return double(h&0xffffU)/65535.0-.5;
}
}
void validate_look(const pe_look& look){
  require(look.kind<=1 && look.adaptive<=1,"Invalid look mode");
  for(double v:{look.adaptation,look.texture,look.vignette,look.grain})
    require(std::isfinite(v)&&v>=0&&v<=1,"Look controls must be in [0,1]");
}
pe_look_analysis analyze_look(const pe_image& image){
  Statistics stats;
  const uint32_t nx=std::min(128U,image.info.width),ny=std::min(128U,image.info.height);
  for(uint32_t y=0;y<ny;++y)for(uint32_t x=0;x<nx;++x){
    const size_t p=(size_t(sample_at(y,image.info.height,ny))*image.info.width+sample_at(x,image.info.width,nx))*4;
    stats.add(image.pixels.data()+p);
  }
  return stats.finish();
}
pe_look_analysis analyze_look(const DiskImage& disk){
  // Histograms are invariant under EXIF orientation. Read at most 128 sensor
  // rows, rather than loading the image or analysing each output strip anew.
  const auto& info=disk.metadata.info;
  const uint32_t nx=std::min(128U,info.width),ny=std::min(128U,info.height);
  std::vector<uint16_t> row(size_t(info.width)*4);
  Statistics stats;
  for(uint32_t y=0;y<ny;++y){
    const uint64_t offset=uint64_t(sample_at(y,info.height,ny))*info.width*8;
    require(pread(fileno(disk.file),row.data(),row.size()*2,off_t(offset))==ssize_t(row.size()*2),
        "Cannot read RAW/JPEG colour analysis samples",PE_IO_ERROR);
    for(uint32_t x=0;x<nx;++x)stats.add(row.data()+size_t(sample_at(x,info.width,nx))*4);
  }
  return stats.finish();
}
std::unique_ptr<pe_image> apply_look(const pe_image& source,const pe_lut& lut,double strength,
    const pe_look& look,const pe_look_analysis& analysis,uint32_t width,uint32_t height,uint32_t origin){
  validate_look(look);
  require(std::isfinite(strength)&&strength>=0&&strength<=1,"Invalid look strength");
  checked_pixels(width,height);
  require(source.info.width==width && origin<height && source.info.height<=height-origin,"Invalid look region");
  require(std::isfinite(analysis.exposure)&&analysis.exposure>=-.35&&analysis.exposure<=.45&&
      std::isfinite(analysis.temperature)&&std::abs(analysis.temperature)<=.20&&
      std::isfinite(analysis.median)&&analysis.median>=0&&analysis.median<=1&&
      std::isfinite(analysis.confidence)&&analysis.confidence>=0&&analysis.confidence<=1,"Invalid look analysis");
  if(!look.kind || strength==0)return apply_lut(source,lut,strength);
  const double adaptive=look.adaptive?look.adaptation:0;
  pe_params correction{};correction.exposure=analysis.exposure*adaptive;correction.temperature=analysis.temperature*adaptive;
  auto output=apply_params(source,correction);
  output=apply_lut(*output,lut,1);
  const auto detail=look.texture>0?output->pixels:std::vector<uint16_t>{};
  const double dark_protection=adaptive*clamp((.25-analysis.median)/.20)*.35;
  const uint32_t longest=std::max(width,height);
  for(uint32_t y=0;y<source.info.height;++y)for(uint32_t x=0;x<width;++x){
    const size_t p=(size_t(y)*width+x)*4;
    double local=0;
    if(look.texture>0){
      double average=0,weights=0;
      for(int dy=-2;dy<=2;dy+=2)for(int dx=-2;dx<=2;dx+=2){
        const auto xx=uint32_t(std::clamp(int(x)+dx,0,int(width)-1));
        const auto yy=uint32_t(std::clamp(int(y)+dy,0,int(source.info.height)-1));
        const size_t q=(size_t(yy)*width+xx)*4;
        const double weight=detail[q+3]/65535.0*(dx==0&&dy==0?4:dx==0||dy==0?2:1);
        average+=weight*(.2126*detail[q]+.7152*detail[q+1]+.0722*detail[q+2])/65535.0;weights+=weight;
      }
      if(weights>0)local=std::clamp((.2126*detail[p]+.7152*detail[p+1]+.0722*detail[p+2])/65535.0-average/weights,-.08,.08)*look.texture*.8;
    }
    const double nx=(x+.5)/width*2-1,ny=(origin+y+.5)/height*2-1;
    const double radial=clamp((nx*nx+ny*ny-.15)/1.85);
    const double falloff=std::exp2(-look.vignette*1.8*radial*radial);
    const double grain=noise(uint32_t((x+.5)*2048/longest),uint32_t((origin+y+.5)*2048/longest))*look.grain*.045;
    for(int c=0;c<3;++c){
      const double original=source.pixels[p+c]/65535.0;
      double styled=output->pixels[p+c]/65535.0;
      styled=styled*(1-dark_protection)+original*dark_protection;
      styled=linear(clamp(display(clamp((styled+local)*falloff))+grain));
      output->pixels[p+c]=quantize(original*(1-strength)+styled*strength);
    }
    output->pixels[p+3]=source.pixels[p+3];
  }
  return output;
}
}
