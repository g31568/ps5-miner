# PS5 C29 bottleneck measurements (2026-10-13)

## First-trim tagged reservation cache (2026-10-14)

The optional first-trim output cache retains degree predicates privately, then
reuses the existing 32 KiB shared array for destination tags, counts and reserved
ranges. Mode 1 uses 1,024 direct-mapped tags, mode 2 uses 2,048; colliding
destinations fall back to direct global reservations. No graph buffers or shared
memory are added. Later trims and the checksum diagnostic retain their existing
paths. Optional tuning field nine selects the mode; absent/zero stays direct.
Tiny differential graphs scale the cache to one quarter or half their bucket
count, so random endpoints exercise tag conflicts as well as exact multisets.
The intentional dense overflow test retains all bounds checks and rejection.

All **33 desktop tests pass**, including both cache sizes under padded and
permuted layouts. Native trials use the selected layout 5, seed mode 3, flat
CPU map, 256/256 seed widths, stagger 32 and 68 trim rounds. Each variant runs
64 identical graphs twice, with reversed order on the second pass. Results and
independently checked proof hashes are in
`build/ps5-c29-native/output-cache-trials/`.

| Mode | Mean active graphs/s | Change from direct writes |
| --- | ---: | ---: |
| Direct (selected) | 3.789224 | +0.00% |
| 1,024 tags | 3.441600 | -9.17% |
| 2,048 tags | 3.125306 | -17.52% |

All six runs recover the identical three independently verified, locally accepted
proofs, including the known reference hash: 384 graphs, 18 accepted submissions,
zero capacity discards. **Keep mode 0**. Tagged caching loses throughput despite
reducing global destination reservations; the added predicate retention, edge
reads and shared tag/count work outweigh that reduction in these trials.
This comparison does not isolate register pressure, occupancy or memory stalls.
The next output experiment should avoid additional full-edge passes.

Unlimited real-pool mining resumes with the original private settings and direct
writes. Raw deployment readback is verified. Native runtime source hashes match
`build/ps5-c29-native/PROVENANCE.txt`. Deployed SELF SHA256:
`ee736bc8a8466babba44b17bf21ad974a8d4464f14a79cb28910df16acaf1875`.
Live restart evidence: `build/ps5-c29-native/output-cache-continuous-status.json`.
The restart log observes 237 completed graphs, four real-pool accepted shares
and zero rejections. Both runtime and graph limits remain zero; startup confirms
cache mode zero.

## Current trim profile and output spacing (2026-10-14)

The deployed pipeline's offline known-proof fixture passes all original 160
survivor counts and independent proof verification with timestamp plausibility
checks. Seed plus redistribution takes **91.52 ms** of GPU kernel time. The
first trim takes **78.26 ms**, the first four trims total **116.68 ms**, and
the first 68 trims total **160.10 ms**. Candidate nonce recovery takes 38.12 ms.
These isolated fixture measurements disable prefetch and pool command batching;
they are not ordinary mining throughput measurements.

Three diagnostic repetitions retain degree counting/filtering while replacing
rebucketing with checksums: median **19.84 ms** versus the normal first trim's
78.26 ms. The combined output reservation/scatter path remains a major target.
This does not separate atomic contention, memory transactions or occupancy.
Evidence: `build/ps5-c29-native/trim-current-profile.json` and adjacent log.

Stagger step is now a specialization constant, with capacity-preserving padding
in layout 5. Read offsets, output offsets, bounds checks and allocation strides
use the same step. A coarse alternating-order trial compares 16/32/64, then a
refinement compares 24/32/40/48. Every variant runs twice for 64 graphs; each
comparison recovers identical locally accepted proofs without capacity discards.

| Step (edges) | Mean active graphs/s | Change from matched step 32 |
| ---: | ---: | ---: |
| 16 | 3.608480 | -4.90% |
| 24 | 3.699102 | -2.46% |
| 32 | 3.792368–3.794392 | baseline |
| 40 | 3.738209 | -1.43% |
| 48 | 3.777929 | -0.38% |
| 64 | 2.984170 | -21.35% |

