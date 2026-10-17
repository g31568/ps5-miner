// SPDX-License-Identifier: GPL-3.0-or-later
#include "pool.h"
#include <cstdio>
static void log_line(const char *text) { std::puts(text); std::fflush(stdout); }
// Verify authentication and job normalization without starting hashing or submitting a share.
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  MomPoolConfig config;
  if (!mom_pool_config_read(argv[1], &config)) return 2;
  MomManager manager;
  mom_manager_init(&manager);
  MomPool *pool = mom_pool_create(&config, &manager, log_line);
  if (!pool) return 2;
  uint64_t start = mom_pool_now_ms();
  while (!manager.has_job && mom_pool_now_ms() - start < 30000) mom_pool_pump(pool, 100);
  bool ready = manager.has_job;
  mom_pool_destroy(pool);
  if (ready) std::puts("PASS pool authentication and C29 job normalization");
  return ready ? 0 : 1;
}
