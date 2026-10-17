#pragma once
#include "native-options.hpp"
#include "rapidjson/document.h"
#include <string>

namespace native {
// Native-only settings stay fixed; editable JSON retains every other saved field.
inline bool editable_path(const std::string &p) {
    return p=="/cpu/rx"||p=="/cpu/yield"||p=="/pools/0/url"||p=="/pools/0/user"||p=="/pools/0/pass"||p=="/pools/0/rig-id"||p=="/pools/0/keepalive"||p=="/pools/0/socks5"||p=="/http/enabled"||p=="/http/port"||p=="/donate-level"||p=="/print-time";
}
inline bool unique_json(const rapidjson::Value &v) {
    if(v.IsString())return strlen(v.GetString())==v.GetStringLength();
    if(v.IsArray()){for(auto &x:v.GetArray())if(!unique_json(x))return false;}
    if(v.IsObject())for(auto i=v.MemberBegin();i!=v.MemberEnd();++i){
        if(!unique_json(i->name)||!unique_json(i->value))return false;
        for(auto j=v.MemberBegin();j!=i;++j)if(i->name==j->name)return false;
    }
    return true;
}
inline bool fixed_fields_equal(const rapidjson::Value &a,const rapidjson::Value &b,const std::string &path,std::string &error) {
    if(editable_path(path))return true;
    if(a.IsObject()&&b.IsObject()){
        for(auto i=a.MemberBegin();i!=a.MemberEnd();++i){std::string p=path+"/"+i->name.GetString();auto j=b.FindMember(i->name);if(editable_path(p))continue;if(j==b.MemberEnd()||!fixed_fields_equal(i->value,j->value,p,error)){if(error.empty())error=p;return false;}}
        for(auto i=b.MemberBegin();i!=b.MemberEnd();++i)if(!a.HasMember(i->name)&&!editable_path(path+"/"+i->name.GetString())){error=path+"/"+i->name.GetString();return false;}
        return true;
    }
    if(a.IsArray()&&b.IsArray()&&a.Size()==b.Size()){for(unsigned i=0;i<a.Size();i++)if(!fixed_fields_equal(a[i],b[i],path+"/"+std::to_string(i),error))return false;return true;}
    if(a==b)return true;error=path;return false;
}
inline std::string validate_config(const rapidjson::Document &doc,const rapidjson::Document &previous,Options &result) {
    if(!doc.IsObject()||!unique_json(doc))return "Use a JSON object without duplicate keys or NUL characters";
    std::string locked;if(!fixed_fields_equal(previous,doc,"",locked))return "Unsupported native setting: "+locked;
    if(!doc.HasMember("cpu")||!doc["cpu"].IsObject()||!doc["cpu"].HasMember("rx")||!doc["cpu"]["rx"].IsArray())return "cpu.rx must contain 1-16 entries of -1";
    const auto &cpu=doc["cpu"];const auto &threads=cpu["rx"];
    if(threads.Empty()||threads.Size()>16)return "cpu.rx must contain 1-16 entries of -1";
    for(auto &v:threads.GetArray())if(!v.IsInt()||v.GetInt()!=-1)return "cpu.rx entries must be -1 (automatic affinity)";
    result.threads=threads.Size();
    if(!cpu.HasMember("yield")||!cpu["yield"].IsBool()||(!cpu["yield"].GetBool()&&result.threads>12))return "cpu.yield must be true above 12 threads";
    if(!doc.HasMember("pools")||!doc["pools"].IsArray()||doc["pools"].Size()!=1||!doc["pools"][0].IsObject())return "Keep one pool object";
    const auto &pool=doc["pools"][0];
    for(const char *key:{"url","user"}){
        if(!pool.HasMember(key)||!pool[key].IsString())return std::string("pools[0].")+key+" must be text";
        if(auto error=edit(result,!strcmp(key,"url")?1:2,pool[key].GetString()))return error;
    }
    for(const char *key:{"pass","rig-id"})if(pool.HasMember(key)&&!pool[key].IsNull()&&(!pool[key].IsString()||pool[key].GetStringLength()>191))return std::string(key)+" must be text of at most 191 bytes or null";
    // Share rig-id validation with controller edits so both interfaces stay consistent.
    result.rig_id[0]=0;
    if(pool.HasMember("rig-id")&&pool["rig-id"].IsString())if(auto error=edit(result,5,pool["rig-id"].GetString()))return error;
    // SOCKS5 uses ordinary native TCP; null disables the optional pool proxy.
    result.proxy[0]=0;
    if(pool.HasMember("socks5")&&!pool["socks5"].IsNull()){
        if(!pool["socks5"].IsString()||edit(result,6,pool["socks5"].GetString())||!*result.proxy)return "pools[0].socks5 must be host:port or null";
    }
    if(pool.HasMember("keepalive")&&!pool["keepalive"].IsBool()&&(!pool["keepalive"].IsUint()||pool["keepalive"].GetUint()>3600))return "keepalive must be boolean or seconds (0-3600)";
    if(!doc.HasMember("http")||!doc["http"].IsObject())return "Keep the http object";
    const auto &http=doc["http"];
    if(!http.HasMember("enabled")||!http["enabled"].IsBool()||!http.HasMember("port")||!http["port"].IsUint()||http["port"].GetUint()>65535)return "HTTP needs enabled (boolean) and port (0-65535)";
    result.http=http["enabled"].GetBool();result.port=http["port"].GetUint();
    if(!doc.HasMember("donate-level")||!doc["donate-level"].IsUint()||doc["donate-level"].GetUint()>99)return "donate-level must be 0-99";
    result.donation=doc["donate-level"].GetUint();
    if(doc.HasMember("print-time")&&(!doc["print-time"].IsUint()||doc["print-time"].GetUint()<1||doc["print-time"].GetUint()>3600))return "print-time must be 1-3600";
    return "";
}
}
