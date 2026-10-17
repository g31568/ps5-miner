# Native PS5 C29 performance trials — 2026-10-13

These trials compare the six follow-up findings against the native miner reviewed
at MOM `90c9088`, including its existing untracked PS5 implementation. Every
performance comparison runs on the same PS5, with the same Tari-format template,
starting nonce and consecutive graph sequence. A local TLS pool independently
checks proof packing and hashes and acknowledges every valid submission.
These local acknowledgements establish transport correctness, not MoneroOcean acceptance.

## Isolated screening

Each variant completed two runs of 32 graphs, reversing variant order on the
second pass. Rates below are the mean active graphs/s, including CPU search and
speculative GPU seeding. All non-fault variants submitted the exact same two
proofs. Console startup/login time is excluded from active rate; wall rates are
also retained in the JSON evidence.

| Change | Mean active graphs/s | Change from baseline |
| --- | ---: | ---: |
| Baseline | 2.821496 | — |
| 128-thread trimming after round 8 | 2.581790 | -8.50% |
| 256-thread trimming after round 8 | 2.732357 | -3.16% |
| 512-thread trimming after round 8 | 2.806896 | -0.52% |
| 64 trim rounds | 2.449198 | -13.20% |
| 96 trim rounds | 2.880549 | +2.09% |
| 128 trim rounds | 2.851669 | +1.07% |
| Three seed stages in one submission | 2.833112 | +0.41% |
| Preserve prefetched graph across candidate recovery | 2.888217 | +2.36% |
| Recent performance measurement | 2.817648 | -0.14% (no speed improvement) |

The original deployed package independently measured 2.815414 and 2.818887
active graphs/s, consistent with the experiment package's baseline.
The first clear-text control stalled and is excluded; all reported comparisons
use the previously validated native TLS path.

Reducing trim depth trades GPU work for more CPU survivors. At 64 rounds the
CPU forest takes longer than the next seed, eliminating the overlap benefit.
The shader's existing recovery table is disjoint from bucket counters, allowing
the prefetched seed to survive recovery without allocating another graph buffer.

Two injected drained capacity failures were discarded on each overflow run;
mining continued for 32 completed graphs with clean shutdown. Those runs skip
the first two nonces and therefore cannot be compared as matched speed samples.
The fault exercises retry/lifecycle handling; it does not measure natural overflow
frequency. Staggered bucket reads are also capped at their actual usable capacity
so a flagged overflow stays in bounds before the host discards the graph.

Evidence: `build/ps5-c29-native/performance-trials/results.json`, per-variant logs
in that directory, and `performance-trials-original/results.json`.
The measured experiment package and source hashes are retained under
`build/ps5-c29-native/performance-trials-package/`.

## Reproduction

Build with `scripts/build-ps5-c29.sh`, then run, for example:

```sh
python3 scripts/test-ps5-c29-performance.py --deploy \
  --variants baseline rounds96 seedbatch prefetch \
  --graphs 32 --repeats 2 --output-dir build/ps5-c29-native/repeated-screen
```

The runner requires the separate title to be idle. It backs up and restores the
exact private configuration and rate cache and verifies restored file bytes.
It uses public local test credentials; no wallet appears in result files.
`--outage` closes the first connection after two seconds and delays the next
connection by five seconds to compare performance-cache behavior.
`--nonce 0x1000` selects another deterministic starting graph sequence.

`experiment-c29.txt` (or desktop `MOM_C29_EXPERIMENT`) accepts eight integers:
`late_threads late_start trim_rounds seed_batch preserve_prefetch retry_overflow recent_rate injected_failures`.
These are diagnostic controls, separate from existing shader layout/workgroup
`tune-c29.txt` overrides. The offline known-proof solve retains all 160 rounds
for comparison with the original survivor-count oracle.

## Refinement and selected pipeline

Two further 32-graph runs each measured 2.894880 graphs/s at 80 rounds and
2.867256 at 112 rounds. Switching to 512 threads only after round 64 measured
2.816407, still providing no useful gain. The combined 96-round pipeline
measured 2.967084 against 2.822368 for its paired baseline (+5.13%).

A second deterministic sequence starting at nonce `0x1000` ran 64 graphs twice
per variant in reversed order:

| Pipeline | Mean active graphs/s | Change |
| --- | ---: | ---: |
| Original pipeline | 2.867255 | — |
| 96 rounds + seed batching + preserved prefetch | 2.989187 | +4.25% |
| 80 rounds + seed batching + preserved prefetch | 3.007802 | +4.90% |

All three variants submitted identical proofs on that sequence, with two
accepted local submissions per run. Smaller workgroups remain disabled.
The selected ordinary pool defaults are **80 trim rounds, 1,024-thread trimming,
one submission for the three seed stages, and preserved prefetch during recovery**.
Bounded overflow retries and recent performance measurement are also enabled.

The small seed-batching gain (~0.4%) is consistent across two runs but remains
smaller than the gains from trim depth and prefetch preservation. These short,
controlled measurements do not establish a long-term thermal rate or a matched
NVIDIA comparison. Fewer trim rounds preserve cycles but increase CPU survivor
work; 64 rounds was clearly too shallow on this implementation.

Evidence: `performance-trials-refine/results.json` and
`performance-trials-other-seed/results.json` under `build/ps5-c29-native/`.

## Disconnect and performance-cache check

The local server closed the first TLS session after two seconds, delayed the
next connection for five seconds, and then resumed the same template. Both
variants completed 32 graphs, authenticated twice, submitted the same two proofs
and exited cleanly. Active rates were effectively identical (2.785273 versus
2.786000 graphs/s). The original session-time calculation persisted 1.559089
while the recent-work calculation persisted 2.606542 graphs/s. The latter is a
20-graph sample at the save point, not the final 32-graph active average.

This corrects advertised-rate accuracy after outages; it does not speed up the GPU.
The recent window includes stale and discarded graph work, excludes disconnected
idle time and retains MOM's division by 42 during login advertisement.
Evidence: `build/ps5-c29-native/performance-trials-outage/results-outage.json`.

## Final build validation

The selected package passes all seven desktop tests. On PS5 it reproduces every
one of the original 160 full-solve survivor counts and the exact known proof.
Native local-pool acceptance, deliberate rejection, empty-prefix handling,
retarget cancellation, saved-performance login and actual prefetch reuse pass.

A final real MoneroOcean run completes 60 graphs at **3.015530 active** and
**2.887114 wall graphs/s**, with no stale graphs, submissions or rejections.
This live run verifies the selected defaults on real jobs; it is not a matched
speed comparison and does not add evidence of real-pool share acceptance.
The prior matched local runs establish the reported 4.90% improvement.

The final SELF is deployed with raw readback verification. SHA256:
`5ba1f29d29403402955746dd123fe68137ca66354944aeb90f2250bf6f84ce2d`.
Runtime source hashes match the package provenance. The original private pool
configuration and performance cache match their initial backup byte for byte;
all transient overrides are absent, and the console is left idle.

Final evidence under `build/ps5-c29-native/`:
`performance-trials-summary.json`, `performance-trials-final-desktop-tests.log`,
`performance-trials-final-solve.log` and `.json`,
`performance-trials-final-fixture.fixture.json`,
`performance-trials-final-rejection.fixture.json`, and
`performance-trials-final-live.timing.json` / `.pool.json`.
