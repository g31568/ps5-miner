// SPDX-License-Identifier: GPL-3.0-or-later
#include "manager.h"
#include <stdio.h>
#include <string.h>

// Keep checks enabled in Release builds and on the console, where assert may be disabled.
#define REQUIRE(expression) do { if (!(expression)) { \
  fprintf(stderr, "management check failed at %d: %s\n", __LINE__, #expression); \
  return 1; } } while (0)

int mom_manager_selftest(void) {
  MomManager manager;
  MomJob job, grin, raw;
  MomWork first, next, changed;
  uint8_t blob[40], target[32], hash[32];
  memset(blob, 0x5a, sizeof(blob));
  memset(target, 0xff, sizeof(target));
  REQUIRE(mom_job_build(&job, "tari", MOM_JOB_TARI, blob, 40, 8, 0, 42, target, 7, 0));
  REQUIRE(job.blob_size == 48 && job.nonce_offset == 0 && job.blob[8] == 0x5a);
  REQUIRE(mom_job_build(&grin, "grin", MOM_JOB_GRIN, blob, 40, 4, 0, 32, target, 7, 0));
  REQUIRE(grin.blob_size == 44 && grin.nonce_offset == 40 && grin.blob[40] == 0);
  REQUIRE(mom_job_build(&raw, "raw", MOM_JOB_RAW, blob, 40, 8, 32, 42, target, 7, 0));
  REQUIRE(raw.blob[31] == 0x5a && raw.blob[32] == 0 && raw.blob[39] == 0);
  REQUIRE(!mom_job_build(&raw, "bad", MOM_JOB_RAW, blob, 40, 8, 33, 42, target, 7, 0));
  REQUIRE(!mom_job_build(&raw, "bad", MOM_JOB_TARI, blob, 40, 4, 0, 41, target, 7, 0));
  mom_manager_init(&manager);
  REQUIRE(!mom_manager_acquire(&manager, &first));
  REQUIRE(mom_manager_connected(&manager));
  REQUIRE(!mom_manager_connected(&manager));
  REQUIRE(mom_manager_job(&manager, &job));
  REQUIRE(mom_manager_acquire(&manager, &first) && first.nonce == 7);
  REQUIRE(first.job.blob[0] == 0 && first.job.blob[7] == 7);
  REQUIRE(first.seed[0] == UINT64_C(0xbc775c8b36b5b3e3));
  REQUIRE(first.seed[3] == UINT64_C(0xd180991bf91afe1e));
  REQUIRE(!mom_manager_acquire(&manager, &next));
  REQUIRE(mom_manager_job(&manager, &job)); // Same id still replaces old work.
  REQUIRE(mom_manager_cancelled(&manager, &first));
  REQUIRE(!mom_manager_acquire(&manager, &next)); // Old GPU resources must drain first.
  REQUIRE(mom_manager_complete(&manager, &first, true));
  REQUIRE(!mom_manager_complete(&manager, &first, true));
  REQUIRE(manager.stats.stale_graphs == 1 && manager.stats.graphs == 0);
  REQUIRE(mom_manager_acquire(&manager, &next) && next.nonce == 8);
  REQUIRE(!mom_manager_complete(&manager, &first, true));
  changed = next;
  ++changed.nonce;
  REQUIRE(!mom_manager_complete(&manager, &changed, true));
  mom_manager_pause(&manager, true);
  REQUIRE(!mom_manager_cancelled(&manager, &next)); // Pause drains current work safely.
  REQUIRE(mom_manager_complete(&manager, &next, true));
  REQUIRE(!mom_manager_acquire(&manager, &first));
  mom_manager_pause(&manager, false);
  REQUIRE(mom_manager_acquire(&manager, &first) && first.nonce == 9);
  uint32_t nonces[42];
  for (unsigned i = 0; i < 42; ++i) nonces[i] = i;
  REQUIRE(mom_c29_proof_hash(nonces, 42, hash));
  // Python hashlib independently generated this 29-bit packed proof digest.
  static const uint8_t expected[32] = {
    0x81, 0x24, 0x37, 0x60, 0xde, 0x07, 0x80, 0xa9,
    0x08, 0xf4, 0xce, 0x98, 0x9d, 0x6f, 0xa3, 0x1e,
    0xd9, 0x1a, 0x27, 0xf8, 0x9a, 0xf6, 0xa4, 0x76,
    0x93, 0x92, 0x7e, 0xbc, 0xcc, 0x81, 0x6d, 0xf4
  };
  REQUIRE(memcmp(hash, expected, sizeof(hash)) == 0);
  REQUIRE(mom_c29_meets_target(hash, target));
  uint8_t boundary[32];
  for (unsigned i = 0; i < 32; ++i) boundary[i] = hash[31 - i];
  REQUIRE(!mom_c29_meets_target(hash, boundary));
  ++boundary[31];
  REQUIRE(mom_c29_meets_target(hash, boundary));
  boundary[31] -= 2;
  REQUIRE(!mom_c29_meets_target(hash, boundary));
  REQUIRE(!mom_manager_share(&manager, &first, nonces, 42, hash));
  REQUIRE(manager.stats.invalid_proofs == 1);
  nonces[1] = 0;
  REQUIRE(!mom_c29_proof_hash(nonces, 42, hash));
  nonces[1] = 1;
  nonces[41] = 1u << 29;
  REQUIRE(!mom_c29_proof_hash(nonces, 42, hash));
  manager.pending[0] = 100; // Inject transport replies without pretending a graph found a proof.
  REQUIRE(!mom_manager_share_reply(&manager, manager.session + 1, 100, true));
  REQUIRE(mom_manager_share_reply(&manager, manager.session, 100, true));
  REQUIRE(!mom_manager_share_reply(&manager, manager.session, 100, true));
  manager.pending[0] = 101;
  REQUIRE(mom_manager_share_reply(&manager, manager.session, 101, false));
  manager.pending[0] = 102;
  mom_manager_disconnected(&manager, 1000);
  REQUIRE(manager.stats.accepted == 1 && manager.stats.rejected == 1 && manager.stats.lost == 1);
  REQUIRE(!mom_manager_retry_due(&manager, 1999) && mom_manager_retry_due(&manager, 2000));
  REQUIRE(mom_manager_cancelled(&manager, &first));
  REQUIRE(!mom_manager_share_reply(&manager, manager.session, 102, true));
  REQUIRE(mom_manager_connected(&manager));
  REQUIRE(mom_manager_job(&manager, &job));
  REQUIRE(!mom_manager_acquire(&manager, &next));
  REQUIRE(mom_manager_complete(&manager, &first, true));
  REQUIRE(mom_manager_acquire(&manager, &next) && next.nonce == 10);
  REQUIRE(mom_manager_complete(&manager, &next, true));
  // A different template can use the final nonce, but cannot cross a protected bit.
  job.blob[8] ^= 1;
  job.nonce = 255;
  job.nonce_mask = UINT64_C(0xffffffffffffff00);
  REQUIRE(mom_manager_job(&manager, &job));
  REQUIRE(mom_manager_acquire(&manager, &first) && first.nonce == 255);
  REQUIRE(mom_manager_complete(&manager, &first, true));
  REQUIRE(!mom_manager_acquire(&manager, &next));
  REQUIRE(mom_manager_job(&manager, &job));
  REQUIRE(!mom_manager_acquire(&manager, &next)); // Retarget does not replay exhaustion.
  // A new protected pool prefix is new work, even when the header and mask match.
  job.nonce = 0;
  REQUIRE(mom_manager_job(&manager, &job));
  REQUIRE(!mom_manager_acquire(&manager, &next)); // Same prefix remains exhausted.
  job.nonce = 256;
  REQUIRE(mom_manager_job(&manager, &job));
  REQUIRE(mom_manager_acquire(&manager, &first) && first.nonce == 256);
  REQUIRE(mom_manager_complete(&manager, &first, true));
  job.nonce = UINT64_MAX;
  job.nonce_mask = 0;
  REQUIRE(mom_manager_job(&manager, &job));
  REQUIRE(mom_manager_acquire(&manager, &first) && first.nonce == UINT64_MAX);
  REQUIRE(mom_manager_complete(&manager, &first, true));
  REQUIRE(!mom_manager_acquire(&manager, &next));
  for (unsigned i = 0; i < 10; ++i) mom_manager_disconnected(&manager, 0);
  REQUIRE(manager.retry_at_ms == 60000);
  mom_manager_disconnected(&manager, UINT64_MAX - 1);
  REQUIRE(manager.retry_at_ms == UINT64_MAX);
  mom_manager_stop(&manager);
  REQUIRE(!mom_manager_retry_due(&manager, UINT64_MAX));
  REQUIRE(!mom_manager_connected(&manager));
  mom_manager_init(&manager);
  REQUIRE(mom_manager_connected(&manager));
  job.nonce = UINT32_MAX;
  job.nonce_bytes = 4;
  REQUIRE(mom_manager_job(&manager, &job));
  REQUIRE(mom_manager_acquire(&manager, &first));
  REQUIRE(mom_manager_complete(&manager, &first, true));
  REQUIRE(!mom_manager_acquire(&manager, &next));
  job.nonce = 0;
  job.blob[8] ^= 1;
  REQUIRE(mom_manager_job(&manager, &job));
  REQUIRE(mom_manager_acquire(&manager, &first));
  REQUIRE(mom_manager_complete(&manager, &first, false));
  REQUIRE(manager.stopped && !mom_manager_acquire(&manager, &next));
  // Discarding completed capacity failures preserves counts, ownership checks and nonce progression.
  mom_manager_init(&manager);
  REQUIRE(mom_manager_connected(&manager) && mom_manager_job(&manager,&job));
  REQUIRE(mom_manager_acquire(&manager,&first));
  changed=first;changed.ticket++;
  REQUIRE(!mom_manager_discard(&manager,&changed) && manager.busy);
  REQUIRE(mom_manager_discard(&manager,&first) && !manager.busy && !manager.stopped);
  REQUIRE(manager.stats.graphs==0 && !mom_manager_discard(&manager,&first));
  REQUIRE(mom_manager_acquire(&manager,&next) && next.nonce==first.nonce+1);
  // Structural verifier must reject two disjoint rings, duplicate edges and broken degrees.
  uint32_t edges[42][2];
  for (unsigned count = 32; count <= 42; count += 10) {
    for (unsigned i = 0; i < count / 2; ++i) {
      edges[2 * i][0] = i;
      edges[2 * i][1] = i;
      edges[2 * i + 1][0] = (i + 1) % (count / 2);
      edges[2 * i + 1][1] = i;
    }
    REQUIRE(mom_c29_single_cycle((const uint32_t (*)[2])edges, count));
    edges[0][0] = 1000;
    REQUIRE(!mom_c29_single_cycle((const uint32_t (*)[2])edges, count));
  }
  for (unsigned i = 0; i < 16; ++i) {
    unsigned offset = i < 8 ? 0 : 8;
    edges[2 * i][0] = i;
    edges[2 * i][1] = i;
    edges[2 * i + 1][0] = offset + (i + 1 - offset) % 8;
    edges[2 * i + 1][1] = i;
  }
  REQUIRE(!mom_c29_single_cycle((const uint32_t (*)[2])edges, 32));
  return 0;
}

#ifdef MOM_MANAGER_TEST_MAIN
int main(void) {
  int result = mom_manager_selftest();
  if (!result) puts("PASS native management lifecycle and proof checks");
  return result;
}
#endif
