#include "internal.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <cstring>
#include <sstream>
#include <iomanip>
#include <cctype>

namespace pe {
namespace {
std::vector<uint32_t> codepoints(const std::string& text) {
  std::vector<uint32_t> result;
  for (size_t i = 0; i < text.size();) {
    uint8_t first = uint8_t(text[i++]);
    uint32_t cp; int remaining;
    if (first < 0x80) { cp = first; remaining = 0; }
    else if ((first & 0xe0) == 0xc0) { cp = first & 31; remaining = 1; }
    else if ((first & 0xf0) == 0xe0) { cp = first & 15; remaining = 2; }
    else if ((first & 0xf8) == 0xf0) { cp = first & 7; remaining = 3; }
    else throw Error(PE_INVALID_ARGUMENT, "Caption is not valid UTF-8");
    for (int n = 0; n < remaining; ++n) {
      require(i < text.size() && (uint8_t(text[i]) & 0xc0) == 0x80, "Caption is not valid UTF-8");
      cp = (cp << 6) | (uint8_t(text[i++]) & 63);
    }
    require(cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff) &&
        !(remaining == 1 && cp < 128) && !(remaining == 2 && cp < 2048) &&
        !(remaining == 3 && cp < 65536), "Caption is not valid UTF-8");
    require(cp >= 32, "Caption must be a single printable line");
    result.push_back(cp);
  }
  return result;
}
struct Font {
  FT_Library library = nullptr; FT_Face face = nullptr;
  ~Font() { if (face) FT_Done_Face(face); if (library) FT_Done_FreeType(library); }
};
}
void paint_caption(pe_image* out,const pe_image_info& source,const pe_border& b,
    uint32_t pad,uint32_t bottom,uint32_t origin_y);
