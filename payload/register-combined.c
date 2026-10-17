// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
// Ask the existing homebrew manager to discover the new title through its public API.
int main(void) {
    int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return 1;
    struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_port=htons(10101);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(connect(fd,(void *)&a,sizeof(a))){close(fd);return 2;}
    const char request[]="POST /api/v1/scan HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";
    size_t sent=0;while(sent<sizeof(request)-1){ssize_t n=send(fd,request+sent,sizeof(request)-1-sent,0);if(n<=0){close(fd);return 3;}sent+=(size_t)n;}
    FILE *f=fopen("/data/ps5-miner-register.log","wb");if(!f){close(fd);return 4;}
    char buffer[4096];ssize_t n;while((n=recv(fd,buffer,sizeof(buffer),0))>0)fwrite(buffer,1,(size_t)n,f);
    fclose(f);close(fd);return 0;
}
