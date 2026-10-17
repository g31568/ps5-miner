# MOM and PS5 C29 comparison

Compared on 2026-10-13 against the unchanged tracked MOM source at `90c9088`.
The adjacent `../MO-Miner` release checkout is at `a0a4caa`; its SipHash helper
matches except for comments, and its cycle header is identical. This is a local
source comparison, not a benchmark of the original SYCL GPU miner on this Mac.

## Confirmed nonce serialization bug, corrected

MOM's C29 dispatch loads header nonces with `load_nonce(..., true)` and embeds
counter values with `store_nonce(..., true)` (`native/core/execution.inc`,
`native/job/execution.inc`, `native/job-boundary.h`). Here `true` means big endian.
The PS5 manager previously embedded the numeric counter little endian. For wire
nonce `000000000000001e`, MOM hashes header bytes `00 00 00 00 00 00 00 1e`;
the old PS5 code hashed `1e 00 00 00 00 00 00 00`.

The earlier golden fixture compensated by choosing numeric nonce
`0x1e00000000000000`, and its local pool expected that same reversed number.
Consequently those tests demonstrated a valid graph/proof and internal transport
consistency but did not establish nonce compatibility with the real pool.
This could cause a genuine live proof to fail pool verification. It does not
explain why the completed live graphs contained no requested 42-edge cycles:
the wrong serialization still generates random graph seeds.

The manager now serializes big endian. Fixture jobs use logical nonce `0x1e`
and expect wire nonce `000000000000001e`. The regression in
`tests/native/ps5_c29_solver.cpp` directly compares four- and eight-byte headers
with MOM's original `store_nonce` helper. It failed before the change and passed
afterward. The known fixture's seed, edge proof, and Blake2b hash are unchanged.

## Implementation differences

| Area | Original MOM | Current PS5 | Consequence |
| --- | --- | --- | --- |
| GPU runtime | SYCL and vendor runtime | Vulkan, Mesa RADV/ACO | Separate GPU implementation |
| SipHash arithmetic | Native uint64 | GLSL low/high uint32 pairs | Same stateful 64-edge block algorithm; differential checks pass |
| Edge storage | Bucketed endpoint arrays, about 6.751 GiB in the three main arrays | 192 MiB bitmaps, two 800 MiB survivor caches, 64 MiB CPU previous bitmap | About 1.813 GiB working buffers; regenerates blocks for the first four rounds, caches from round five and compacts every cached round |
| Trim depth | Three initial passes plus 80 U/V pairs on portable profile (163); Intel profile usually 87 | 160 alternating side passes | Different survivor counts; correct leaf removal preserves valid cycles at either depth |
| GPU scheduling | Persistent buffers and host cycle search on a detached thread | Per-graph allocation/pipeline setup; synchronous CPU recovery and one fence wait per round | More setup, GPU/CPU round trips and no CPU recovery overlap |
| Validation | Production bucket operations | Every-round offline checks; live bitmap/degree-plane scans every sixteen rounds and the final round | Live scans still cost about 423 ms per graph |
| Cycle finder | `sycl/c29/cycle.h` | Includes that same unchanged header | Same forest; PS5 inserts U/V edges in nonce order, MOM hands off V/U edges in bucket order |
| Recovery | Full GPU nonce search for candidate endpoints | CPU regeneration only for survivor blocks, minimum surviving nonce per endpoint pair | Different recovery strategy; known proof agrees |
| Proof serialization | Sorted 29-bit nonces; Blake2b-256; reversed output | Same packing/hash convention with additional independent cycle verification | Known proof/hash agrees |
| Target | Strict comparison of most significant 64-bit hash word | Same word normalized to BE 256-bit threshold with zero tail | Equivalent strict decision for this compact target format |
| Pool implementation | Node TLS/JSON and general miner lifecycle | Native C++ SOCKS5/MbedTLS and bounded manager | Rewritten implementation; local acceptance is not live acceptance |
| Startup performance | Measured C29 performance advertised as graph rate / 42 | Empty `algo-perf` object | Initial live job was cycle difficulty 50; temporary `+1` login returned difficulty one |
| Run lifetime | Continuous mining | Current restored settings: no graph limit, one-hour runtime cap | Bounded live mining; runtime cap checked between graphs |

The PS5 parser also rejects an explicitly empty `xn` string whereas MOM accepts
it as no prefix (`ps5/pool.cpp` versus `miner/jobs.js`). The observed live sessions
accepted their jobs, so this compatibility difference did not cause those runs
to find zero cycles. It is not changed as part of the nonce correction.

## Evidence limits

The scalar oracle matched MOM's extracted SipHash functions for 500 blocks /
32,000 edges. The seven desktop regression tests passed after the correction.
Previous full-graph PS5 versus MoltenVK round-count comparisons exercise the
same new GLSL implementation, not the original SYCL bucket kernels.

