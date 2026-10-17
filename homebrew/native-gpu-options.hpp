// Controller edits retain the transport document, including unexposed advanced keys.
#pragma once
#include "native-options.hpp"
#include "rapidjson/document.h"
namespace native {
inline constexpr unsigned gpu_option_count=8;
inline const char *gpu_key(unsigned row){static const char *keys[]={"url","port","login","pass","proxy_host","proxy_port","is_tls","tls_verify"};return row<gpu_option_count?keys[row]:"";}
inline const char *gpu_field_name(unsigned row){static const char *names[]={"Pool host","Port","User / wallet","Worker / password","Proxy host","Proxy port","TLS","Verify certificate"};return row<gpu_option_count?names[row]:"";}
inline void gpu_field(const rapidjson::Document &doc,unsigned row,char *value,size_t size,bool display=false){
    const char *key=gpu_key(row);auto it=doc.FindMember(key);
    // Match the transport's defaults for omitted TLS fields exactly.
    if(it==doc.MemberEnd())snprintf(value,size,"%s",row==6?"On":row==7?"Off":row==1||row==5?"0":"");
    else if(it->value.IsString())snprintf(value,size,"%s",display&&row==3&&it->value.GetStringLength()?"Hidden - X to edit":it->value.GetString());
    else if(it->value.IsBool())snprintf(value,size,"%s",it->value.GetBool()?"On":"Off");
    else if(it->value.IsUint())snprintf(value,size,"%u",it->value.GetUint());
    else snprintf(value,size,"Invalid value");
}
inline const char *gpu_edit(rapidjson::Document &doc,unsigned row,const char *text){
    if(row>=gpu_option_count)return "Unknown GPU field";
    auto &a=doc.GetAllocator();rapidjson::Value value;
    if(row==1||row==5){unsigned n;if(!number(text,65535,n)||(row==1&&!n))return "Use a port from 1 to 65535 (proxy: 0 disables)";value.SetUint(n);}
    else if(row>=6){
        if(!strcmp(text,"On")||!strcmp(text,"on"))value.SetBool(true);
        else if(!strcmp(text,"Off")||!strcmp(text,"off"))value.SetBool(false);
        else return "Use On or Off";
    }else{
        size_t limit=row==2?511:row==3?255:253;
        if(strlen(text)>limit||((row==0||row==2)&&!*text))return "GPU field is empty or too long";
        for(const unsigned char *p=reinterpret_cast<const unsigned char *>(text);*p;p++)
            if(*p<32||((row==0||row==4)&&(*p<=32||*p=='/'||*p=='\\')))return "Invalid characters in GPU field";
        value.SetString(text,a);
    }
    const char *key=gpu_key(row);auto it=doc.FindMember(key);
    if(it==doc.MemberEnd())doc.AddMember(rapidjson::Value(key,a),value,a);else it->value=std::move(value);
    return nullptr;
}
}
