#!/usr/bin/env bash
set -euo pipefail
# Compile the configured local P2Pool JSON into a small installer payload.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export LLVM_CONFIG="${LLVM_CONFIG:-/opt/homebrew/opt/llvm/bin/llvm-config}"
export PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-$ROOT/toolchain/ps5-payload-sdk}"
mkdir -p "$ROOT/build"
python3 - "$ROOT" <<'PY'
import json
import sys
from pathlib import Path
# Validate and encode JSON as a C string rather than interpolating shell text.
root = Path(sys.argv[1])
config = json.loads((root / 'config/xmrig-p2pool.json').read_text())
content = json.dumps(config, indent=4) + '\n'
(root / 'build/p2pool-config.h').write_text(
    '/* Config bytes for the PS5 uploader. */\nstatic const char p2pool_config[] = ' + json.dumps(content) + ';\n')
PY
"$PS5_PAYLOAD_SDK/bin/prospero-clang" -I"$ROOT/build" -O2 \
    "$ROOT/payload/config-upload.c" -o "$ROOT/build/p2pool-config-upload.elf"
