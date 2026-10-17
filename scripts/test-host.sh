#!/usr/bin/env bash
set -euo pipefail
# Run production boundary tests without connecting to or modifying a console.
cd "$(dirname "$0")/.."
mkdir -p build/host-tests
cxx=${CXX:-clang++}
python3 tests/native-web.py
for name in native-options native-gpu-options native-keyboard native-memory; do
  "$cxx" -std=c++20 -pthread -Ihomebrew -Iupstream/xmrig/src/3rdparty \
    "tests/$name.cpp" -o "build/host-tests/$name"
  "build/host-tests/$name"
done
"$cxx" -std=c++17 -pthread -Ihomebrew tests/native-gpu-runtime.cpp \
  homebrew/native-gpu.cpp -o build/host-tests/native-gpu-runtime
build/host-tests/native-gpu-runtime
# This validator needs the generated release starter configuration.
if [[ -f build/native-app/PPSA99531/assets/config.json ]]; then
  "$cxx" -std=c++20 -Ihomebrew -Iupstream/xmrig/src/3rdparty \
    tests/native-config.cpp -o build/host-tests/native-config
  build/host-tests/native-config
fi
