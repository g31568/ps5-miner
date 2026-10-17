// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MOM_PS5_C29_SOLVER_H
#define MOM_PS5_C29_SOLVER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  uint64_t survivors, regenerated_blocks, cycles, verified;
} MomC29SolveStats;
typedef bool (*MomC29Cancelled)(void *context);
typedef bool (*MomC29Proof)(void *context, const uint32_t *nonces,
                           size_t count, const uint8_t hash[32]);
// Select before starting a search; worker threads only read this setting.
void mom_c29_set_flat_dictionary(bool enabled);
// The caller owns the completed bitmap until this synchronous CPU search returns.
// Returns 0 on success, 1 on cancellation, -1 on invalid input, capacity or proof failure.
int mom_c29_solve_bitmap(const uint64_t seed[4], const uint32_t *alive,
                         size_t words, uint32_t first_nonce, uint32_t edge_count,
                         unsigned proof_size, MomC29Cancelled cancelled,
                         MomC29Proof proof, void *context, MomC29SolveStats *stats);
// Return endpoint cycles for GPU nonce recovery without regenerating survivors on the CPU.
int mom_c29_find_endpoint_cycles(const uint32_t *uv, size_t count, unsigned proof_size,
                                 uint32_t *cycles, size_t capacity, size_t *cycle_count);
// Search an owned endpoint copy while the owning event loop submits the next GPU seed.
typedef void (*MomC29Overlap)(void *context);
int mom_c29_find_endpoint_cycles_overlap(const uint32_t *uv, size_t count, unsigned proof_size,
    uint32_t *cycles, size_t capacity, size_t *cycle_count, MomC29Overlap overlap, void *context);
int mom_c29_solver_selftest(void);
#ifdef __cplusplus
}
#endif
#endif