**Step 32 remains selected.** These experiments change spacing and padded stride
together, so they do not isolate a particular cache/bank mechanism. At full size,
step 16 saves 128 MiB of buffer B and step 64 adds 256 MiB versus step 32; A1's
existing allocation covers every tested regular layout. No full graph is added.
All 29 desktop tests pass, including exact endpoints, overflow boundaries,
permuted layouts and output batching. Optional tuning field eight selects the
step, e.g. `5 1024 0 3 1 256 256 24`. Allowed steps are 16/24/32/40/48/64.

Evidence: `build/ps5-c29-native/stagger-trials/` and `stagger-refine/`, each with
`results.json`, `summary.json` and logs. The native tests use the same 68-round
pipeline and current pool settings are restored byte for byte after trials.
Continuous real-pool mining is resumed without runtime or graph limits.
Final deployed SELF SHA256:
`d0e08ff945873ded1c9eb1e93639fae92271e5f0be384091540b27eb3bdbc29c`.
Raw deployment readback is verified and runtime sources match provenance.
The post-resume log observes 290 completed graphs, four accepted real-pool
shares and zero rejections, with recent rates near 3.8 graphs/s and both stop
limits zero. Evidence: `stagger-continuous-status.json`, its private adjacent
log and `PROVENANCE.txt` under `build/ps5-c29-native/`.

## Independent seed workgroup comparison (2026-10-14)

Seed generation and redistribution now support independent 128/256-thread
workgroups. Nonce dispatch coverage follows the selected generation width;
128-thread tile copying visits both halves of the 64 bins. The selected
256-thread path retains a single-bin copy without a bin traversal loop.
Recovery retains 256 threads. All graph buffer sizes are unchanged.

Eight native runs compare all four combinations, 64 graphs per run, reversing
variant order on the second pass. Layout 5, circular seed mode 3, flat CPU search
and 68 trim rounds stay fixed. Every run passes and recovers the same three
proofs accepted by the local TLS pool, with zero graph discards.

| Generation threads | Redistribution threads | Mean active graphs/s | Change |
| ---: | ---: | ---: | ---: |
| 256 | 256 | 3.776593 | baseline |
| 128 | 256 | 3.356938 | -11.11% |
| 256 | 128 | 3.126146 | -17.22% |
| 128 | 128 | 2.829981 | -25.07% |

**256/256 remains selected**. These measurements reject smaller workgroups for
this pipeline; they do not isolate occupancy, register pressure or memory stalls.
The comparison package used a generic bin loop; the final selected path removes
that loop. Evidence: `build/ps5-c29-native/workgroup-trials/results.json` and
`summary.json`. Twenty-one desktop tests cover all combinations, partial groups,
legacy seed modes, output batching and overflow boundaries.

Optional tuning fields six and seven are generation and redistribution widths:
`5 1024 0 3 1 128 256`. Omitted fields retain 256/256. The benchmark runner
exposes `wg128_256`, `wg256_128`, `wg128_128` and `wg256_256`.
These are short native comparisons with local proof acceptance, not a long-term
thermal test. Continuous pool mining is resumed with no runtime or graph limit.

Final selected-path validation completes 64 graphs on nonce sequence `0x1000`
at **3.800926 active / 3.694937 wall graphs/s**, with identical default settings,
two local acceptances and zero stale graphs, rejections or capacity discards.
All 21 desktop tests pass after restoring the selected single-bin fast path.
Raw SELF readback is verified and runtime sources match provenance. Final SELF:
`a633dbefb265168191e2d4b449ecf43c70bc7536d1f551d43052fabddf9a4def`.
After restoring private settings, the continuous real-pool verification observes
128 completed graphs, four accepted shares and zero rejections, with both stop
limits set to zero and the title still running. Evidence:
`build/ps5-c29-native/workgroup-final/results.json`,
`workgroup-continuous-status.json`, the adjacent logs and `PROVENANCE.txt`.

## Selected trim depth after CPU optimization (2026-10-14)

Ordinary pool work now uses **68 trim rounds**, retaining layout 5, seed mode 3,
1,024-thread trimming, seed submission batching and the flat CPU dictionary.
The known-proof offline oracle still uses 160 rounds. No buffer sizes change.

