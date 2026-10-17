# Third-party components

PS5 XMR Miner is distributed under GPL-3.0-or-later; see `LICENSE`.
The release includes upstream license texts in `PPSA99531/licenses/`.

| Component | Source revision | Source |
| --- | --- | --- |
| XMRig | `b2ca72480c58d197e18c885d9fc1a0c8d517e60a` | https://github.com/xmrig/xmrig |
| libuv | `e9f29cb984231524e3931aa0ae2c5dae1a32884e` | https://github.com/libuv/libuv |
| Native app framework | `2f672d1c2f508e26f82ce6e27cef289a0861413c` | https://github.com/blackbearreloaded/ps5-native-app-boilerplate |
| Payload SDK | release `v0.43`, SHA-256 in `scripts/setup.sh` | https://github.com/ps5-payload-dev/sdk |
| PS5 Vulkan / RADV integration | `252bf912a745e3566a93b69b098bdc8ec4048837` | https://github.com/mihawk-99/PS5_Vulkan |
| PS5 Mesa / RADV / ACO | `47e9eb61ee7e1112194557e1e047178562827bc6` | https://github.com/mihawk-99/PS5_Mesa |
| GPU payload SDK and platform layer | `b83202be73e930050e00de0f1b4d9ec46c0391bf` | https://github.com/mihawk-99/PS5_PayloadSDK |
| Mbed TLS | `e185d7fd85499c8ce5ca2a54f5cf8fe7dbe3f8df` | https://github.com/Mbed-TLS/mbedtls |

The GPU bootstrap pins zlib 1.3.2 for the native tools and Mesa’s zlib 1.3.1
wrap (including its Meson patch), verifying each source archive. The SDK's
C++ runtime and LLVM compiler builtins retain their upstream LLVM licenses.
See the PS5 Vulkan upstream `NOTICE.md` for its runtime dependencies.

The copied C29 solver and Blake2 implementation come from MO-Miner/XMRig;
their source headers retain their original notices. The vendored QR encoder is
Nayuki's MIT-licensed QR Code generator (`homebrew/vendor/qrcodegen/`).
The vendored nlohmann/json header retains its MIT license notice.

`payload/refresh-title-metadata.c` optionally links SQLite 3.50.4 (public domain),
downloaded with a pinned checksum by `scripts/refresh-title-metadata.py`.
This deployment helper is not linked into the mining app.

Public CA certificates are copied from the build host's trust store for GPU TLS.
No Sony proprietary SDK, wallet, private proxy configuration, or console database
is included in the release package. Build instructions and local patches are
included in the companion source archive; the setup scripts fetch the pinned
upstream sources listed above.
## Dashboard typography

Bitstream Vera regular and bold fonts are bundled in `homebrew/vendor/vera/`.
The checked-in dashboard glyph data is generated from these fonts; the full
redistribution notice is in `homebrew/vendor/vera/LICENSE.txt` and accompanies
the packaged app as `licenses/Bitstream-Vera-LICENSE.txt`.


## License mapping and provenance

| Code or resource | Applicable terms / notice |
| --- | --- |
| XMRig CPU integration | GPL-3.0-or-later; `LICENSE` and upstream per-file notices |
| MO-Miner retained `sycl/` / `xmrig/` sources | GPLv3 notices from [MoneroOcean/mo-miner](https://github.com/MoneroOcean/mo-miner); C29 cycle header credits MoneroOcean 2025–2026. Copied code predates this repository; per-file notices are retained. Local adaptations are not represented as a byte-identical upstream release. |
| Native app framework and PS5 Vulkan integration | GPL-3.0-or-later; original notices, build-fetched LICENSE/NOTICE |
| libuv | MIT and upstream `LICENSE` / `LICENSE-extra` notices |
| Mesa/RADV/ACO | Per-file MIT/BSD and other upstream terms; complete Mesa `licenses/` and `docs/license.rst` accompany the app |
| Mbed TLS | Dual Apache-2.0 / GPL-2.0-or-later; Apache-2.0 terms used for this GPLv3 combination; full upstream LICENSE bundled |
| RandomX / compiled Blake2 | BSD-3-Clause plus original Argon2 attribution; `licenses/RandomX-BSD-3-Clause.txt` and source headers |
| XMRig Argon2, RapidJSON, fmt, llhttp | Their MIT/BSD notices in `licenses/` and upstream source headers |
| nlohmann/json v3.11.3 | MIT; `ps5/third-party/nlohmann/LICENSE.MIT` |
| Nayuki QR generator v1.8.0 | MIT; full notice in both vendored source headers |
| Bitstream Vera fonts and derived glyph data | Bitstream Vera redistribution terms; `homebrew/vendor/vera/LICENSE.txt` |
| SDK linked libc helpers | GPL-3.0-or-later and individual BSD notices in `licenses/SDK-libc-NOTICES.txt` |
| LLVM compiler builtins / libc++ / libc++abi / libunwind | Apache-2.0 WITH LLVM-exception and retained upstream notices; fetched via SDK build inputs |
| zlib | zlib license; `licenses/zlib.txt` and fetched source |
| Optional SQLite metadata helper | Public domain; not linked into the mining title |

The root GPL text covers this project's combined work; it does not replace permissive component notices or claim authorship of upstream code. Preserve all original headers when modifying copied sources. The integration's build and adapter code differs from upstream and is identified by this repository's history and `patches/`.

## References

- [XMRig](https://github.com/xmrig/xmrig) — CPU miner and RandomX integration.
- [MO-Miner](https://github.com/MoneroOcean/mo-miner) — C29 cycle search and protocol reference.
- [Native app framework](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) — ELF/SELF tooling and native title scaffold.
- [PS5 Vulkan](https://github.com/mihawk-99/PS5_Vulkan), [PS5 Mesa](https://github.com/mihawk-99/PS5_Mesa), [GPU SDK fork](https://github.com/mihawk-99/PS5_PayloadSDK) — RADV/ACO platform integration.
- [Payload SDK](https://github.com/ps5-payload-dev/sdk), [libuv](https://github.com/libuv/libuv), [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) — native toolchain, event loop and GPU TLS.
- [Nayuki QR generator](https://github.com/nayuki/QR-Code-generator/tree/v1.8.0/c), [nlohmann/json](https://github.com/nlohmann/json/tree/v3.11.3) — bundled utilities.
