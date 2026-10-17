// SPDX-License-Identifier: GPL-3.0-or-later
#include "native-gpu.hpp"
#include "../ps5/c29/native-engine.h"
#include "../ps5/pool.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <iostream>
static std::atomic<int> runs{0}, outcome{0};
static std::atomic<bool> configured{false}, allow_finish{false};
// A controlled worker verifies real UI/worker synchronization without requiring Vulkan.
extern "C" int sceKernelDebugOutText(int,const char *) { return 0; }
extern "C" bool mom_pool_config_read(const char *,MomPoolConfig *) { return configured; }
extern "C" int ps5_gpu_run(const char *) {
    ++runs;MomManager m{};m.connected=true;m.stats.graphs=7;m.stats.accepted=2;m.stats.rejected=1;
    ps5_gpu_publish(&m,3.5);
    while(!ps5_gpu_cancelled())std::this_thread::yield();
    while(!allow_finish)std::this_thread::yield();
    return outcome;
}
template<class F> void until(F predicate) {
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!predicate()){assert(std::chrono::steady_clock::now()<deadline);std::this_thread::yield();}
}
int main() {
    assert(!native::gpu_start());configured=true;assert(native::gpu_start());
    until([]{return native::gpu_status().connected;});
    auto s=native::gpu_status();assert(s.graphs==7&&s.accepted==2&&s.rejected==1&&s.gps==3.5);
    assert(native::gpu_start()&&runs==1);
    native::gpu_stop();assert(native::gpu_status().stopping&&!native::gpu_start());
    allow_finish=true;until([]{return !native::gpu_status().running;});
    s=native::gpu_status();assert(!s.connected&&!s.stopping&&s.gps==0);
    // A device timeout permanently forbids restart until this process exits.
    outcome=-1;allow_finish=false;assert(native::gpu_start());until([]{return runs==2;});
    native::gpu_stop();allow_finish=true;until([]{return native::gpu_status().faulted;});
    assert(!native::gpu_start());std::cout<<"GPU lifecycle, publication, drain and fault tests PASS\n";
}
