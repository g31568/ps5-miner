// GPU controller edits must validate input and preserve advanced transport settings.
#include "native-gpu-options.hpp"
#include <cassert>
#include <iostream>
int main(){
    rapidjson::Document doc;doc.Parse("{\"url\":\"pool.example\",\"port\":10128,\"login\":\"wallet\",\"pass\":\"worker\",\"is_tls\":true,\"tls_verify\":true,\"max_graphs\":0,\"ca_file\":\"/app0/assets/cacert.pem\"}");
    assert(!native::gpu_edit(doc,0,"new.pool.example"));assert(!strcmp(doc["url"].GetString(),"new.pool.example"));
    for(auto text:{"", "host name", "http://host", "host/path"})assert(native::gpu_edit(doc,0,text));
    for(auto text:{"0","65536","-1","8x"})assert(native::gpu_edit(doc,1,text));
    assert(!native::gpu_edit(doc,1,"443")&&doc["port"].GetUint()==443);
    assert(!native::gpu_edit(doc,5,"0"));assert(native::gpu_edit(doc,2,""));
    char user[513];memset(user,'a',512);user[512]=0;assert(native::gpu_edit(doc,2,user));user[511]=0;assert(!native::gpu_edit(doc,2,user));
    assert(!native::gpu_edit(doc,3,""));assert(!native::gpu_edit(doc,3,"worker"));
    char shown[1024];native::gpu_field(doc,3,shown,sizeof(shown),true);assert(!strcmp(shown,"Hidden - X to edit"));
    native::gpu_field(doc,3,shown,sizeof(shown));assert(!strcmp(shown,"worker"));
    assert(!native::gpu_edit(doc,6,"Off")&&!doc["is_tls"].GetBool());assert(native::gpu_edit(doc,7,"maybe"));
    assert(doc["tls_verify"].GetBool()&&doc["max_graphs"].GetUint()==0&&!strcmp(doc["ca_file"].GetString(),"/app0/assets/cacert.pem"));
    // Omitted fields must describe the effective transport policy, not a UI default.
    doc.RemoveMember("is_tls");doc.RemoveMember("tls_verify");
    native::gpu_field(doc,6,shown,sizeof(shown));assert(!strcmp(shown,"On"));
    native::gpu_field(doc,7,shown,sizeof(shown));assert(!strcmp(shown,"Off"));
    std::cout<<"GPU field validation, masking and advanced-setting preservation PASS\n";
}
