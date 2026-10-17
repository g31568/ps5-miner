#!/usr/bin/env bash
set -euo pipefail

# Keep third-party checkouts and every native artifact inside the ignored build tree.
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cache="$root/build"
image=mom-ps5-radv-builder:llvm18
# Respect the selected Docker engine; no project-specific context is required.
export DOCKER_CONTEXT="${PS5_DOCKER_CONTEXT:-${DOCKER_CONTEXT:-default}}"
mkdir -p "$cache/ps5-c29"
# Stage only public trust roots from the local OpenSSL CA store for native TLS verification.
ca_bundle=${MOM_PS5_CA_BUNDLE:-/opt/homebrew/etc/openssl@3/cert.pem}
if [[ ! -f $ca_bundle ]]; then ca_bundle=/etc/ssl/certs/ca-certificates.crt; fi
[[ -f $ca_bundle ]] || { echo 'Set MOM_PS5_CA_BUNDLE to a PEM CA bundle' >&2; exit 1; }
cp "$ca_bundle" "$cache/ps5-c29/cacert.pem"

# Never reset an existing dependency checkout or follow a mutable branch silently.
fetch_pin() {
  local name=$1 url=$2 revision=$3
  local directory="$cache/$name"
  if [[ ! -d $directory/.git ]]; then
    git clone --filter=blob:none --no-checkout "$url" "$directory"
    git -C "$directory" fetch --depth 1 origin "$revision"
    git -C "$directory" checkout --detach "$revision"
  fi
  [[ $(git -C "$directory" rev-parse HEAD) == "$revision" ]] || {
    echo "Dependency revision mismatch: $directory" >&2
    exit 1
  }
  [[ -z $(git -C "$directory" status --porcelain --untracked-files=no) ]] || {
    echo "Dependency has tracked changes: $directory" >&2
    exit 1
  }
}

fetch_pin ps5-vulkan-radv https://github.com/mihawk-99/PS5_Vulkan.git \
  252bf912a745e3566a93b69b098bdc8ec4048837
fetch_pin PS5_PayloadSDK https://github.com/mihawk-99/PS5_PayloadSDK.git \
  b83202be73e930050e00de0f1b4d9ec46c0391bf
fetch_pin PS5_Mesa https://github.com/mihawk-99/PS5_Mesa.git \
  47e9eb61ee7e1112194557e1e047178562827bc6
fetch_pin MbedTLS https://github.com/Mbed-TLS/mbedtls.git \
  e185d7fd85499c8ce5ca2a54f5cf8fe7dbe3f8df
# Its framework revision is pinned by the parent checkout's gitlink.
git -C "$cache/MbedTLS" submodule update --init --depth 1

# Prefetch Mesa's pinned wrap files on the host; container HTTPS can be unavailable.
wrap_cache="$cache/ps5-vulkan-radv/.deps/work/radv-src/subprojects/packagecache"
mkdir -p "$wrap_cache"
fetch_archive() {
  local name=$1 url=$2 expected=$3
  if [[ ! -f "$wrap_cache/$name" ]]; then
    curl -fL --retry 2 "$url" -o "$wrap_cache/$name.download"
    [[ $(shasum -a 256 "$wrap_cache/$name.download" | cut -d ' ' -f 1) == "$expected" ]] || {
      echo "Dependency checksum mismatch: $name" >&2; exit 1;
    }
    mv "$wrap_cache/$name.download" "$wrap_cache/$name"
  fi
  [[ $(shasum -a 256 "$wrap_cache/$name" | cut -d ' ' -f 1) == "$expected" ]] || {
    echo "Dependency checksum mismatch: $name" >&2; exit 1;
  }
}
fetch_archive zlib-1.3.1.tar.gz https://zlib.net/fossils/zlib-1.3.1.tar.gz \
  9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
fetch_archive zlib_1.3.1-1_patch.zip \
  https://github.com/mesonbuild/wrapdb/releases/download/zlib_1.3.1-1/zlib_1.3.1-1_patch.zip \
  e79b98eb24a75392009cec6f99ca5cdca9881ff20bfa174e8b8926d5c7a47095

# GLSL is compiled locally; the console's RADV/ACO compiler consumes this embedded SPIR-V.
glslc --target-env=vulkan1.0 -O -mfmt=c "$root/ps5/c29/seed.comp" \
  -o "$cache/ps5-c29/seed.spv.inc"
# Embed the same bounded-memory trimmer used by desktop differential checks.
glslc --target-env=vulkan1.0 -O -mfmt=c "$root/ps5/c29/trim.comp" \
  -o "$cache/ps5-c29/trim.spv.inc"
# Compile the bucket pipeline used by the same differential checks.
glslc --target-env=vulkan1.0 -O -mfmt=c "$root/ps5/c29/bucket.comp" \
  -o "$cache/ps5-c29/bucket.spv.inc"
docker build -t "$image" -f "$root/ps5/c29/sdk.Dockerfile" "$root/ps5/c29"
docker run --rm --mount "type=bind,src=$root,dst=/project" \
  --workdir /project/build/ps5-vulkan-radv "$image" \
  bash /project/ps5/c29/build-native.sh
