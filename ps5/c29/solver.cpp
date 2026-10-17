// SPDX-License-Identifier: GPL-3.0-or-later
// Keep MOM's cycle forest unchanged; adapt bitmap survivors and recover exact edge identities.
#include "solver.h"
#include "reference.h"
#include "../manager.h"
#include "../../sycl/c29/cycle.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <unordered_map>
#include <vector>
#include <pthread.h>

namespace {
struct Edge {
  uint32_t u, v;
  uint32_t x() const { return u; }
  uint32_t y() const { return v; }
};
uint64_t key(const Edge &edge) {
  return (uint64_t{edge.u} << 32) | edge.v;
}
constexpr uint64_t max_survivors = 1u << 20;
bool flat_dictionary = false; // Standalone callers retain the baseline; the PS5 entry point selects tuning.
}

extern "C" void mom_c29_set_flat_dictionary(bool enabled) { flat_dictionary = enabled; }

extern "C" int mom_c29_solve_bitmap(const uint64_t seed[4], const uint32_t *alive,
                                    size_t words, uint32_t first, uint32_t count,
                                    unsigned proof_size, MomC29Cancelled cancelled,
                                    MomC29Proof proof, void *context, MomC29SolveStats *stats) {
  if (!stats) return -1;
  *stats = {};
  if (!seed || !alive || (first & 63) || first >= (1u << 29) ||
      count > (1u << 29) - first || words != (count + 31u) / 32u ||
      (proof_size != 32 && proof_size != 42)) return -1;
  if (words && (count & 31) && (alive[words - 1] >> (count & 31))) return -1;
  for (size_t i = 0; i < words; ++i) {
    if (!(i & 4095) && cancelled && cancelled(context)) return 1;
    stats->survivors += static_cast<unsigned>(__builtin_popcount(alive[i]));
    if (stats->survivors > max_survivors) return -1;
  }
  std::vector<Edge> edges;
  edges.reserve(static_cast<size_t>(stats->survivors));
  std::unordered_map<uint64_t, uint32_t> recovery;
  recovery.reserve(static_cast<size_t>(stats->survivors));
  // One regenerated block covers two bitmap words; never scan SipHash over the full graph.
  for (size_t word = 0; word < words; word += 2) {
    if (!(word & 4095) && cancelled && cancelled(context)) return 1;
    uint64_t bits = alive[word];
    if (word + 1 < words) bits |= uint64_t{alive[word + 1]} << 32;
    if (!bits) continue;
    uint32_t block[64][2];
    uint32_t base = first + static_cast<uint32_t>(word * 32);
    c29_reference_block(seed, base, block);
    ++stats->regenerated_blocks;
    while (bits) {
      unsigned bit = static_cast<unsigned>(__builtin_ctzll(bits));
      bits &= bits - 1;
      Edge edge{block[bit][0], block[bit][1]};
      edges.push_back(edge);
      // Ascending bitmap traversal preserves MOM's minimum-nonce collision recovery rule.
      recovery.emplace(key(edge), base + bit);
    }
  }
  if (cancelled && cancelled(context)) return 1;
  const auto cycles = mom_c29::find_cycles(edges, proof_size);
  stats->cycles = cycles.size();
  for (const auto &cycle : cycles) {
    if (cancelled && cancelled(context)) return 1;
    std::vector<uint32_t> nonces;
    nonces.reserve(proof_size);
    for (const Edge &edge : cycle) {
      const auto found = recovery.find(key(edge));
      if (found == recovery.end()) return -1;
      nonces.push_back(found->second);
    }
    std::sort(nonces.begin(), nonces.end());
    uint8_t hash[32];
    if (!mom_c29_verify(seed, nonces.data(), nonces.size(), hash)) return -1;
    ++stats->verified;
    if (proof && !proof(context, nonces.data(), nonces.size(), hash)) return 1;
  }
  return 0;
}

