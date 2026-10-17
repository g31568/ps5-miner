#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <uv.h>

namespace native {
// The bounded LAN editor shares the miner's proven native libuv event loop.
struct WebReply { unsigned status=200; std::string body; const char *type="application/json"; };
using WebHandler=WebReply (*)(const std::string &,const std::string &,const std::string &);
class WebServer {
    struct Client {
        uv_tcp_t socket{};uv_write_t write{};WebServer *owner=nullptr;
        bool active=false;uint64_t expires=0;std::string input,output;char bytes[4096];
    };
    uv_tcp_t listener{};
    bool listening=false;
    uint64_t clock=0;
    std::array<Client,4> clients;
    std::string authority,url_,error_;
    WebHandler handler=nullptr;
    static void close_client(Client &);
    void accept_client();
    void respond(Client &,WebReply);
    void request(Client &);
public:
    bool start(WebHandler,const char *route_host="8.8.8.8");
    void poll(uint64_t microseconds);
    const std::string &url() const { return url_; }
    const std::string &error() const { return error_; }
};
}