An alternating-order screen compares 56/64/72/80/96 rounds with two 32-graph
runs per variant. Mean active rates are 3.213013 / 3.617458 / 3.758074 /
3.742035 / 3.717473 graphs/s respectively. Shallower trimming still becomes
CPU-bound despite the faster dictionary. All variants recover identical proofs.

A second nonce sequence (`0x1000`) compares 68/72/76/80 rounds with two
64-graph runs each, reversing order. Mean active rates are 3.800701 / 3.791806 /
3.782618 / 3.772696 graphs/s: **68 rounds gains 0.74% over 80**.
Confirmation on the original sequence (`0x1e`), again with two 64-graph runs
per variant in reversed order, measures **3.792705 versus 3.764375**, a **0.75%
gain**. Each sequence recovers identical locally accepted proofs at all depths,
with zero capacity discards. These are modest short-run gains, not evidence
of long-term thermal throughput or real-pool acceptance.

Evidence: `build/ps5-c29-native/trim-depth-screen/`, `trim-depth-refine/` and
`trim-depth-confirm/`, each containing `results.json`, `summary.json` and logs.
The performance runner exposes `depth68`, `depth80` and the other screened
depths; `defaults` clears both public tuning overrides to validate the actual
package selection. Original settings and the rate cache are restored after runs.

The final package, without either tuning override, completes 64 graphs at
**3.799121 active / 3.691740 wall graphs/s**, with two locally accepted proofs
and zero stale graphs, rejected shares or capacity discards. Native selftests,
clean exit and idle state pass; all thirteen desktop tests pass. Deployment
verifies raw SELF bytes and source hashes match provenance. Final SELF SHA256:
`5e7c0e00da2970fec3263e74612301eade5c371f1204bffa82212e1139316c17`.
Original settings and rate cache are restored byte for byte; the title is idle.
Evidence: `build/ps5-c29-native/trim-depth-final/results.json`, its adjacent
console log and `build/ps5-c29-native/PROVENANCE.txt`.

## Selected flat CPU vertex dictionary (2026-10-14)

PS5 endpoint cycle search now selects an open-addressed vertex dictionary using
contiguous key/value arrays rather than per-vertex `unordered_map` nodes.
Dense IDs are still assigned in first-seen order, with separate bipartite
identities, so forest traversal and emitted cycle order remain unchanged.
Other callers of the shared `find_cycles` helper retain its original default.
The GPU pipeline, 80-round depth and shared endpoint ownership are unchanged.

Four matched native runs use 64 graphs at nonce `0x1e`, alternating baseline/flat
then flat/baseline. Mean active throughput improves from **3.341952 to 3.764596
graphs/s**, a **12.65% gain**. Every run passes, recovers the same three proofs
accepted by the local TLS pool, reuses 63 seeds and discards no graphs.
Mean reused-graph CPU/search overlap falls from **127.25 to 93.40 ms**;
GPU prefetch remains near 90.9 ms and trimming near 170 ms. The overlap timing
includes GPU prefetch and must not be interpreted as isolated CPU execution.
Evidence: `build/ps5-c29-native/cpu-flat-trials/results.json` and `summary.json`.

The optional fifth tuning field selects the CPU dictionary: `5 1024 0 3 0`
reproduces the old dictionary and `5 1024 0 3 1` selects the flat table.
Without that field, PS5 defaults to the flat table. Thirteen desktop tests pass,
including 300 randomized exact-order dictionary comparisons with duplicate
edges and full-width vertex IDs. ASan/UBSan cycle tests also pass.
Local acknowledgements do not establish real-pool acceptance or long-term
thermal throughput. The flat table uses host arrays; no GPU buffers are added.

The final default package completes a second 64-graph sequence starting at
nonce `0x1000` at **3.770472 active / 3.665311 wall graphs/s**. Two local proofs
are accepted, with zero stale graphs, rejected shares or capacity discards.
Native selftests and clean exit pass. Deployment verifies raw SELF readback,
and final sources match package provenance. Final SELF SHA256:
`1220232761dd47aaeccead30400ec29b99b7ce877af6523c19a41ae16527a9d5`.
Original console settings and rate cache are restored byte for byte; the title
is left idle. Evidence: `build/ps5-c29-native/cpu-flat-final/results.json`, its
console log and `build/ps5-c29-native/PROVENANCE.txt`.

