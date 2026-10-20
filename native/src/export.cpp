#include "internal.hpp"
#include "heif_support.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

namespace pe {
namespace {
struct Output {
  std::string path; FILE* file=nullptr; bool committed=false;
  explicit Output(const std::string& value):path(value){
    const int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0644);
    require(fd>=0,"Destination already exists or cannot be created: "+path,PE_IO_ERROR);
    file=fdopen(fd,"wb");if(!file){::close(fd);::unlink(path.c_str());throw Error(PE_IO_ERROR,"Cannot open output");}
  }
  ~Output(){if(file)std::fclose(file);if(!committed)::unlink(path.c_str());}
  void finish(){
    const bool flushed=std::fflush(file)==0 && ::fsync(fileno(file))==0;
    const int status=std::fclose(file);file=nullptr;
    require(flushed&&status==0,"Cannot flush output",PE_IO_ERROR);committed=true;
  }
};
std::unique_ptr<pe_image> render(std::unique_ptr<pe_image> source,const pe_params& params,
    const pe_effects& effects,const pe_lut* lut,double strength,const pe_look* look,
    const pe_look_analysis& analysis,uint32_t width,uint32_t height,uint32_t origin){
  source=apply_params(*source,params);
  source=apply_effects(*source,effects);
  if(lut)source=look&&look->kind
      ?apply_look(*source,*lut,strength,*look,analysis,width,height,origin)
      :apply_lut(*source,*lut,strength);
  return source;
}
}

