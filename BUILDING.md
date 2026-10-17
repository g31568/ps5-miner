# Build PS5 XMR Miner

The project is **PS5 XMR Miner**. The app appears as **XMR Miner**, title ID
`PPSA99531`. The output is a homebrew folder ZIP, not a retail PKG.

## Requirements

The supported build path is macOS on Apple Silicon, with Apple command-line
tools, Homebrew, Python 3, and a running Docker engine. Ubuntu 24.04 is the CI cross-build host; see the workflow below. Windows native
builds are unverified.

Install the host tools:

```sh
xcode-select --install
brew install python cmake llvm shaderc pkg-config libuv openssl@3
```

Install and start a Docker engine such as Docker Desktop or OrbStack. The first
GPU dependency build downloads and compiles Mesa and can take a while. It uses
at most two parallel Ninja compiler jobs to limit memory pressure. Leave several
GB of free disk space for sources, containers and intermediate files.

## Build from the source release

Extract `PS5-XMR-Miner-<version>-source.tar.gz`, then enter the extracted folder.
Run every command below from that folder. Do not copy an existing `build/` or
`toolchain/` directory from another machine.

```sh
# Host compilers use the Homebrew LLVM installation.
export LLVM_CONFIG="$(brew --prefix llvm)/bin/llvm-config"
export PATH="$(brew --prefix llvm)/bin:$PATH"

# Choose the context shown by `docker context ls`.
export PS5_DOCKER_CONTEXT=default
docker --context "$PS5_DOCKER_CONTEXT" info

# Fetch pinned CPU sources, the native framework and SDK; apply PS5 patches.
bash scripts/setup.sh

# Fetch pinned GPU sources and build RADV, platform libraries, TLS and stubs.
bash scripts/setup-gpu.sh

# Build the combined app and create public release artifacts.
python3 scripts/build-release.py --version 2026.10.17
```

For OrbStack, use `export PS5_DOCKER_CONTEXT=orbstack` instead. The GPU step builds
the `mom-ps5-radv-builder:llvm18` Docker image itself. It also builds an offline
C29 probe to generate the linker metadata needed by the combined app; it does
not install or launch that probe on a PS5.

The setup scripts stop if an existing dependency has a different revision.
Use a new source directory to switch revisions rather than resetting local work.
Pinned source revisions are listed in `THIRD_PARTY.md`.

## Output

`dist/2026.10.17/` contains:

- `PS5-XMR-Miner-2026.10.17.zip`: the `PPSA99531/` app folder.
- `PS5-XMR-Miner-2026.10.17-source.tar.gz`: app sources, patches and build scripts.
- `BUILDING.md`, `README.md`, `THIRD_PARTY.md` and `LICENSE`.
- `SHA256SUMS`: checksums for these files.

Verify the download from inside that directory:

```sh
shasum -a 256 -c SHA256SUMS
```

After dependencies are built, `python3 scripts/build-homebrew.py` rebuilds just
the app into `build/native-app/PPSA99531.zip`. Release packaging forces the
production defaults: eight CPU threads, RandomX fast mode, AES enabled,
interpreter execution, prefetch mode 3, CPU yielding, and the existing C29 GPU
profile. CPU donation defaults to 0 and remains editable.

The dashboard's sans-serif glyphs are checked in as `homebrew/native-ui-font.hpp`.
Normal builds require no font runtime. To regenerate after a typography change,
run `python3 scripts/build-ui-font.py` with Pillow installed. The bundled Bitstream
Vera fonts and license live in `homebrew/vendor/vera/`; packaging includes the license.
For a host layout preview, compile `tests/native-dashboard-preview.cpp` with
`-std=c++20 -Ihomebrew -Iupstream/native-app/src -Iupstream/xmrig/src/3rdparty`, then redirect its output to a
PPM file. Arguments `1` and `2` preview stopped and connecting/fault states;
`3` previews Settings with maximum-width input. Compile with
`-fsanitize=address,undefined` to check the host drawing path for memory errors.
Arguments `4` and `5` preview the GPU pool and TLS settings pages. Host field
validation is exercised by `tests/native-gpu-options.cpp` with
`-Ihomebrew -Iupstream/xmrig/src/3rdparty`.

