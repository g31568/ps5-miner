// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MOM_PS5_POOL_H
#define MOM_PS5_POOL_H
#include "manager.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MomPool MomPool;
typedef void (*MomPoolLog)(const char *message);
typedef struct {
  char host[254], login[512], password[256], proxy_host[254], ca_file[512];
  char performance_file[512]; // Public measured rates live beside the private configuration.
  double c29_gps;
  unsigned port, proxy_port, max_graphs, runtime_seconds;
  bool tls, tls_verify; // Match MOM: certificate verification is an explicit pool option.
} MomPoolConfig;
// Configuration and protocol contents are never printed by the transport.
bool mom_pool_config_read(const char *path, MomPoolConfig *config);
MomPool *mom_pool_create(const MomPoolConfig *config, MomManager *manager, MomPoolLog log);
void mom_pool_destroy(MomPool *pool);
void mom_pool_pump(MomPool *pool, unsigned timeout_ms);
uint64_t mom_pool_submit(MomPool *pool, const MomWork *work,
                         const uint32_t *nonces, size_t count);
uint64_t mom_pool_now_ms(void);
bool mom_pool_pending(const MomPool *pool);
// Save only sustained measured performance; login converts graphs/s to MOM's pool units.
bool mom_pool_record_performance(MomPool *pool, double graphs_per_second, uint64_t graphs);
// Testable protocol normalization mirrors MOM's object-login C29 dialect.
bool mom_pool_target(const char *hex, uint8_t target[32]);
#ifdef __cplusplus
}
#endif
#endif
