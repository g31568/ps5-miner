# Native MOM management

`manager.c` provides a bounded, single-threaded C11 job manager for a PS5
event loop. It reuses MOM's integer Blake2b and C29 input/proof conventions.
It needs neither Node.js nor SYCL. The existing native GPU probe runs its
offline self-test before allocating Vulkan resources.

The transport must deliver authenticated connection events and validated,
normalized C29 jobs. `mom_job_build` accepts Tari `nonce || blob`, Grin
`pre_pow || nonce`, or an explicit raw header layout. C29 nonces are big endian, matching MOM native dispatch;
proof sizes are 32 or 42. The target is a full **big-endian** 256-bit threshold.
For MOM's 64-bit C29 target word, place that word big endian in bytes 0..7 and
zero bytes 8..31. Pool target hex must be decoded by the protocol adapter first.
The share hash returned to the transport retains MOM's reversed Blake2b order.

## Worker contract

1. Call `mom_manager_connected` after transport authentication and
   `mom_manager_job` after job validation. Only C29 jobs belong here.
2. Acquire one `MomWork` lease. Pass its four seed words to the Vulkan worker;
   its copied header already contains the reserved nonce.
3. Check `mom_manager_cancelled` between GPU stages and during CPU search.
   Replacing work invalidates its generation but retains resource ownership.
4. Recover sorted edge nonces and call `mom_manager_share` while the lease is
   active. It recomputes endpoints, checks one complete cycle, packs the proof,
   hashes it and checks the target. Nonzero return values are submission IDs.
   A transport sends that ID with the lease's job ID, nonce, hash and proof.
5. Call `mom_manager_complete` only after every GPU fence and CPU solver has
   drained. A timeout alone is not completion. A failed worker stops management.
6. Route submission replies using both session and request ID. Disconnects
   invalidate jobs and mark pending shares lost. Retry deadlines use supplied
   monotonic milliseconds and exponential backoff capped at 60 seconds.

Pause prevents new dispatches while current work drains. Stop invalidates work
and prevents reconnection. Callers must drain before reinitializing the manager.
Do not invoke these functions concurrently from network or solver threads;
marshal events to the owning event loop. Sixteen pending shares and 64 distinct
proofs per graph bound memory and prevent repeated proof submission.

Retargets and reconnection to the latest identical template preserve its nonce
cursor. The manager does not retain a history of all earlier templates; a
transport restoring an older template must supply its saved cursor or a fresh
random starting nonce, as MOM does when resuming C29 work.

## Validation and current limits

Build and run with the commands in [c29/README.md](c29/README.md). CTest includes
`ps5_management`, and both GPU probe variants run the same checks. Tests cover
job layouts, Python-derived seed/proof hashes, replacement during in-flight work,
retarget nonce continuation, pause/resume, reconnect, exhaustion, protected nonce
bits, reply correlation and malformed/disconnected-cycle rejection.

