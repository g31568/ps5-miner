#include "native-web.hpp"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <map>
#include <new>

namespace native {
static std::string lower(std::string s) {for(char &c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s;}
static std::string trim(std::string s) {size_t a=s.find_first_not_of(" \t"),b=s.find_last_not_of(" \t");return a==s.npos?"":s.substr(a,b-a+1);}
void WebServer::respond(Client &c,WebReply r) {
    const char *reason=r.status==200?"OK":r.status==400?"Bad Request":r.status==403?"Forbidden":r.status==404?"Not Found":r.status==409?"Conflict":r.status==413?"Content Too Large":"Error";
    char head[1024];snprintf(head,sizeof(head),"HTTP/1.1 %u %s\r\nContent-Type: %s; charset=utf-8\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'\r\n\r\n",r.status,reason,r.type,r.body.size());c.output=head;c.output+=r.body;
    uv_read_stop(reinterpret_cast<uv_stream_t *>(&c.socket));c.write.data=&c;
    uv_buf_t buffer=uv_buf_init(c.output.data(),static_cast<unsigned>(c.output.size()));
    if(uv_write(&c.write,reinterpret_cast<uv_stream_t *>(&c.socket),&buffer,1,[](uv_write_t *write,int){close_client(*static_cast<Client *>(write->data));}))close_client(c);
}
bool WebServer::start(WebHandler callback,const char *route_host) {
    handler=callback;
    auto failed=[&](const char *stage){error_=std::string(stage)+" errno="+std::to_string(errno);return false;};
    // UDP routing discovers the console's LAN address without transmitting a packet.
    int probe=socket(AF_INET,SOCK_DGRAM,0);if(probe<0)return failed("UDP socket");
    sockaddr_in route{};route.sin_family=AF_INET;route.sin_port=htons(9);
    if(inet_pton(AF_INET,route_host,&route.sin_addr)!=1)inet_pton(AF_INET,"8.8.8.8",&route.sin_addr);
    socklen_t size=sizeof(route);char ip[INET_ADDRSTRLEN]={};
    if(connect(probe,reinterpret_cast<sockaddr *>(&route),sizeof(route))){int saved=errno;close(probe);errno=saved;return failed("UDP route");}
    if(getsockname(probe,reinterpret_cast<sockaddr *>(&route),&size)){int saved=errno;close(probe);errno=saved;return failed("UDP local address");}
    close(probe);if(!inet_ntop(AF_INET,&route.sin_addr,ip,sizeof(ip)))return failed("IPv4 format");
    int rc=uv_tcp_init(uv_default_loop(),&listener);if(rc)return failed(uv_strerror(rc));
    listener.data=this;sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=route.sin_addr.s_addr;address.sin_port=0;
    rc=uv_tcp_bind(&listener,reinterpret_cast<sockaddr *>(&address),0);
    if(!rc)rc=uv_listen(reinterpret_cast<uv_stream_t *>(&listener),4,[](uv_stream_t *stream,int status){if(status==0)static_cast<WebServer *>(stream->data)->accept_client();});
    int length=sizeof(address);if(!rc)rc=uv_tcp_getsockname(&listener,reinterpret_cast<sockaddr *>(&address),&length);
    if(rc){uv_close(reinterpret_cast<uv_handle_t *>(&listener),nullptr);return failed(uv_strerror(rc));}
    listening=true;authority=std::string(ip)+":"+std::to_string(ntohs(address.sin_port));url_="http://"+authority+"/";return true;
}
void WebServer::close_client(Client &c) {
    if(!uv_is_closing(reinterpret_cast<uv_handle_t *>(&c.socket)))uv_close(reinterpret_cast<uv_handle_t *>(&c.socket),[](uv_handle_t *handle){
        auto &client=*static_cast<Client *>(handle->data);client.active=false;client.input.clear();client.output.clear();
    });
}
void WebServer::accept_client() {
    Client *slot=nullptr;for(auto &c:clients)if(!c.active){slot=&c;break;}
    if(!slot){
        // Drain excess connections without admitting another request or body buffer.
        auto *rejected=new(std::nothrow) uv_tcp_t;if(!rejected)return;
        if(uv_tcp_init(uv_default_loop(),rejected)){delete rejected;return;}
        uv_accept(reinterpret_cast<uv_stream_t *>(&listener),reinterpret_cast<uv_stream_t *>(rejected));
        uv_close(reinterpret_cast<uv_handle_t *>(rejected),[](uv_handle_t *handle){delete reinterpret_cast<uv_tcp_t *>(handle);});return;
    }
    auto &c=*slot;if(uv_tcp_init(uv_default_loop(),&c.socket))return;
    c.active=true;c.owner=this;c.socket.data=&c;c.expires=clock+10000000;
    if(uv_accept(reinterpret_cast<uv_stream_t *>(&listener),reinterpret_cast<uv_stream_t *>(&c.socket))){close_client(c);return;}
    int rc=uv_read_start(reinterpret_cast<uv_stream_t *>(&c.socket),[](uv_handle_t *handle,size_t,uv_buf_t *buffer){
        auto &client=*static_cast<Client *>(handle->data);*buffer=uv_buf_init(client.bytes,sizeof(client.bytes));
    },[](uv_stream_t *stream,ssize_t n,const uv_buf_t *buffer){
        auto &client=*static_cast<Client *>(stream->data);
        if(n>0){client.input.append(buffer->base,n);client.owner->request(client);}else if(n<0)close_client(client);
    });
    if(rc)close_client(c);
}
void WebServer::request(Client &c) {
    size_t end=c.input.find("\r\n\r\n");if(end==std::string::npos){if(c.input.size()>4096)respond(c,{413,"{\"error\":\"Headers too large\"}"});return;}
    if(end>4096){respond(c,{413,"{\"error\":\"Headers too large\"}"});return;}
    auto bad=[&](unsigned status,const char *message){respond(c,{status,std::string("{\"error\":\"")+message+"\"}"});};
    size_t line=c.input.find("\r\n");std::string first=c.input.substr(0,line);size_t a=first.find(' '),b=first.rfind(' ');
    if(a==first.npos||b==a||first.substr(b+1)!="HTTP/1.1"){bad(400,"Invalid request line");return;}
    std::string method=first.substr(0,a),path=first.substr(a+1,b-a-1);std::map<std::string,std::string> headers;
    for(size_t pos=line+2;pos<end;){size_t next=c.input.find("\r\n",pos),colon=c.input.find(':',pos);
        if(colon==c.input.npos||colon>=next){bad(400,"Invalid header");return;}
        std::string key=lower(c.input.substr(pos,colon-pos));if(headers.count(key)){bad(400,"Duplicate header");return;}
        headers[key]=trim(c.input.substr(colon+1,next-colon-1));pos=next+2;
    }
    // Host and Origin checks block DNS rebinding; a custom header blocks cross-origin forms.
    if(headers["host"]!=authority||(headers.count("origin")&&headers["origin"]!="http://"+authority)){bad(403,"Use the address shown on the console");return;}
    if(headers.count("transfer-encoding")){bad(400,"Chunked requests are unsupported");return;}
    size_t length=0;
    if(headers.count("content-length")){const auto &s=headers["content-length"];if(s.empty()||s.size()>5){bad(413,"Config is too large");return;}for(char digit:s){if(digit<'0'||digit>'9'){bad(400,"Invalid content length");return;}length=length*10+digit-'0';}}
    if(length>16383){bad(413,"Config must be under 16 KiB");return;}
    if(method=="POST"&&(headers["x-xmrig-control"]!="1"||headers["content-type"].find("application/json")!=0)){bad(403,"JSON control header required");return;}
    if(c.input.size()<end+4+length)return;
    if(c.input.size()!=end+4+length){bad(400,"Unexpected request data");return;}
    respond(c,handler(method,path,c.input.substr(end+4,length)));
}
void WebServer::poll(uint64_t now) {
    clock=now;
    // UV_RUN_NOWAIT is safe while mining is idle as well as after controller startup.
    uv_run(uv_default_loop(),UV_RUN_NOWAIT);
    if(listening)for(auto &c:clients)if(c.active&&now>=c.expires)close_client(c);
}
}
