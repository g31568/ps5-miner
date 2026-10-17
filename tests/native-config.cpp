#include "native-config.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <iostream>

// Exercise malformed browser edits before they can reach the native miner parser.
int main(){
    std::ifstream file("build/native-app/PPSA99531/assets/config.json");std::string bytes((std::istreambuf_iterator<char>(file)),{});
    rapidjson::Document base;base.Parse(bytes.c_str());assert(!base.HasParseError());
    rapidjson::Document doc;native::Options options;
    auto reset=[&](){doc.CopyFrom(base,doc.GetAllocator());};
    auto validate=[&](){return native::validate_config(doc,base,options);};
    reset();assert(validate().empty());assert(options.threads==8);
    doc["pools"][0]["user"].SetString("browser-test");assert(validate().empty());assert(!strcmp(options.user,"browser-test"));
    doc["pools"][0]["user"].SetString("");assert(!validate().empty());
    reset();doc["cpu"]["rx"].PushBack(-1,doc.GetAllocator());assert(validate().empty()&&options.threads==9);
    for(unsigned i=9;i<17;i++)doc["cpu"]["rx"].PushBack(-1,doc.GetAllocator());assert(!validate().empty());
    reset();doc["cpu"]["rx"][0].SetInt(0);assert(!validate().empty());
    reset();for(unsigned i=8;i<14;i++)doc["cpu"]["rx"].PushBack(-1,doc.GetAllocator());doc["cpu"]["yield"].SetBool(false);assert(!validate().empty());
    reset();doc["randomx"]["mode"].SetString("light");assert(validate().find("/randomx/mode")!=std::string::npos);
    reset();doc["http"]["port"].SetInt(-1);assert(!validate().empty());
    reset();doc["donate-level"].SetUint(100);assert(!validate().empty());
    reset();doc["pools"][0]["pass"].SetString("changed-password");assert(validate().empty());
    // A browser rig-id must populate the same Options value used by the controller.
    reset();doc["pools"][0].AddMember("rig-id","web-rig",doc.GetAllocator());assert(validate().empty()&&!strcmp(options.rig_id,"web-rig"));
    doc["pools"][0]["rig-id"].SetString("bad\nrig");assert(!validate().empty());
    doc["pools"][0]["rig-id"].SetNull();assert(validate().empty()&&!*options.rig_id);
    // Proxy configuration accepts explicit remote endpoints and can be cleared.
    reset();doc["pools"][0].RemoveMember("socks5");doc["pools"][0].AddMember("socks5","10.1.1.111:8964",doc.GetAllocator());assert(validate().empty()&&!strcmp(options.proxy,"10.1.1.111:8964"));
    for(const char *endpoint:{"host:0","host:65536","http://host:8964","host :8964",""}){doc["pools"][0]["socks5"].SetString(endpoint,doc.GetAllocator());assert(!validate().empty());}
    doc["pools"][0]["socks5"].SetInt(8964);assert(!validate().empty());
    doc["pools"][0]["socks5"].SetNull();assert(validate().empty()&&!*options.proxy);
    reset();doc.AddMember("unknown",true,doc.GetAllocator());assert(!validate().empty());
    reset();doc.RemoveMember("randomx");assert(!validate().empty());
    doc.Parse("{\"a\":1,\"a\":2}");assert(!native::unique_json(doc));
    doc.Parse("{\"a\":\"a\\u0000b\"}");assert(!native::unique_json(doc));
    std::cout<<"Native JSON validation PASS\n";
}