The opt-in [full solve](c29/README.md#full-c29-solve) now connects a managed MOM
job to Vulkan trimming, MOM's cycle finder and exact nonce recovery. The normal
synthetic rings test structure; the full solve requires MOM's genuine recorded
42-edge proof hash. `pool.cpp` now connects that worker to bounded TCP/TLS
JSON-RPC mining through an optional SOCKS5 proxy. See the pool configuration below.

The management checks passed on PS5 firmware 13.60 in the separate PPSA99529
title, followed by the existing 34,324-edge and 396-round GPU tests and clean
native exit. Evidence is in `build/ps5-c29-native/console-management.log` and
its JSON verdict. All four desktop CTests and a standalone AddressSanitizer /
UndefinedBehaviorSanitizer management run also passed. XMRig was not modified.

The full solver now passes on both desktop and PS5: identical 160-round counts,
83,366 surviving edges, and the same genuine 42-edge proof with MOM's expected
hash. The independent Python proof hash also matches. See
`build/ps5-c29-native/solve-comparison.json` for the cross-device evidence.
The seven-test suite includes the recovered-proof regression and a local TLS/SOCKS5
pool that checks reconnection, cancellation, exact proof serialization and an accepted
test reply. A test-pool reply does not establish live pool acceptance.


## Pool configuration

Place a private `c29-pool.json` beside the native title's `eboot.bin` to enable
pool mode. The deploy/test script uploads it temporarily and removes it after
its run. Wallet settings stay outside the distributable ZIP.

```json
{
  "url": "gulf.moneroocean.stream",
  "port": 20512,
  "is_tls": true,
  "tls_verify": false,
  "login": "YOUR_WALLET",
  "pass": "PS5",
  "proxy_host": "10.1.1.111",
  "proxy_port": 8964,
  "max_graphs": 2,
  "runtime_seconds": 180
}
```

MOM's default `tls_verify: false` encrypts the connection without authenticating
the server certificate. Set it to `true` for certificate-chain and hostname
verification; `ca_file` can select a private PEM trust store. Native defaults to
`/app0/sce_sys/cacert.pem`; desktop uses OpenSSL's trust store. The current live
mining endpoint presents a self-signed certificate, so the supplied settings
follow MOM's default policy. No certificate pinning is performed.

The adapter advertises C29 only, with MOM's `mo-native` and `submit-result`
extensions. After at least twenty completed graphs it saves measured throughput
to `c29-pool.json.perf.json` beside the configuration. Subsequent logins advertise
that graph rate divided by 42, matching MOM; missing or malformed measurements
leave `algo-perf` empty. The version-2 sidecar stores integer billionths of a
graph/s (`c29_nanogps`), avoiding the PS5 runtime's decimal JSON parser. Legacy
decimal caches are ignored safely. The sidecar contains no wallet or connection details.
It accepts embedded or
separately pushed jobs, C29 extranonce controls, Tari and Grin layouts, and
32/42-edge proofs. Pool updates are polled between GPU batches and after CPU search.
Live trimming submits at most eight rounds per batch; offline/profile runs retain
per-round counts. CPU forest search uses an owned endpoint copy on a joined
thread while the event loop seeds the next graph speculatively. Stale work drains before its resources are released.

Native numeric IPv4/IPv6 proxy addresses bypass the SDK's stubbed DNS resolver.
The SOCKS5 proxy resolves the pool hostname. Native direct hostname connections
remain unsupported by this SDK. SOCKS5 username/password authentication is not
implemented. Pool lines, nesting, output queue and outstanding shares are bounded.
TLS entropy comes from kernel `KERN_ARND`; the SDK's timestamp PRNG is not used.

`max_graphs: 0` disables the graph limit. `runtime_seconds: 0` disables the time
limit for manual deployment; the console test script requires a bounded runtime.
The time limit is checked between graphs, so an in-flight graph may exceed it.
Neither a completed graph nor a successful login guarantees a qualifying share.

```sh
build/ps5-c29/ps5-pool-check /path/to/private-config.json
python3 scripts/test-ps5-c29.py --host 10.1.1.51 \
  --pool-config /path/to/private-config.json --timeout 260 \
  --output build/ps5-c29-native/console-pool.log
```

Desktop GPU mining uses `MOM_C29_POOL_CONFIG=/path/to/private-config.json` with
`build/ps5-c29/c29-seed-probe`. The same worker and proof validation run on PS5.
The console runner writes a separate `.pool.json` with graph/share/reply counts;
only `accepted > 0` from a live pool demonstrates a real accepted share.


The PS5 firmware 13.60 live run in `build/ps5-c29-native/console-pool-nonblocking.log`
completed SOCKS5, TLS and login with the supplied pool, then all 160 rounds of a
real 2^29-edge job and CPU search. It exited cleanly with one completed graph,
89,427 survivors, and zero qualifying shares. All console verdict fields passed.
This establishes native live-job processing; live share acceptance remains unverified.
The final transport uses `poll`, atomic nonblocking socket creation where available,
and `MSG_DONTWAIT` on native TLS/SOCKS operations. The earlier descriptor-only
attempt stalled before round 1 and was closed; its log is not a successful run.


The follow-up `build/ps5-c29-native/console-pool-live.log` run completed two live
graphs, cancelled one stale graph after a replacement job, and exited cleanly.
All verdict fields passed and all seven desktop tests passed. No qualifying
shares were found in either live run. The supplied private config is installed
only in PPSA99529 for repeat launches, capped at two completed graphs and 180
seconds checked between graphs; fixture markers are absent. XMRig is unchanged.


## Native share-path debugging

Run `python3 scripts/test-ps5-c29-pool.py` to send MOM's known proof-producing
job through a local SOCKS5/TLS test server to the installed PS5 title. The test
runs the actual 2^29-edge GPU pipeline, validates all 42 returned edge nonces
and independently computes the packed Blake2b hash before accepting the share.
It then requires the console to process the acceptance reply and exit cleanly.
The user's exact private pool configuration is backed up and restored.

The console run in `build/ps5-c29-native/console-pool-fixture.log` passed:
83,366 survivors, one independently verified cycle, one submitted share and
one processed acceptance reply. `console-pool-fixture.fixture.json` records
all test-server checks. This is a local test-pool acceptance, not MoneroOcean
acceptance. The earlier two live graphs each found zero 42-edge cycles, so
neither reached submission. Real-pool wire acceptance remains unverified.

The installed live configuration intentionally stops after two completed
graphs or 180 seconds checked between graphs. Closing after that bounded run
is expected. A job replacement can add cancelled graphs to its stale counter.

### C29 throughput timing

Pool runs emit `graph timing` for every attempt and `pool throughput` on exit. Rates
use the console's monotonic clock, with one completed C29 graph as the unit (G/s).
`total_ms` includes per-graph allocation, pipeline setup, trimming, validation,
recovery, and cleanup. `trim_ms` includes setup and the fused edge-generation/trim
passes; `recovery_ms` covers CPU endpoint regeneration, cycle search, proof checks,
and submission queueing. These are wall durations, not GPU timestamp queries.

`active_gps` divides completed current-job graphs by their combined durations.
`wall_gps` also includes login, job waits, cancelled stale work, and reply waits.
Neither metric measures accepted shares or Monero-equivalent pool hashrate.
The console test runner saves these measurements in `.timing.json` next to its log.

Measured on PS5 firmware 13.60 (2026-10-13): five completed live C29 graphs
averaged 15.5932 seconds each, giving 0.064131 active G/s. Including one stale
attempt and connection/job waits, five graphs over 94.873 seconds gave
0.052702 wall G/s. CPU recovery averaged 153 ms per completed graph; setup,
fused generation/trimming, and validation dominate the current probe. No cycles
or accepted live shares occurred in this short run. All seven regression tests
and console lifecycle checks passed; the original live config was restored.
Evidence: `build/ps5-c29-native/console-benchmark.log` and `.timing.json`.

### Local native pool acceptance and rejection tests

`scripts/test-ps5-c29-pool.py` now defaults to a Tari `blob` template that omits
its prepended eight-byte nonce, preceded by `mining.set_extranonce`. The known
MOM fixture still produces exactly the same independently checked 42-edge proof
and packed Blake2b hash. `--job-layout raw` retains the original raw fixture path.
`--reject-share` deliberately rejects that valid proof to verify that the native
worker increments rejected rather than accepted. These tests use a temporary
SOCKS5/TLS listener on the Mac and restore the exact installed private settings.

```sh
python3 scripts/test-ps5-c29-pool.py --output build/ps5-c29-native/console-local-tari-accept.log
python3 scripts/test-ps5-c29-pool.py --reject-share --output build/ps5-c29-native/console-local-tari-reject.log
```

The `.fixture.json` result records proof receipt and correct reply processing;
`.pool.json` contains console share counters. Local fixture acceptance is not
MoneroOcean acceptance and does not measure the live chance of finding a cycle.

### MoneroOcean live acceptance diagnostic (2026-10-13)

The live endpoint returned `cuckaroo`, proof size 42, eight nonce bytes, a
32-byte Tari template, and initial target `b81e85eb51b81e05` (approximately
cycle difficulty 50). A separate metadata-only login with a temporary `+1`
difficulty suffix returned target `ffffffffffffffff`. The pool's fixed-difficulty
login parsing is documented by its own implementation:
https://github.com/MoneroOcean/nodejs-pool/blob/master/lib/pool/miners.js

Two bounded native sessions using that difficulty-one login completed 60 graphs
in 1,045.240 seconds total, with ten stale attempts. Completed graph work measured
0.064190 G/s; including connection waits and stale work, 0.057403 G/s. Both sessions
passed lifecycle checks and restored the exact original `PS5-C29` settings.
They found zero requested-length cycles, so submitted zero shares: neither live
acceptance nor live rejection was exercised. This does not establish a proof or
transport bug; it also does not establish real pool acceptance. The first
normal-difficulty diagnostic was deliberately stopped before these sessions and
must not be interpreted as a crash regression.

Evidence: `build/ps5-c29-native/moneroocean-debug-summary.json`,
`console-live-easy-debug.log`, and `console-live-easy-debug2.log`.


### Endpoint-cache optimization (2026-10-13)

The first endpoint-cache version cached surviving endpoints after twelve rounds,
within an extra 224 MiB allocation, rather than repeatedly regenerating SipHash
during all 160 rounds. GPU commands clear degree planes. Live pool work checks bitmap invariants
every sixteen rounds and on the final round; offline probes retain checks on
every round. `trim stages` reports GPU fence waiting, CPU validation and total
wall durations to identify the remaining bottleneck. GPU waiting includes clears
and dispatch overhead; it is not a GPU timestamp measurement.

Desktop regressions pass and all 160 fixture survivor counts plus the genuine
proof match the previous console result. The native package builds successfully.
The new build was deployed and passed native GPU checks plus the genuine-proof
local acceptance test. Five live MoneroOcean graphs averaged 3.9308 seconds each,
giving **0.254401 active G/s** and **0.241932 wall G/s**. This is 3.97× the earlier
0.064131 active G/s measurement. One live share was accepted, zero rejected,
and all console lifecycle checks passed. Evidence is in
`build/ps5-c29-native/console-speed-cached-live.log`, `.timing.json`, `.pool.json`,
and `cache-proof-comparison.json`. The exact saved private settings were restored.
This short benchmark establishes a PS5 improvement and real pool acceptance;
RTX 3060 parity remains unmeasured.


### Early caching and survivor compaction (2026-10-13)

The next deployed build switches to cached endpoints after four rounds and
compacts survivors on the GPU after every cached round. Tombstones prevent
repeated atomic clears for removed edges. Specialized pipelines separate cached
trimming from SipHash generation. Coherent device-local memory avoids the PS5
driver's host-allocation zeroing pass. Two 800 MiB caches plus bitmap/validation
storage use about 1,856 MiB, excluding driver/runtime overhead.

All seven desktop regressions pass. The native full solve matches all 160 old
round counts, 83,366 survivors and the exact known 42-edge proof. Local fixture
submission and acceptance also pass. Five live MoneroOcean graphs averaged
2.9338 seconds each: **0.340855 active G/s**, **0.316656 wall G/s**, 34.0% above
the previous 0.254401 active G/s. No qualifying shares or stale graphs occurred
in this five-graph sample. The exact private settings and one-hour runtime cap
were restored. Evidence: `build/ps5-c29-native/console-speed-v5-solve.log`,
`console-speed-v5-fixture.fixture.json`, `console-speed-v5-live.timing.json`,
and `speed-v5-comparison.json`. RTX 3060 parity remains unmeasured.


## Current bucket worker (2026-10-13)

Live C29 mining uses the stored-endpoint bucket pipeline described in
[c29/README.md](c29/README.md#bucket-pipeline-validation-2026-10-13).
Native known-proof and local pool-acceptance checks pass. Five live graphs measure
1.305483 active G/s, 3.83 times the previous build, with zero stale attempts or
rejections and no qualifying share in that sample. Persistent main buffers use
6.751 GiB plus metadata and runtime overhead. The original pool settings remain
on the deployed console; packages contain no private pool configuration.


The subsequent verified bucket-layout/workgroup tuning raises active throughput
to **2.626050 G/s** across 20 live graphs (2.01 times the preceding build).
Staggered starts and 1,024-thread trimming are the deployed defaults; output
batching remains disabled after measuring a regression. Exact reference proof
and local pool acceptance checks pass. See [the profiling report](c29/PROFILING.md).


## Scheduling, seed writes and pool compatibility (2026-10-13)

The default seed pipeline now buffers eight-edge output tiles into 64 coarse
buckets, then redistributes the upper half before the lower half. It reuses the
existing GPU arrays without increasing their capacities. On the identical proof
fixture, total seed GPU time is 132.55 ms versus 154.22 ms for direct seeding.
All 160 reference counts and the exact independently verified proof match.
The alternate per-workgroup histogram path takes 187.43 ms and remains opt-in.

Normal mining no longer waits 100 ms on a quiet socket when work is ready.
CPU cycle search overlaps the next GPU seed without changing manager ownership;
only a matching acquired seed can reuse that work. Replacements are processed
before completion, and candidate nonce recovery invalidates speculative state.
Empty `xn` strings are accepted as no prefix, matching MOM.

The final PS5 package completed 60 live MoneroOcean graphs at **2.891566 active
G/s** and **2.761795 wall G/s**, with one cancelled stale attempt, no submitted
shares and no rejections. A preceding 60-graph run of the same GPU pipeline
measured 2.886558 / 2.709660 G/s and received **one real accepted share**. The prior 20-graph result was 2.626050 active / 1.930316
wall G/s. These are short runs with different live jobs and connection overhead,
not a controlled attribution of each improvement or a long-term benchmark.
The public performance sidecar was saved and its /42 login normalization was
checked against the native local pool. Exact original private settings were restored.

Seven desktop regressions, three AddressSanitizer/UndefinedBehaviorSanitizer
checks, native local acceptance/rejection and replacement/prefetch checks pass.
`recovery_ms` now includes speculative next-graph seeding when enabled;
`prefetch_ms` reports that interval and `seed_reused` identifies consumed prefetch.
Those overlapping intervals must not be added to total graph time.
Evidence: `build/ps5-c29-native/all-fixes-final-*` and
`all-fixes-seed-comparison.json`. See [the profiling report](c29/PROFILING.md).
