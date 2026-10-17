# PS5 XMR Miner

[![Build PS5 miner](https://github.com/g31568/ps5-miner/actions/workflows/build.yml/badge.svg)](https://github.com/g31568/ps5-miner/actions/workflows/build.yml)

Native **RandomX CPU + C29 Vulkan GPU mining** in one PS5 homebrew title. Built around XMRig, MO-Miner's C29 solver and PS5 Vulkan/RADV. The app appears as **XMR Miner**, title ID `PPSA99531`.

**Experimental; runtime testing targets jailbroken firmware 13.60.** Other firmware and loaders are unverified. The release is a homebrew folder ZIP, not a retail PKG.

## Install

1. Download a [release](https://github.com/g31568/ps5-miner/releases) or a successful [Actions build artifact](https://github.com/g31568/ps5-miner/actions/workflows/build.yml), and verify its checksums.
2. Extract the app ZIP and copy the entire `PPSA99531/` folder to your loader's homebrew directory, commonly `/data/homebrew/PPSA99531/`. Rescan and launch **XMR Miner**.
3. Open Options, scan the local editor QR, and set **your payout wallet for both engines**. Mining starts stopped; both starter configurations use `YOUR_WALLET`.
4. L1 starts/stops CPU; R1 starts/stops GPU; Square controls both. Stop GPU before saving its settings.

See the full [installation/update guide](docs/INSTALLATION.md) for prerequisites, controls and troubleshooting.

**Security:** the HTTP editor has no authentication or encryption. Anyone who can reach it on the LAN can read settings, redirect payouts and control mining. CPU pool transport is plaintext; GPU defaults use verified TLS. Use an isolated trusted network and read [SECURITY.md](SECURITY.md). Never enter a wallet seed or private key.

## Features and limits

- Independent CPU/GPU pools, controls and status; controller dashboard plus locally served web editor. QR encoding happens locally.
- CPU defaults to eight workers, RandomX fast mode, hardware AES, interpreter and prefetch mode 3. Donation defaults to 0 and is editable.
- Optimized C29 defaults: 68 trim rounds, 1,024 trim threads, 256/256 seed/redistribution, stagger 32 and direct writes.
- CPU speed is hashes/s; GPU speed is graphs/s. Their rates are separate and cannot be added.
- GPU requires the MO-Miner C29 pool protocol; a RandomX-only P2Pool endpoint cannot supply C29 work.
- Saved settings persist in the app's private `/download0` sandbox. GPU cancellation is cooperative; a fence timeout requires relaunch before another GPU start.
- Power Saver capability is declared in title metadata, but reduced power consumption has not been verified.

## Build

The reference development host is Apple Silicon macOS with command-line tools, Homebrew and Docker. The GitHub workflow cross-builds on Ubuntu 24.04 using LLVM and a Linux container.

```sh
git clone https://github.com/g31568/ps5-miner.git
cd ps5-miner
# Install prerequisites and configure LLVM/Docker as described in BUILDING.md.
bash scripts/setup.sh
bash scripts/setup-gpu.sh
python3 scripts/build-release.py --version 2026.10.17
```

[BUILDING.md](BUILDING.md) explains prerequisites, dependency pins, build stages and output. Generated app ZIP, source archive and checksums appear under `dist/<version>/`. CI builds on pushes, pull requests and manual dispatch; it never deploys or starts mining.

## Documentation

- [Installation and updates](docs/INSTALLATION.md)
- [Configuration reference](docs/CONFIGURATION.md)
- [Repository structure and runtime architecture](docs/STRUCTURE.md)
- [Build and packaging](BUILDING.md)
- [Security boundaries and reporting](SECURITY.md)
- [Third-party licenses and references](THIRD_PARTY.md)
- [Historical console validation](VALIDATION.md) and [benchmarks](benchmark.md)

## Development checks

```sh
bash scripts/setup.sh
bash scripts/test-host.sh
```

Host tests exercise real HTTP sockets, browser-origin/framing restrictions, settings validation, controller input, memory failure paths and GPU lifecycle. They do not establish pool acceptance or console runtime stability. Read the developer deployment scripts before running them; some change console settings, launch mining or update system metadata.

## License and credits

Distributed under **GPL-3.0-or-later**, consistent with XMRig, the native framework and this integration. Original third-party notices and additional terms remain in force; see [LICENSE](LICENSE), [THIRD_PARTY.md](THIRD_PARTY.md) and `licenses/`. This is an independent homebrew project, unaffiliated with Sony, XMRig, MoneroOcean or the referenced upstream authors.
