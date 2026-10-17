// SPDX-License-Identifier: GPL-3.0-or-later
// Native lifecycle follows MOM's miner/jobs.js and native/job-boundary.h semantics.
#include "manager.h"
#include "c29/reference.h"
#include "crypto/randomx/blake2/blake2.h"
#include <limits.h>
#include <string.h>

// Select the portable implementation; neither Node nor a vendor compute runtime is needed.
void (*rx_blake2b_compress)(blake2b_state *, const uint8_t *) = rx_blake2b_compress_integer;
int (*rx_blake2b)(void *, size_t, const void *, size_t) = rx_blake2b_default;

static bool valid_job(const MomJob *job) {
  if (!job || !job->id[0] || !memchr(job->id, 0, sizeof(job->id)) ||
      !job->blob_size || job->blob_size > MOM_PS5_BLOB_MAX ||
      (job->nonce_bytes != 4 && job->nonce_bytes != 8) ||
      (job->proof_size != 32 && job->proof_size != 42) ||
      job->nonce_offset > job->blob_size ||
      job->nonce_bytes > job->blob_size - job->nonce_offset) return false;
  if (job->nonce_bytes == 4 && (job->nonce > UINT32_MAX || job->nonce_mask > UINT32_MAX))
    return false;
  uint8_t any = 0;
  for (unsigned i = 0; i < 32; ++i) any |= job->target[i];
  return any != 0;
}

bool mom_job_build(MomJob *out, const char *id, MomJobLayout layout,
                   const uint8_t *blob, size_t size, unsigned bytes,
                   size_t offset, unsigned proof, const uint8_t target[32],
                   uint64_t nonce, uint64_t mask) {
  if (!out || !id || !blob || !target || !size || size > MOM_PS5_BLOB_MAX ||
      (bytes != 4 && bytes != 8) || layout > MOM_JOB_RAW || layout < MOM_JOB_TARI)
    return false;
  size_t length = 0;
  while (length <= MOM_PS5_ID_MAX && id[length]) ++length;
  if (!length || length > MOM_PS5_ID_MAX) return false;
  MomJob job = {0};
  memcpy(job.id, id, length);
  memcpy(job.target, target, 32);
  job.nonce_bytes = bytes;
  job.proof_size = proof;
  job.nonce = nonce;
  job.nonce_mask = mask;
  if (layout == MOM_JOB_RAW) {
    job.blob_size = size;
    job.nonce_offset = offset;
    memcpy(job.blob, blob, size);
  } else {
    if (size > MOM_PS5_BLOB_MAX - bytes) return false;
    job.blob_size = size + bytes;
    job.nonce_offset = layout == MOM_JOB_TARI ? 0 : size;
    memcpy(job.blob + (layout == MOM_JOB_TARI ? bytes : 0), blob, size);
  }
  if (!valid_job(&job)) return false;
  // Embedded bytes are overwritten by the explicit counter, so canonicalize the template.
  memset(job.blob + job.nonce_offset, 0, job.nonce_bytes);
  *out = job;
  return true;
}

void mom_manager_init(MomManager *m) {
  memset(m, 0, sizeof(*m));
}

bool mom_manager_connected(MomManager *m) {
  if (m->stopped || m->connected || m->session == UINT64_MAX) return false;
  ++m->session;
  m->connected = true;
  m->has_job = false;
  return true;
}

void mom_manager_disconnected(MomManager *m, uint64_t now) {
  m->connected = m->has_job = false;
  for (unsigned i = 0; i < 16; ++i) {
    if (m->pending[i]) ++m->stats.lost;
    m->pending[i] = 0;
  }
  // Cap reconnect delay and saturate the deadline instead of wrapping the clock.
  unsigned shift = m->failures < 6 ? m->failures : 6;
  uint64_t delay = UINT64_C(1000) << shift;
  if (delay > 60000) delay = 60000;
  if (m->failures < 6) ++m->failures;
  m->retry_at_ms = now > UINT64_MAX - delay ? UINT64_MAX : now + delay;
}

bool mom_manager_retry_due(const MomManager *m, uint64_t now) {
  return !m->stopped && !m->connected && now >= m->retry_at_ms;
}