## Selected circular seed staging

Seed mode **3** retains the buffered coarse-bucket pipeline but stores each
56-edge shared tile as a circular queue. Advancing its head preserves remainders
without copying them. Generation stages two iterations before each flush,
reducing steady-state workgroup barriers from 256 to 128 per 64-edge lane block.
Tile overflow still rejects the graph rather than overwriting shared storage.
The existing 32 KiB shared array and all graph buffer sizes are unchanged.

Four matched native runs compare modes 2/3 then 3/2, with layout 5, 80 trim
rounds and 64 graphs starting at nonce `0x1e`. Mode 2 averages **3.222964 active
graphs/s**; mode 3 averages **3.344220**, a **3.76% gain**. All four runs pass,
recover identical proofs, receive three local TLS acknowledgements each, reuse
63 seeds and discard no graphs. These are local acceptance checks, not real-pool
acceptance or a long-term thermal benchmark.

Among reused-seed graphs, mean prefetch falls from **134.15 to 90.91 ms**.
The overlapping CPU/recovery phase falls from 137.43 to 126.77 ms, while trimming
stays near 169.9 ms. Further seed-only savings can be hidden by CPU search.
Evidence: `build/ps5-c29-native/seed-ring-trials/results.json` and `summary.json`.

Mode 3 is selected by default. To reproduce the comparison, use tuning values
`5 1024 0 2` versus `5 1024 0 3` through `tune-c29.txt` on PS5 or
`MOM_C29_TUNE` on desktop. Twelve desktop tests pass, including wraparound,
batched trimming, the original seed modes and overflow boundary handling.

The final package, with layout 5 and seed mode 3 selected without tuning
overrides, completes a second 64-graph sequence starting at nonce `0x1000` at
**3.340292 active / 3.256997 wall graphs/s**. Two local proofs are accepted,
with zero stale graphs, rejected shares or capacity discards. The native GPU
selftests, clean exit and idle state pass. Deployment verifies raw SELF bytes;
runtime sources match package provenance. Final SELF SHA256:
`b52c3f724e23db446facec3cd21937cd45c448f6bbbff0a66c194c713c884e76`.
Original console settings and rate cache are restored byte for byte, and the
title is left idle. Evidence: `build/ps5-c29-native/seed-ring-final/results.json`,
the adjacent console log and `build/ps5-c29-native/PROVENANCE.txt`.

## Selected first-trim padding

Layout **5** staggers regular bucket output starting with the first trim;
layout **7** additionally enables the existing bucket permutation. Layouts
0–3 retain their original behavior. Ordinary mining now defaults to **5** after
the matched native comparison below.
Padding adds 8,192 endpoint slots to each full-size regular bucket stride, so
the largest start offset cannot reduce the original usable capacity. Buffer B
grows by **256 MiB**; A1's existing seed allocation already covers its larger
regular layout. No additional full graph buffer is allocated.

Set `tune-c29.txt` to `5 1024 0 2` on PS5, or set
`MOM_C29_TUNE='5 1024 0 2'` on desktop. Compare against `1 1024 0 2`
using identical jobs/nonces. Remove the override to restore selected defaults.
The PS5 successfully allocated and ran this layout in the matched native trials.

The CMake tests cover padded, permuted/direct-seed and output-batched layouts
against exact endpoint multisets. The GPU selftest also intentionally overfills
the last destination bucket to check overflow handling at the padded boundary.

Four native runs use 64 consecutive graphs each, alternating layouts 1/5 then
5/1 with identical jobs and starting nonce `0x1e`. Layout 1 averaged
**3.003920 active graphs/s**; layout 5 averaged **3.230773**, a **7.55% gain**.
Every run passed, reused 63 prefetched seeds, discarded no graphs, and recovered
the same three proofs accepted by the local TLS pool. These acknowledgements
verify local proof/transport handling, not real-pool acceptance. Evidence:
`build/ps5-c29-native/first-trim-trials/results.json` and `summary.json`.
The comparison package's SELF SHA256 is
`910d59fc9f0188d162f046f5c20d2486b4d4f43a16c61eb18a42cb15cd31c80d`.
These short trials do not establish long-term thermal throughput.

