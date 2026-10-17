// SPDX-License-Identifier: GPL-3.0-or-later
// C29 SipHash logic adapted from MoneroOcean's SYCL miner (2025-2026).
#ifndef MOM_PS5_C29_REFERENCE_H
#define MOM_PS5_C29_REFERENCE_H
#include <stdint.h>

// Scalar uint64 arithmetic independently checks the shader's low/high pair arithmetic.
static uint64_t c29_rotate(uint64_t value, unsigned bits) {
  return (value << bits) | (value >> (64 - bits));
}

// Match the stateful SipHash-2-4 walk in sycl/c29/search.inc.
static void c29_round(uint64_t v[4]) {
  v[0] += v[1];
  v[2] += v[3];
  v[1] = c29_rotate(v[1], 13) ^ v[0];
  v[3] = c29_rotate(v[3], 16) ^ v[2];
  v[0] = c29_rotate(v[0], 32);
  v[2] += v[1];
  v[0] += v[3];
  v[1] = c29_rotate(v[1], 17) ^ v[2];
  v[3] = c29_rotate(v[3], 21) ^ v[0];
  v[2] = c29_rotate(v[2], 32);
}

// C29 blocks restart from the seed; the 64 hashes within each block share state.
static void c29_reference_block(const uint64_t seed[4], uint32_t base, uint32_t edges[64][2]) {
  uint64_t v[4] = {seed[0], seed[1], seed[2], seed[3]}, hashes[64];
  for (unsigned i = 0; i < 64; ++i) {
    uint64_t nonce = (uint64_t)base + i;
    v[3] ^= nonce;
    c29_round(v);
    c29_round(v);
    v[0] ^= nonce;
    v[2] ^= 255;
    for (unsigned round = 0; round < 4; ++round) c29_round(v);
    hashes[i] = v[0] ^ v[1] ^ v[2] ^ v[3];
  }
  for (unsigned i = 0; i < 64; ++i) {
    uint64_t edge = i == 63 ? hashes[63] : hashes[i] ^ hashes[63];
    edges[i][0] = (uint32_t)edge & 0x1fffffff;
    edges[i][1] = (uint32_t)(edge >> 32) & 0x1fffffff;
  }
}
#endif
