/* Native XMRig screen; miner and controls share this app's process and event loop.
 * Renderer/controller ABI adapted from ps5-native-app-boilerplate (GPLv3+).
 */
#include "demo_renderer.hpp"
#include "native-options.hpp"
#include "native-keyboard.hpp"
#include "native-logo.hpp"
#include "native-dashboard.hpp"
#include "native-web.hpp"
#include "native-gpu.hpp"
#include "native-gpu-options.hpp"
#include "../ps5/pool.h"
#include "native-config.hpp"
#include "vendor/qrcodegen/qrcodegen.h"
#include "crypto/randomx/randomx.h"
#include "crypto/rx/RxAlgo.h"
#include "base/net/stratum/Job.h"
#include "net/Network.h"
#include "base/kernel/interfaces/IStrategy.h"
#include "core/Controller.h"
#include "core/Miner.h"
#include "base/kernel/Process.h"
#include "base/io/log/Log.h"
#include "backend/common/Hashrate.h"
#include "backend/common/interfaces/IBackend.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "rapidjson/prettywriter.h"
#include <uv.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdlib>
#include <sys/socket.h>
#include <arpa/inet.h>

struct PadSample { uint32_t buttons; uint8_t axes[6]; uint8_t reserved0[66]; int32_t connected; uint64_t timestamp; uint8_t reserved1[32]; };
static_assert(sizeof(PadSample)==120);
// ScePad takes RGB bytes followed by a zero reserved byte.
struct PadLightBar { uint8_t red,green,blue,reserved; };
static_assert(sizeof(PadLightBar)==4);
extern "C" {
int sceUserServiceInitialize(const void *);
int sceUserServiceGetInitialUser(int *);
int scePadInit();
int scePadOpen(int,int,int,const void *);
int scePadRead(int,PadSample *,int);
int scePadSetLightBar(int,const PadLightBar *);
int sceSystemServiceLoadExec(const char *,const char **);
uint64_t sceKernelGetProcessTime();
int sceKernelUsleep(uint32_t);
int sceKernelDebugOutText(int, const char *);
// Renderer ownership check avoids pulling a second C++ allocation runtime.
bool ps5ObserveOwnedAllocation(const void *p) noexcept { return p!=nullptr; }
}
using ps5::demo::Canvas;
using ps5::demo::Color;
static std::unique_ptr<xmrig::Process> process;
static std::unique_ptr<xmrig::Controller> controller;
// Confirmed options drive mining; pending edits never alter the running profile.
static native::Options options,pending;
static bool editing=false;
static unsigned selected_row=0;
// Each engine owns its own draft; changing tabs never silently discards edits.
static bool gpu_settings=false;
static rapidjson::Document gpu_pending;
static std::string gpu_revision;
static std::string revision(const rapidjson::Document &doc);
static int initial_user=0;
static native::HourAverage hour_average;
static unsigned current_rate=0;
static uint64_t next_rate_refresh=0,notice_until=0;
static int pad=-1;
static FILE *diagnostic=nullptr;
static bool first_frame=true;
static uint32_t held=0;
static uint64_t next_test_check=0;
extern "C" void *xmrigNativeAllocate(size_t,size_t);
extern "C" void xmrigNativeFree(void *);
static char message[128]="";
static unsigned short http_port=0;
static native::WebServer web;
static uint8_t web_qr[qrcodegen_BUFFER_LEN_FOR_VERSION(4)]{};
static bool web_qr_ready=false;
// Hidden tuning probes leave the approved controller layout unchanged.
static unsigned perf_prefetch=1;
static bool perf_yield=true;

static const char *config_path="/download0/xmrig-native.json";

