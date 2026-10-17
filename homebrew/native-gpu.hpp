// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace native {
// Snapshots copy worker state under a mutex; the render loop never waits for a GPU fence.
struct GpuStatus { bool running=false, stopping=false, connected=false, faulted=false; double gps=0; uint64_t graphs=0, accepted=0, rejected=0; int result=0; };
bool gpu_start();
void gpu_stop();
GpuStatus gpu_status();
}