void process_file(const std::string& path,const std::string& destination,const pe_decode_options& input,
    const pe_params& params,const pe_effects& effects,const char* lut_path,double strength,
    const pe_border& border,pe_format format,uint32_t quality,bool metadata,uint64_t requested,
    const pe_frame* style,const pe_look* look){
  if(look)validate_look(*look);
  const uint64_t automatic=memory_budget();
  const uint64_t budget=requested?std::min(requested,automatic):automatic;
  std::error_code destination_error;
  require(!std::filesystem::exists(destination,destination_error) && !destination_error,
      "Destination already exists or cannot be inspected: "+destination,PE_IO_ERROR);
  if(style)validate_frame(*style);
  auto logo=style?frame_logo(*style):nullptr;
  auto probe=probe_image(path,input.max_file_bytes);
  checked_pixels(probe.metadata.info.width,probe.metadata.info.height,input.max_pixels);
  auto info=probe.metadata.info;
  if(probe.metadata.orientation>=5)std::swap(info.width,info.height);
  // Validate recipes before allocating a full image or creating an output.
  {auto tiny=image(1,1);apply_params(*tiny,params);apply_effects(*tiny,effects);border_strip(info,border,0,1,style,logo.get());}
  require(std::isfinite(strength)&&strength>=0&&strength<=1,"LUT strength must be in [0,1]");
  std::unique_ptr<pe_lut> lut;
  if(lut_path && *lut_path)lut=load_lut(lut_path);
  require(!look || !look->kind || lut,"A styled look requires its LUT");
  const uint64_t lut_bytes=(lut?lut->values.size()*sizeof(std::array<double,3>):0)+(logo?logo->pixels.size()*sizeof(uint16_t):0);
  const uint32_t edge=std::min(info.width,info.height),pad=uint32_t(std::lround(edge*border.width)),bottom=uint32_t(std::lround(edge*border.bottom));
  const uint32_t output_width=info.width+pad*2,output_height=info.height+pad*2+bottom;
  checked_pixels(output_width,output_height);
  if(bottom)border_strip(info,border,pad+info.height,1,style,logo.get());

  if(probe.jpeg) {
    // JPEG input goes through bounded strips regardless of the camera's MP.
    // Temporary linear pixels also allow EXIF transpose without a full RAM copy.
    // Large HEIF outputs use bounded grid tiles, retaining compressed tile
    // payloads until file writing. Include those payloads in the strip budget.
    const uint64_t heif_memory=format==PE_HEIF?heif_export_memory(output_width,output_height):0;
    const std::string route="原尺寸导出（JPEG "+std::to_string(info.width)+"x"+
        std::to_string(info.height)+" → HEIF；"+
        (heif_grid(output_width,output_height)?"网格分块编码阶段":"整图 HEIF 编码阶段")+"）";
    if(format==PE_HEIF)require_memory(heif_memory+lut_bytes,budget,route);
    const uint64_t reserve=8*1024*1024+lut_bytes+heif_memory;
    const uint32_t effect_halo=(effects.sharpen>0 || effects.denoise_luma>0 || effects.denoise_chroma>0)
      ?uint32_t(std::ceil(effects.radius)):0;
    const uint32_t halo=effect_halo+(look&&look->kind&&strength>0&&look->texture>0?2:0);
    require_memory(reserve+uint64_t(output_width)*(16+2*halo)*80,budget,"JPEG 分条处理");
    const uint32_t strip_rows=uint32_t(std::min<uint64_t>(128,
      (budget-reserve)/(uint64_t(output_width)*80)-2*halo));
    auto disk=jpeg_to_disk(path,input,budget-lut_bytes);
    // One source analysis for the whole photo; never adapt individual strips.
    const auto analysis=look&&look->kind?analyze_look(*disk):pe_look_analysis{};
    Output output(destination);
    auto output_info=info;output_info.width=output_width;output_info.height=output_height;
    auto writer=row_writer(output.file,output_info,format,quality,metadata);
    for(uint32_t y=0;y<output_height;y+=strip_rows){
      const uint32_t rows=std::min(strip_rows,output_height-y);
      auto frame=border_strip(info,border,y,rows,style,logo.get());
      const uint32_t first=std::max(y,pad),last=std::min(y+rows,pad+info.height);
      if(first<last){
        const uint32_t start=first-pad,finish=last-pad;
        const uint32_t begin=start>halo?start-halo:0,end=std::min(info.height,finish+halo);
        auto edited=render(disk->strip(begin,end-begin),params,effects,lut.get(),strength,
            look,analysis,info.width,info.height,begin);
        for(uint32_t row=first;row<last;++row)
          std::copy_n(edited->pixels.data()+size_t(row-pad-begin)*info.width*4,size_t(info.width)*4,
              frame->pixels.data()+(size_t(row-y)*output_width+pad)*4);
      }
      writer->rows(*frame,0,rows);
    }
    // Release source cache and analysis resources before the encoder's peak.
    disk.reset();
    if(format==PE_HEIF)
      require_memory(heif_memory+lut_bytes,
          std::min(budget,memory_budget()),route);
    writer->finish();output.finish();return;
  }

  // Other decoders currently need full-frame storage. Count the whole pipeline,
  // including spatial scratch buffers and BOTH input and enlarged border canvas.
  const uint64_t pixels=uint64_t(info.width)*info.height;
  // Non-JPEG metadata probes may omit EXIF/RAW rotation: budget both canvases.
  const uint64_t canvas=std::max(uint64_t(output_width)*output_height,
      uint64_t(info.height+2*pad)*(info.width+2*pad+bottom));
  const uint64_t decode_peak=probe.raw?probe.sensor_pixels*64+pixels*24:pixels*40+probe.file_bytes*2;
  const uint64_t render_peak=std::max(pixels*40,pixels*16+canvas*(format==PE_HEIF?32:16));
  const std::string output_format=format==PE_JPEG?"JPEG":format==PE_PNG?"PNG":"HEIF";
  const std::string full_frame_reason=probe.raw?"RAW 解码尚需完整缓冲":
      probe.jpeg?"HEIF 编码尚需完整缓冲":"此输入格式解码尚需完整缓冲";
  const std::string route="原尺寸导出（"+probe.format+" "+std::to_string(info.width)+"x"+
      std::to_string(info.height)+" → "+output_format+"；"+full_frame_reason+"）";
  const uint64_t estimated=std::max(decode_peak,render_peak)+lut_bytes+16*1024*1024;
  if(estimated>budget){
    std::string message=route+"预计需要 "+std::to_string((estimated+1048575)/1048576)+
        " MiB，当前处理预算为 "+std::to_string(budget/1048576)+" MiB。";
    message+=probe.jpeg?"可选择 JPEG/PNG 使用现有分条导出，或释放内存后重试。":
        "尚未提供此输入的低内存解码；仅降低输出质量不会降低原尺寸像素缓冲。请释放内存后重试。";
    throw Error(PE_LIMIT_EXCEEDED,message);
  }
  auto options=input;options.max_edge=0;
  auto source=decode(path,options);
  const auto analysis=look&&look->kind?analyze_look(*source):pe_look_analysis{};
  const uint32_t width=source->info.width,height=source->info.height;
  source=render(std::move(source),params,effects,lut.get(),strength,look,analysis,width,height,0);
  source=add_border(*source,border,style,logo.get());
  encode(*source,destination,format,quality,metadata);
}
}
