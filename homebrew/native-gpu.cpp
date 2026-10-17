// SPDX-License-Identifier: GPL-3.0-or-later
#include "native-gpu.hpp"
#include "../ps5/c29/native-engine.h"
#include "../ps5/pool.h"
#include <atomic>
#include <mutex>
#include <pthread.h>
#include <cstdio>
extern "C" int sceKernelDebugOutText(int,const char *);
namespace {
std::mutex lock;
native::GpuStatus status;
std::atomic<bool> stop_requested{false};
pthread_t worker{};
bool joinable=false;
// GPU lifecycle calls belong to the UI thread; completion only publishes status.
void *run(void *) {
    int result=ps5_gpu_run("/download0/c29-pool.json");
    std::lock_guard<std::mutex> guard(lock);
    status.running=false;status.stopping=false;status.connected=false;status.gps=0;
    status.result=result;status.faulted=result<0;
    return nullptr;
}
}
extern "C" bool ps5_gpu_cancelled(void) { return stop_requested.load(); }
extern "C" void ps5_gpu_publish(const MomManager *m,double gps) {
    std::lock_guard<std::mutex> guard(lock);
    status.connected=m->connected;status.graphs=m->stats.graphs;
    status.accepted=m->stats.accepted;status.rejected=m->stats.rejected;
    if(gps>=0)status.gps=gps;
}
extern "C" void ps5_gpu_log(const char *message) {
    char line[544];snprintf(line,sizeof(line),"[PS5 GPU] %s",message);sceKernelDebugOutText(0,line);
}
namespace native {
GpuStatus gpu_status() { std::lock_guard<std::mutex> guard(lock);return status; }
void gpu_stop() {
    stop_requested.store(true);
    std::lock_guard<std::mutex> guard(lock);status.stopping=status.running;
}
bool gpu_start() {
    {std::lock_guard<std::mutex> guard(lock);if(status.running)return !status.stopping;if(status.faulted)return false;}
    if(joinable){pthread_join(worker,nullptr);joinable=false;}
    MomPoolConfig config{};
    // Reject an unconfigured GPU before creating Vulkan objects or changing CPU mining.
    if(!mom_pool_config_read("/download0/c29-pool.json",&config))return false;
    stop_requested.store(false);
    {std::lock_guard<std::mutex> guard(lock);status={};status.running=true;}
    if(pthread_create(&worker,nullptr,run,nullptr)){
        std::lock_guard<std::mutex> guard(lock);status.running=false;status.result=1;return false;
    }
    joinable=true;return true;
}
}
