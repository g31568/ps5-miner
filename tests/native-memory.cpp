#define PS5_NATIVE_DIRECT_ALIGNMENT 0x200000
#include "../homebrew/native-memory.cpp"
#include <cassert>
#include <iostream>
static bool reject_large=false;static int reject_maps=0,allocations=0,releases=0;static size_t last_alignment=0;
// Simulate allocation failures without touching real console memory.
extern "C" size_t sceKernelGetDirectMemorySize(){return size_t(4)<<30;}
extern "C" int sceKernelAllocateDirectMemory(int64_t,int64_t,size_t,size_t a,int,int64_t *p){last_alignment=a;if(reject_large&&a>0x4000)return -1;*p=0x200000;allocations++;return 0;}
extern "C" int sceKernelMapDirectMemory(void **p,size_t,int,int,int64_t,size_t a){last_alignment=a;if(reject_maps>0){reject_maps--;return -1;}*p=reinterpret_cast<void*>(0x400000);return 0;}
extern "C" int sceKernelReleaseDirectMemory(int64_t,size_t){releases++;return 0;}
extern "C" int sceKernelMunmap(void*,size_t){return 0;}
extern "C" int sceKernelMmap(void*,size_t,int,int,int,int64_t,void**){return -1;}
extern "C" int sceKernelMprotect(const void*,size_t,int){return 0;}
extern "C" void xmrigNativeDiagnostic(const char*){}
int main(){
 void *p=xmrigNativeAllocate(0x200000,64);assert(p&&last_alignment==0x200000);xmrigNativeFree(p);assert(allocations==releases);
 reject_large=true;p=xmrigNativeAllocate(0x200000,64);assert(p&&last_alignment==0x4000);xmrigNativeFree(p);assert(allocations==releases);
 // A caller's mandatory alignment must survive every fallback.
 assert(!xmrigNativeAllocate(0x200000,0x200000));assert(allocations==releases);
 reject_large=false;reject_maps=1;p=xmrigNativeAllocate(0x200000,64);assert(p&&last_alignment==0x4000);xmrigNativeFree(p);assert(allocations==releases);
 reject_maps=2;assert(!xmrigNativeAllocate(0x200000,64));assert(allocations==releases);
 assert(!xmrigNativeAllocate((size_t(3)<<30)+1,64));
 std::cout<<"Native memory alignment fallback and ownership passed\n";
}