bool mom_manager_job(MomManager *m, const MomJob *job) {
  if (m->stopped || !m->connected || !valid_job(job) || m->generation == UINT64_MAX)
    return false;
  // Retarget/reconnect of the latest template continues its reserved nonce cursor.
  bool same = valid_job(&m->job) && job->blob_size == m->job.blob_size &&
    job->nonce_offset == m->job.nonce_offset && job->nonce_bytes == m->job.nonce_bytes &&
    job->nonce_mask == m->job.nonce_mask &&
    (job->nonce & job->nonce_mask) == (m->job.nonce & m->job.nonce_mask) &&
    memcmp(job->blob, m->job.blob, job->blob_size) == 0;
  uint64_t cursor = m->job.nonce;
  bool exhausted = same && m->exhausted && job->nonce <= cursor;
  m->job = *job;
  if (same && m->job.nonce < cursor) m->job.nonce = cursor;
  ++m->generation; // Even a retarget of the same job id invalidates earlier work.
  m->has_job = true;
  m->exhausted = exhausted;
  m->failures = 0; // Authentication without a usable job does not reset backoff.
  return true;
}

void mom_manager_pause(MomManager *m, bool paused) {
  m->paused = paused;
}

void mom_manager_stop(MomManager *m) {
  m->stopped = true;
  mom_manager_disconnected(m, 0);
}

bool mom_manager_acquire(MomManager *m, MomWork *out) {
  if (!out || m->stopped || m->paused || !m->connected || !m->has_job ||
      m->busy || m->exhausted || m->ticket == UINT64_MAX) return false;
  MomWork work = {0};
  work.ticket = ++m->ticket;
  work.generation = m->generation;
  work.session = m->session;
  work.nonce = m->job.nonce;
  work.job = m->job;
  // MOM's C29 dispatch serializes the numeric pool nonce big endian before hashing the header.
  for (unsigned i = 0; i < work.job.nonce_bytes; ++i)
    work.job.blob[work.job.nonce_offset + i] =
      (uint8_t)(work.nonce >> (8 * (work.job.nonce_bytes - 1 - i)));
  uint8_t digest[32];
  if (rx_blake2b_default(digest, sizeof(digest), work.job.blob, work.job.blob_size))
    return false;
  for (unsigned k = 0; k < 4; ++k)
    for (unsigned b = 0; b < 8; ++b)
      work.seed[k] |= (uint64_t)digest[k * 8 + b] << (8 * b);
  // Reserve before dispatch; cancellation must never replay an already-issued nonce.
  uint64_t maximum = work.job.nonce_bytes == 4 ? UINT32_MAX : UINT64_MAX;
  if (work.nonce == maximum ||
      ((work.nonce + 1) & work.job.nonce_mask) != (work.nonce & work.job.nonce_mask))
    m->exhausted = true;
  else ++m->job.nonce;
  m->work = work;
  m->submitted_count = 0;
  m->busy = true;
  *out = work;
  return true;
}

static bool owns_work(const MomManager *m, const MomWork *work) {
  return work && m->busy && work->ticket == m->work.ticket &&
    work->generation == m->work.generation && work->session == m->work.session &&
    work->nonce == m->work.nonce;
}

bool mom_manager_cancelled(const MomManager *m, const MomWork *work) {
  return !owns_work(m, work) || m->stopped || !m->connected || !m->has_job ||
    work->session != m->session || work->generation != m->generation;
}

bool mom_manager_complete(MomManager *m, const MomWork *work, bool success) {
  if (!owns_work(m, work)) return false;
  if (mom_manager_cancelled(m, work)) ++m->stats.stale_graphs;
  else if (success) ++m->stats.graphs;
  m->busy = false; // Call only after fences AND any CPU solver have drained.
  if (!success) mom_manager_stop(m); // Device failure requires explicit reinitialization.
  return true;
}

bool mom_manager_discard(MomManager *m, const MomWork *work) {
  if (!owns_work(m, work)) return false;
  if (mom_manager_cancelled(m, work)) ++m->stats.stale_graphs;
  m->busy = false; // The caller has already drained GPU and CPU owners.
  return true;
}