The final package selects layout 5 without an override. A second sequence
starting at nonce `0x1000` completed 64 graphs at **3.240506 active** and
**3.161743 wall graphs/s**, with two accepted local proofs, zero stale graphs,
zero rejected shares and zero capacity discards. All ten desktop tests pass.
Deployment verified raw SELF readback; final source hashes match provenance.
Final SELF SHA256:
`8bb336de91c392899a8ff57405f1d28297d59e5cbb8074938803e0b231aa8955`.
Original console settings and the rate cache were restored byte for byte;
the separate title was left idle. Evidence:
`build/ps5-c29-native/first-trim-final/results.json`, its console log and
`build/ps5-c29-native/PROVENANCE.txt`.

Latest follow-up: [matched performance trials](PERFORMANCE-TRIALS.md) select
80 pool trim rounds, seed submission batching and preserved prefetch. The
measurements below document preceding pipeline stages.

The dominant measured cost is GPU bucket processing, particularly reserving
output slots and scattering endpoint pairs. Pure hash arithmetic and CPU cycle
search are much smaller targets.

Five live graphs with buffered GPU timestamps averaged **761.8 ms per graph**.
The selected compute queue supplies valid timestamp counters; the analyzer checks
that kernel time <= command GPU time <= host dispatch wall time. Samples are
buffered until after the timed graph to avoid synchronous kernel-log stalls.

| Work | Mean ms/graph | Evidence |
| --- | ---: | --- |
| Seed kernel | 151.30 | GPU timestamps |
| Trim kernels, rounds 1–4 | 159.20 | GPU timestamps; round 1 alone 110.85 ms |
| Trim kernels, rounds 5–160 | 284.22 | GPU timestamps |
| Tail compaction | 0.04 | GPU timestamps |
| GPU clears/barriers outside kernels | 1.54 | GPU command intervals minus kernels |
| Host dispatch time outside GPU intervals | 44.73 | Host wall intervals minus GPU intervals |
| CPU forest and proof handling | 14.60 | Host wall time; no candidates in this live sample |
| Initial allocation/pipeline setup, amortized | 1.03 | Host wall time |
| Other host work | 105.14 | Residual: polling, survivor-counter reads, existing round logs |

GPU command execution totals **596.29 ms**, about **78.3%** of graph time.
The residual host work has not been individually profiled. Fence waiting overlaps
GPU execution and must not be added to GPU time as a separate expense.

## Controlled diagnostic passes

The offline known-proof workload adds three repeats of each diagnostic before
running the normal graph. Runtime branches use the same compiled pipeline;
checksums consume generated endpoints so the compiler cannot remove the work.
Diagnostics never run against a real pool.

| Kernel | Normal ms | Diagnostic median ms |
| --- | ---: | ---: |
| Seed | 153.17 | 31.77 with small checksum output instead of bucket scatter |
| First trim | 110.15 | 26.47 with degree counting/filtering and checksum output, without rebucketing |

These experiments implicate output reservation, scattered writes and their
interaction with memory/cache behavior. They do **not** separate atomic-counter
contention from memory transactions, prove physical bandwidth saturation, or
measure shader occupancy/spills. Diagnostic checksum work is also nonzero;
subtracting times is an estimate of the combined output-path cost, not a hardware
counter measurement. The candidate proof's full GPU nonce recovery pass took
about 39 ms in the earlier timestamped fixture.

All 160 survivor counts and the exact reference proof still match after the
controlled passes. Seven desktop regression tests pass with profiling enabled.
The profiler is disabled for normal mining; private pool settings are restored.
Five additional graphs with profiling disabled measured **1.307190 active G/s**,
consistent with the preceding 1.305483 G/s build. No qualifying shares, stale
attempts or rejections occurred in either five-graph live sample.

## Next targets

1. Improve bucket reservation and output locality before micro-optimizing SipHash.
2. Test bucket start offsets/padding: the current main bucket stride is 679,936
   bytes, divisible by 8,192. MOM's `sycl/c29/trim.inc` staggers later bucket starts
   by `(bucket & 255) * 32` edges; this Vulkan port currently omits that layout.
   Cache/bank aliasing is a hypothesis requiring an A/B measurement.