The `cycles=0` log counts only cycles of the requested proof length, not all
cycles or connected components in the graph. The previous 60 difficulty-one live
graphs therefore establish zero discovered 42-edge candidates, not the absence
of every cycle. No genuine live share was submitted in those sessions. A
same-job/same-nonce comparison of original SYCL and PS5 outputs over multiple
live seeds remains useful and has not yet been performed.

The corrected native package was built and deployed with raw SELF readback
verification. SHA256 of `eboot.bin`:
`e95eb57fd6effa37b283948d8d86a8a490bd5ffd24dbc44be700414e2184e9fd`.
The PS5 then solved the Tari-format fixture with logical nonce `0x1e`, submitted
wire nonce `000000000000001e`, and processed one local acceptance, with no rejected
shares and a clean native exit. Evidence:
`build/ps5-c29-native/console-nonce-fixed-accept.log` and `.fixture.json`.
This supersedes the earlier byte-order-compensated local fixture results.

A post-fix MoneroOcean session also authenticated and completed two live graphs
with no stale work, no candidates and no submissions, then exited cleanly and
restored the saved config. Its measured active rate was about 0.0644 G/s.
Evidence: `build/ps5-c29-native/console-nonce-fixed-live.log` and `.json`.
Real MoneroOcean acceptance remains unverified after this correction.


## Remaining throughput gap after early-cache optimization

Source inspection uses local MOM `90c9088` and the adjacent release `a0a4caa`.
The live upstream README was also checked on 2026-10-13: its C29 NVIDIA row is
**RTX 5060 Ti**, 6.32 g/s on Linux and 6.28 g/s on Windows, not RTX 3060.
https://github.com/MoneroOcean/mo-miner#supported-algorithms
No exact RTX 3060 C29 benchmark was established in this investigation. C29 must
also be compared against C29, not C30 or another Cuckaroo variant. Pool-advertised
`algo-perf` normalization by 42 is distinct from these graphs-per-second rates.

Five measured PS5 graphs average 2,933.8 ms (0.340855 active graphs/s):

| Stage | Mean wall ms | Share of total |
| --- | ---: | ---: |
| First four generation/count/prune rounds, GPU fence waiting | 1,632.4 | 55.6% |
| Cache construction and cached count/prune/compaction, GPU fence waiting | 689.6 | 23.5% |
| CPU bitmap/degree-plane validation | 422.6 | 14.4% |
| CPU survivor extraction, search and recovery | 147.2 | 5.0% |
| Other setup, prefix construction, cleanup and timing overhead | 42.0 | 1.4% |

These are host wall/fence durations, not GPU timestamp samples. The generation
stage also includes degree clears/counting/pruning; it is not a pure SipHash
measurement. Evidence: `build/ps5-c29-native/console-speed-v5-live.log` and
`console-speed-v5-live.timing.json`.

The confirmed implementation differences explain substantial extra work:

- **Seeding once versus repeated generation.** MOM's `sycl/c29/seed.inc` writes
  bucketed endpoints once. Its trimming kernels load those endpoints without
  SipHash. PS5 `trim.comp` regenerates every nonempty 64-nonce block in both
  count and prune for four rounds, then regenerates once more to build the cache:
  up to nine block-generation passes before cached trimming. A single surviving
  edge keeps an entire block's serial 64-nonce hash chain active.
- **Local versus global degree counting.** MOM `sycl/c29/trim.inc` partitions
  vertices by endpoint bits and uses workgroup-local atomic degree bitsets.
  With the NVIDIA 12-bit bucket layout, these occupy 32 KiB per workgroup.
  PS5 scatters degree atomics into two global 64 MiB planes and clears their
  combined 128 MiB every round: 20 GiB of global clears over 160 rounds. Its
  compacted survivor list is not a bucketed graph, so compaction alone cannot
  provide MOM's locality.
- **Persistent asynchronous work versus serialized diagnostics.** MOM keeps
  `C29Buffers` in its device state and launches CPU cycle search on a detached
  thread. PS5 allocates buffers and creates pipelines per graph, waits for each
  round to return to the host, scans validation buffers at checkpoints and
  completes CPU recovery before starting the next graph.
- **Unmeasured compiler differences.** PS5 uses low/high 32-bit arithmetic and
  a private 64-entry hash array; MOM expresses SipHash using `uint64_t`. Their
  machine instruction counts, register occupancy and spills have not been
  compared. Neither a 2x arithmetic penalty nor a driver-performance ceiling
  is established by source inspection.

