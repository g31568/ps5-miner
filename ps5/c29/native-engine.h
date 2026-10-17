// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../manager.h"
#ifdef __cplusplus
extern "C" {
#endif
// The UI requests cancellation; only the GPU thread touches manager or Vulkan state.
int ps5_gpu_run(const char *config_path);
bool ps5_gpu_cancelled(void);
void ps5_gpu_publish(const MomManager *manager, double gps);
void ps5_gpu_log(const char *message);
#ifdef __cplusplus
}
#endif
