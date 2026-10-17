#include "../homebrew/native-keyboard.cpp"
#include <cassert>
#include <iostream>
static int status=0,end_status=0;static char16_t *buffer=nullptr;static unsigned expected_length=191;
// Stub only the platform boundary; exercise actual conversion and dialog lifetime.
extern "C" int sceImeDialogInit(const ImeParam *p,const void*){assert(p->user==123);assert(p->title&&p->maxLength==expected_length);buffer=p->buffer;status=1;return 0;}
extern "C" int sceImeDialogGetStatus(){return status;}
extern "C" int sceImeDialogGetResult(ImeResult *r){r->endStatus=end_status;return 0;}
extern "C" int sceImeDialogTerm(){status=0;return 0;}
extern "C" int sceImeDialogAbort(){status=2;end_status=1;return 0;}
extern "C" int sceCommonDialogInitialize(){return 0;}
extern "C" void xmrigNativeDiagnostic(const char*){}
extern "C" int sceSysmoduleLoadModule(uint16_t id){assert(id==0x96);return 0;}
int main(){char out[192];
 assert(native::keyboard_open(123,2,"user-測試"));assert(native::keyboard_active());assert(native::keyboard_poll(out,sizeof(out))==0);
 status=2;assert(native::keyboard_poll(out,sizeof(out))==1);assert(!strcmp(out,"user-測試"));assert(!native::keyboard_active());
 assert(native::keyboard_open(123,0,"8"));native::keyboard_cancel();assert(native::keyboard_poll(out,sizeof(out))==-1);
 end_status=0;assert(native::keyboard_open(123,2,"x"));buffer[0]=0xd83d;buffer[1]=0xde00;buffer[2]=0;status=2;
 assert(native::keyboard_poll(out,sizeof(out))==1&&!strcmp(out,"😀"));
 assert(native::keyboard_open(123,2,"abcdef"));status=2;assert(native::keyboard_poll(out,3)==-2);
 // The optional rig-id opens a text dialog and accepts a blank result.
 assert(native::keyboard_open(123,5,""));status=2;assert(native::keyboard_poll(out,sizeof(out))==1&&!*out);
 assert(native::keyboard_open(123,6,"10.1.1.111:8964"));native::keyboard_cancel();assert(native::keyboard_poll(out,sizeof(out))==-1);
 // GPU wallet and worker fields honor the transport's larger byte capacities.
 end_status=0;expected_length=511;char wallet[512];memset(wallet,'a',511);wallet[511]=0;
 assert(native::keyboard_open(123,9,wallet));status=2;char large[1024];assert(native::keyboard_poll(large,sizeof(large))==1&&strlen(large)==511);
 expected_length=255;assert(native::keyboard_open(123,10,"worker"));native::keyboard_cancel();assert(native::keyboard_poll(large,sizeof(large))==-1);
 std::cout<<"Keyboard conversion and cancellation passed\n";
}
