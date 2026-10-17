// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MOM_C29_SOLVER_PROBE_H
#define MOM_C29_SOLVER_PROBE_H
#include "solver.h"
#include "../pool.h"
typedef struct {
  MomManager manager;
  MomWork work;
  bool matched;
  MomPool *pool;
  bool prefetch; // The event loop permits speculative seeding only when another graph is needed.
  bool seed_reused;
  // Monotonic wall times include validation and network polling within each stage.
  uint64_t trim_ms, recovery_ms, prefetch_ms;
} C29SolveContext;

// Use MOM's existing 42-edge vector, preserving the original header's exact nonce bytes.
static bool c29_solve_start(C29SolveContext *context) {
  const char *hex = "000000000000001e3695b4b53bb00358b0ad38dc160feb9e004eece09b83a72ef6ba9864d3510c88";
  uint8_t blob[40], target[32];
  for (unsigned i = 0; i < 40; ++i) {
    unsigned byte;
    if (sscanf(hex + 2 * i, "%2x", &byte) != 1) return false;
    blob[i] = (uint8_t)byte;
  }
  memset(target, 255, sizeof(target));
  MomJob job;
  memset(context, 0, sizeof(*context));
  mom_manager_init(&context->manager);
  return mom_job_build(&job, "mom-c29-42-vector", MOM_JOB_RAW, blob, 40, 8, 0, 42,
                       target, UINT64_C(0x1e), 0) &&
    mom_manager_connected(&context->manager) && mom_manager_job(&context->manager, &job) &&
    mom_manager_acquire(&context->manager, &context->work);
}

static bool c29_solve_cancelled(void *opaque) {
  C29SolveContext *context = opaque;
#ifdef PS5_COMBINED_MINER
  // Drain submitted GPU work before cleanup; cancellation invalidates its proof lease.
  if (ps5_gpu_cancelled()) mom_manager_stop(&context->manager);
#endif
  if (context->pool) mom_pool_pump(context->pool, 0);
  return mom_manager_cancelled(&context->manager, &context->work);
}

static bool c29_solve_proof(void *opaque, const uint32_t *nonces,
                            size_t count, const uint8_t hash[32]) {
  C29SolveContext *context = opaque;
  if (context->pool) {
    // Transport checks the current lease and target again before queueing a candidate.
    mom_pool_submit(context->pool, &context->work, nonces, count);
    return !mom_manager_cancelled(&context->manager, &context->work);
  }
  uint8_t checked[32];
  if (!mom_manager_share(&context->manager, &context->work, nonces, count, checked) ||
      memcmp(hash, checked, 32)) return false;
  // A genuine proof must be accepted once and rejected on duplicate submission.
  if (mom_manager_share(&context->manager, &context->work, nonces, count, checked)) return false;
  char hex[65], proof[42 * 9 + 1];
  for (unsigned i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", hash[i]);
  size_t position = 0;
  for (size_t i = 0; i < count; ++i)
    position += (size_t)snprintf(proof + position, sizeof(proof) - position,
                                 "%08x%s", nonces[i], i + 1 == count ? "" : ",");
  c29_report("verified proof hash=%s\n", hex);
  c29_report("verified proof nonces=%s\n", proof);
  if (strcmp(hex, "68005b0465cf34675f804de6ef37b3ea2fed0f4796236fc55d1ecd996b54db2d") == 0)
    context->matched = true;
  return true;
}

static bool c29_solve_finish(C29SolveContext *context, const uint32_t *alive, size_t words) {
  MomC29SolveStats stats;
  // The pool chooses a 32-edge Grin or 42-edge Tari proof.
  int result = mom_c29_solve_bitmap(context->work.seed, alive, words, 0, 1u << 29, context->work.job.proof_size,
    c29_solve_cancelled, c29_solve_proof, context, &stats);
  c29_report("cycle search result=%d survivors=%" PRIu64 " blocks=%" PRIu64
    " cycles=%" PRIu64 " verified=%" PRIu64 "\n", result, stats.survivors,
    stats.regenerated_blocks, stats.cycles, stats.verified);
  if (context->pool) {
    if (result < 0) return false;
    return mom_manager_complete(&context->manager, &context->work, true);
  }
  bool passed = result == 0 && stats.verified > 0 && context->matched;
  passed = mom_manager_complete(&context->manager, &context->work, passed) && passed;
  mom_manager_stop(&context->manager); // Offline verification sends no pool messages.
  if (passed) c29_report("PASS c29 solve: MOM reference hash matched, proofs independently verified\n");
  return passed;
}
#endif
