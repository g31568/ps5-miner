#pragma once
namespace native {
// Monero orange (#FF6600) is shared by the screen, icons and controller light.
inline constexpr unsigned char monero_red=255,monero_green=102,monero_blue=0;
inline constexpr unsigned monero_rgba=0xff000000u|(unsigned(monero_blue)<<16)|(unsigned(monero_green)<<8)|monero_red;
// One Monero-inspired shape drives both the screen and app icon.
inline bool logo_pixel(int x,int y,int size) {
    double px=(2.0*x+1)/size-1,py=(2.0*y+1)/size-1;
    if(px*px+py*py>1)return false;
    constexpr double polygon[][2]={{-.7,-.5},{0,.15},{.7,-.5},{.7,.4},{1,.4},{1,.6},{.45,.6},{.45,.03},{0,.45},{-.45,.03},{-.45,.6},{-1,.6},{-1,.4},{-.7,.4}};
    bool cut=false;
    for(unsigned i=0,j=13;i<14;j=i++){
        auto &a=polygon[i];auto &b=polygon[j];
        if((a[1]>py)!=(b[1]>py)&&px<(b[0]-a[0])*(py-a[1])/(b[1]-a[1])+a[0])cut=!cut;
    }return !cut;
}
}
