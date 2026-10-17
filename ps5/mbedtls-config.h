// SPDX-License-Identifier: GPL-3.0-or-later
// Seed Mbed TLS's PSA entropy path from the native kernel CSPRNG provider in pool.cpp.
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT
