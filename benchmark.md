# PS5 XMR Miner test results

## 2026-10-17 — grid UI and mining control validation

Tested the combined `PPSA99531` title on PS5 firmware 13.60 with four
configured CPU threads and the existing C29 GPU profile. Both engines ran
and connected to their pools after the UI update.

| Observation at the final recorded sample | Result |
| --- | --- |
| CPU RandomX rate | 1,412 H/s |
| GPU C29 rate | 3.623 graphs/s |
| GPU graphs completed since its last restart | 182 |
| GPU shares accepted / rejected | 4 / 0 |
| GPU fault | None reported |

These are short live observations from functional testing, not a controlled
performance comparison or a long-term stability benchmark. CPU hashes/s and
GPU graphs/s are different units and must not be added together. CPU share
counts are not exposed by the status endpoint used for this sample.

Both pool usernames had a `+1` fixed-difficulty request during this test.
The pool can clamp that request to its allowed minimum. This setting was
inherited from the existing runtime configuration; it was not added by the
UI renderer. Share counts therefore describe the test's pool difficulty,
not comparative earnings or improved mining performance. The suffix was
removed from both saved usernames after recording this result, with all other
saved fields preserved. Both miners reconnected and resumed work without a
reported GPU fault. See `logs/ui-grid/after-difficulty-reset.json`; that immediate
recovery sample is not a new benchmark or proof of post-change share acceptance.

### Functional checks

All 11 live checks in `scripts/validate-controls.py` passed:

- CPU start preserves GPU state.
- Web CPU stop leaves GPU completing graphs.
- Web CPU restart leaves GPU running.
- L1 toggles only CPU mining.
- R1 toggles only GPU mining.
- GPU configuration saves are rejected while GPU is running.
- GPU stops and drains while CPU keeps mining.
- Stopping GPU preserves its saved configuration.
- GPU restarts and mines beside CPU.
- Square stops both engines.
- Square restarts both engines.

Actual console captures confirmed the grid dashboard, separate Settings
screen, and phone configuration QR. Settings open/cancel, pending thread
edit/discard, and system keyboard open/cancel passed. Saved CPU/GPU
configuration hashes remained unchanged through installation and those tests.

Host sanitizer checks covered rendering, long settings strings and
width-based truncation. Options validation and GPU lifecycle/publication/
drain/fault tests passed. No hardware fault was induced on-console.

### Evidence

- `logs/ui-grid/console-final-status.json`: rates and share counts above.
- `logs/combined-controls.json`: the 11 live control checks.
- `logs/ui-grid/console-running.png`: actual running dashboard.
- `logs/ui-grid/console-stopped.png`: actual stopped dashboard.
- `VALIDATION.md`: deployment and validation details.

The Settings capture contains private runtime configuration and is excluded
from this report's screenshot links. No wallet addresses or credentials are
recorded here.