The main next architectural step is porting MOM's bucketed seed and shared-memory
trim pipeline to Vulkan, within the console's measured allocation budget. Its
6.751 GiB main arrays exclude driver, counters, tail and host overhead; the
reported 8 GiB device heap alone does not prove the whole miner fits. Persistent
resources and overlapped CPU recovery are secondary improvements. Removing all
measured validation/recovery time alone would only raise the current pipeline
to about 0.423 graphs/s, so those changes cannot close a multi-graphs/s gap by
themselves. The software comparison identifies avoidable work; without a
controlled same-job GPU profile on the reference NVIDIA card it does not
quantify the hardware or driver contribution.


## Bucketed Vulkan port result (2026-10-13)

The port above is implemented and verified on the PS5. `ps5/c29/bucket.comp`
seeds stored endpoint pairs once and trims each bucket using 32 KiB shared degree
planes. `bucket-worker.h` preserves its 6.751 GiB main buffers and pipelines
between graphs. Full-graph bitmap validation and repeated seed generation are
removed from live trimming. Exact GPU nonce recovery runs only for cycle candidates.

The console reproduced every baseline survivor count over 160 rounds and the
independently verified 42-edge proof. The local pool accepted that exact proof.
Five real pool graphs measured **1.305483 active G/s**, versus **0.340855 G/s**
previously: **3.83 times faster**. Mean graph time fell from 2,933.8 to 766 ms.
Mean trim/setup time was 746 ms and CPU forest time was 17.8 ms; none of these
five live graphs needed the extra GPU nonce recovery pass. Wall throughput was
0.856311 G/s including connection/wait overhead. No qualifying real-pool shares
occurred in this sample. Evidence: `build/ps5-c29-native/bucket-v1-comparison.json`.

The major software differences in the preceding diagnosis are now addressed.
Remaining differences include synchronous per-round fences and CPU search,
nonaggregated global bucket counters, and unprofiled shader register/occupancy
behavior. Their individual costs are not yet measured. A matched RTX 3060 C29
benchmark remains unavailable, so its exact remaining gap cannot be stated.


## Layout/workgroup tuning result (2026-10-13)

The Vulkan port now staggers regular bucket starts, following MOM's offset layout,
and uses 1,024-thread trim workgroups. On the identical known-proof job, trim GPU
time falls from 452.88 to 199.53 ms. Physical bucket permutation provides no
meaningful benefit; shared-memory output batching makes this implementation slower
and stays disabled. Native reference counts and proofs match for all nine variants.

The final package also removes exhaustive per-round survivor-counter scans and
synchronous round logs from normal live mining. Twenty live graphs then measure
**2.626050 active G/s**, versus **1.307190 G/s** previously, with no stale graphs,
rejections or qualifying shares in the sample. Independent known-proof and local
pool acceptance checks pass. Evidence and limitations are in
[the profiling report](c29/PROFILING.md) and
`build/ps5-c29-native/tuning-final-comparison.json`.


## Remaining findings addressed (2026-10-13)

The six follow-up findings are implemented: ready work bypasses the 100 ms
socket wait; buffered coarse seeding reduces global output reservations;
live trimming batches eight rounds per fence; CPU forest search overlaps the
next speculative GPU seed; empty extranonces match MOM; and sustained measured
throughput persists in a public sidecar and is advertised with MOM's /42 units.
The event loop retains exclusive manager, transport and Vulkan ownership.

The selected seed path takes 132.55 ms versus 154.22 ms direct on the same
known-proof job, with every reference count and the exact proof matching.
The histogram experiment is slower (187.43 ms) and is disabled by default.
Sixty final real-pool graphs measure 2.891566 active / 2.761795 wall G/s, with
one cancelled stale attempt and no submitted shares or rejections. A preceding
60-graph run of the same GPU pipeline received one accepted MoneroOcean share. The native
fixture checks acceptance, rejection, empty-prefix normalization, retarget
cancellation, prefetch reuse and saved performance on subsequent login.
These short runs do not establish a matched NVIDIA benchmark or long-term rate.
See [the profiling report](c29/PROFILING.md) for the current behavior and evidence;
preceding pipeline descriptions and throughput numbers document earlier stages.


## Follow-up performance trials (2026-10-13)

All six remaining findings were tested over 1,536 controlled PS5 graphs. The
selected pool pipeline uses 80 trim rounds, retains 1,024-thread workgroups,
batches the three seed stages into one submission and preserves prefetched
buckets during candidate recovery. On a second matched 64-graph sequence,
two runs each average 3.007802 active graphs/s versus 2.867255 for baseline
(+4.90%), with identical submitted proofs. Smaller late workgroups and 64
rounds were slower. Bounded overflow retries and recent-work performance
measurement improve recovery and advertised-rate accuracy rather than GPU speed.

The offline full solve retains 160 rounds and reproduces all original survivor
counts and the exact proof. Seven desktop checks, native acceptance/rejection,
retarget/prefetch and disconnect tests pass. See
[the full trial report](c29/PERFORMANCE-TRIALS.md) for evidence and limits.
