#!/usr/bin/env bash
set -euo pipefail
# Experimental CPU-only port; a successful link still needs console validation.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-$ROOT/toolchain/ps5-payload-sdk}"
export LLVM_CONFIG="${LLVM_CONFIG:-/opt/homebrew/opt/llvm/bin/llvm-config}"
cmake -S "$ROOT/upstream/libuv" -B "$ROOT/build/ps5-libuv-1.48" \
    -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DLIBUV_BUILD_TESTS=OFF -DLIBUV_BUILD_BENCH=OFF \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build "$ROOT/build/ps5-libuv-1.48" --target uv_a -j 4
# Keep native assembly experiments separate from the payload build.
XMRIG_BUILD="${PS5_XMRIG_BUILD_DIR:-$ROOT/build/ps5-xmrig}"
XMRIG_ASM="${PS5_XMRIG_ASM:-OFF}"
# RelWithDebInfo avoids upstream's Release strip command targeting the TLS name.
cmake -S "$ROOT/upstream/xmrig" -B "$XMRIG_BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DUV_INCLUDE_DIR="$ROOT/upstream/libuv/include" \
    -DUV_LIBRARY="$ROOT/build/ps5-libuv-1.48/libuv.a" \
    -DWITH_HWLOC=OFF -DWITH_TLS=OFF -DWITH_HTTP=ON -DWITH_OPENCL=OFF \
    -DWITH_CUDA=OFF -DWITH_MSR=OFF -DWITH_DMI=OFF -DWITH_ASM="$XMRIG_ASM" \
    -DWITH_VAES=OFF -DWITH_KAWPOW=OFF -DWITH_GHOSTRIDER=OFF
cmake --build "$XMRIG_BUILD" -j 4
# Keep the debug executable and provide a smaller payload with an ELF suffix.
OUTPUT="${PS5_XMRIG_BUILD_DIR:+$XMRIG_BUILD/xmrig.elf}"
OUTPUT="${OUTPUT:-$ROOT/build/xmrig-ps5.elf}"
cp "$XMRIG_BUILD/xmrig-notls" "$OUTPUT"
"$PS5_PAYLOAD_SDK/bin/prospero-strip" "$OUTPUT"
