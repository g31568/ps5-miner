# C29 PS5 compute probe

This native PS5 C29 backend probe generates exact Cuckaroo29 `(U,V)` edges
from four derived SipHash seed words. Pool mining and the full solve use
bucketed endpoints with shared-memory degree counting; bitmap trimming remains
available as a diagnostic reference.
An opt-in full solve feeds survivors to MOM's unchanged CPU cycle finder,
recovers edge nonces and independently verifies proofs. Pool mining uses the
adapter documented in [../README.md](../README.md).

`seed.comp` uses 32-bit low/high pairs for SipHash, so it requires no `shaderInt64`
feature. Each invocation processes one complete 64-nonce block. State persists
within a block and restarts from the seed for each new block, matching
`sycl/c29/search.inc`. The last hash is used directly; preceding hashes XOR with
the last hash. Output remains `(U,V)`; the existing miner's final trimmed buffers
use `(V,U)`, which the bucket worker normalizes according to trim-round parity.

## Build and run on a desktop GPU

Requires CMake, a C11 compiler, Python 3, shaderc's `glslc`, Vulkan headers, and a
working Vulkan implementation. All computation and checks are offline.

```sh
cmake -S ps5/c29 -B build/ps5-c29
cmake --build build/ps5-c29
ctest --test-dir build/ps5-c29 --output-on-failure
```

On macOS with Homebrew Vulkan headers, shaderc and MoltenVK:

```sh
cmake -S ps5/c29 -B build/ps5-c29 \
  -DVulkan_INCLUDE_DIR="$(brew --prefix vulkan-headers)/include" \
  -DVulkan_LIBRARY="$(brew --prefix molten-vk)/lib/libMoltenVK.dylib" \
  -DC29_TEST_ICD_DISPATCH=ON
cmake --build build/ps5-c29
ctest --test-dir build/ps5-c29 --output-on-failure
```

The GPU test compares 34,324 generated edges over 40 dispatches with a scalar
64-bit CPU oracle. Cases include zero output, partial blocks, unaligned ranges,
extra idle invocations, carry-heavy seeds, and the highest legal C29 nonce.
Every word outside the requested output range must retain its guard value.
The separate CPU test compares the oracle with the actual existing SYCL
SipHash helper bodies for 500 blocks (32,000 edges).
The optional ICD test exercises the native function-pointer dispatch path on
the desktop GPU as well; it requires linking directly to an ICD such as MoltenVK.

The trimming test compares 396 rounds (33 cases × 12 alternating U/V rounds)
against an independent CPU degree-counting oracle. It checks every survivor
bit, both degree planes, all unused words, and guard regions after every
round. Cases include zero edges, partial words/blocks, highest legal nonces,
empty/sparse alive sets, dense atomic contention, and two-dimensional dispatch
with padded invocations. The deterministic final survivor total is 56,343.

## Native PS5 build

