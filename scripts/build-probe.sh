#!/usr/bin/env bash
set -euo pipefail
# Use the local SDK and leave host architecture detection to the cross toolchain.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-$ROOT/toolchain/ps5-payload-sdk}"
export LLVM_CONFIG="${LLVM_CONFIG:-/opt/homebrew/opt/llvm/bin/llvm-config}"
cmake -S "$ROOT/probe" -B "$ROOT/build/ps5-probe" \
    -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build "$ROOT/build/ps5-probe" --target randomx-probe -j 4
