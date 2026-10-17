#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>

namespace native {
inline constexpr unsigned option_count=7,screen_option_count=6;
// Keep stable config field IDs while excluding the web-only HTTP API from navigation.
inline unsigned screen_field(unsigned row) {static const unsigned fields[]={0,1,2,6,4,5};return fields[row%screen_option_count];}
inline unsigned adjacent_field(unsigned field,int direction) {
    for(unsigned row=0;row<screen_option_count;row++)if(screen_field(row)==field)return screen_field((row+screen_option_count+direction)%screen_option_count);
    return screen_field(0);
}
// Pending settings are independent of the active miner until confirmed.
struct Options {
    unsigned threads=8, port=0, donation=0;
    bool http=true;
    // Public defaults also apply if the starter configuration cannot be loaded.
    char address[192]="gulf.moneroocean.stream:128", user[192]="YOUR_WALLET", rig_id[192]="", proxy[192]="";
};
inline const char *field_name(unsigned row) {
    static const char *names[]={"Threads","Address","User","HTTP API","Donation","Rig ID","Proxy"};
    return row<option_count?names[row]:"";
}
inline bool number(const char *s,unsigned limit,unsigned &out) {
    if(!s||!*s)return false;
    for(const char *p=s;*p;p++)if(*p<'0'||*p>'9')return false;
    errno=0;char *end=nullptr;unsigned long n=strtoul(s,&end,10);
    if(errno||*end||n>limit)return false;out=static_cast<unsigned>(n);return true;
}
inline const char *edit(Options &o,unsigned row,const char *value) {
    unsigned n=0;
    switch(row){
    case 0: if(!number(value,16,n)||n<1)return "Threads must be 1-16";o.threads=n;break;
    case 1: {
        size_t size=strlen(value);const char *colon=strrchr(value,':');
        if(size>=sizeof(o.address)||!colon||colon==value||!number(colon+1,65535,n)||!n)return "Use host:port (1-65535)";
        for(const char *p=value;*p;p++)if(static_cast<unsigned char>(*p)<=32||*p=='/'||*p=='\\')return "Use host:port without spaces";
        // IPv6 hosts need brackets so the endpoint remains unambiguous.
        if(strchr(value,':')!=colon&&(value[0]!='['||colon[-1]!=']'))return "Use [IPv6]:port";
        if(value[0]=='['&&(colon[-1]!=']'||colon-value<3))return "Invalid IPv6 endpoint";
        snprintf(o.address,sizeof(o.address),"%s",value);break;
    }
    case 2:
        if(!*value||strlen(value)>=sizeof(o.user))return "User must be 1-191 bytes";
        for(const char *p=value;*p;p++)if(static_cast<unsigned char>(*p)<32)return "User contains a control character";
        snprintf(o.user,sizeof(o.user),"%s",value);break;
    case 3:
        if(!strcmp(value,"off")||!strcmp(value,"OFF")){o.http=false;o.port=0;break;}
        if(!strcmp(value,"auto")||!strcmp(value,"AUTO"))n=0;
        else if(!number(value,65535,n))return "HTTP API: 0, off, or port 1-65535";
        o.http=true;o.port=n;break;
    case 4: if(!number(value,99,n))return "Donation must be 0-99";o.donation=n;break;
    case 5:
        // Empty text restores the upstream unset rig-id; never invent an identifier.
        if(strlen(value)>=sizeof(o.rig_id))return "Rig ID must be 0-191 bytes";
        for(const char *p=value;*p;p++)if(static_cast<unsigned char>(*p)<32)return "Rig ID contains a control character";
        snprintf(o.rig_id,sizeof(o.rig_id),"%s",value);break;
    case 6: {
        // An empty proxy is disabled; endpoint validation matches the pool address.
        if(!*value||!strcmp(value,"off")||!strcmp(value,"OFF")){o.proxy[0]=0;break;}
        Options endpoint;if(edit(endpoint,1,value))return "Proxy: host:port, blank, or off";
        snprintf(o.proxy,sizeof(o.proxy),"%s",value);break;
    }
    default:return "Unknown field";
    }
    return nullptr;
}
inline void field(const Options &o,unsigned row,char *s,size_t size,bool display=false) {
    switch(row){
    case 0:snprintf(s,size,"%u",o.threads);break;
    case 1:snprintf(s,size,"%s",o.address);break;
    case 2:snprintf(s,size,"%s",o.user);break;
    case 3:if(!o.http)snprintf(s,size,"%s",display?"Off":"off");else if(!o.port)snprintf(s,size,"%s",display?"0":"auto");else snprintf(s,size,"%u",o.port);break;
    case 4:snprintf(s,size,display?"%u%%":"%u",o.donation);break;
    case 5:snprintf(s,size,"%s",display&&!*o.rig_id?"Default":o.rig_id);break;
    case 6:snprintf(s,size,"%s",display&&!*o.proxy?"Off":o.proxy);break;
    default:*s=0;
    }
}
// A full rolling hour includes stopped seconds; unavailable history stays blank.
class HourAverage {
    std::array<double,3600> rates{};
    uint64_t first=0,last=0;
    bool initialized=false;
    double sum=0;
public:
    void add(uint64_t second,double rate) {
        if(!initialized){initialized=true;first=last=second;}
        if(second<last)return;
        uint64_t gap=second-last;
        if(gap>=rates.size()){rates.fill(0);sum=0;}
        else for(uint64_t s=last+1;s<=second;s++){size_t i=s%rates.size();sum-=rates[i];rates[i]=0;}
        size_t i=second%rates.size();sum-=rates[i];rates[i]=rate;sum+=rate;last=second;
    }
    bool ready() const {return initialized&&last-first>=3599;}
    double value() const {return sum/rates.size();}
};
}