/* Keep upstream defaults, including rig-id, and persist only confirmed options. */
static bool read_document(const char *path,rapidjson::Document &doc) {
    FILE *f=fopen(path,"rb");if(!f)return false;
    char data[16384];size_t n=fread(data,1,sizeof(data)-1,f);bool ok=!ferror(f)&&feof(f);fclose(f);data[n]=0;
    if(!ok)return false;doc.Parse(data);return !doc.HasParseError()&&doc.IsObject();
}
static bool write_document(const rapidjson::Document &doc) {
    rapidjson::StringBuffer out;rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(out);doc.Accept(writer);
    if(out.GetSize()>=16383)return false;
    // Atomic replacement leaves the previous settings intact if writing fails.
    const char *temporary="/download0/xmrig-native.json.tmp";
    FILE *f=fopen(temporary,"wb");if(!f)return false;
    bool ok=fwrite(out.GetString(),1,out.GetSize(),f)==out.GetSize();
    if(fflush(f)||fsync(fileno(f)))ok=false;if(fclose(f))ok=false;
    if(ok)ok=rename(temporary,config_path)==0;if(!ok)unlink(temporary);return ok;
}
static bool prepare_config() {
    rapidjson::Document doc;if(!read_document(config_path,doc)&&!read_document("/app0/assets/config.json",doc))return false;
    auto &a=doc.GetAllocator();
    doc["cpu"]["priority"].SetInt(0);
    // Preserve event-loop scheduling when workers saturate the app CPU budget.
    doc["cpu"].RemoveMember("yield");doc["cpu"].AddMember("yield",perf_yield||options.threads>12,a);
    doc["randomx"].RemoveMember("scratchpad_prefetch_mode");doc["randomx"].AddMember("scratchpad_prefetch_mode",perf_prefetch,a);
    doc["cpu"]["rx"].SetArray();for(unsigned i=0;i<options.threads;i++)doc["cpu"]["rx"].PushBack(-1,a);
    doc["pools"][0]["url"].SetString(options.address,a);
    doc["pools"][0]["user"].SetString(options.user,a);
    // Keep an absent default absent; a cleared explicit identifier becomes null.
    auto &pool=doc["pools"][0];
    if(*options.rig_id){if(!pool.HasMember("rig-id"))pool.AddMember("rig-id",rapidjson::Value(options.rig_id,a),a);else pool["rig-id"].SetString(options.rig_id,a);}
    else if(pool.HasMember("rig-id"))pool["rig-id"].SetNull();
    // Native proxy edits share the browser's pools[0].socks5 value.
    if(*options.proxy){if(!pool.HasMember("socks5"))pool.AddMember("socks5",rapidjson::Value(options.proxy,a),a);else pool["socks5"].SetString(options.proxy,a);}
    else if(pool.HasMember("socks5"))pool["socks5"].SetNull();
    doc["http"]["enabled"].SetBool(options.http);doc["http"]["port"].SetUint(options.port);
    doc["donate-level"].SetUint(options.donation);
    return write_document(doc);
}
static void load_options() {
    rapidjson::Document doc;if(!read_document(config_path,doc)&&!read_document("/app0/assets/config.json",doc))return;
    native::Options loaded;char value[192];
    if(doc.HasMember("cpu")&&doc["cpu"].IsObject()&&doc["cpu"].HasMember("rx")&&doc["cpu"]["rx"].IsArray()){
        snprintf(value,sizeof(value),"%u",doc["cpu"]["rx"].Size());native::edit(loaded,0,value);
    }
    if(doc.HasMember("pools")&&doc["pools"].IsArray()&&!doc["pools"].Empty()&&doc["pools"][0].IsObject()){
        auto &pool=doc["pools"][0];
        if(pool.HasMember("url")&&pool["url"].IsString())native::edit(loaded,1,pool["url"].GetString());
        if(pool.HasMember("user")&&pool["user"].IsString())native::edit(loaded,2,pool["user"].GetString());
        if(pool.HasMember("rig-id")&&pool["rig-id"].IsString())native::edit(loaded,5,pool["rig-id"].GetString());
        if(pool.HasMember("socks5")&&pool["socks5"].IsString())native::edit(loaded,6,pool["socks5"].GetString());
    }
    if(doc.HasMember("http")&&doc["http"].IsObject()){
        auto &http=doc["http"];
        if(http.HasMember("port")&&http["port"].IsUint()&&http["port"].GetUint()<=65535)loaded.port=http["port"].GetUint();
        if(http.HasMember("enabled")&&http["enabled"].IsBool())loaded.http=http["enabled"].GetBool();
    }
    if(doc.HasMember("donate-level")&&doc["donate-level"].IsUint()&&doc["donate-level"].GetUint()<=99)loaded.donation=doc["donate-level"].GetUint();
    if(doc.HasMember("cpu")&&doc["cpu"].IsObject()&&doc["cpu"].HasMember("yield")&&doc["cpu"]["yield"].IsBool())perf_yield=doc["cpu"]["yield"].GetBool();
    if(doc.HasMember("randomx")&&doc["randomx"].IsObject()&&doc["randomx"].HasMember("scratchpad_prefetch_mode")&&doc["randomx"]["scratchpad_prefetch_mode"].IsUint()&&doc["randomx"]["scratchpad_prefetch_mode"].GetUint()<4)perf_prefetch=doc["randomx"]["scratchpad_prefetch_mode"].GetUint();
    options=loaded;pending=options;
}
static uint32_t pressed() {
    if(pad<0)return 0;std::array<PadSample,16> samples{};
    int count=scePadRead(pad,samples.data(),samples.size());uint32_t result=0;
    for(int i=0;i<count && i<(int)samples.size();i++){result|=samples[i].buttons&~held;held=samples[i].buttons;}
    return result;
}
/* Bounded stage logs make native startup observable without launching mining. */
static void record(const char *stage) {
    char system_line[256];snprintf(system_line,sizeof(system_line),"[XMRig Native] %s\n",stage);sceKernelDebugOutText(0,system_line);
    if(diagnostic){fprintf(diagnostic,"%llu %s\n",(unsigned long long)sceKernelGetProcessTime(),stage);fflush(diagnostic);}
}
extern "C" void xmrigNativeDiagnostic(const char *stage) { record(stage); }
/* Publish the assigned native port without promising a prohibited fixed bind. */
extern "C" void xmrigNativeHttpPort(unsigned short port) {
    // Reuse the detected interface address from the local web editor.
    std::string host=web.url();auto end=host.rfind(':');if(end!=std::string::npos)host.resize(end);
    http_port=port;char line[160];snprintf(line,sizeof(line),"HTTP status %s:%u/2/summary",host.c_str(),port);record(line);
}
/* Check the exact libuv pipe operations before its abort-only initializer. */
static bool pipe_ready(int flags=O_CLOEXEC) {
    int fd[2]={-1,-1};errno=0;int result=pipe2(fd,flags);int error=errno;
    char line[160];snprintf(line,sizeof(line),"pipe2 result=%d errno=%d fd=%d,%d",result,error,fd[0],fd[1]);record(line);
    if(result)return false;
    char value=42;errno=0;ssize_t written=write(fd[1],&value,1);error=errno;
    snprintf(line,sizeof(line),"pipe write result=%ld errno=%d",(long)written,error);record(line);
    if(written==1){char read_value=0;ssize_t received=read(fd[0],&read_value,1);result=received==1&&read_value==42?0:-1;}
    else result=-1;
    close(fd[0]);close(fd[1]);return result==0;
}
static void notice(const char *reason) {
    snprintf(message,sizeof(message),"%s",reason);notice_until=sceKernelGetProcessTime()+5000000;record(reason);
}
static bool mining() {return controller&&controller->miner()->isEnabled();}
// CPU lifecycle never changes the independent C29 worker.
static void pause_cpu(const char *reason) {
    if(controller)controller->miner()->setEnabled(false);
    current_rate=0;next_rate_refresh=0;notice(reason);
}
static bool start_cpu() {
    record("CPU START requested");
    if(!controller){
        // Set prefetch before the first job generates RandomX code after relaunch.
        randomx_set_scratchpad_prefetch_mode(perf_prefetch);
        if(!pipe_ready()||!prepare_config()){notice("Miner setup failed - see log");return false;}
        static char program[]="xmrig-native",option[]="--config";static char *args[]={program,option,const_cast<char *>(config_path),nullptr};
        process.reset(new xmrig::Process(3,args));controller.reset(new xmrig::Controller(process.get()));
        if(!controller->isReady()||controller->init()!=0){controller.reset();notice("Miner initialization failed");return false;}
        controller->start();record("Controller started");
    }
    controller->miner()->setEnabled(true);next_rate_refresh=0;
    notice("XMRig started");return true;
}
static void pause_miner(const char *reason) {
    native::gpu_stop();pause_cpu(reason);
}
static void start_miner() {
    // Shared start attempts both engines even if one cannot initialize.
    bool cpu=start_cpu();
    // GPU failure leaves CPU mining available and is visible in both interfaces.
    bool gpu=native::gpu_start();notice(cpu?(gpu?"CPU + GPU mining started":"CPU started - GPU start rejected"):(gpu?"GPU started - CPU start failed":"Both starts failed - see log"));
}
static void confirm_options() {
    native::Options previous=options;options=pending;
    if(!prepare_config()){options=previous;notice("Options could not be saved");return;}
    bool resume=mining();
    if(controller){
        // Reload joins replaced workers while retaining the shared cache and controller.
        controller->miner()->setEnabled(false);rapidjson::Document doc;
        if(options.http!=previous.http||options.port!=previous.port)http_port=0;
        if(!read_document(config_path,doc)||!controller->reload(doc)){
            options=previous;prepare_config();controller->miner()->setEnabled(resume);notice("Options reload failed");return;
        }
        controller->miner()->setEnabled(resume);
    }
    editing=false;current_rate=0;next_rate_refresh=0;notice("Options confirmed");
}
// Validate and atomically save through the same transport parser used at GPU start.
static const char *save_gpu_document(const rapidjson::Document &doc){
    auto status=native::gpu_status();
    if(status.running||status.stopping)return "Stop GPU with R1 before saving; CPU continues";
    rapidjson::StringBuffer out;rapidjson::Writer<rapidjson::StringBuffer> writer(out);doc.Accept(writer);
    if(out.GetSize()>4096)return "GPU configuration is too large";
    const char *tmp="/download0/c29-pool.json.tmp";FILE *f=fopen(tmp,"wb");
    if(!f)return "GPU configuration could not be saved";
    bool ok=fwrite(out.GetString(),1,out.GetSize(),f)==out.GetSize();
    if(fflush(f)||fsync(fileno(f)))ok=false;if(fclose(f))ok=false;
    MomPoolConfig checked{};ok=ok&&mom_pool_config_read(tmp,&checked);
    if(ok)ok=rename(tmp,"/download0/c29-pool.json")==0;
    if(!ok){unlink(tmp);return "Invalid GPU settings: check host, user and proxy host/port";}
    return nullptr;
}
static bool load_gpu_draft(){
    rapidjson::Document doc;
    if(!read_document("/download0/c29-pool.json",doc)&&!read_document("/app0/assets/gpu-config.json",doc)){
        notice("GPU configuration unavailable");return false;
    }
    gpu_pending.CopyFrom(doc,gpu_pending.GetAllocator());gpu_revision=revision(doc);return true;
}
static bool cpu_draft_changed(){
    for(unsigned row=0;row<native::option_count;row++){
        char before[192],after[192];native::field(options,row,before,sizeof(before));native::field(pending,row,after,sizeof(after));
        if(strcmp(before,after))return true;
    }
    return false;
}
static void confirm_gpu_options(){
    rapidjson::Document current;
    if(!read_document("/download0/c29-pool.json",current)&&!read_document("/app0/assets/gpu-config.json",current)){notice("GPU configuration unavailable");return;}
    if(revision(current)!=gpu_revision){notice("GPU settings changed externally; cancel and reopen");return;}
    if(const char *error=save_gpu_document(gpu_pending)){notice(error);return;}
    editing=false;notice("GPU settings saved; start GPU with R1 when ready");
}
static void accept_edit(const char *value) {
    const char *error=gpu_settings?native::gpu_edit(gpu_pending,selected_row,value):native::edit(pending,selected_row,value);
    if(error)notice(error);else notice("Change pending - Options to confirm");
}
static void handle_buttons(uint32_t buttons) {
    if(native::keyboard_active())return;
    if(buttons&0x8){if(editing){if(gpu_settings)confirm_gpu_options();else confirm_options();}else{pending=options;gpu_settings=false;editing=true;selected_row=0;record("Options entered");}return;}
    if(buttons&0x8000){if(mining()||native::gpu_status().running)pause_miner("Mining stopped");else start_miner();return;}
    // Shoulder buttons toggle one engine from either the status or Options screen.
    if(buttons&0x400){if(mining())pause_cpu("XMRig stopped");else start_cpu();return;}
    if(buttons&0x800){auto gpu=native::gpu_status();
        // Match disabled on-screen actions and avoid repeated cancellation requests.
        if(gpu.stopping||gpu.faulted){notice(gpu.faulted?"Relaunch app before starting GPU":"GPU is still stopping");return;}
        if(gpu.running){native::gpu_stop();notice("C29 stopping");}else notice(native::gpu_start()?"C29 started":"C29 start rejected - check web status");return;}
    // Circle discards the pending settings and returns to the dashboard.
    if(editing&&(buttons&0x2000)){editing=false;notice("Changes discarded");return;}
    if(!editing)return;
    // Left/Right selects an engine; dirty drafts must be saved or canceled first.
    if(buttons&(0x20|0x80)){
        bool next=(buttons&0x20)!=0;
        if(next!=gpu_settings){
            bool dirty=gpu_settings?revision(gpu_pending)!=gpu_revision:cpu_draft_changed();
            if(dirty){notice("Save or cancel current edits before switching engines");return;}
            if(next&&!load_gpu_draft())return;
            gpu_settings=next;selected_row=0;
        }
        return;
    }
    if(buttons&0x10)selected_row=gpu_settings?(selected_row+native::gpu_option_count-1)%native::gpu_option_count:native::adjacent_field(selected_row,-1);
    if(buttons&0x40)selected_row=gpu_settings?(selected_row+1)%native::gpu_option_count:native::adjacent_field(selected_row,1);
    if(buttons&0x4000){char value[1024];
        if(gpu_settings)native::gpu_field(gpu_pending,selected_row,value,sizeof(value));else native::field(pending,selected_row,value,sizeof(value));
        if(!native::keyboard_open(initial_user,gpu_settings?7+selected_row:selected_row,value))notice("System keyboard could not open - see log");
    }
}
/* Match the bitmap font with outline controller icons, independent of Unicode support. */
static void button_icon(Canvas &canvas,unsigned x,unsigned y,unsigned scale,unsigned icon,Color color) {
    static constexpr unsigned char rows[5][7]={
        {0x41,0x22,0x14,0x08,0x14,0x22,0x41}, // Cross.
        {0x1c,0x22,0x41,0x41,0x41,0x22,0x1c}, // Circle.
        {0x08,0x14,0x14,0x22,0x22,0x41,0x7f}, // Triangle.
        {0x08,0x10,0x20,0x7f,0x20,0x10,0x08}, // Left arrow.
        {0x08,0x04,0x02,0x7f,0x02,0x04,0x08}  // Right arrow.
    };
    for(unsigned row=0;row<7;row++)for(unsigned col=0;col<7;col++)
        if(rows[icon][row]&(1u<<(6-col)))canvas.rectangle(x+col*scale,y+row*scale,scale,scale,color);
}
static void text(Canvas &canvas,unsigned x,unsigned y,const char *value,unsigned scale,Color color) {
    char upper[160];size_t n=0;for(;value[n] && n<sizeof(upper)-1;n++)upper[n]=value[n]>='a'&&value[n]<='z'?value[n]-32:value[n];upper[n]=0;
    // Explicit icon tokens keep controller prompts consistent in every status state.
    for(size_t i=0;i<n&&x<1920;){
        unsigned icon=5;
        if(i+2<n&&upper[i]=='{'&&upper[i+2]=='}'){
            if(upper[i+1]=='X')icon=0;else if(upper[i+1]=='O')icon=1;else if(upper[i+1]=='T')icon=2;else if(upper[i+1]=='L')icon=3;else if(upper[i+1]=='R')icon=4;
        }
        if(icon<5){button_icon(canvas,x,y,scale,icon,color);x+=8*scale;i+=3;}
        else{canvas.text(x,y,std::string_view(upper+i,1),scale,color);x+=6*scale;i++;}
    }
}
// Colors use the renderer's little-endian RGBA packing.
static constexpr Color background=static_cast<Color>(0xff191310),muted=static_cast<Color>(0xffbeb3b0),green=static_cast<Color>(0xffa4f55a),red=static_cast<Color>(0xff6060f0),pink=static_cast<Color>(0xffbc68ec);
static void outline(Canvas &c,unsigned x,unsigned y,unsigned w,unsigned h,Color color) {
    c.rectangle(x,y,w,3,color);c.rectangle(x,y+h-3,w,3,color);c.rectangle(x,y,3,h,color);c.rectangle(x+w-3,y,3,h,color);
}
static void logo(Canvas &c) {
    for(int y=0;y<110;y++)for(int x=0;x<110;x++)if(native::logo_pixel(x,y,110))c.rectangle(110+x,65+y,1,1,static_cast<Color>(native::monero_rgba));
}
static void debug_command() {
    FILE *f=fopen("/download0/xmrig-native-test.once","rb");if(!f)return;
    char command[256]={};fread(command,1,sizeof(command)-1,f);fclose(f);
    if(unlink("/download0/xmrig-native-test.once"))return;
    if(!strcmp(command,"START\n"))start_miner();
    else if(!strncmp(command,"START ",6)){unsigned count=0;char extra=0;if(sscanf(command+6,"%u %c",&count,&extra)==1&&count>=1&&count<=16){pending=options;pending.threads=count;confirm_options();start_miner();}}
    else if(!strncmp(command,"TUNE ",5)&&!native::keyboard_active()&&!editing){
        unsigned prefetch=0,yield=0;char extra=0;
        if(sscanf(command+5,"%u %u %c",&prefetch,&yield,&extra)==2&&prefetch<4&&yield<2){
            if(!yield&&options.threads>12){notice("Non-yielding tests require at most 12 threads");return;}
            // Join workers before changing shared JIT templates, then rebuild them.
            if(controller)controller->miner()->stop();
            perf_prefetch=prefetch;perf_yield=yield!=0;
            randomx_set_scratchpad_prefetch_mode(perf_prefetch);
            if(controller)xmrig::RxAlgo::apply(controller->miner()->job().algorithm());
            pending=options;confirm_options();
            char line[96];snprintf(line,sizeof(line),"Tuning prefetch=%u yield=%u",perf_prefetch,unsigned(perf_yield));record(line);
        }
    }
    else if(!strcmp(command,"PAUSE\n"))pause_miner("Mining stopped");
    // These sandbox-only probes exercise the same button and validation paths.
    else if(!strncmp(command,"BUTTON ",7)){unsigned buttons=0;if(sscanf(command+7,"%x",&buttons)==1)handle_buttons(buttons);}
    else if(!strncmp(command,"EDIT ",5)&&editing&&!native::keyboard_active()){
        char *end=strchr(command+5,'\n');if(end)*end=0;accept_edit(command+5);
    }
    else if(!strcmp(command,"KEYBOARD_CANCEL\n"))native::keyboard_cancel();
    else if(!strcmp(command,"CAPTURE\n")){FILE *marker=fopen("/download0/xmrig-native-capture.once","wb");if(marker)fclose(marker);}
}
static void save_ui_state(bool connected) {
    // Small structured snapshots expose real UI state for console debugging.
    rapidjson::Document doc;doc.SetObject();auto &a=doc.GetAllocator();
    doc.AddMember("mining",mining(),a);doc.AddMember("connected",connected,a);doc.AddMember("editing",editing,a);
    doc.AddMember("settings_engine",rapidjson::Value(gpu_settings?"gpu":"cpu",a),a);
    doc.AddMember("row",selected_row,a);doc.AddMember("keyboard",native::keyboard_active(),a);
    doc.AddMember("hashrate",current_rate,a);doc.AddMember("refresh_us",1000000,a);
    doc.AddMember("hour_ready",hour_average.ready(),a);doc.AddMember("hour_average",hour_average.value(),a);
    doc.AddMember("prefetch",perf_prefetch,a);doc.AddMember("yield",perf_yield||options.threads>12,a);
    doc.AddMember("threads",options.threads,a);doc.AddMember("pending_threads",pending.threads,a);
    doc.AddMember("http_port",http_port,a);doc.AddMember("http_enabled",options.http,a);
    doc.AddMember("notice",rapidjson::Value(message,a),a);
    doc.AddMember("donation",options.donation,a);doc.AddMember("configured_http_port",options.port,a);
    doc.AddMember("address",rapidjson::Value(options.address,a),a);
    doc.AddMember("user",rapidjson::Value(options.user,a),a);
    doc.AddMember("rig_id",rapidjson::Value(options.rig_id,a),a);
    doc.AddMember("proxy",rapidjson::Value(options.proxy,a),a);
    doc.AddMember("web_url",rapidjson::Value(web.url().c_str(),a),a);
    rapidjson::Document config;
    if(read_document(config_path,config)&&config.HasMember("pools")&&config["pools"].IsArray()&&!config["pools"].Empty())
        doc.AddMember("explicit_rig_id",config["pools"][0].HasMember("rig-id")&&config["pools"][0]["rig-id"].IsString()&&config["pools"][0]["rig-id"].GetStringLength()>0,a);
    rapidjson::StringBuffer buffer;rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);doc.Accept(writer);
    FILE *f=fopen("/download0/xmrig-native-ui.json","wb");if(f){fwrite(buffer.GetString(),1,buffer.GetSize(),f);fclose(f);}
}
// Config revisions compare the actual file, including changes made with a controller.
static std::string revision(const rapidjson::Document &doc) {
    rapidjson::StringBuffer out;rapidjson::Writer<rapidjson::StringBuffer> writer(out);doc.Accept(writer);
    uint64_t hash=14695981039346656037ull;for(size_t i=0;i<out.GetSize();i++){hash^=static_cast<unsigned char>(out.GetString()[i]);hash*=1099511628211ull;}
    char value[24];snprintf(value,sizeof(value),"%016llx",static_cast<unsigned long long>(hash));return value;
}
static std::string json_text(const rapidjson::Document &doc) {
    rapidjson::StringBuffer out;rapidjson::Writer<rapidjson::StringBuffer> writer(out);doc.Accept(writer);return {out.GetString(),out.GetSize()};
}
static native::WebReply web_error(unsigned status,const char *error) {
    rapidjson::Document doc;doc.SetObject();doc.AddMember("error",rapidjson::Value(error,doc.GetAllocator()),doc.GetAllocator());return {status,json_text(doc)};
}
static native::WebReply web_request(const std::string &method,const std::string &path,const std::string &body) {
    // Web CPU controls share the controller lifecycle without touching the GPU.
    if(method=="POST"&&path=="/cpu/start")return start_cpu()?native::WebReply{200,"{}"}:web_error(409,"CPU start failed; check the mining log");
    if(method=="POST"&&path=="/cpu/stop"){pause_cpu("XMRig stopped");return {200,"{}"};}
    if(path=="/gpu/config"){
        const char *gpu_path="/download0/c29-pool.json";
        if(method=="GET"){
            rapidjson::Document doc;
            if(!read_document(gpu_path,doc)&&!read_document("/app0/assets/gpu-config.json",doc))return web_error(500,"GPU configuration unavailable");
            return {200,json_text(doc)};
        }
        if(method=="POST"){
            if(editing&&gpu_settings)return web_error(409,"Save or cancel GPU settings on the controller first");
            rapidjson::Document doc;doc.Parse(body.data(),body.size());
            if(doc.HasParseError()||!doc.IsObject()||!native::unique_json(doc)||body.size()>4096)return web_error(400,"Invalid GPU configuration");
            if(const char *error=save_gpu_document(doc))return web_error(native::gpu_status().running?409:400,error);
            return {200,"{\"saved\":true}"};
        }
    }
    if(method=="POST"&&path=="/gpu/start")return native::gpu_start()?native::WebReply{200,"{}"}:web_error(409,"GPU start rejected; check settings, stopping state or fault");
    if(method=="POST"&&path=="/gpu/stop"){native::gpu_stop();return {200,"{}"};}
    if(method=="GET"){
        const char *asset=nullptr,*type="text/html";
        if(path=="/")asset="/app0/assets/web/index.html";
        else if(path=="/app.js"){asset="/app0/assets/web/app.js";type="text/javascript";}
        else if(path=="/style.css"){asset="/app0/assets/web/style.css";type="text/css";}
        else if(path=="/icon.png"||path=="/favicon.ico"){asset="/app0/assets/web/icon.png";type="image/png";}
        if(asset){FILE *f=fopen(asset,"rb");if(!f)return web_error(500,"Editor asset unavailable");char bytes[16384];size_t n=fread(bytes,1,sizeof(bytes),f);fclose(f);return {200,std::string(bytes,n),type};}
        rapidjson::Document result;result.SetObject();auto &a=result.GetAllocator();
        if(path=="/config"){
            rapidjson::Document doc;if(!read_document(config_path,doc))return web_error(500,"Configuration unavailable");
            result.AddMember("revision",rapidjson::Value(revision(doc).c_str(),a),a);result.AddMember("config",rapidjson::Value(doc,a),a);
        }else if(path=="/status"){
            result.AddMember("mining",mining(),a);result.AddMember("connected",controller&&controller->network()->strategy()->isActive(),a);result.AddMember("hashrate",current_rate,a);
            auto gpu=native::gpu_status();
            rapidjson::Value g(rapidjson::kObjectType);g.AddMember("running",gpu.running,a);g.AddMember("stopping",gpu.stopping,a);
            g.AddMember("connected",gpu.connected,a);g.AddMember("faulted",gpu.faulted,a);g.AddMember("result",gpu.result,a);
            g.AddMember("graphs_per_second",gpu.gps,a);g.AddMember("graphs",gpu.graphs,a);
            g.AddMember("accepted",gpu.accepted,a);g.AddMember("rejected",gpu.rejected,a);result.AddMember("gpu",g,a);
            // Only the browser receives the mining API's current assigned port.
            result.AddMember("http_api_enabled",options.http,a);result.AddMember("http_api_port",http_port,a);
        }else if(path=="/log"){
            // Expose only this title's bounded mining-log tail, never arbitrary files.
            char bytes[32769]={};size_t n=0;FILE *f=fopen("/download0/xmrig-native.log","rb");
            if(f){if(!fseek(f,0,SEEK_END)){long size=ftell(f);fseek(f,size>32768?size-32768:0,SEEK_SET);n=fread(bytes,1,32768,f);}fclose(f);}
            result.AddMember("log",rapidjson::Value(bytes,static_cast<rapidjson::SizeType>(n),a),a);
        }else return web_error(404,"Not found");
        return {200,json_text(result)};
    }
    if(method!="POST"||path!="/config")return web_error(404,"Not found");
    if(editing||native::keyboard_active())return web_error(409,"Confirm the app's pending Options before saving from the browser");
    rapidjson::Document request,previous,doc;request.Parse(body.data(),body.size());
    if(request.HasParseError()||!request.IsObject()||!native::unique_json(request)||!request.HasMember("config")||!request["config"].IsObject()||!request.HasMember("revision")||!request["revision"].IsString())return web_error(400,"Invalid JSON save request");
    if(!read_document(config_path,previous))return web_error(500,"Current configuration unavailable");
    if(revision(previous)!=request["revision"].GetString())return web_error(409,"Configuration changed. Reload saved before applying your edits");
    doc.CopyFrom(request["config"],doc.GetAllocator());native::Options next;
    std::string error=native::validate_config(doc,previous,next);if(!error.empty())return web_error(400,error.c_str());
    // Disk persistence and runtime reload happen on the same event loop as local edits.
    if(!write_document(doc))return web_error(500,"Configuration could not be saved");
    bool resume=mining();unsigned short previous_http_port=http_port;
    if(options.http!=next.http||options.port!=next.port)http_port=0;
    if(controller){
        controller->miner()->setEnabled(false);
        if(!controller->reload(doc)){http_port=previous_http_port;bool restored=write_document(previous);controller->miner()->setEnabled(resume);return web_error(500,restored?"Reload rejected; previous configuration restored":"Reload failed; restoring the saved file also failed");}
        controller->miner()->setEnabled(resume);
    }
    load_options();current_rate=0;next_rate_refresh=0;notice("Web configuration applied");
    rapidjson::Document result;result.SetObject();result.AddMember("revision",rapidjson::Value(revision(doc).c_str(),result.GetAllocator()),result.GetAllocator());return {200,json_text(result)};
}
static void draw_web_qr(Canvas &canvas) {
    if(!web_qr_ready)return;
    // Four white modules on every side preserve the QR quiet zone on a dark screen.
    unsigned size=qrcodegen_getSize(web_qr),scale=4,x=1010,y=750;
    canvas.rectangle(x,y,(size+8)*scale,(size+8)*scale,Color::white);
    for(unsigned row=0;row<size;row++)for(unsigned col=0;col<size;col++)if(qrcodegen_getModule(web_qr,col,row))canvas.rectangle(x+(col+4)*scale,y+(row+4)*scale,scale,scale,static_cast<Color>(0xff000000));
    native::ui::label(canvas,1210,752,"Phone configuration",32);
    native::ui::label(canvas,1210,800,"Scan to open settings",32,native::ui::muted);
    native::ui::label(canvas,1210,848,web.url().c_str(),32);
}
static void draw(Canvas &canvas) noexcept {
    if(first_frame){first_frame=false;record("IDLE first frame rendered - mining not started");}
    uint32_t buttons=pressed();bool keyboard_was_active=native::keyboard_active();
    char value[1024];int result=native::keyboard_poll(value,sizeof(value));
    if(result==1)accept_edit(value);else if(result==-2)notice("Input is too long or invalid");
    // Ignore the key that dismissed the keyboard so it cannot trigger an app action.
    if(!keyboard_was_active)handle_buttons(buttons);
    uint64_t now=sceKernelGetProcessTime();
    if(now>=next_test_check){next_test_check=now+1000000;debug_command();}
    web.poll(now);
    bool connected=controller&&controller->network()->strategy()->isActive();
    if(now>=next_rate_refresh){
        next_rate_refresh=now+1000000;double rate=0,sample=0;
        if(mining())for(auto *backend:controller->miner()->backends())if(auto *h=backend->hashrate()){
            auto current=h->calc(xmrig::Hashrate::ShortInterval);if(current.first)rate+=current.second;
            auto second=h->calc(1000);if(second.first)sample+=second.second;
        }
        current_rate=static_cast<unsigned>(rate);hour_average.add(now/1000000,sample);save_ui_state(connected);
    }
    // Monitoring and editing are separate screens on the same 12-column grid.
    native::ui::header(canvas);
    auto gpu=native::gpu_status();
    if(editing){
        if(gpu_settings)native::ui::gpu_settings(canvas,gpu_pending,selected_row,native::gpu_status().running);
        else native::ui::settings(canvas,pending,selected_row);
        draw_web_qr(canvas);
    }else{
        char detail[64],cpu_rate[32],gpu_rate[32];
        snprintf(detail,sizeof(detail),"RandomX - %u threads",options.threads);
        const char *cpu_state=!mining()?"Stopped":!connected?"Connecting...":current_rate?"Mining":"Starting...";
        snprintf(cpu_rate,sizeof(cpu_rate),"%u",current_rate);
        if(!mining()||!connected)snprintf(cpu_rate,sizeof(cpu_rate),"--");
        native::ui::engine(canvas,248,"CPU",detail,cpu_state,mining()&&connected&&current_rate,
            mining()&&!connected,cpu_rate,"H/s","L1",mining()?"Stop CPU":"Start CPU");
        native::ui::rule(canvas,528);
        const char *gpu_state=gpu.faulted?"Relaunch app":gpu.stopping?"Stopping...":gpu.running?
            (gpu.connected?(gpu.gps>0?"Mining":"Starting..."):"Connecting..."):gpu.result?"Check settings":"Stopped";
        snprintf(gpu_rate,sizeof(gpu_rate),"%.3f",gpu.gps);
        if(!gpu.running||!gpu.connected||gpu.stopping||gpu.faulted)snprintf(gpu_rate,sizeof(gpu_rate),"--");
        native::ui::engine(canvas,600,"GPU","C29",gpu_state,gpu.running&&gpu.connected&&gpu.gps>0&&!gpu.stopping&&!gpu.faulted,
            gpu.faulted||gpu.result||(gpu.running&&!gpu.connected),gpu_rate,"graphs/s","R1",
            gpu.faulted?"Unavailable":gpu.stopping?"Stopping...":gpu.running?"Stop GPU":"Start GPU",!gpu.faulted&&!gpu.stopping);
        native::ui::settings_hint(canvas);
    }
    // Notices stay above the footer; removed pool/share blocks remain absent.
    if(*message&&now<notice_until){
        // Bound feedback to the left of the settings screen's QR connection block.
        native::ui::fit(message,value,sizeof(value),editing?880:1728,32);
        native::ui::label(canvas,96,editing?816:880,value,32,native::ui::muted);
    }
    native::ui::rule(canvas,928);
    sceKernelUsleep(100000);
}
/* An explicit marker captures one frame; normal rendering never writes pixels. */
extern "C" void xmrigNativeCaptureFrame(const uint32_t *pixels) {
    const char *marker="/download0/xmrig-native-capture.once";
    FILE *request=fopen(marker,"rb");if(!request)return;fclose(request);if(unlink(marker))return;
    FILE *f=fopen("/download0/xmrig-native-frame.rgba","wb");if(!f)return;
    // VideoOut uses 128x128 tiled blocks; export a linear RGBA screenshot.
    uint32_t row[1920];bool ok=true;
    for(unsigned y=0;y<1080;y++){
        for(unsigned x=0;x<1920;x++){
            uint32_t offset=((y<<4)&0x70)^((y<<5)&0xf00)^((y<<9)&0x1000)^((y<<8)&0x4000)^((x<<2)&0xc)^((x<<5)&0x380)^((x<<4)&0x400)^((x<<6)&0x800)^((x<<9)&0xa000);
            size_t block=(y>>7)*15+(x>>7);row[x]=pixels[((block<<16)+offset)/4];
        }
        if(fwrite(row,4,1920,f)!=1920){ok=false;break;}
    }
    if(fclose(f))ok=false;record(ok?"Frame captured":"Frame capture failed");
}
/* Compare native sandbox binding rules without leaving probe listeners open. */
static void check_bind_ports() {
    for(int port:{18188,58188,0}){
        int fd=socket(AF_INET,SOCK_STREAM,0);struct sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);
        errno=0;int result=fd<0?-1:bind(fd,reinterpret_cast<sockaddr *>(&address),sizeof(address));
        char line[160];snprintf(line,sizeof(line),"native bind port=%d result=%d errno=%d",port,result,errno);record(line);if(fd>=0)close(fd);
    }
}
int main() {
    diagnostic=fopen("/download0/xmrig-native-app.log","w");record("Entered native main");
    if(pipe_ready()&&pipe_ready(O_CLOEXEC|O_NONBLOCK)){record(uv_default_loop()?"UV loop ready":"UV loop unavailable");}
    void *memory_check=xmrigNativeAllocate(256u*1024u*1024u,64);
    record(memory_check?"256 MB native allocation ready":"256 MB native allocation failed");xmrigNativeFree(memory_check);
    check_bind_ports();
    load_options();
    // Management remains available before the first mining start and when API HTTP is off.
    rapidjson::Document saved;if(!read_document(config_path,saved)&&!prepare_config())record("Initial config persistence failed");
    std::string route_host=options.address;route_host=route_host.substr(0,route_host.find(':'));
    // Loopback starter pools must still advertise a reachable LAN management URL.
    if(route_host=="127.0.0.1"||route_host=="localhost")route_host="8.8.8.8";
    if(web.start(web_request,route_host.c_str())){
        record((std::string("WEB editor ")+web.url()).c_str());uint8_t temp[sizeof(web_qr)];
        web_qr_ready=qrcodegen_encodeText(web.url().c_str(),temp,web_qr,qrcodegen_Ecc_MEDIUM,1,4,qrcodegen_Mask_AUTO,true);
    }else record((std::string("WEB editor failed: ")+web.error()).c_str());
    (void)sceUserServiceInitialize(nullptr);
    if(!sceUserServiceGetInitialUser(&initial_user) && scePadInit()>=0)pad=scePadOpen(initial_user,0,0,nullptr);
    record(pad>=0?"Controller ready":"Controller unavailable");
    // Match the app branding after the controller handle becomes available.
    if(pad>=0){
        const PadLightBar orange{native::monero_red,native::monero_green,native::monero_blue,0};
        record(scePadSetLightBar(pad,&orange)>=0?"Controller light orange":"Controller light update failed");
    }
    ps5::demo::run_frames(draw,"XMR Miner ready - mining is idle");
}
