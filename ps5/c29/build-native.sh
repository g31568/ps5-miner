#!/usr/bin/env bash
set -euo pipefail

# This script runs in sdk.Dockerfile's Linux container, with the repository at /project.
export PATH="/builder/bin:/usr/lib/llvm-18/bin:$PATH"
project=/project
driver="$project/build/ps5-vulkan-radv"
work="$project/build/ps5-c29-native"
sdk="$driver/.deps/native/ps5-payload-sdk"
archive="$driver/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
native="$driver/tooling/native"
tool="$driver/build/host/ps5-native-tool"
mkdir -p "$work"
# Limit simultaneous compiler processes on small-memory builders.
cat > "$work/ninja-bounded" <<'SH'
#!/bin/sh
exec /usr/bin/ninja -j 2 "$@"
SH
chmod +x "$work/ninja-bounded"
export NINJA="$work/ninja-bounded"

# The bind mount retains macOS ownership; trust only these three owned source checkouts.
for directory in "$driver" "$project/build/PS5_Mesa" "$project/build/PS5_PayloadSDK"; do
  git config --global --add safe.directory "$directory"
done
cd "$driver"
bash tools/setup-native-dependencies.sh
bash tools/build-radv.sh release
mkdir -p "$work/obj" "$work/stubs" "$driver/build/host"

# Compile the upstream clean-room ELF/FSELF tools, without building its graphics samples.
zlib_root="$driver/.deps/native/zlib/root"
clang++ -std=c++20 -O2 -Wall -Wextra -Werror -I "$zlib_root/usr/include" \
  "$native/native_app_builder.cpp" "$native/self_container.cpp" \
  "$native/elf_object.cpp" "$native/sce_module_writer.cpp" \
  "$zlib_root/usr/lib/libz.a" -o "$tool"

# Use the same title CRT and C++ runtime as the driver's own RADV acceptance title.
cross_compile() {
  PS5_PAYLOAD_SDK="$sdk" sh "$driver/tooling/prospero-clang18" "$@"
}
cross_compile -std=c11 -O2 -Wall -Wextra -Werror -DMOM_PS5_RADV=1 \
  -I "$driver/.deps/native/radv-release/include" -I "$project/build/ps5-c29" \
  -c "$project/ps5/c29/probe.c" -o "$work/obj/probe.o"
for source in manager manager-test; do
  cross_compile -std=c11 -O2 -Wall -Wextra -Werror -I "$project/xmrig" \
    -c "$project/ps5/$source.c" -o "$work/obj/$source.o"
done
cross_compile -std=c11 -O2 -Wall -Wextra -Werror -I "$project/xmrig" \
  -c "$project/xmrig/crypto/randomx/blake2/blake2b.c" -o "$work/obj/blake2b.o"
cross_compile -std=c++17 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti \
  -c "$project/ps5/c29/solver.cpp" -o "$work/obj/solver.o"
# Cross-build pinned TLS libraries; use hardware entropy rather than platform PRNG fallbacks.
export PS5_PAYLOAD_SDK="$sdk"
cat > "$work/prospero-cmake-cc" <<SH
#!/bin/sh
exec sh "$driver/tooling/prospero-clang18" "\$@"
SH
chmod +x "$work/prospero-cmake-cc"
cmake --fresh -S "$project/build/MbedTLS" -B "$project/build/ps5-mbedtls" \
  -DCMAKE_SYSTEM_NAME=FreeBSD -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  -DCMAKE_C_COMPILER="$work/prospero-cmake-cc" \
  -DCMAKE_CXX_COMPILER="$work/prospero-cmake-cc" \
  -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF \
  -DCMAKE_C_FLAGS="-fPIC -DMBEDTLS_USER_CONFIG_FILE=\\\"$project/ps5/mbedtls-config.h\\\""
