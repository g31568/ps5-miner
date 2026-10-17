// Render the production dashboard helpers into a host PPM for layout inspection.
#include "native-dashboard.hpp"
#include <algorithm>
#include <cstdlib>
#include <vector>
#include <cassert>
namespace ps5::demo {
void Canvas::clear(Color color) noexcept { std::fill(pixels_,pixels_+1920*1080,static_cast<uint32_t>(color)); }
void Canvas::rectangle(unsigned x,unsigned y,unsigned w,unsigned h,Color color) noexcept {
    for(unsigned row=y;row<std::min(1080u,y+h);row++)
        for(unsigned col=x;col<std::min(1920u,x+w);col++)pixels_[row*1920+col]=static_cast<uint32_t>(color);
}
void Canvas::circle(unsigned x,unsigned y,unsigned r,Color color) noexcept {
    for(int dy=-int(r);dy<=int(r);dy++)for(int dx=-int(r);dx<=int(r);dx++)
        if(dx*dx+dy*dy<=int(r*r)&&int(x)+dx>=0&&int(y)+dy>=0)rectangle(x+dx,y+dy,1,1,color);
}
namespace detail {
[[noreturn]] void present(DrawScene draw,std::string_view,bool) noexcept {
    std::vector<uint32_t> pixels(1920*1080);Canvas canvas(pixels.data());draw(canvas);
    std::printf("P6\n1920 1080\n255\n");
    for(auto pixel:pixels){unsigned char rgb[]={static_cast<unsigned char>(pixel),static_cast<unsigned char>(pixel>>8),static_cast<unsigned char>(pixel>>16)};std::fwrite(rgb,1,3,stdout);}
    std::exit(0);
}
}
}
static int state=0;
static void preview(ps5::demo::Canvas &canvas) noexcept {
    using namespace native::ui;
    header(canvas);
    if(state==4||state==5){
        rapidjson::Document doc;doc.Parse("{\"url\":\"pool.example\",\"port\":10128,\"login\":\"YOUR_WALLET\",\"pass\":\"PS5-GPU\",\"is_tls\":true,\"tls_verify\":true}");
        gpu_settings(canvas,doc,state==4?0:7,true);rule(canvas,928);return;
    }
    if(state==3){
        // Exercise maximum-width input rather than only short demo values.
        native::Options options;
        memset(options.user,'W',191);options.user[191]=0;
        memset(options.rig_id,'W',191);options.rig_id[191]=0;
        settings(canvas,options,2);rule(canvas,928);return;
    }
    engine(canvas,248,"CPU","RandomX - 16 threads",state==1?"Stopped":state==2?"Connecting...":"Mining",state==0,state==2,
        state?"--":"12345","H/s","L1",state==1?"Start CPU":"Stop CPU");
    rule(canvas,528);
    engine(canvas,600,"GPU","C29",state==1?"Stopped":state==2?"Relaunch app":"Mining",state==0,state==2,
        state?"--":"12.345","graphs/s","R1",state==1?"Start GPU":state==2?"Unavailable":"Stop GPU",state!=2);
    rule(canvas,928);settings_hint(canvas);
}
int main(int argc,char **argv){
    // Regression: truncation stays within the field and preserves a wallet suffix.
    char value[192],shown[192];memset(value,'W',191);value[191]=0;
    native::ui::fit(value,shown,sizeof(shown),1112,32,true);
    assert(native::ui::width(shown,32)<=1112);
    assert(strlen(shown)>7&&!strcmp(shown+strlen(shown)-7,"...WWWW"));
    native::ui::fit("A",shown,sizeof(shown),0,32);assert(!*shown);
    state=argc>1?std::atoi(argv[1]):0;ps5::demo::detail::present(preview,"",false);
}