extern "C" int mom_c29_solver_selftest(void) {
  // Unit checks exercise the bitmap adapter; full runs supply a genuine MOM proof vector.
  uint64_t seed[4] = {};
  uint32_t alive[2] = {0, 0};
  MomC29SolveStats stats;
  if (mom_c29_solve_bitmap(seed, alive, 0, 0, 0, 42, nullptr, nullptr, nullptr, &stats) ||
      stats.survivors || stats.verified) return 1;
  alive[1] = 1;
  if (mom_c29_solve_bitmap(seed, alive, 2, (1u << 29) - 64, 33, 42,
                          nullptr, nullptr, nullptr, &stats) ||
      stats.survivors != 1 || stats.regenerated_blocks != 1 || stats.cycles) return 1;
  alive[1] = 2;
  if (mom_c29_solve_bitmap(seed, alive, 2, 0, 33, 42,
                          nullptr, nullptr, nullptr, &stats) != -1) return 1;
  if (mom_c29_solve_bitmap(seed, alive, 2, 1, 33, 42,
                          nullptr, nullptr, nullptr, &stats) != -1) return 1;
  auto cancel = [](void *) -> bool { return true; };
  if (mom_c29_solve_bitmap(seed, alive, 2, 0, 64, 42,
                          cancel, nullptr, nullptr, &stats) != 1) return 1;
  return 0;
}

// Bucket survivors already contain endpoints; MOM's unchanged forest finds candidate cycles.
extern "C" int mom_c29_find_endpoint_cycles(const uint32_t *uv, size_t count,
    unsigned proof_size, uint32_t *output, size_t capacity, size_t *cycle_count) {
  if (!cycle_count) return -1;
  *cycle_count = 0;
  if ((!uv && count) || !output || count > max_survivors ||
      (proof_size != 32 && proof_size != 42)) return -1;
  std::vector<Edge> edges;
  edges.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    if (uv[2*i] >= (1u<<29) || uv[2*i+1] >= (1u<<29)) return -1;
    edges.push_back({uv[2*i], uv[2*i+1]});
  }
  const auto found = mom_c29::find_cycles(edges, proof_size, flat_dictionary);
  if (found.size() > capacity) return -1;
  for (const auto &cycle : found) {
    for (size_t i = 0; i < cycle.size(); ++i) {
      size_t offset = ((*cycle_count)*proof_size+i)*2;
      output[offset] = cycle[i].u; output[offset+1] = cycle[i].v;
    }
    ++*cycle_count;
  }
  return 0;
}

extern "C" int mom_c29_find_endpoint_cycles_overlap(const uint32_t *uv, size_t count,
    unsigned proof_size, uint32_t *output, size_t capacity, size_t *cycle_count,
    MomC29Overlap overlap, void *context) {
  if (!overlap) return mom_c29_find_endpoint_cycles(uv, count, proof_size, output, capacity, cycle_count);
  if ((!uv && count) || count > max_survivors || !output || !cycle_count) return -1;
  // Only immutable endpoints and private result storage cross the thread boundary.
  std::vector<uint32_t> owned;
  if (count) owned.assign(uv, uv + count * 2);
  struct Search {
    const uint32_t *uv; size_t count; unsigned length; uint32_t *output;
    size_t capacity; size_t *cycles; int result;
  } search{owned.data(), count, proof_size, output, capacity, cycle_count, -1};
  auto run = [](void *opaque) -> void * {
    auto &s = *static_cast<Search *>(opaque);
    s.result = mom_c29_find_endpoint_cycles(s.uv, s.count, s.length, s.output, s.capacity, s.cycles);
    return nullptr;
  };
  pthread_t thread;
  if (pthread_create(&thread, nullptr, run, &search)) {
    // Low-resource fallback preserves proof correctness and still allows GPU prefetch.
    run(&search); overlap(context);
  } else {
    overlap(context);
    // Join before releasing either endpoints or result storage.
    if (pthread_join(thread, nullptr)) std::abort();
  }
  return search.result;
}