cmake --build "$project/build/ps5-mbedtls" -j 2
cross_compile -std=c++17 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti \
  -DMOM_POOL_MBEDTLS=1 -DMBEDTLS_USER_CONFIG_FILE="\"$project/ps5/mbedtls-config.h\"" \
  -I "$project/build/MbedTLS/include" -c "$project/ps5/pool.cpp" -o "$work/obj/pool.o"
for source in app_crt app_cpp_runtime; do
  cross_compile -std=c++20 -O2 -fno-exceptions -fno-rtti \
    -c "$native/$source.cpp" -o "$work/obj/$source.o"
done

# Import facades name console AGC modules; they contain no replacement GPU implementation.
for entry in 'libSceAgc agc_canary_link_stub' 'libSceAgcDriver agc_driver_canary_link_stub'; do
  read -r library source <<< "$entry"
  cross_compile -std=c11 -O2 -fPIC \
    -c "$driver/vendor/ps5/sdk/stubs/$source.c" -o "$work/obj/$library.o"
  "$sdk/bin/prospero-lld" --shared -soname "$library.prx" \
    -o "$work/stubs/$library.so" "$work/obj/$library.o"
done

# Keep Mesa's dispatch table members and apply its PS5 platform heap/libc/thread bindings.
source "$driver/tools/radv-link.sh"
radv_link_recipe "$driver" "$sdk" "$archive"
link_probe() {
  "$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" \
    --version-script "$native/app-symbols.map" --exclude-libs=ALL -e _start "$@" \
    -o "$work/c29-probe-pie.elf" "$work/obj/app_crt.o" "$work/obj/app_cpp_runtime.o" \
    "$work/obj/probe.o" "$work/obj/manager.o" "$work/obj/manager-test.o" \
    "$work/obj/blake2b.o" "$work/obj/solver.o" "$work/obj/pool.o" \
    "$project/build/ps5-mbedtls/library/libmbedtls.a" \
    "$project/build/ps5-mbedtls/library/libmbedx509.a" \
    "$project/build/ps5-mbedtls/library/libmbedcrypto.a" \
    "$work/stubs/libSceAgc.so" "$work/stubs/libSceAgcDriver.so" \
    "${radv_link_inputs[@]}" --as-needed "$sdk"/target/lib/*.so
}
link_probe
# Undefined weak Mesa table entries mean NULL/fallback, not mandatory system-module imports.
"$sdk/bin/llvm-nm" -D --undefined-only --format=posix "$work/c29-probe-pie.elf" \
  > "$work/undefined-symbols.txt"
python3 - "$work" <<'PY'
from pathlib import Path
import re
import sys
work = Path(sys.argv[1])
names = []
prefixes = ("radv_", "vk_", "wsi_", "ctx_roll_", "rmv_", "rra_", "utrace_",
            "sqtt_", "annotate_", "threaded_")
for line in (work / "undefined-symbols.txt").read_text().splitlines():
    name, kind, *_ = line.split()
    if kind not in ("w", "v"):
        continue
    if not name.startswith(prefixes) or not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name):
        raise SystemExit("Unexpected weak symbol: " + name)
    names.append(name)
(work / "mesa-weak-null.ld").write_text(
    "".join("HIDDEN(" + name + " = 0);\n" for name in sorted(names)))
print("Resolving", len(names), "optional Mesa entry points to NULL")
PY
link_probe -T "$work/mesa-weak-null.ld"
"$tool" link --in "$work/c29-probe-pie.elf" --out "$work/c29-probe.elf" \
  --stub-dir "$sdk/target/lib" --stub "$work/stubs/libSceAgc.so" \
  --stub "$work/stubs/libSceAgcDriver.so" --module-sdk 0x02000009 \
  --companion-sdk 0x08050001 --file-name eboot.elf

# Package a separate, offline probe title. This does not install or launch it on the console.
app="$work/PPSA99529"
mkdir -p "$app/sce_sys" "$app/sce_module"
"$tool" self --sign --in "$work/c29-probe.elf" --out "$app/eboot.bin" --magic 0x1D3D154F
python3 - "$driver/sce_sys/param-radv.json" "$app/sce_sys/param.json" <<'PY'
import json
import sys
with open(sys.argv[1]) as source:
    metadata = json.load(source)
metadata.update(titleId="PPSA99529", conceptId="99529",
                contentId="UP9000-PPSA99529_00-MOMC29PROBE00001")
metadata["localizedParameters"]["en-US"]["titleName"] = "C29 GPU Probe"
with open(sys.argv[2], "w") as output:
    json.dump(metadata, output, indent=2)
    output.write("\n")
PY
if [[ ! -f $driver/runtime/libc.prx ]]; then
  bash "$driver/tools/rebuild-libc.sh"
fi
(cd "$driver/runtime" && sha256sum --check --strict libc.prx.sha256)
cp "$driver/runtime/libc.prx" "$app/sce_module/libc.prx"
# ShadowMountPlus requires a launcher icon before it can register a title.
python3 "$project/ps5/c29/make-icon.py" "$app/sce_sys/icon0.png"
# Package public trust roots only; wallet configuration stays outside the distributable ZIP.
cp "$project/build/ps5-c29/cacert.pem" "$app/sce_sys/cacert.pem"
"$tool" self --inspect --file "$app/eboot.bin" > "$work/inspect.txt"

# Bind the produced binary to its source and driver revisions for later console validation.
{
  echo 'Target firmware: 13.60 (runtime evidence recorded separately)'
  for directory in "$driver" "$project/build/PS5_Mesa" "$project/build/PS5_PayloadSDK" \
    "$project/build/MbedTLS" "$project/build/MbedTLS/framework"; do
    printf '%s %s\n' "$(basename "$directory")" "$(git -C "$directory" rev-parse HEAD)"
  done
  sha256sum "$project/ps5/c29/probe.c" "$project/ps5/c29/seed.comp" \
    "$project/ps5/manager.c" "$project/ps5/manager.h" "$project/ps5/manager-test.c" \
    "$project/ps5/c29/solver.cpp" "$project/ps5/c29/solver.h" \
    "$project/ps5/c29/solver-probe.h" "$project/sycl/c29/cycle.h" \
    "$project/ps5/pool.cpp" "$project/ps5/pool.h" "$project/ps5/mbedtls-config.h" \
    "$project/ps5/c29/pool-run.h" "$project/build/ps5-mbedtls/library/"*.a \
    "$project/ps5/third-party/nlohmann/json.hpp" "$app/sce_sys/cacert.pem" \
    "$project/xmrig/crypto/randomx/blake2/blake2b.c" \
    "$project/ps5/c29/reference.h" "$project/ps5/c29/radv_dispatch.h" \
    "$project/ps5/c29/build-native.sh" "$project/ps5/c29/sdk.Dockerfile" \
    "$project/ps5/c29/make-icon.py" "$app/sce_sys/icon0.png" \
    "$project/ps5/c29/siphash.glsl" "$project/ps5/c29/trim.comp" \
    "$project/ps5/c29/bucket.comp" "$project/ps5/c29/bucket-worker.h" \
    "$project/build/ps5-c29/bucket.spv.inc" "$project/ps5/c29/trim-probe.h" "$project/build/ps5-c29/trim.spv.inc" \
    "$project/build/ps5-c29/seed.spv.inc" "$archive" "$app/eboot.bin"
} > "$work/PROVENANCE.txt"
# Include provenance beside the title in a portable archive for later installation.
python3 - "$work" <<'PY'
from pathlib import Path
import sys
import zipfile
work = Path(sys.argv[1])
with zipfile.ZipFile(work / "C29-GPU-Probe-PPSA99529.zip", "w", zipfile.ZIP_DEFLATED) as archive:
    for file in sorted((work / "PPSA99529").rglob("*")):
        if file.is_file():
            archive.write(file, file.relative_to(work))
    archive.write(work / "PROVENANCE.txt", "PROVENANCE.txt")
PY
printf 'Built native RADV probe: %s\n' "$app"