void paint_frame(pe_image*,const pe_image_info&,const pe_frame&,const pe_image*,uint32_t);
std::unique_ptr<pe_image> add_border(const pe_image& src, const pe_border& b,const pe_frame* style,const pe_image* logo) {
  require(std::isfinite(b.width) && b.width >= 0 && b.width <= .25 &&
      std::isfinite(b.bottom) && b.bottom >= 0 && b.bottom <= .4, "Invalid border dimensions");
  require(std::memchr(b.text, 0, sizeof(b.text)) && std::memchr(b.font_path, 0, sizeof(b.font_path)), "Border strings must be null-terminated");
  uint32_t short_edge = std::min(src.info.width, src.info.height);
  uint32_t pad = uint32_t(std::lround(short_edge * b.width));
  uint32_t bottom = uint32_t(std::lround(short_edge * b.bottom));
  if (!pad && !bottom) return std::make_unique<pe_image>(src);
  uint32_t w = src.info.width + pad * 2, h = src.info.height + pad * 2 + bottom;
  auto out = image(w, h);
  out->info = src.info; out->info.width = w; out->info.height = h;
  uint16_t color[]{quantize(linear(((b.color >> 16) & 255) / 255.0)),
                   quantize(linear(((b.color >> 8) & 255) / 255.0)),
                   quantize(linear((b.color & 255) / 255.0)), 65535};
  for (size_t i = 0; i < out->pixels.size(); i += 4) std::copy_n(color, 4, out->pixels.data() + i);
  for (uint32_t y = 0; y < src.info.height; ++y)
    std::copy_n(src.pixels.data() + size_t(y) * src.info.width * 4, size_t(src.info.width) * 4,
                out->pixels.data() + (size_t(y + pad) * w + pad) * 4);
  if(style)paint_frame(out.get(),src.info,*style,logo,0);
  else paint_caption(out.get(),src.info,b,pad,bottom,0);
  return out;
}
void paint_caption(pe_image* out,const pe_image_info& source,const pe_border& b,
    uint32_t pad,uint32_t bottom,uint32_t origin_y) {
  std::string text = b.text[0] ? b.text : caption(source);
  if (!bottom || text.empty()) return;
  const uint32_t w=source.width+pad*2,h=source.height+pad*2+bottom;
  const uint16_t color[]{quantize(linear(((b.color>>16)&255)/255.0)),
    quantize(linear(((b.color>>8)&255)/255.0)),quantize(linear((b.color&255)/255.0))};
  require(b.font_path[0], "A font path is required for an EXIF/text border");
  auto chars = codepoints(text);
  Font font;
  require(FT_Init_FreeType(&font.library) == 0 && FT_New_Face(font.library, b.font_path, 0, &font.face) == 0,
      "Cannot load the caption font", PE_IO_ERROR);
  uint32_t size = std::max(1U, uint32_t(bottom * .23));
  int64_t text_width = 0;
  for (;;) {
    require(FT_Set_Pixel_Sizes(font.face, 0, size) == 0, "Cannot set font size");
    text_width = 0;
    for (uint32_t cp : chars) {
      require(FT_Get_Char_Index(font.face, cp) != 0, "Caption font does not contain all characters");
      require(FT_Load_Char(font.face, cp, FT_LOAD_DEFAULT) == 0, "Cannot measure caption glyph");
      text_width += font.face->glyph->advance.x >> 6;
    }
    if (text_width <= int64_t(w) - 2 * std::max(pad, 2U) || size <= 1) break;
    --size;
  }
  require(text_width <= int64_t(w) - 2 * std::max(pad, 2U), "Caption does not fit this image size");
  int pen = int((int64_t(w) - text_width) / 2);
  int ascent = int(font.face->size->metrics.ascender >> 6);
  int descent = int(font.face->size->metrics.descender >> 6);
  int baseline = int(pad + source.height) + int(bottom) / 2 + (ascent + descent) / 2;
  double ink = (.2126 * color[0] + .7152 * color[1] + .0722 * color[2]) > 24000 ? .025 : .94;
  for (uint32_t cp : chars) {
    require(FT_Load_Char(font.face, cp, FT_LOAD_RENDER) == 0, "Cannot rasterize caption glyph");
    auto glyph = font.face->glyph;
    require(!glyph->bitmap.rows || (glyph->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY &&
        std::abs(glyph->bitmap.pitch) >= int(glyph->bitmap.width)),
        "Caption font must produce grayscale glyphs", PE_UNSUPPORTED);
    for (unsigned y = 0; y < glyph->bitmap.rows; ++y) for (unsigned x = 0; x < glyph->bitmap.width; ++x) {
      int dx = pen + glyph->bitmap_left + int(x), dy = baseline - glyph->bitmap_top + int(y);
      if (dx < 0 || dy < int(pad + source.height) || dx >= int(w) || dy >= int(h - pad) ||
          dy<int(origin_y) || dy>=int(origin_y+out->info.height)) continue;
      int pitch = glyph->bitmap.pitch;
      size_t row = pitch >= 0 ? y * size_t(pitch) : (glyph->bitmap.rows - 1 - y) * size_t(-pitch);
      double coverage = glyph->bitmap.buffer[row + x] / 255.0;
      size_t p = (size_t(dy-origin_y) * w + size_t(dx)) * 4;
      for (int c = 0; c < 3; ++c) out->pixels[p + c] = quantize(out->pixels[p + c] / 65535.0 * (1 - coverage) + ink * coverage);
    }
    pen += int(glyph->advance.x >> 6);
  }
}
std::unique_ptr<pe_image> border_strip(const pe_image_info& source,const pe_border& b,uint32_t y,uint32_t rows,const pe_frame* style,const pe_image* logo) {
  require(std::isfinite(b.width)&&b.width>=0&&b.width<=.25&&std::isfinite(b.bottom)&&b.bottom>=0&&b.bottom<=.4,"Invalid border dimensions");
  require(std::memchr(b.text,0,sizeof(b.text))&&std::memchr(b.font_path,0,sizeof(b.font_path)),"Border strings must be null-terminated");
  const uint32_t edge=std::min(source.width,source.height),pad=uint32_t(std::lround(edge*b.width)),bottom=uint32_t(std::lround(edge*b.bottom));
  const uint32_t w=source.width+pad*2,h=source.height+pad*2+bottom;
  checked_pixels(w,h);require(y<h&&rows<=h-y,"Invalid border strip");
  auto out=image(w,rows);out->info=source;out->info.width=w;out->info.height=rows;
  const uint16_t color[]{quantize(linear(((b.color>>16)&255)/255.0)),quantize(linear(((b.color>>8)&255)/255.0)),quantize(linear((b.color&255)/255.0)),65535};
  for(size_t p=0;p<out->pixels.size();p+=4)std::copy_n(color,4,out->pixels.data()+p);
  if(y+rows>pad+source.height && y<h-pad){
    if(style)paint_frame(out.get(),source,*style,logo,y);
    else paint_caption(out.get(),source,b,pad,bottom,y);
  }
  return out;
}
void validate_frame(const pe_frame& f) {
  require(f.fields<=127 && f.layout<=2 && f.brand<=4,"Invalid frame options");
  require(std::memchr(f.logo_path,0,sizeof(f.logo_path)),"Logo path must be terminated");
}
std::unique_ptr<pe_image> frame_logo(const pe_frame& f) {
  validate_frame(f);if(!f.logo_path[0])return nullptr;
  std::ifstream file(f.logo_path,std::ios::binary);unsigned char signature[8]{};
  file.read(reinterpret_cast<char*>(signature),8);
  const unsigned char png[]{137,80,78,71,13,10,26,10};
  require(file.gcount()==8 && std::memcmp(signature,png,8)==0,"Frame logo must be PNG",PE_UNSUPPORTED);
  pe_decode_options options{512,1024*1024,4*1024*1024};
  return decode(f.logo_path,options);
}
namespace {
std::string join(const std::string& a,const std::string& b) {
  return a.empty()?b:b.empty()?a:a+"   "+b;
}
std::string brand_name(const pe_image_info& source,uint32_t brand) {
  if(brand==1){
    std::string make=source.make;
    std::transform(make.begin(),make.end(),make.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(make.find("sony")!=std::string::npos)brand=2;
    else if(make.find("fuji")!=std::string::npos)brand=3;
    else if(make.find("hasselblad")!=std::string::npos)brand=4;
    else return source.make;
  }
  return brand==2?"SONY":brand==3?"FUJIFILM":brand==4?"HASSELBLAD":"";
}
void frame_line(pe_image* out,const pe_border& b,const std::string& text,int left,int available,
    int middle,uint32_t size,uint32_t origin,int top,int bottom,int alignment) {
  if(text.empty() || available<=0)return;
  require(b.font_path[0],"A font path is required for frame text");
  Font font;require(FT_Init_FreeType(&font.library)==0 && FT_New_Face(font.library,b.font_path,0,&font.face)==0,
      "Cannot load frame font",PE_IO_ERROR);
  const auto chars=codepoints(text);int64_t length=0;
  for(;;){
    require(FT_Set_Pixel_Sizes(font.face,0,size)==0,"Cannot set frame font size");length=0;
    for(auto cp:chars){
      require(FT_Get_Char_Index(font.face,cp)!=0,"Frame font does not contain all characters");
      require(FT_Load_Char(font.face,cp,FT_LOAD_DEFAULT)==0,"Cannot measure frame text");
      length+=font.face->glyph->advance.x>>6;
    }
    if(length<=available || size<=1)break;--size;
  }
  require(length<=available,"Frame text does not fit image");
  int pen=left+(alignment==1?(available-int(length))/2:alignment==2?available-int(length):0);
  const int baseline=middle+int((font.face->size->metrics.ascender+font.face->size->metrics.descender)>>7);
  const double brightness=.2126*((b.color>>16)&255)+.7152*((b.color>>8)&255)+.0722*(b.color&255);
  const double ink=brightness>160?.025:.94;
  for(auto cp:chars){
    require(FT_Load_Char(font.face,cp,FT_LOAD_RENDER)==0,"Cannot render frame text");auto glyph=font.face->glyph;
    require(!glyph->bitmap.rows || (glyph->bitmap.pixel_mode==FT_PIXEL_MODE_GRAY && std::abs(glyph->bitmap.pitch)>=int(glyph->bitmap.width)),
        "Frame font must produce grayscale glyphs",PE_UNSUPPORTED);
    for(unsigned gy=0;gy<glyph->bitmap.rows;++gy)for(unsigned gx=0;gx<glyph->bitmap.width;++gx){
      const int x=pen+glyph->bitmap_left+int(gx),y=baseline-glyph->bitmap_top+int(gy);
      if(x<left || x>=left+available || x<0 || x>=int(out->info.width) || y<top || y>=bottom || y<int(origin) || y>=int(origin+out->info.height))continue;
      const int pitch=glyph->bitmap.pitch;
      const size_t row=pitch>=0?gy*size_t(pitch):(glyph->bitmap.rows-1-gy)*size_t(-pitch);
      const double alpha=glyph->bitmap.buffer[row+gx]/255.0;
      const size_t pixel=(size_t(y-origin)*out->info.width+x)*4;
      for(int c=0;c<3;++c)out->pixels[pixel+c]=quantize(out->pixels[pixel+c]/65535.0*(1-alpha)+ink*alpha);
    }
    pen+=int(glyph->advance.x>>6);
  }
}
}
void paint_frame(pe_image* out,const pe_image_info& source,const pe_frame& f,const pe_image* logo,uint32_t origin) {
  validate_frame(f);const auto& b=f.border;
  const uint32_t edge=std::min(source.width,source.height),pad=uint32_t(std::lround(edge*b.width));
  const int height=int(std::lround(edge*b.bottom)),top=int(pad+source.height),bottom=top+height;
  if(height<=0 || int(origin)>=bottom || int(origin+out->info.height)<=top)return;
  const int margin=std::max(2,int(pad)),right=int(out->info.width)-margin;
  int left=margin;
  if(logo){
    const double scale=std::min(double(height)*.65/logo->info.height,
        double(std::max(1,right-left))*.24/logo->info.width);
    const int lw=std::max(1,int(std::lround(logo->info.width*scale))),lh=std::max(1,int(std::lround(logo->info.height*scale)));
    const int ly=top+(height-lh)/2;
    for(int y=std::max(ly,int(origin));y<std::min(ly+lh,int(origin+out->info.height));++y)
      for(int x=0;x<lw && left+x<right;++x){
        const size_t src=(size_t(std::min(logo->info.height-1,uint32_t((y-ly)/scale)))*logo->info.width+
            std::min(logo->info.width-1,uint32_t(x/scale)))*4;
        const size_t dst=(size_t(y-origin)*out->info.width+left+x)*4;
        const double alpha=logo->pixels[src+3]/65535.0;
        for(int c=0;c<3;++c)out->pixels[dst+c]=quantize(logo->pixels[src+c]/65535.0*alpha+out->pixels[dst+c]/65535.0*(1-alpha));
      }
    left+=lw+std::max(2,height/8);
  }
  auto selected=source;
  selected.model[0]=0;
  if(!(f.fields&2))selected.focal_length=0;
  if(!(f.fields&4))selected.aperture=0;
  if(!(f.fields&8))selected.shutter=0;
  if(!(f.fields&16))selected.iso=0;
  const std::string identity=join(brand_name(source,f.brand),(f.fields&1)?source.model:"");
  const std::string details=caption(selected);
  const std::string extra=join((f.fields&32)?source.captured_at:"",(f.fields&64)?b.text:"");
  const int available=right-left;
  auto line=[&](const std::string& text,int x,int width,double y,double size,int align){
    frame_line(out,b,text,x,width,top+int(height*y),std::max(1U,uint32_t(height*size)),origin,top,bottom,align);
  };
  if(f.layout==0){
    line(join(join(identity,details),extra),left,available,.5,.23,1);
  }else if(f.layout==1){
    if(identity.empty() || details.empty())line(join(identity,details),left,available,extra.empty()?.5:.36,.22,0);
    else{
      const int gap=std::max(2,height/8),first=std::max(0,(available-gap)*2/5);
      line(identity,left,first,extra.empty()?.5:.36,.23,0);
      line(details,left+first+gap,std::max(0,available-first-gap),extra.empty()?.5:.36,.20,2);
    }
    line(extra,left,available,.75,.15,0);
  }else{
    if(identity.empty() || details.empty())line(join(identity,details),left,available,extra.empty()?.5:.35,.23,1);
    else{line(identity,left,available,extra.empty()?.32:.25,.23,1);line(details,left,available,extra.empty()?.7:.55,.17,1);}
    line(extra,left,available,.82,.14,1);
  }
}

}