bool mom_c29_single_cycle(const uint32_t (*edges)[2], size_t count) {
  if (!edges || (count != 32 && count != 42)) return false;
  unsigned mate[MOM_PS5_PROOF_MAX][2];
  for (size_t i = 0; i < count; ++i) {
    for (unsigned side = 0; side < 2; ++side) {
      unsigned matches = 0;
      for (size_t j = 0; j < count; ++j) {
        if (i != j && edges[i][side] == edges[j][side]) {
          mate[i][side] = (unsigned)j;
          ++matches;
        }
      }
      if (matches != 1) return false;
    }
  }
  // Degree two alone also accepts disjoint cycles; walk every edge exactly once.
  bool seen[MOM_PS5_PROOF_MAX] = {false};
  unsigned at = 0, side = 0;
  for (size_t i = 0; i < count; ++i) {
    if (seen[at]) return false;
    seen[at] = true;
    at = mate[at][side];
    side ^= 1;
  }
  return at == 0;
}

bool mom_c29_proof_hash(const uint32_t *nonces, size_t count, uint8_t hash[32]) {
  if (!nonces || !hash || (count != 32 && count != 42)) return false;
  uint8_t packed[(MOM_PS5_PROOF_MAX * 29 + 7) / 8] = {0}, digest[32];
  for (size_t i = 0; i < count; ++i) {
    if (nonces[i] >= (1u << 29) || (i && nonces[i] <= nonces[i - 1])) return false;
    for (unsigned b = 0; b < 29; ++b) {
      size_t bit = i * 29 + b;
      if (nonces[i] & (1u << b)) packed[bit / 8] |= (uint8_t)(1u << (bit % 8));
    }
  }
  if (rx_blake2b_default(digest, 32, packed, (count * 29 + 7) / 8)) return false;
  for (unsigned i = 0; i < 32; ++i) hash[i] = digest[31 - i];
  return true;
}

bool mom_c29_meets_target(const uint8_t hash[32], const uint8_t target[32]) {
  if (!hash || !target) return false;
  // MOM's wire hash is little endian; compare its reverse with the normalized BE target.
  for (unsigned i = 0; i < 32; ++i) {
    if (hash[31 - i] != target[i]) return hash[31 - i] < target[i];
  }
  return false; // Match MOM's strict target comparison, including equality rejection.
}

bool mom_c29_verify(const uint64_t seed[4], const uint32_t *nonces,
                     size_t count, uint8_t hash[32]) {
  if (!seed || !nonces || !hash) return false;
  uint32_t edges[MOM_PS5_PROOF_MAX][2], block[64][2];
  uint8_t result[32];
  if (!mom_c29_proof_hash(nonces, count, result)) return false;
  uint32_t base = UINT32_MAX;
  for (size_t i = 0; i < count; ++i) {
    uint32_t next = nonces[i] & ~63u;
    if (next != base) c29_reference_block(seed, base = next, block);
    memcpy(edges[i], block[nonces[i] & 63], sizeof(edges[i]));
  }
  if (!mom_c29_single_cycle((const uint32_t (*)[2])edges, count)) return false;
  memcpy(hash, result, 32);
  return true;
}

uint64_t mom_manager_share(MomManager *m, const MomWork *work,
                           const uint32_t *nonces, size_t count, uint8_t hash[32]) {
  if (mom_manager_cancelled(m, work) || !hash || !nonces) return 0;
  const MomWork *lease = &m->work; // Never trust a caller-mutated template or seed.
  uint8_t result[32];
  if (count != lease->job.proof_size || !mom_c29_verify(lease->seed, nonces, count, result)) {
    ++m->stats.invalid_proofs;
    return 0;
  }
  if (!mom_c29_meets_target(result, lease->job.target) ||
      m->next_share == UINT64_MAX || m->submitted_count == 64) return 0;
  for (unsigned i = 0; i < m->submitted_count; ++i)
    if (memcmp(result, m->submitted[i], 32) == 0) return 0;
  for (unsigned i = 0; i < 16; ++i) {
    if (m->pending[i]) continue;
    m->pending[i] = ++m->next_share;
    ++m->stats.shares;
    memcpy(m->submitted[m->submitted_count++], result, 32);
    memcpy(hash, result, 32);
    return m->pending[i];
  }
  return 0; // Bounded outstanding submissions provide backpressure.
}

bool mom_manager_share_reply(MomManager *m, uint64_t session,
                             uint64_t request, bool accepted) {
  if (!m->connected || session != m->session || !request) return false;
  for (unsigned i = 0; i < 16; ++i) {
    if (m->pending[i] != request) continue;
    m->pending[i] = 0;
    if (accepted) ++m->stats.accepted;
    else ++m->stats.rejected;
    return true;
  }
  return false;
}