3. Tune trim workgroup size separately from seed workgroups. Vulkan currently
   uses 256 threads; the reference non-Intel trim uses 1,024. No optimum for the
   PS5 has been established.
4. Batch later rounds and reduce routine round logging/counter readback. This can
   reduce host overhead, but does not eliminate the measured GPU kernel costs.

## Reproduce and inspect

Profiling is opt-in: temporarily write `1` to `/app0/profile-c29.txt` on the
console; `2` additionally enables offline-only diagnostic passes. Remove the
marker to restore ordinary execution. On desktop use `MOM_C29_PROFILE=1` or `2`.
Run the existing known-proof or bounded pool harness, preserving private settings
as described in the worker documentation. Analyze captured logs with:

```sh
python3 scripts/analyze-ps5-c29-profile.py \
  build/ps5-c29-native/console-profile-v2-live.log \
  --output build/ps5-c29-native/profile-v2-live-analysis.json
```

Evidence: `console-profile-v2-solve.log`, `console-profile-v2-live.log`,
`profile-v2-solve-analysis.json`, `profile-v2-live-analysis.json`, and
`bottleneck-summary.json` under `build/ps5-c29-native/`. Preliminary unbuffered
`console-profile-live.log` wall times include diagnostic logging overhead and
must not be used as a mining-performance result.


## Completed tuning experiments (2026-10-13)

Twenty-four combinations of layout, workgroup size and output batching pass the
desktop GPU's exact endpoint oracle. Nine native variants also match all 160
baseline survivor counts and independently verify the exact known 42-edge proof.
Each native timing below uses that same job and includes its GPU nonce recovery
pass. These are dispatch measurements, not whole-graph mining rates.

| Layout | Trim threads | Batched rounds | Trim GPU ms | Dispatch host ms |
| --- | ---: | ---: | ---: | ---: |
| Original | 256 | 0 | 452.88 | 698.05 |
| Staggered | 256 | 0 | 232.16 | 437.87 |
| Permuted | 256 | 0 | 452.28 | 697.44 |
| Staggered + permuted | 256 | 0 | 231.90 | 443.95 |
| Staggered | 512 | 0 | 202.21 | 412.46 |
| Staggered | 1024 | 0 | 199.53 | 410.35 |
| Staggered | 256 | 4 | 331.18 | 540.94 |
| Staggered | 512 | 4 | 301.81 | 510.70 |
| Staggered | 1024 | 4 | 306.63 | 516.19 |

Selected defaults are staggered regular buckets, 1,024-thread trim workgroups
and no batching. Seed/recovery workgroups remain 256 threads. Staggering is
introduced after the first trim, preserving the first destination's capacity;
source flags track which buffers contain offsets. Explicit overflow detection
remains enabled.

The selected variant reduces trim GPU time from 452.88 to 199.53 ms (55.9%).
Physical bucket permutation alone provides no meaningful benefit in these
samples. Shared-memory batching reserves output ranges once per destination
bucket, but its extra passes/bookkeeping make it slower; it remains opt-in for
reproduction and is compiled out of the selected trim pipeline.

Normal pool mining now skips exhaustive survivor-counter scans and synchronous
round logs. Offline proof checks and profiled pool runs still retain every round's
counts. Every live round continues checking cancellation and capacity overflow;
compacted candidates still pass independent proof verification before submission.

Evidence: `build/ps5-c29/tuning-matrix/results.json`,
`build/ps5-c29-native/tuning-results.json`, `tuning-selection.json` and
`tune-*.profile.json`. `scripts/test-ps5-c29-tuning.py` repeats the identical-job
native comparison while preserving an explicitly supplied private settings backup.
Public opt-in tuning uses `tune-c29.txt` containing `layout threads batched_rounds`,
or `MOM_C29_TUNE` on desktop. Remove overrides to use the selected package defaults.


## Final deployed build and live result

The final default package passes the native 160-round known-proof test and local
Tari pool acceptance test. Twenty live MoneroOcean graphs completed in 7,616 ms
of graph work: **2.626050 active G/s**, versus **1.307190 G/s** previously
(**2.01 times faster**). Including connection/wait overhead, wall throughput was
1.930316 G/s. No qualifying shares, stale graphs or rejections occurred in this
20-graph sample; local acceptance is verified separately with the known proof.