Both pool configurations contain `YOUR_WALLET`. CPU defaults to the public
RandomX endpoint `gulf.moneroocean.stream:128`; GPU uses its separate C29/TLS
endpoint `gulf.moneroocean.stream:10128`. Set your wallet and preferred pools in
the app before starting. GPU mining requires a C29-compatible pool.

## Install and update

Runtime testing targets a jailbroken PS5 on firmware 13.60 with a compatible
homebrew loader. Extract the app ZIP and copy the complete `PPSA99531/` folder to
the homebrew directory scanned by your loader, commonly
`/data/homebrew/PPSA99531/`. Close the app before replacing files, then rescan
through the loader and launch **XMR Miner**. Square starts or stops both miners.
Scan the QR code for the local web configuration page.

Saved CPU and GPU settings live in the app's private `download0` sandbox. Keep
that data when updating. The release contains no private wallet or proxy settings.

On an existing installation, a scan can leave the system's old name and icon
cached even when the app folder is updated. The developer metadata helper updates
only `PPSA99531`, backs up metadata, and updates the title's SQLite records without
uninstalling it. It requires the FTP service on port 2121, the title control
service on 9111, and the payload loader on 9021 used by this development setup.
These services are separate from the app and are not prerequisites for building.
After building, close the app and set your console address to run it:

```sh
PS5_HOST=192.168.1.50 python3 scripts/refresh-title-metadata.py
```

## Checks

```sh
python3 tests/native-web.py
clang++ -std=c++17 -pthread -Ihomebrew tests/native-gpu-runtime.cpp \
  homebrew/native-gpu.cpp -o build/test-native-gpu
build/test-native-gpu
```

`VALIDATION.md` records earlier console mining and control tests. A successful
build or SELF integrity check does not establish pool acceptance or long-term
stability for a new build. The scripts that copy settings from other installed
titles are development tools for the original test console, not release setup
steps.

## Troubleshooting

- **Docker connection failure:** start the engine and check `docker context ls`.
  Pass the same `PS5_DOCKER_CONTEXT` to setup and subsequent builds.
- **`glslc` missing:** install Homebrew `shaderc` and check your `PATH`.
- **Missing `radv-release` archive, TLS libraries, stubs or `mesa-weak-null.ld`:**
  complete `bash scripts/setup-gpu.sh` before building the combined app.
- **Missing CA bundle:** set `MOM_PS5_CA_BUNDLE` to a public PEM CA bundle before
  GPU setup, for example `$(brew --prefix)/etc/openssl@3/cert.pem`.
- **Unexpected source revision or patch conflict:** extract a fresh source
  archive and rerun setup. The scripts preserve existing dependency edits.
- **SDK archive checksum failure:** remove only `toolchain/sdk.zip`, then rerun
  setup to download and verify it again.

## GitHub Actions and Linux

`.github/workflows/build.yml` runs automatically on pushes and pull requests, and can be dispatched manually. It uses Ubuntu 24.04, read-only repository permission, commit-pinned checkout/upload actions, and no console credentials. It installs Clang/LLVM 18, CMake, Ninja, glslc, libuv/OpenSSL/zlib development packages, runs host tests, fetches pinned dependencies, cross-builds RADV in Docker, and packages the full title. Artifacts contain app ZIP, source archive, docs and checksums; these are build artifacts, not automatically published GitHub Releases.

For a matching Linux build, install the packages shown in the workflow, set `LLVM_CONFIG=/usr/bin/llvm-config-18`, ensure Docker is running, then run the same three setup/build commands. CA roots default to `/etc/ssl/certs/ca-certificates.crt`. Do not reuse macOS-generated CMake caches on Linux. Compilation and SELF checks still require a separate on-console validation for each binary.

## Source and redistribution

The source archive contains integration sources, original copied-code notices, patches, dependency-fetch/build recipes and CI. Exact external dependency revisions are in `THIRD_PARTY.md`; setup requires network access to retrieve them. Keep corresponding upstream sources and license notices available when redistributing a modified binary, and include your modifications and build instructions. A source recipe alone should not be described as a self-contained offline source bundle.
