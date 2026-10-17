#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <mutex>
extern "C" {
size_t sceKernelGetDirectMemorySize();
int sceKernelAllocateDirectMemory(int64_t,int64_t,size_t,size_t,int,int64_t *);
int sceKernelMapDirectMemory(void **,size_t,int,int,int64_t,size_t);
int sceKernelMmap(void *,size_t,int,int,int,int64_t,void **);
int sceKernelMprotect(const void *,size_t,int);
int sceKernelMunmap(void *,size_t);
int sceKernelReleaseDirectMemory(int64_t,size_t);
void xmrigNativeDiagnostic(const char *);
}
#ifndef PS5_NATIVE_DIRECT_ALIGNMENT
#define PS5_NATIVE_DIRECT_ALIGNMENT 0x4000
#endif
/* Keep direct-memory ownership separate from ordinary libc allocations. */
struct Block {void *address;int64_t physical;size_t size;};
static Block blocks[64]{};
static std::mutex block_lock;
extern "C" void *xmrigNativeAllocate(size_t size,size_t alignment) {
    // Bound allocations while admitting the 2080 MiB RandomX fast-mode dataset.
    if(!size||size>size_t(3)*1024u*1024u*1024u)return nullptr;
    size=(size+0x3fff)&~size_t(0x3fff);if(alignment<0x4000)alignment=0x4000;
    const size_t required_alignment=alignment;
    // Test larger alignment, but never relax a caller's required alignment.
    if(size>=0x200000&&alignment<PS5_NATIVE_DIRECT_ALIGNMENT)alignment=PS5_NATIVE_DIRECT_ALIGNMENT;
    std::lock_guard<std::mutex> lock(block_lock);
    Block *slot=nullptr;for(auto &b:blocks)if(!b.address){slot=&b;break;}if(!slot)return nullptr;
    int64_t physical=0;void *mapped=nullptr;
    const size_t pool_size=sceKernelGetDirectMemorySize();
    char line[160];snprintf(line,sizeof(line),"direct pool bytes=%zu requested=%zu alignment=%zu",pool_size,size,alignment);xmrigNativeDiagnostic(line);
    int r=sceKernelAllocateDirectMemory(0,pool_size,size,alignment,0,&physical);
    snprintf(line,sizeof(line),"direct allocate bytes=%zu result=%x",size,r);xmrigNativeDiagnostic(line);
    if(r<0&&alignment>required_alignment){alignment=required_alignment;r=sceKernelAllocateDirectMemory(0,pool_size,size,alignment,0,&physical);xmrigNativeDiagnostic("Direct allocation retried with ordinary alignment");}
    if(r<0)return nullptr;
    // CPU read/write only; no executable-memory protection or kernel patches.
    r=sceKernelMapDirectMemory(&mapped,size,3,0,physical,alignment);
    if(r<0&&alignment>required_alignment){alignment=required_alignment;r=sceKernelMapDirectMemory(&mapped,size,3,0,physical,alignment);}
    snprintf(line,sizeof(line),"direct map result=%x address=%p physical=%llx alignment=%zu",r,mapped,(unsigned long long)physical,alignment);xmrigNativeDiagnostic(line);
    if(r<0){sceKernelReleaseDirectMemory(physical,size);return nullptr;}
    *slot={mapped,physical,size};return mapped;
}
extern "C" void xmrigNativeFree(void *address) {
    if(!address)return;std::lock_guard<std::mutex> lock(block_lock);
    for(auto &b:blocks)if(b.address==address){sceKernelMunmap(b.address,b.size);if(b.physical>=0)sceKernelReleaseDirectMemory(b.physical,b.size);b={};return;}
}

/* JIT uses normal native APIs and owns each mapping; denials return to the interpreter. */
extern "C" void *xmrigNativeAllocateExecutable(size_t size) {
    if(!size||size>16u*1024u*1024u)return nullptr;
    size=(size+0x3fff)&~size_t(0x3fff);
    std::lock_guard<std::mutex> lock(block_lock);
    Block *slot=nullptr;for(auto &b:blocks)if(!b.address){slot=&b;break;}if(!slot)return nullptr;
    void *mapped=nullptr;int64_t physical=-1;
    int r=sceKernelMmap(nullptr,size,7,0x1002,-1,0,&mapped);
    char line[160];snprintf(line,sizeof(line),"JIT mmap bytes=%zu result=%x",size,r);xmrigNativeDiagnostic(line);
    if(r<0){
        // Direct mappings offer a second documented native allocation path.
        r=sceKernelAllocateDirectMemory(0,sceKernelGetDirectMemorySize(),size,0x4000,0,&physical);
        if(r>=0){r=sceKernelMapDirectMemory(&mapped,size,7,0,physical,0x4000);if(r<0)sceKernelReleaseDirectMemory(physical,size);}
        snprintf(line,sizeof(line),"JIT direct mapping result=%x",r);xmrigNativeDiagnostic(line);
    }
    if(r<0||!mapped)return nullptr;
    // Validate write/execute transitions before exposing a mapping to RandomX.
    int rw=sceKernelMprotect(mapped,size,3),rx=rw<0?rw:sceKernelMprotect(mapped,size,5);
    int all=rx<0?rx:sceKernelMprotect(mapped,size,7);
    snprintf(line,sizeof(line),"JIT protections RW=%x RX=%x RWX=%x",rw,rx,all);xmrigNativeDiagnostic(line);
    if(all<0){sceKernelMunmap(mapped,size);if(physical>=0)sceKernelReleaseDirectMemory(physical,size);return nullptr;}
    *slot={mapped,physical,size};return mapped;
}
/* RandomX assumes 4 KiB pages; protect the owned 16 KiB-aligned allocation instead. */
extern "C" bool xmrigNativeProtect(void *address,size_t size,int protection) {
    std::lock_guard<std::mutex> lock(block_lock);
    for(auto &b:blocks){uintptr_t start=(uintptr_t)b.address,p=(uintptr_t)address;
        if(b.address&&p>=start&&p-start<b.size&&size<=b.size-(p-start))return sceKernelMprotect(b.address,b.size,protection)==0;
    }
    return false;
}