The native backend now targets [Mesa RADV for PS5](https://github.com/mihawk-99/PS5_Vulkan),
with its pinned Mesa fork and PS5 payload SDK platform layer. RADV's NIR/ACO
compiler turns embedded SPIR-V into native GPU instructions. The probe obtains
Vulkan entry points from `vk_icdGetInstanceProcAddr` and `vkGetDeviceProcAddr`;
it links no separate Vulkan loader. AnyPS5 is not required.

Requires Docker (OrbStack works on macOS), Git and a host `glslc`:

```sh
bash scripts/build-ps5-c29.sh
```

The script downloads open-source dependencies and the upstream payload SDK,
builds a local Linux container, then cross-compiles the driver and packages a
separate native title under `build/ps5-c29-native/PPSA99529/`. All downloads and
build artifacts remain under ignored `build/` paths. No deployment or console
configuration is performed. The first build downloads roughly 312 MB of Linux
packages plus SDK/source dependencies, and needs several GiB of disk space.
The portable package is `build/ps5-c29-native/C29-GPU-Probe-PPSA99529.zip`.

The recipe uses the driver's title CRT, platform heap/libc/thread bindings,
AGC import facades, ELF conversion and clean-room runtime. A generic payload
link is insufficient. Mesa's unresolved weak table entries are explicitly
bound to hidden null symbols before ELF conversion, retaining its normal
fallback semantics; strong references still have to resolve to real code or
console-module imports. Launching the resulting title requires a compatible
native homebrew title loader; an ELF payload loader alone is not established
as sufficient. Target firmware is 13.60; runtime verification is recorded below.
Diagnostics and the exact PASS/FAIL verdict go to klog.

Pinned revisions:

- PS5_Vulkan: `252bf912a745e3566a93b69b098bdc8ec4048837`
- PS5_Mesa: `47e9eb61ee7e1112194557e1e047178562827bc6`
- PS5_PayloadSDK: `b83202be73e930050e00de0f1b4d9ec46c0391bf`

The build records source hashes, driver archive and `eboot.bin` identity in
`build/ps5-c29-native/PROVENANCE.txt`. Check for
`PASS c29 seed: 40 cases, 34324 edges, all guards intact` in a console run;
a successful desktop test or native link alone cannot establish that result.

For an already-installed probe with FTP, klogsrv and the upstream `ps5vkctl`
helper running, deploy and test only this title with:

```sh
python3 scripts/test-ps5-c29.py --host 10.1.1.51
# Opt in to all 2^29 edges, starting with one U/V pair.
python3 scripts/test-ps5-c29.py --host 10.1.1.51 --no-deploy \
  --full-rounds 2 --timeout 180 --output build/ps5-c29-native/console-full-trim.log
```

The script refuses updates while any game is active, verifies raw SELF file
readback hashes, arms fresh logging before launch, and requires seed PASS,
trimming PASS, cleanup success and native shutdown. It never updates XMRig.
Logs and machine-readable verdicts go under `build/ps5-c29-native/`.
`--full-rounds` accepts even counts from 2 to 160. The script sets a transient
`/app0/full-graph-rounds.txt` marker and removes it afterward. Desktop full-size
runs use `MOM_C29_FULL_ROUNDS=2` instead. Normal launches run only differential
cases. Use `--full-rounds 160 --timeout 300` for the complete baseline schedule.

## Shader ABI

Set 0, binding 0 is a writable `std430` storage buffer of `uvec2` edges (8 bytes
per edge). The descriptor covers guards and output. Push constants occupy
44 bytes: four low/high seed pairs at byte 0, `firstNonce` at 32, `edgeCount`
at 36, and `outputOffset` at 40. The offset counts edges, not bytes.

Callers must validate `firstNonce < 2^29`, `edgeCount <= 2^29 - firstNonce`, and
buffer capacity for `outputOffset + edgeCount`. Dispatch
`ceil(ceil(((firstNonce % 64) + edgeCount) / 64) / 64)` workgroups, with at least
one for the zero-output guard test. Local size is 64. Wait for a fence and make
shader writes visible to host reads before inspecting coherent mapped memory.

## Bounded-memory trimming ABI

`trim.comp` regenerates edges from `siphash.glsl`; no full edge array is stored.
One alive bit per edge plus two saturating degree bit planes requires 192 MiB
for `2^29` edges and `2^29` vertices per side, plus 256 bytes of guards.
The first four rounds regenerate SipHash for counting and pruning. From round
5, a bounded endpoint cache stores `(relative nonce,U,V)` for surviving edges.
Each of two cache buffers contains a 32 MiB prefix area plus capacity for 2^26
twelve-byte records, using 800 MiB each. If the graph exceeds that capacity,
regeneration continues until it fits. The GPU compacts survivors after each
cached round and swaps source/destination buffers. Removed entries receive a
`0xffffffff` nonce tombstone, avoiding repeated atomic clears.

Reduced differential tests switch after two rounds to exercise all seeds,
partial blocks and sparse sets. Full-size validation keeps another 64 MiB
previous-alive bitmap on the CPU. The two caches plus bitmap storage and this
CPU copy total about 1,856 MiB, excluding driver/runtime overhead. Coherent
host-visible device-local memory is preferred, with a host-memory fallback.

Set 0/bindings 1 and 2 hold source/destination caches: block-prefix area,
survivor count and packed endpoint records. The cache-build phase is
`0x80000000`; cached count/prune phases set `0x40000000`; GPU compaction uses
`0x20000000`. Specialization constant 0 separates the cached pipeline from the
SipHash path. Compaction uses a shared 64-lane bitmap and one global reservation
per nonempty workgroup. Its output count must match the alive bitmap at each
validation checkpoint. Cached pruning atomically clears shared alive words.

Set 0/binding 0 is one `uint` storage buffer: 16 guard words, alive words,
16 guards, seen degree words, 16 guards, repeated degree words, 16 guards.
Alive words are `ceil(edgeCount/32)`; each degree plane is `(nodeMask+1)/32`.
Push constants are seed pairs at 0, `firstNonce` at 32, `edgeCount` at 36,
`nodeMask` at 40 and `phase` at 44 (48 bytes). `firstNonce` must be 64-aligned;
the range must stay below `2^29`, and `nodeMask+1` must be a power of two
between 32 and `2^29`. Initialize only valid alive bits; trailing bits stay zero.

Clear both degree planes on the GPU with transfer-write visibility before each round. Dispatch phase `2*round` to count
the selected side, issue a shader-write → shader-read barrier, then dispatch
phase `2*round+1` to prune. Sides alternate U then V. Alive bits only disappear;
the graph retains its original `(U,V)` orientation and nonce identities.
Two-dimensional dispatch linearizes blocks as `globalY*groupsX*64+globalX`.
A full graph needs 131,072 workgroups, e.g. X=32,768 and Y=4. Check actual device
limits before dispatching. Offline full-size runs verify monotonic survivor sets,
degree-plane consistency, and guards every round; pool work checks every sixteen
rounds and the final round to reduce CPU bitmap traffic; reduced cases independently check every
bit against scalar CPU degrees. Full-size cases use a fixed all-zero seed.

## Verified state and remaining work

On 2026-10-12 the optimized SPIR-V executed successfully through MoltenVK on
Apple M2: both the direct Vulkan and ICD dispatch tests passed 40 cases each,
with 34,324 matching edges per path and intact guards. The differential CPU
test passed (32,000 edges). The pinned Mesa RADV release archive built, and
the probe linked and packaged as the native title `PPSA99529`.

The FSELF inspector reports valid integrity. Static checks confirmed x86-64
ELF identity, 16 KiB-aligned load segments, no writable/executable load segment,
6,743 optional weak references bound to hidden null addresses, and retention
of real ICD, common physical-device enumeration, and compute-pipeline code.
The ZIP and recorded source/artifact hashes were verified. On PS5 firmware
13.60, the native Mesa RADV run passed all 40 cases and 34,324 edges, with
all guards intact. The device identified as `PlayStation 5 GPU (RADV NAVI21)`;
RADV reported a 4 GiB host heap and an 8 GiB device-local heap. These are
reported capacities, not a measurement of the console's free memory.

The first run crashed after PASS when the native CRT called libc `exit`.
The probe now overrides `catchReturnFromMain` to request shell shutdown with
`sceSystemServiceLoadExec("exit", NULL)` after Vulkan cleanup. This shutdown
change was verified on a second console run: GPU PASS, cleanup status 0,
`Kill for LoadExec => 0`, and no running title afterward. ShadowMountPlus
recorded a normal stop with no probe crash. A separate `SceSysCore.elf` helper
reported signal 12 during shutdown; this was not the probe process.
The launcher icon is required for
ShadowMountPlus registration and is generated by `make-icon.py`.

The bitmap trimmer also passed 396 reduced-graph rounds on PS5 with 56,343
total final survivors; both degree planes and all guards matched the CPU
oracle. A full `2^29`-edge graph allocated 201,326,848 bytes of bitmap storage
and completed two rounds on both Apple M2 and PS5: 339,360,052 survivors after
U trimming and 159,007,191 after V trimming. PS5 exited cleanly. This verifies
full allocation/dispatch and cross-device agreement, not a complete proof or
mining throughput. Evidence: `console-trim.log`, `console-full-trim.log`.

The full 160-round schedule then passed on both devices. Every round's
survivor count matched; the final graph retained 72,792 edges. PS5 guards and
bitmap invariants remained intact, Vulkan cleanup succeeded, and the title
exited through the shell without a probe crash. Full-size validation compares
round counts across devices, not every degree against a full-size CPU oracle.
Evidence: `console-full-trim-160.log`, `console-full-trim-160.json`, and
`full-trim-comparison.json`.

## Full C29 solve

`solver.cpp` reuses `sycl/c29/cycle.h` without changing its forest algorithm.
It scans the alive bitmap, regenerates only nonempty 64-edge blocks, and retains
the minimum original nonce for each `(U,V)` edge. Sorted proof nonces are checked
again with scalar SipHash, single-cycle validation and MOM's packed Blake2b hash.
CPU search is synchronous, capped at 1,048,576 survivors, with cancellation checks
around forest search and during extraction/recovery. The forest itself has no
internal cancellation hook. Its C++ allocations use the native platform heap;
allocation failure terminates the native title rather than producing a proof.

The opt-in test derives a real graph from MOM's existing 42-edge vector in
`tests/vectors/equihash.js`. It requires that recorded proof hash to be found,
independent validation to pass, and the management layer to reject duplicate
submission. No network traffic or simulated pool acceptance is involved.

```sh
MOM_C29_SOLVE=1 build/ps5-c29/c29-seed-probe
python3 scripts/test-ps5-c29.py --host 10.1.1.51 --solve --timeout 300 \
  --output build/ps5-c29-native/console-solve.log
```

The console runner enables 160 rounds and removes both transient request files
afterward. Normal title launches retain the small offline tests.

The full solve passed on Apple M2 and PS5 firmware 13.60. All 160 round counts
matched; both retained 83,366 edges and found the same 42 nonces. The proof hash
is `68005b0465cf34675f804de6ef37b3ea2fed0f4796236fc55d1ecd996b54db2d`,
matching MOM's original vector and an independent Python packed-proof hash.
The PS5 title exited cleanly. Evidence: `build/ps5-c29/solve-desktop.log`,
`build/ps5-c29-native/console-solve.log`, `console-solve.json`, and
`solve-comparison.json`. These paths refer to generated files under `build`.

CTest also runs `ps5_c29_solver`, a fast genuine-proof regression using the
recovered nonces. It covers sparse bitmap recovery, wrong seeds, mutated and
duplicate nonces, wrong proof size, capacity limits and stale/duplicate shares.

The native manager and verified solver now connect to the optional
[pool transport](../README.md#pool-configuration).
The current SYCL A1/A2/B buffers alone consume about 6.75 GiB. The probe logs
RADV's actual heaps and storage-buffer limits; full mining must fit the usable
console memory budget rather than assume the desktop layout fits. This probe
establishes full-graph trimming and a genuine proof on desktop and PS5 GPUs;
live pool jobs now complete on PS5; sustained throughput and real pool share
acceptance remain unverified. Earlier console evidence is saved under
`build/ps5-c29-native/console-klog.log`, `console-postrun.log`, and
`console-clean-exit.log`.


## Initial cached trimmer validation (2026-10-13)

All seven desktop regressions pass with the endpoint cache and GPU degree clears.
A full cached desktop solve reproduced all 160 original PS5 fixture round counts,
83,366 survivors and the exact known 42-edge proof hash. Evidence is in
`build/ps5-c29/solve-cached-clears-desktop.log` and
`build/ps5-c29-native/cache-proof-comparison.json`. Native packaging also succeeds.
The deployed PS5 build passed the native differential checks and the local
known-proof acceptance test. Five live MoneroOcean graphs then completed in
19.654 seconds of graph work: **0.254401 active G/s**, compared with the previous
0.064131 G/s (3.97× faster). Including connection and reply waits, throughput was
0.241932 G/s. The live pool accepted one share, with zero rejections or stale
attempts. Console cleanup and exit checks passed. Evidence:
`build/ps5-c29-native/console-speed-cached-fixture.fixture.json`,
`console-speed-cached-live.log`, `.timing.json`, `.pool.json`, and
`cache-proof-comparison.json`. This is a short five-graph measurement; RTX 3060
parity has not been established.


## Early-cache compaction validation (2026-10-13)

All seven desktop tests pass with specialized pipelines, tombstones and GPU
survivor compaction. Native full-size validation matches every one of the 160
baseline round counts and the genuine proof. Compacted record counts match the
alive bitmap; native cleanup and exit succeed. The local Tari fixture receives
the exact proof and processes acceptance correctly.

Five real MoneroOcean graphs completed in 14.669 seconds of graph work,
yielding **0.340855 active G/s** and **0.316656 wall G/s**. This is 34.0% faster
than the preceding 0.254401 G/s build. No qualifying shares, rejections or stale
graphs occurred in this sample. GPU wait time is approximately 1.63 seconds in
the four regeneration rounds and 0.69 seconds in cached generation/trimming/
compaction per graph. These are fence wall durations, not GPU timestamp queries.
The selected native memory type is 3, heap 1, flags 7 (device-local, host-visible,
coherent). Total working buffers are about 1,856 MiB plus driver/runtime overhead.

Evidence: `build/ps5-c29-native/console-speed-v5-solve.log`,
`console-speed-v5-fixture.fixture.json`, `console-speed-v5-live.timing.json`,
and `speed-v5-comparison.json`. The deployed title retains the saved pool
configuration and one-hour runtime limit.


## Bucket pipeline validation (2026-10-13)

`bucket.comp` seeds stored endpoint pairs once, partitions them into 4,096 buckets,
and trims with two workgroup-local degree planes (32 KiB). `bucket-worker.h`
keeps buffers and specialized pipelines alive across graphs. It reads small
bucket counters between rounds and explicitly rejects capacity overflow instead
of silently dropping edges. Main buffers occupy 7,248,510,976 bytes (6.751 GiB),
with another 8,552,512 bytes for counters, compacted survivors and recovery tables.
All four allocations succeeded in native memory type 3, on the device-local heap.

MOM's unchanged CPU forest consumes compacted endpoint pairs. Only graphs with
candidate cycles regenerate the full graph on the GPU to recover minimum nonces;
every resulting proof passes the independent CPU verifier before submission.
The worker remains synchronous and polls current-job ownership between rounds.

All seven desktop tests pass. Both desktop GPU paths and the PS5 compare exact
endpoint multisets over seven reduced graphs and 84 trim rounds. The native full
solve matches all 160 baseline round counts, 83,366 survivors and the exact
known proof hash. The local Tari pool fixture receives and accepts that proof.

Five live MoneroOcean graphs took 3,830 ms of graph work: **1.305483 active G/s**,
**3.83 times** the preceding 0.340855 G/s build. Wall throughput was 0.856311 G/s,
including startup/login and reply waits. These five graphs contained no candidate
cycles, qualifying shares, stale attempts or rejections. Candidate graphs perform
an additional recovery pass; the known-proof fixture took 817 ms (57 ms recovery).
This short sample does not establish RTX 3060 parity.

Evidence: `build/ps5-c29-native/console-bucket-v1-solve.json`,
`bucket-v1-round-comparison.json`, `console-bucket-v1-fixture.fixture.json`,
`console-bucket-v1-live.timing.json` and `bucket-v1-comparison.json`.
The deployed title retains the original private pool settings and one-hour limit.


## GPU bottleneck profiling

Opt-in GPU timestamps and controlled offline diagnostic passes identify bucket
output/rebucketing as the main target. See [PROFILING.md](PROFILING.md) for
measurements, limitations, reproduction and the prioritized next experiments.


## Selected layout and workgroups (2026-10-13)

Pool work now uses **68 trim rounds**, selected after flat-table CPU optimization.
Repeated matched comparisons improve throughput about **0.75%** over 80 rounds
on two nonce sequences. The offline known-proof oracle retains 160 rounds.
See [trim-depth measurements](PROFILING.md#selected-trim-depth-after-cpu-optimization-2026-10-14).

The selected defaults use padded layout 5, stagger bucket starts from the first
trim, and use 1,024-thread trim workgroups. Padding adds 256 MiB to buffer B.
Matched 64-graph native runs measured 3.230773 versus 3.003920 active graphs/s
for layout 1, a 7.55% improvement with identical locally accepted proofs.
See [first-trim measurements](PROFILING.md#selected-first-trim-padding).
Circular seed mode 3 is also selected: it retains tile remainders in place and
halves generation's staging barriers without adding GPU storage. Matched native
runs improve active throughput another 3.76%, from 3.222964 to 3.344220 graphs/s.
See [seed staging measurements](PROFILING.md#selected-circular-seed-staging).
PS5 CPU search now uses a flat vertex dictionary, preserving baseline dense-ID
and cycle order while avoiding per-vertex allocations. Matched native runs
improve active throughput another 12.65%, from 3.341952 to 3.764596 graphs/s.
The optional fifth tuning value is 0 for the old dictionary or 1 for the new
default. See [CPU measurements](PROFILING.md#selected-flat-cpu-vertex-dictionary-2026-10-14).
Optional tuning field nine enables a first-trim tagged destination cache:
`5 1024 0 3 1 256 256 32 1` selects 1,024 tags, and a final `2` selects
2,048 tags. Conflicting destinations retain direct writes. The cache reuses
existing degree storage after retaining filter predicates, adding no GPU buffers
or shared memory. Mode `0` is the direct-write default. Tiny GPU oracle graphs
scale the tag count to exercise collisions as well as output overflow.
See [cache measurements](PROFILING.md#first-trim-tagged-reservation-cache-2026-10-14).

Normal pool mining skips exhaustive round diagnostics while retaining
cancellation, overflow detection and independent proof verification. Experimental
output batching was slower and stays disabled. All 24 desktop tuning combinations
pass the exact endpoint oracle; nine native variants match every reference round
and the genuine proof. The final package also passes local pool acceptance.

An earlier 20-graph live build measured **2.626050 active G/s**, **2.01 times** the previous
1.307190 G/s build, and 1.930316 wall G/s. No qualifying share, stale graph or
rejection occurred in the sample. Original private settings are restored.
See [PROFILING.md](PROFILING.md#completed-tuning-experiments-2026-10-13) for the
same-job comparisons and `build/ps5-c29-native/tuning-final-comparison.json`
for the final benchmark and package hash.
