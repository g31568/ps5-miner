#!/usr/bin/env bash
set -euo pipefail
# Pin the exact source revisions tested in this experiment; preserve local edits.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/upstream" "$ROOT/toolchain" "$ROOT/logs"
fetch_source() {
    local name="$1" url="$2" revision="$3"
    if [[ ! -d "$ROOT/upstream/$name/.git" ]]; then
        git init "$ROOT/upstream/$name"
        git -C "$ROOT/upstream/$name" remote add origin "$url"
        git -C "$ROOT/upstream/$name" fetch --depth 1 origin "$revision"
        git -C "$ROOT/upstream/$name" checkout --detach FETCH_HEAD
    fi
    [[ "$(git -C "$ROOT/upstream/$name" rev-parse HEAD)" == "$revision" ]] || {
        echo "Unexpected revision in upstream/$name; preserving it and stopping." >&2
        exit 1
    }
}
fetch_source xmrig https://github.com/xmrig/xmrig.git b2ca72480c58d197e18c885d9fc1a0c8d517e60a
fetch_source native-app https://github.com/blackbearreloaded/ps5-native-app-boilerplate.git 2f672d1c2f508e26f82ce6e27cef289a0861413c
fetch_source libuv https://github.com/libuv/libuv.git e9f29cb984231524e3931aa0ae2c5dae1a32884e
if [[ ! -f "$ROOT/toolchain/sdk.zip" ]]; then
    curl -fL --retry 2 -o "$ROOT/toolchain/sdk.zip" \
        https://github.com/ps5-payload-dev/sdk/releases/download/v0.43/ps5-payload-sdk.zip
fi
# Verify the downloaded archive against the SHA-256 recorded during this build.
[[ "$(shasum -a 256 "$ROOT/toolchain/sdk.zip" | cut -d ' ' -f 1)" == \
   a9cc9929f21b2b2c5d5b309f3bab4997067c45281c0622cf4838b1aecba66fcb ]] || {
    echo 'SDK archive checksum mismatch.' >&2
    exit 1
}
if [[ ! -d "$ROOT/toolchain/ps5-payload-sdk" ]]; then
    unzip -q "$ROOT/toolchain/sdk.zip" -d "$ROOT/toolchain"
fi
for name in xmrig libuv; do
    if git -C "$ROOT/upstream/$name" apply --reverse --check "$ROOT/patches/$name-ps5.patch" 2>/dev/null; then
        echo "$name PS5 patch already applied."
    else
        git -C "$ROOT/upstream/$name" apply --check "$ROOT/patches/$name-ps5.patch"
        git -C "$ROOT/upstream/$name" apply "$ROOT/patches/$name-ps5.patch"
    fi
done
