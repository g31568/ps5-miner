#!/usr/bin/env bash
set -euo pipefail
# Embed a local dashboard and deterministic icon in a standalone PS5 payload.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export LLVM_CONFIG="${LLVM_CONFIG:-/opt/homebrew/opt/llvm/bin/llvm-config}"
export PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-$ROOT/toolchain/ps5-payload-sdk}"
mkdir -p "$ROOT/build"
python3 - "$ROOT" <<'PY'
import json,struct,sys,zlib
from pathlib import Path
# Draw a simple X icon directly; no external assets or network dependencies.
root=Path(sys.argv[1]); size=512
rows=bytearray()
for y in range(size):
    rows.append(0)
    for x in range(size):
        mark=100<x<412 and 100<y<412 and (abs(x-y)<44 or abs(x+y-511)<44)
        rows.extend((182,246,221,255) if mark else (16,24,32,255))
def chunk(kind,data):
    return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
icon=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',size,size,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b'')
param={'applicationCategoryType':65536,'titleId':'XMRG18089','deeplinkUri':'http://127.0.0.1:18089/',
       'localizedParameters':{'defaultLanguage':'en-US','en-US':{'titleName':'XMRig Status'}}}
html=(root/'dashboard/index.html').read_text()
header='/* Dashboard and shortcut assets generated from workspace sources. */\n'
header+='static const char dashboard_html[] = '+json.dumps(html)+';\n'
header+='static const char shortcut_json[] = '+json.dumps(json.dumps(param))+';\n'
header+='static const unsigned char shortcut_icon[] = {'+','.join(map(str,icon))+'};\n'
(root/'build/dashboard-assets.h').write_text(header)
(root/'build/dashboard-icon.png').write_bytes(icon)
PY
# Embed the proven miner so browser restarts do not depend on the Mac.
test -f "$ROOT/build/xmrig-ps5.elf" || bash "$ROOT/scripts/build-xmrig.sh"
cat > "$ROOT/build/miner-image.S" <<ASM
.section .rodata
.global miner_image
.global miner_image_end
miner_image:
.incbin "$ROOT/build/xmrig-ps5.elf"
miner_image_end:
.section .note.GNU-stack,"",@progbits
ASM
"$PS5_PAYLOAD_SDK/bin/prospero-clang" -O2 -c "$ROOT/build/miner-image.S" -o "$ROOT/build/miner-image.o"
"$PS5_PAYLOAD_SDK/bin/prospero-clang++" -Wall -Wextra -O2 -std=c++11 -I"$ROOT/payload" -I"$ROOT/upstream/xmrig/src/3rdparty" \
    -c "$ROOT/payload/miner-control.cpp" -o "$ROOT/build/miner-control.o"
"$PS5_PAYLOAD_SDK/bin/prospero-clang" -Wall -Wextra -O2 -I"$ROOT/build" \
    -c "$ROOT/payload/status-dashboard.c" -o "$ROOT/build/status-dashboard.o"
"$PS5_PAYLOAD_SDK/bin/prospero-clang++" "$ROOT/build/status-dashboard.o" "$ROOT/build/miner-control.o" "$ROOT/build/miner-image.o" -ldl \
    -o "$ROOT/build/xmrig-status-dashboard.elf"
"$PS5_PAYLOAD_SDK/bin/prospero-clang" -Wall -Wextra -O2 -DINSTALL_ONLY -I"$ROOT/build" \
    -c "$ROOT/payload/status-dashboard.c" -o "$ROOT/build/status-install.o"
"$PS5_PAYLOAD_SDK/bin/prospero-clang++" "$ROOT/build/status-install.o" "$ROOT/build/miner-control.o" "$ROOT/build/miner-image.o" -ldl \
    -o "$ROOT/build/xmrig-status-install.elf"
