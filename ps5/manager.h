// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MOM_PS5_MANAGER_H
#define MOM_PS5_MANAGER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { MOM_PS5_BLOB_MAX = 4096, MOM_PS5_ID_MAX = 256, MOM_PS5_PROOF_MAX = 42 };
typedef enum { MOM_JOB_TARI, MOM_JOB_GRIN, MOM_JOB_RAW } MomJobLayout;
typedef struct {
  char id[MOM_PS5_ID_MAX + 1];
  uint8_t blob[MOM_PS5_BLOB_MAX], target[32]; // Normalized full target, big endian.
  size_t blob_size, nonce_offset;
  unsigned nonce_bytes, proof_size;
  uint64_t nonce, nonce_mask; // Protected bits must never change when incrementing.
} MomJob;
typedef struct {
  uint64_t ticket, generation, session, nonce, seed[4];
  MomJob job; // Own the template until GPU and CPU work have both drained.
} MomWork;
typedef struct {
  uint64_t graphs, stale_graphs, invalid_proofs, shares, accepted, rejected, lost;
} MomStats;
typedef struct {
  MomJob job;
  MomWork work;
  MomStats stats;
  uint64_t generation, session, ticket, retry_at_ms;
  uint64_t pending[16], next_share;
  uint8_t submitted[64][32];
  unsigned failures, submitted_count;
  bool connected, has_job, paused, stopped, busy, exhausted;
} MomManager;

// All calls belong to one event-loop thread; GPU completion must return its exact lease.
bool mom_job_build(MomJob *out, const char *id, MomJobLayout layout,
                   const uint8_t *blob, size_t size, unsigned nonce_bytes,
                   size_t nonce_offset, unsigned proof_size, const uint8_t target[32],
                   uint64_t nonce, uint64_t protected_mask);
void mom_manager_init(MomManager *manager);
bool mom_manager_connected(MomManager *manager);
void mom_manager_disconnected(MomManager *manager, uint64_t now_ms);
bool mom_manager_retry_due(const MomManager *manager, uint64_t now_ms);
bool mom_manager_job(MomManager *manager, const MomJob *job);
void mom_manager_pause(MomManager *manager, bool paused);
void mom_manager_stop(MomManager *manager);
bool mom_manager_acquire(MomManager *manager, MomWork *work);
bool mom_manager_cancelled(const MomManager *manager, const MomWork *work);
bool mom_manager_complete(MomManager *manager, const MomWork *work, bool success);
// Discard only drained capacity failures; the next lease advances to a fresh nonce.
bool mom_manager_discard(MomManager *manager, const MomWork *work);
// Returns zero unless the current graph's proof is independently valid and below target.
uint64_t mom_manager_share(MomManager *manager, const MomWork *work,
                           const uint32_t *nonces, size_t count, uint8_t hash[32]);
bool mom_manager_share_reply(MomManager *manager, uint64_t session,
                             uint64_t request, bool accepted);
bool mom_c29_single_cycle(const uint32_t (*edges)[2], size_t count);
bool mom_c29_proof_hash(const uint32_t *nonces, size_t count, uint8_t hash[32]);
bool mom_c29_meets_target(const uint8_t hash[32], const uint8_t target[32]);
bool mom_c29_verify(const uint64_t seed[4], const uint32_t *nonces,
                     size_t count, uint8_t hash[32]);
int mom_manager_selftest(void);
#ifdef __cplusplus
}
#endif
#endif