Staggered bucket starts and 1,024-thread trimming are enabled by default. Output
batching, physical bucket permutation, profiling and tuning overrides are disabled.
All full-size buffers retain their original capacities. The original private pool
settings are restored and normal mining is resumed. This is a short benchmark;
no matched RTX 3060 C29 comparison has been established.

Evidence: `build/ps5-c29-native/console-tuned-final-solve.json`,
`console-tuned-final-fixture.fixture.json`, `console-tuned-final-live.timing.json`,
`console-tuned-final-live.pool.json`, `tuning-final-comparison.json`, and
`console-tuned-final-resumed.log`.


## Seed and scheduling improvements (2026-10-13)

Three seed paths run the identical proof fixture with all 160 survivor counts
and the exact independent proof matching the baseline:

| Seed mode | GPU seed time, including redistribution |
| --- | ---: |
| 0: direct per-edge reservation | 154.22 ms |
| 1: workgroup histogram reservation | 187.43 ms |
| 2: buffered coarse buckets and redistribution (default) | 132.55 ms |

Mode 2 copies contiguous eight-edge tiles after one reservation. It stages into
existing B/A1 storage, redistributes the upper half to a disjoint destination,
then redistributes the lower half. No full-size buffer was added. A bounded
56-edge shared tile preserves remainders; capacity overflow rejects work rather
than overwriting storage. These are single same-fixture GPU measurements, not
repeated statistical samples. Profile operation 6 records seed redistribution.

Normal pool work submits eight trim rounds per fence, retaining inter-round GPU
memory dependencies and checking overflow/cancellation at batch boundaries.
Offline and profiled runs still submit individual rounds. The GPU oracle checks
both individual and batched dispatches against exact endpoint multisets.

CPU forest search runs on its own endpoint copy while the owning event-loop
thread seeds the next nonce using a private manager snapshot. The real next
lease is reserved normally; seed equality is required before reusing speculative
GPU output. Candidate recovery invalidates prefetch. No second full graph buffer
is allocated. `prefetch_ms` overlaps CPU search and is included in `recovery_ms`;
profiled runs disable speculation so existing isolated kernel comparisons remain
meaningful. Ready graphs also bypass the former 100 ms socket wait.

Sixty final live graphs measure **2.891566 active G/s**, **2.761795 wall G/s**,
with one cancelled stale attempt, no submissions and no rejections. A preceding
60-graph validation run of the same GPU pipeline measured 2.886558 / 2.709660
G/s and received one real accepted share. The previous 20-graph build measured
2.626050 / 1.930316 respectively: approximately 10.1% and 43.1% higher for the
final run in these samples. Different live seeds, run lengths and initial
connection delays prevent assigning those gains to individual changes.

The final package passes native local acceptance and rejection. The multi-graph
empty-prefix fixture also exercises retarget cancellation and next-seed reuse.
The native version-2 performance sidecar stores integer billionths of a graph/s
(`c29_nanogps`). It avoids the PS5 runtime's decimal JSON scanner assertion;
legacy decimal caches are ignored without invoking that scanner. Subsequent
/42 login advertisement is checked against the local server. Seven desktop tests and three ASan/UBSan tests pass.

Tuning files/environment values accept an optional fourth seed-mode field:
`layout trim_threads trim_output_batch_rounds seed_mode`. Existing three-field
overrides use the default seed mode. Modes 0 and 1 remain diagnostic alternatives;
that build used mode 2, superseded by the selected mode 3 documented above.
This field is independent of command batching.

Evidence: `all-fixes-seed-comparison.json`, `all-fixes-seed-2-solve.log`,
`all-fixes-final-live-v2.timing.json`, `all-fixes-final-live-v2.pool.json`,
`all-fixes-final-live.pool.json`, `all-fixes-cache-migration.fixture.json`,
`all-fixes-final-fixture.fixture.json`, `all-fixes-final-rejection.fixture.json`,
`all-fixes-final-performance.json` and `all-fixes-sanitizers.log` under
`build/ps5-c29-native/`. Original private pool settings were restored.
