// SPDX-License-Identifier: GPL-3.0-or-later
// C29 SipHash adapted from MoneroOcean SYCL miner (2025-2026).
// Low/high pairs avoid requiring shaderInt64 on the native PS5 driver.
uvec2 add64(uvec2 a, uvec2 b) {
  uint low = a.x + b.x;
  return uvec2(low, a.y + b.y + uint(low < a.x));
}

// SipHash uses rotations below 32 and an exact 32-bit half swap.
uvec2 rotate64(uvec2 value, uint bits) {
  if (bits == 32) return value.yx;
  return (value << bits) | (value.yx >> (32 - bits));
}

void sipRound(inout uvec2 a, inout uvec2 b, inout uvec2 c, inout uvec2 d) {
  a = add64(a, b);
  c = add64(c, d);
  b = rotate64(b, 13) ^ a;
  d = rotate64(d, 16) ^ c;
  a = rotate64(a, 32);
  c = add64(c, b);
  a = add64(a, d);
  b = rotate64(b, 17) ^ c;
  d = rotate64(d, 21) ^ a;
  c = rotate64(c, 32);
}

