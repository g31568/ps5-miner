#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <net/if.h>
extern "C" {
/* Native apps do not exec children; tolerate unsupported pipe close-on-exec. */
int pipe2(int fd[2], int flags) {
    int pair[2]={-1,-1};char line[160];
    extern void xmrigNativeDiagnostic(const char *);
    int r=pipe(pair);snprintf(line,sizeof(line),"native pipe result=%d errno=%d fd=%d,%d",r,errno,pair[0],pair[1]);xmrigNativeDiagnostic(line);
    if(r){r=socketpair(AF_UNIX,SOCK_STREAM,0,pair);snprintf(line,sizeof(line),"native socketpair result=%d errno=%d",r,errno);xmrigNativeDiagnostic(line);if(r)return -1;}
    for(int i=0;i<2;i++) {
        r=(flags&O_CLOEXEC)?fcntl(pair[i],F_SETFD,FD_CLOEXEC):0;
        snprintf(line,sizeof(line),"native cloexec fd=%d result=%d errno=%d",pair[i],r,errno);xmrigNativeDiagnostic(line);
        // This title never spawns children; EINVAL on FD_CLOEXEC is harmless.
        if(r<0&&errno==EINVAL)r=0;
        if(r>=0&&(flags&O_NONBLOCK)){r=fcntl(pair[i],F_SETFL,O_NONBLOCK);snprintf(line,sizeof(line),"native nonblock fd=%d result=%d errno=%d",pair[i],r,errno);xmrigNativeDiagnostic(line);}
        if(r<0){int saved=errno;close(pair[0]);close(pair[1]);errno=saved;return -1;}
    }
    fd[0]=pair[0];fd[1]=pair[1];return 0;
}
/* Native JIT uses Sony APIs; never invoke the SDK's kernel patch fallback. */
int kernel_mprotect(int, intptr_t, size_t, int) { errno=EPERM;return -1; }
/* Optional libuv symbol probes can fail normally without a payload loader. */
void *__dlsym(void *, const char *) { return nullptr; }
char *__dlerror() { static char message[]="Payload dynamic lookup disabled in native app";return message; }
}

/* Resolve IPv6 scope names through RADV's native interface list, without payload syscalls. */
extern "C" unsigned if_nametoindex(const char *name) {
    if(!name||!*name){errno=EINVAL;return 0;}
    struct if_nameindex *list=if_nameindex();if(!list)return 0;
    unsigned result=0;
    for(auto *item=list;item->if_index&&item->if_name;++item)if(!strcmp(item->if_name,name)){result=item->if_index;break;}
    if_freenameindex(list);if(!result)errno=ENXIO;return result;
}

/* Native titles have no terminal, so libuv must skip its optional TTY backend. */
extern "C" int isatty(int) { errno=ENOTTY;return 0; }

/* Bind ordinary mappings to native APIs rather than payload syscall stubs. */
extern "C" int sceKernelMmap(void *,size_t,int,int,int,int64_t,void **);
extern "C" int sceKernelMunmap(void *,size_t);
extern "C" int sceKernelMprotect(const void *,size_t,int);
extern "C" void *mmap(void *address,size_t size,int protection,int flags,int fd,int64_t offset) {
    void *result=nullptr;int error=sceKernelMmap(address,size,protection,flags,fd,offset,&result);
    if(error<0){errno=static_cast<unsigned>(error)&0xffffu;return reinterpret_cast<void *>(-1);}
    return result;
}
extern "C" int munmap(void *address,size_t size) {
    int result=sceKernelMunmap(address,size);if(result<0){errno=static_cast<unsigned>(result)&0xffffu;return -1;}return 0;
}
extern "C" int mprotect(void *address,size_t size,int protection) {
    int result=sceKernelMprotect(address,size,protection);if(result<0){errno=static_cast<unsigned>(result)&0xffffu;return -1;}return 0;
}
