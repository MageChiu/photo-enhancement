#include "internal.hpp"
#include "font_io.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include <cstring>

namespace pe {
namespace {
// Decode bounded, printable UTF-8. Newlines are accepted for card body text.
std::vector<uint32_t> characters(const std::string& s) {
  std::vector<uint32_t> out;
  for(size_t i=0;i<s.size();) {
    const uint8_t c=s[i++];uint32_t cp=c;int n=0;
    if(c>=128){if((c&224)==192){cp=c&31;n=1;}else if((c&240)==224){cp=c&15;n=2;}
      else if((c&248)==240){cp=c&7;n=3;}else require(false,"Invalid template UTF-8");}
    for(int j=0;j<n;++j){require(i<s.size()&&(uint8_t(s[i])&192)==128,"Invalid template UTF-8");cp=(cp<<6)|(uint8_t(s[i++])&63);}
    require(cp<=0x10ffff&&!(cp>=0xd800&&cp<=0xdfff)&&!(n==1&&cp<128)&&!(n==2&&cp<2048)&&!(n==3&&cp<65536),"Invalid template UTF-8");
    require(cp>=32||cp==10,"Template text contains control characters");out.push_back(cp);
  }
  return out;
}
struct Font {
  FT_Library lib=nullptr;FT_Face face=nullptr;
  std::vector<unsigned char> storage;
  explicit Font(const char* path){
    require(*path,"请选择相框字体");
    require(FT_Init_FreeType(&lib)==0,"Cannot initialize template font",PE_IO_ERROR);
    if(open_font(lib,path,storage,&face)!=0){FT_Done_FreeType(lib);lib=nullptr;throw Error(PE_IO_ERROR,"Cannot load template font");}
    regularWeight();
  }
  void regularWeight() {
    if(!FT_HAS_MULTIPLE_MASTERS(face))return;
    FT_MM_Var* axes=nullptr;
    if(FT_Get_MM_Var(face,&axes)!=0)return;
    std::vector<FT_Fixed> coords(axes->num_axis);
    for(FT_UInt i=0;i<axes->num_axis;++i){const auto& axis=axes->axis[i];coords[i]=axis.tag==FT_MAKE_TAG('w','g','h','t')?std::clamp(FT_Fixed(400*65536),axis.minimum,axis.maximum):axis.def;}
    FT_Set_Var_Design_Coordinates(face,axes->num_axis,coords.data());FT_Done_MM_Var(lib,axes);
  }
  ~Font(){if(face)FT_Done_Face(face);if(lib)FT_Done_FreeType(lib);}
};
struct TextRow {std::vector<uint32_t> chars;int width=0;};
std::vector<TextRow> wrap(Font& font,const std::string& text,int size,int width,bool multiline) {
  require(FT_Set_Pixel_Sizes(font.face,0,std::max(1,size))==0,"Cannot size template font");
  std::vector<TextRow> rows;
  if(text.empty())return rows;
  rows.emplace_back();
  for(auto cp:characters(text)){
    if(cp==10){if(multiline)rows.emplace_back();else cp=32;if(multiline)continue;}
    require(FT_Get_Char_Index(font.face,cp)!=0,"相框字体缺少字形，请导入包含中文或相应字符的字体",PE_UNSUPPORTED);
    require(FT_Load_Char(font.face,cp,FT_LOAD_DEFAULT)==0,"Cannot measure template text");
    const int advance=font.face->glyph->advance.x>>6;
    require(advance<=width,"Template text exceeds canvas width");
    if(rows.back().width+advance>width){
      rows.emplace_back();
    }
    rows.back().chars.push_back(cp);rows.back().width+=advance;
  }
  return rows;
}
struct Metrics {
  FrameCanvas canvas;int edge,pad,size,line,header,bodyHeight,footer;bool above;
};
Metrics metrics(const pe_image_info& src,const pe_border& b,const pe_frame_template& t) {
  require(std::isfinite(b.width)&&b.width>=0&&b.width<=.25&&std::isfinite(b.bottom)&&b.bottom>=0&&b.bottom<=.4,"Invalid border dimensions");
  require(std::memchr(b.font_path,0,sizeof(b.font_path))&&std::memchr(b.text,0,sizeof(b.text)),"Unterminated frame strings");
  Metrics m{};m.edge=std::min(src.width,src.height);m.pad=std::max(2,int(std::lround(m.edge*.045)));
  m.size=std::max(1,int(std::lround(m.edge*.033)));m.line=std::max(2,int(std::ceil(m.size*1.55)));
  if(t.kind==1){
    const int rail=std::max(4,int(std::lround(m.edge*.085))),rim=std::max(2,int(std::lround(m.edge*.035)));
    const int x=t.variant==1?rim:rail,y=t.variant==1?rail:rim;
    m.canvas={src.width+uint32_t(x*2),src.height+uint32_t(y*2+m.line),uint32_t(x),uint32_t(y)};return m;
  }
  Font font(b.font_path);
  const int textWidth=src.width;
  const auto body=wrap(font,t.body,m.size,textWidth,true);
  m.bodyHeight=body.empty()?0:int(body.size())*m.line+m.pad/2;
  // Body precedes the photograph on Facebook, X and Moments.
  m.above=t.variant!=0;
  m.header=std::max(m.line*2,m.pad*2)+m.pad*2;
  m.footer=m.pad+(t.flags&4?m.line*2:0)+(*t.stats?m.line:0)+(*t.timestamp?m.line:0)+(m.above?0:m.bodyHeight);
  m.canvas={src.width+uint32_t(m.pad*2),src.height+uint32_t(m.header+(m.above?m.bodyHeight:0)+m.footer),uint32_t(m.pad),uint32_t(m.header+(m.above?m.bodyHeight:0))};
  return m;
}
struct Painter {
  pe_image& image;int origin;uint32_t bg;
  void pixel(int x,int y,uint32_t color,double alpha=1) {
    if(x<0||x>=int(image.info.width)||y<origin||y>=origin+int(image.info.height))return;
    const size_t p=(size_t(y-origin)*image.info.width+x)*4;
    for(int c=0;c<3;++c){double v=linear(((color>>(16-c*8))&255)/255.0);image.pixels[p+c]=quantize(v*alpha+image.pixels[p+c]/65535.0*(1-alpha));}
    image.pixels[p+3]=65535;
  }
  void rect(int x,int y,int w,int h,uint32_t color,int radius=0) {
    for(int py=std::max(y,origin);py<std::min(y+h,origin+int(image.info.height));++py)
      for(int px=std::max(0,x);px<std::min(x+w,int(image.info.width));++px){
        const double dx=std::max({double(x+radius)-px-.5,0.,px+.5-(x+w-radius)});
        const double dy=std::max({double(y+radius)-py-.5,0.,py+.5-(y+h-radius)});
        const double a=radius?clamp(radius+.5-std::hypot(dx,dy)):1.;if(a)pixel(px,py,color,a);
      }
  }
  void line(double x1,double y1,double x2,double y2,double stroke,uint32_t color){
    const double dx=x2-x1,dy=y2-y1,den=dx*dx+dy*dy;
    for(int y=std::max(origin,int(std::floor(std::min(y1,y2)-stroke)));y<std::min(origin+int(image.info.height),int(std::ceil(std::max(y1,y2)+stroke)));++y)
      for(int x=std::max(0,int(std::floor(std::min(x1,x2)-stroke)));x<std::min(int(image.info.width),int(std::ceil(std::max(x1,x2)+stroke)));++x){
        const double t=den?clamp(((x+.5-x1)*dx+(y+.5-y1)*dy)/den):0;
        const double a=clamp(stroke*.5+.5-std::hypot(x+.5-x1-t*dx,y+.5-y1-t*dy));if(a)pixel(x,y,color,a);
      }
  }
  void text(Font& font,const std::string& s,int x,int top,int size,int width,uint32_t color,bool multiline=false){
    auto rows=wrap(font,s,size,width,multiline);
    while(!multiline&&rows.size()>1&&size>1)rows=wrap(font,s,--size,width,false);
    const int step=std::max(2,int(std::ceil(size*1.55)));
    const int asc=font.face->size->metrics.ascender>>6;
    for(size_t row=0;row<rows.size();++row){int pen=x;const int baseline=top+int(row)*step+asc;
      if(baseline+size<origin||baseline-size*2>=origin+int(image.info.height))continue;
      for(auto cp:rows[row].chars){
        require(FT_Load_Char(font.face,cp,FT_LOAD_RENDER)==0,"Cannot render template text");const auto g=font.face->glyph;
        require(!g->bitmap.rows||g->bitmap.pixel_mode==FT_PIXEL_MODE_GRAY,"请选择支持灰度字形的相框字体",PE_UNSUPPORTED);
        for(unsigned gy=0;gy<g->bitmap.rows;++gy)for(unsigned gx=0;gx<g->bitmap.width;++gx){
          const int pitch=g->bitmap.pitch;const size_t offset=pitch>=0?gy*size_t(pitch):(g->bitmap.rows-1-gy)*size_t(-pitch);
          pixel(pen+g->bitmap_left+gx,baseline-g->bitmap_top+gy,color,g->bitmap.buffer[offset+gx]/255.0);
        }
        pen+=g->advance.x>>6;
      }
    }
  }
  void avatar(const pe_image* src,int x,int y,int size,uint32_t color){
    const double radius=size*.5;
    for(int py=std::max(y,origin);py<std::min(y+size,origin+int(image.info.height));++py)
      for(int px=x;px<x+size;++px){const double a=clamp(radius+.5-std::hypot(px+.5-x-radius,py+.5-y-radius));if(!a)continue;
        if(!src){pixel(px,py,color,a);continue;}
        const double scale=double(std::min(src->info.width,src->info.height))/size;
        const double sx=std::clamp((px+.5-x-radius)*scale+src->info.width*.5-.5,0.,double(src->info.width-1));
        const double sy=std::clamp((py+.5-y-radius)*scale+src->info.height*.5-.5,0.,double(src->info.height-1));
        const int x0=int(sx),y0=int(sy),x1=std::min(x0+1,int(src->info.width)-1),y1=std::min(y0+1,int(src->info.height)-1);
        const double fx=sx-x0,fy=sy-y0;const double weights[]{(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};
        const size_t offsets[]{(size_t(y0)*src->info.width+x0)*4,(size_t(y0)*src->info.width+x1)*4,(size_t(y1)*src->info.width+x0)*4,(size_t(y1)*src->info.width+x1)*4};
        double alpha=0,ink[3]{};for(int i=0;i<4;++i){double w=weights[i]*src->pixels[offsets[i]+3]/65535.;alpha+=w;for(int c=0;c<3;++c)ink[c]+=src->pixels[offsets[i]+c]/65535.*w;}
        if(px<0||px>=int(image.info.width))continue;
        const size_t dst=(size_t(py-origin)*image.info.width+px)*4;
        for(int c=0;c<3;++c)image.pixels[dst+c]=quantize(ink[c]*a+image.pixels[dst+c]/65535.*(1-alpha*a));
      }
  }
  void icon(int kind,int x,int y,int s,uint32_t color){
    const double stroke=std::max(1.,s*.065);auto seg=[&](double a,double b,double c,double d){line(x+a*s,y+b*s,x+c*s,y+d*s,stroke,color);};
    if(kind==0){ // heart
      const double points[][2]{{.5,.9},{.08,.46},{.08,.25},{.24,.12},{.4,.14},{.5,.28},{.6,.14},{.76,.12},{.92,.25},{.92,.46},{.5,.9}};
      for(int i=1;i<11;++i)seg(points[i-1][0],points[i-1][1],points[i][0],points[i][1]);
    }else if(kind==1){seg(.1,.1,.9,.1);seg(.9,.1,.9,.7);seg(.9,.7,.4,.7);seg(.4,.7,.1,.95);seg(.1,.95,.1,.1);
    }else if(kind==2){seg(.05,.45,.95,.05);seg(.95,.05,.6,.95);seg(.6,.95,.42,.58);seg(.42,.58,.05,.45);seg(.42,.58,.95,.05);
    }else{seg(.2,.1,.8,.1);seg(.8,.1,.8,.9);seg(.8,.9,.5,.65);seg(.5,.65,.2,.9);seg(.2,.9,.2,.1);}
  }
};
}
void validate_template(const pe_frame_template& t){
  require(t.kind<=2&&t.variant<=3&&t.flags<=7,"Invalid frame template");
  require(t.kind!=1||t.variant<=1,"Invalid film variant");
  auto check=[](const char* p,size_t n){require(std::memchr(p,0,n),"Unterminated template text");characters(p);};
  check(t.title,sizeof(t.title));check(t.subtitle,sizeof(t.subtitle));check(t.location,sizeof(t.location));check(t.body,sizeof(t.body));check(t.timestamp,sizeof(t.timestamp));check(t.stats,sizeof(t.stats));
  require(std::memchr(t.avatar_path,0,sizeof(t.avatar_path)),"Unterminated avatar path");
}
std::unique_ptr<pe_image> template_avatar(const pe_frame_template* t){
  if(!t||t->kind!=2||!(t->flags&2)||!*t->avatar_path)return nullptr;
  pe_frame f{};std::memcpy(f.logo_path,t->avatar_path,sizeof(f.logo_path));return frame_logo(f);
}
FrameCanvas template_canvas(const pe_image_info& src,const pe_border& b,const pe_frame_template* t){
  require(std::isfinite(b.width)&&b.width>=0&&b.width<=.25&&std::isfinite(b.bottom)&&b.bottom>=0&&b.bottom<=.4,"Invalid border dimensions");
  require(std::memchr(b.font_path,0,sizeof(b.font_path))&&std::memchr(b.text,0,sizeof(b.text)),"Unterminated frame strings");
  if(t&&t->kind){validate_template(*t);auto c=metrics(src,b,*t).canvas;checked_pixels(c.width,c.height);return c;}
  const auto e=std::min(src.width,src.height),p=uint32_t(std::lround(e*b.width)),bottom=uint32_t(std::lround(e*b.bottom));
  return {src.width+p*2,src.height+p*2+bottom,p,p};
}
std::unique_ptr<pe_image> template_strip(const pe_image_info& src,const pe_border& b,const pe_frame_template& t,const pe_image* avatar,uint32_t y,uint32_t rows){
  const auto m=metrics(src,b,t);const auto c=m.canvas;checked_pixels(c.width,c.height);require(y<c.height&&rows<=c.height-y,"Invalid template strip");
  auto out=image(c.width,rows);out->info=src;out->info.width=c.width;out->info.height=rows;
  const uint32_t bg=t.kind==1?0x121212:0xffffff;
  Painter p{*out,int(y),bg};p.rect(0,y,c.width,rows,bg);
  Font font(b.font_path);
  if(t.kind==1){
    const uint32_t amber=0xe8a34b;const int holeW=std::max(2,int(m.edge*.035)),holeH=std::max(2,int(m.edge*.055)),pitch=std::max(4,int(m.edge*.09));
    if(t.flags&1){
      if(t.variant==0){for(int top=c.y;top<int(c.y+src.height);top+=pitch){const int h=std::min(holeH,int(c.y+src.height)-top);p.rect((int(c.x)-holeW)/2,top,holeW,h,0xe6e1d5,2);p.rect(c.x+src.width+(int(c.x)-holeW)/2,top,holeW,h,0xe6e1d5,2);}}
      else for(int left=c.x;left<int(c.x+src.width);left+=pitch){const int w=std::min(holeH,int(c.x+src.width)-left);p.rect(left,(int(c.y)-holeW)/2,w,holeW,0xe6e1d5,2);p.rect(left,c.y+src.height+(int(c.y)-holeW)/2,w,holeW,0xe6e1d5,2);}
    }
    const std::string label=std::string(t.title)+(*t.subtitle?"   "+std::string(t.subtitle):"")+(*t.timestamp?"   "+std::string(t.timestamp):"");
    p.text(font,label,c.x,c.height-m.line,std::max(1,int(m.size*.72)),src.width,amber);return out;
  }
  const uint32_t ink=0x202124,muted=0x777b82,blue=t.variant==3?0x576b95:0x1877f2;
  int nameX=m.pad;const int avatarSize=std::max(m.line*2,m.pad*2);
  if(t.flags&2){p.avatar(avatar,m.pad,m.pad,avatarSize,t.variant==3?0x7ba37b:0xc9cfd8);nameX+=avatarSize+m.pad/2;}
  const int available=c.width-nameX-m.pad;
  p.text(font,t.title,nameX,m.pad,m.size,available,t.variant==3?blue:ink);
  const std::string secondary=t.variant==0?t.location:t.subtitle;
  p.text(font,secondary,nameX,m.pad+m.line,std::max(1,int(m.size*.8)),available,muted);
  if(t.variant!=0&&*t.location)p.text(font,t.location,m.pad,m.header-m.line,std::max(1,int(m.size*.72)),src.width,muted);
  if(m.above)p.text(font,t.body,m.pad,m.header,m.size,src.width,ink,true);
  int top=c.y+src.height+m.pad/2;
  if(t.variant==0&&(t.flags&4)){
    const int s=m.line;for(int i=0;i<3;++i)p.icon(i,m.pad+i*int(s*1.6),top,s,ink);p.icon(3,c.width-m.pad-s,top,s,ink);top+=m.line*2;
  }
  if(*t.stats){p.text(font,t.stats,m.pad,top,std::max(1,int(m.size*.82)),src.width,t.variant==3?blue:ink);top+=m.line;}
  if(!m.above){p.text(font,t.body,m.pad,top,m.size,src.width,ink,true);top+=m.bodyHeight;}
  if(*t.timestamp){p.text(font,t.timestamp,m.pad,top,std::max(1,int(m.size*.75)),src.width,muted);top+=m.line;}
  if(t.variant!=0&&(t.flags&4)){
    p.rect(m.pad,top,src.width,1,0xe4e7eb);top+=m.line/3;
    if(t.variant==1)p.text(font,"Like       Comment       Share",m.pad,top,std::max(1,int(m.size*.85)),src.width,muted);
    else if(t.variant==2){for(int i=0;i<4;++i)p.icon(i==0?1:i==1?2:i==2?0:3,m.pad+i*(src.width/4),top,m.line,muted);}
    else{p.icon(0,m.pad,top,m.line,blue);p.icon(1,m.pad+m.line*2,top,m.line,blue);p.rect(c.width-m.pad-m.line,top+m.line/3,m.line,m.line/2,0xe9ebed,2);}
  }
  return out;
}
std::unique_ptr<pe_image> add_template(const pe_image& src,const pe_frame& f,const pe_frame_adjustments* a,const pe_frame_template& t){
  if(!t.kind){auto logo=frame_logo(f);pe_frame other{};if(a)std::memcpy(other.logo_path,a->accent_path,sizeof(other.logo_path));auto accent=a?frame_logo(other):nullptr;if(a&&!a->enabled)logo=combined_frame_logo(std::move(logo),accent.get());return add_border(src,f.border,&f,logo.get(),a,accent.get());}
  const auto canvas=template_canvas(src.info,f.border,&t);auto avatar=template_avatar(&t);
  auto out=template_strip(src.info,f.border,t,avatar.get(),0,canvas.height);out->lens=src.lens;
  for(uint32_t y=0;y<src.info.height;++y)std::copy_n(src.pixels.data()+size_t(y)*src.info.width*4,size_t(src.info.width)*4,out->pixels.data()+(size_t(y+canvas.y)*canvas.width+canvas.x)*4);
  return out;
}
}
