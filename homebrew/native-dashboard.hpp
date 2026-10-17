// Shared TV drawing code also runs in the host screenshot preview.
#pragma once
#include "demo_renderer.hpp"
#include "native-ui-font.hpp"
#include "native-logo.hpp"
#include "native-options.hpp"
#include "native-gpu-options.hpp"
#include <cstdio>
#include <cstring>

namespace native::ui {
using ps5::demo::Canvas;
using ps5::demo::Color;
inline constexpr Color background=static_cast<Color>(0xff141110), muted=static_cast<Color>(0xffc5bbb6),
    green=static_cast<Color>(0xffb0e268), attention=static_cast<Color>(0xff82c9ff), divider=static_cast<Color>(0xff403a35);
// A 12-column grid uses 96px margins, 24px gutters and an 8px spacing rhythm.
inline constexpr unsigned left=96, rate_x=688, action_x=1480, right=1824;
inline unsigned width(const char *value,unsigned size){
    const Glyph *glyphs=size==96?glyphs96:size==56?glyphs56:size==40?glyphs40:glyphs32;
    unsigned result=0;
    for(const unsigned char *p=reinterpret_cast<const unsigned char *>(value);*p;p++)result+=glyphs[(*p>=32&&*p<127?*p:'?')-32].advance;
    return result;
}
// Fit by measured glyph advances; long wallets keep their recognizable final bytes.
inline void fit(const char *value,char *out,size_t capacity,unsigned available,unsigned size,bool tail=false){
    if(!capacity)return;
    snprintf(out,capacity,"%s",value);
    if(width(out,size)<=available)return;
    char ending[8]="...";
    size_t length=strlen(value);
    if(tail&&length>4)snprintf(ending,sizeof(ending),"...%s",value+length-4);
    while(*out&&(width(out,size)+width(ending,size)>available||strlen(out)+strlen(ending)>=capacity))out[strlen(out)-1]=0;
    if(width(ending,size)<=available&&strlen(out)+strlen(ending)<capacity)strcat(out,ending);
}
inline unsigned label(Canvas &canvas,unsigned x,unsigned y,const char *value,unsigned size,Color color=Color::white,unsigned limit=right) {
    const Glyph *glyphs=size==96?glyphs96:size==56?glyphs56:size==40?glyphs40:glyphs32;
    const Run *runs=size==96?runs96:size==56?runs56:size==40?runs40:runs32;
    for(const unsigned char *p=reinterpret_cast<const unsigned char *>(value);*p;p++){
        const auto &g=glyphs[(*p>=32&&*p<127?*p:'?')-32];
        // Keep all text inside its assigned region, including unexpected runtime values.
        if(x>=limit||g.advance>limit-x)break;
        for(unsigned i=g.offset;i<g.offset+g.count;i++){
            const auto &r=runs[i];unsigned packed=0xff000000;
            for(unsigned shift=0;shift<24;shift+=8){
                unsigned fg=(static_cast<unsigned>(color)>>shift)&255,bg=(static_cast<unsigned>(background)>>shift)&255;
                packed|=((fg*r.alpha+bg*(255-r.alpha)+127)/255)<<shift;
            }
            if(x+r.x<limit)canvas.rectangle(x+r.x,y+r.y,r.width<limit-x-r.x?r.width:limit-x-r.x,1,static_cast<Color>(packed));
        }
        x+=g.advance;
    }
    return x;
}
inline void rule(Canvas &canvas,unsigned y){canvas.rectangle(left,y,right-left,2,divider);}
inline void box(Canvas &canvas,unsigned x,unsigned y,unsigned w,unsigned h,Color color){
    canvas.rectangle(x,y,w,2,color);canvas.rectangle(x,y+h-2,w,2,color);
    canvas.rectangle(x,y,2,h,color);canvas.rectangle(x+w-2,y,2,h,color);
}
inline void header(Canvas &canvas){
    canvas.clear(background);
    for(int y=0;y<88;y++)for(int x=0;x<88;x++)if(native::logo_pixel(x,y,88))
        canvas.rectangle(left+x,64+y,1,1,static_cast<Color>(native::monero_rgba));
    label(canvas,208,64,"XMR Miner",56);
}
inline void status(Canvas &canvas,unsigned y,const char *state,bool healthy,bool warning){
    Color color=healthy?green:warning?attention:muted;
    // A symbol and plain text communicate state without relying on color alone.
    canvas.circle(left+18,y+23,16,color);canvas.circle(left+18,y+23,12,background);
    if(healthy){
        // Draw a check inside the ring without requiring a Unicode font glyph.
        for(unsigned i=0;i<6;i++)canvas.rectangle(left+9+i,y+22+i,3,3,color);
        for(unsigned i=0;i<12;i++)canvas.rectangle(left+14+i,y+27-i,3,3,color);
    }else label(canvas,left+8,y+1,warning?"!":"-",32,color);
    label(canvas,left+48,y,state,32,color);
}
inline void action(Canvas &canvas,unsigned y,const char *button,const char *title,bool available=true){
    box(canvas,action_x,y+8,72,48,available?muted:divider);
    label(canvas,action_x+12,y+9,button,32,available?Color::white:muted);
    label(canvas,action_x+92,y+8,title,32,available?Color::white:muted);
}
inline void engine(Canvas &canvas,unsigned top,const char *name,const char *detail,const char *state,
                   bool healthy,bool warning,const char *rate,const char *unit,const char *button,const char *command,bool available=true){
    label(canvas,left,top,name,56);label(canvas,left,top+80,detail,32,muted);
    status(canvas,top+136,state,healthy,warning);
    // Reserve space for the unit before choosing the rate's display size.
    unsigned size=width(rate,96)+width(unit,32)+16<=action_x-rate_x-24?96:56;
    unsigned end=label(canvas,rate_x,top,rate,size,Color::white,action_x-width(unit,32)-40);
    label(canvas,end+16,top+(size==96?50:12),unit,32,Color::white,action_x-24);
    action(canvas,top+72,button,command,available);
}
inline void settings_hint(Canvas &canvas,const char *title="Settings"){
    for(unsigned i=0;i<3;i++)canvas.rectangle(1576,958+i*12,32,4,Color::white);
    label(canvas,1632,944,title,32);
}
inline void settings_tabs(Canvas &canvas,bool gpu){
    label(canvas,left,192,"CPU",40,gpu?muted:Color::white);
    label(canvas,256,192,"GPU",40,gpu?Color::white:muted);
    canvas.rectangle(gpu?256:left,244,96,3,green);
    label(canvas,424,200,"Left / Right: engine",32,muted);
    label(canvas,left,264,"Up / Down: select    X: edit    Options: save    Circle: cancel",32,muted);
}
inline void settings(Canvas &canvas,const native::Options &pending,unsigned selected){
    settings_tabs(canvas,false);
    for(unsigned index=0;index<native::screen_option_count;index++){
        unsigned row=native::screen_field(index),y=344+index*64;
        if(row==selected)box(canvas,88,y-4,1744,60,green);
        label(canvas,112,y,native::field_name(row),32,muted);
        char value[192],shown[192];native::field(pending,row,value,sizeof(value),true);
        fit(value,shown,sizeof(shown),right-rate_x-24,32,row==2);
        label(canvas,rate_x,y,shown,32);
    }
    settings_hint(canvas,"Save");
}
inline void gpu_settings(Canvas &canvas,const rapidjson::Document &pending,unsigned selected,bool running){
    settings_tabs(canvas,true);
    // Six visible rows preserve the existing QR and feedback area on both pages.
    unsigned first=selected<6?0:6;
    for(unsigned row=first;row<native::gpu_option_count&&row<first+6;row++){
        unsigned y=344+(row-first)*64;
        if(row==selected)box(canvas,88,y-4,1744,60,green);
        label(canvas,112,y,native::gpu_field_name(row),32,muted);
        char value[1024],shown[1024];native::gpu_field(pending,row,value,sizeof(value),true);
        fit(value,shown,sizeof(shown),right-rate_x-24,32,row==2);
        label(canvas,rate_x,y,shown,32);
    }
    label(canvas,left,744,first?"Fields 7-8 / 8":"Fields 1-6 / 8",32,muted);
    label(canvas,left,712,running?"R1: stop GPU before saving":"C29 pool settings; CPU is independent",32,muted);
    settings_hint(canvas,"Save");
}
}
