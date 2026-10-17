#include "native-keyboard.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstddef>

// System IME ABI: zero reserved fields and retain UTF-16 buffers through Term.
struct ImeParam {
    int32_t user;uint32_t type;uint64_t languages;
    uint32_t enterLabel,inputMethod;void *filter;
    uint32_t option,maxLength;char16_t *buffer;
    float x,y;uint32_t horizontal,vertical;
    const char16_t *placeholder,*title;uint8_t reserved[16];
};
struct ImeResult {uint32_t endStatus;uint8_t reserved[12];};
static_assert(sizeof(ImeParam)==96&&offsetof(ImeParam,buffer)==40);
extern "C" {
int sceSysmoduleLoadModule(uint16_t);
int sceCommonDialogInitialize();
int sceImeDialogInit(const ImeParam *,const void *);
int sceImeDialogGetStatus();
int sceImeDialogGetResult(ImeResult *);
int sceImeDialogTerm();
int sceImeDialogAbort();
void xmrigNativeDiagnostic(const char *);
}
namespace native {
static char16_t input[1024],title[64];
static bool active=false;
static int (*ime_init)(const ImeParam *,const void *);
static int (*ime_status)();static int (*ime_result)(ImeResult *);
static int (*ime_term)();static int (*ime_abort)();
// Native weak imports stay unresolved; check strong import addresses opaquely.
template<class T> static T optional_import(T pointer){asm("" : "+r"(pointer));return pointer;}
static bool load_keyboard() {
    if(ime_init)return true;
    // Load IME before initializing the common dialog family, once per process.
    int r=sceSysmoduleLoadModule(0x0096);char line[128];
    snprintf(line,sizeof(line),"Keyboard module load=%x",r);xmrigNativeDiagnostic(line);
    if(r<0)return false;
    auto common=optional_import(sceCommonDialogInitialize);
    auto init=optional_import(sceImeDialogInit);
    auto status=optional_import(sceImeDialogGetStatus);
    auto result=optional_import(sceImeDialogGetResult);
    auto term=optional_import(sceImeDialogTerm);
    auto abort=optional_import(sceImeDialogAbort);
    if(!common||!init||!status||!result||!term||!abort){xmrigNativeDiagnostic("Keyboard native import unavailable");return false;}
    r=common();snprintf(line,sizeof(line),"Keyboard CommonDialog init=%x",r);xmrigNativeDiagnostic(line);
    if(r&&static_cast<uint32_t>(r)!=0x80b80002)return false;
    ime_init=init;ime_status=status;ime_result=result;ime_term=term;ime_abort=abort;
    return true;
}
// UTF-8 conversion preserves usernames without treating bytes as UTF-16 letters.
static bool to_utf16(const char *s,char16_t *out,size_t size) {
    size_t n=0;
    while(*s){uint32_t cp=static_cast<unsigned char>(*s++);unsigned more=0;
        if(cp>=0xf0){more=3;cp&=7;}else if(cp>=0xe0){more=2;cp&=15;}else if(cp>=0xc0){more=1;cp&=31;}else if(cp>=0x80)return false;
        for(unsigned i=0;i<more;i++){uint32_t c=static_cast<unsigned char>(*s);if((c&0xc0)!=0x80)return false;s++;cp=(cp<<6)|(c&63);}
        if(cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return false;
        if(cp>=0x10000){if(n+2>=size)return false;cp-=0x10000;out[n++]=0xd800+(cp>>10);out[n++]=0xdc00+(cp&1023);}
        else{if(n+1>=size)return false;out[n++]=static_cast<char16_t>(cp);}
    }out[n]=0;return true;
}
bool keyboard_open(int user,unsigned row,const char *value) {
    if(active||!to_utf16(value,input,1024)||!load_keyboard())return false;
    const char *labels[]={"Threads (1-16)","Pool address (host:port)","Pool user","HTTP API (0, off, or port)","Donation percent (0-99)","Rig ID (blank for default)","SOCKS5 proxy (host:port; blank to disable)"};
    // Rig ID uses the same text keyboard as the pool user.
    // GPU fields have separate IDs so CPU keyboard labels and numeric modes stay stable.
    const char *gpu_labels[]={"GPU pool host","GPU pool port","GPU user / wallet","GPU worker / password","GPU proxy host","GPU proxy port","GPU TLS (On or Off)","Verify GPU certificate (On or Off)"};
    if(row>=15)return false;to_utf16(row<7?labels[row]:gpu_labels[row-7],title,64);
    ImeParam p{};p.user=user;p.type=row==0||row==4||row==8||row==12?4:1;p.option=2|32|512;p.maxLength=row==9?511:row==10?255:row>=7?253:191;p.buffer=input;p.title=title;
    int r=ime_init(&p,nullptr);char line[128];snprintf(line,sizeof(line),"Keyboard init=%x status=%d field=%u",r,ime_status(),row);xmrigNativeDiagnostic(line);
    active=r==0;return active;
}
bool keyboard_active(){return active;}
int keyboard_poll(char *value,size_t size) {
    if(!active||ime_status()!=2)return 0;
    ImeResult result{};int r=ime_result(&result);ime_term();active=false;
    if(r||result.endStatus)return -1;
    size_t n=0;
    for(size_t i=0;i<1024&&input[i];i++){uint32_t cp=input[i];
        if(cp>=0xd800&&cp<=0xdbff){if(i+1>=1024||input[i+1]<0xdc00||input[i+1]>0xdfff)return -2;cp=0x10000+((cp-0xd800)<<10)+(input[++i]-0xdc00);}
        else if(cp>=0xdc00&&cp<=0xdfff)return -2;
        unsigned bytes=cp<0x80?1:cp<0x800?2:cp<0x10000?3:4;if(n+bytes>=size)return -2;
        if(bytes==1)value[n++]=cp;
        else{value[n++]=(bytes==2?0xc0:bytes==3?0xe0:0xf0)|(cp>>(6*(bytes-1)));for(unsigned j=bytes-1;j>0;j--)value[n++]=0x80|((cp>>(6*(j-1)))&63);}
    }value[n]=0;return 1;
}
void keyboard_cancel(){if(active)ime_abort();}
}
