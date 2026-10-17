# Console validation — XMRig + C29

## Native GPU Settings — 2026-10-17

Settings now has CPU/GPU tabs selected with Left/Right. GPU exposes pool host,
port, user, worker/password, proxy host/port, TLS and certificate verification.
Password values are masked in the list; keyboard editing retains the full value.
Six fields fit on the first page and TLS fields on the second, with Up/Down wrap.

The live console checks in `scripts/validate-gpu-settings.py` passed:

- GPU tab selection and system keyboard open/cancel.
- Navigation across both GPU field pages.
- Dirty draft protection on tab switching and discard without saving.
- Refusal to save while GPU runs and rejection of conflicting web saves.
- Native save while stopped, preserving advanced GPU fields and CPU settings.
- GPU restart alongside CPU after saving.

Host ASan/UBSan checks covered GPU input bounds, invalid ports/hosts, password
masking, advanced field preservation, effective defaults for omitted TLS fields,
long wallet keyboard buffers, and both GPU page layouts. The existing CPU
Options and GPU lifecycle tests also passed. Evidence is in
`logs/ui-grid/gpu-settings-checks.json`; its rates are functional observations,
not a controlled benchmark.

The final build, including the omitted-TLS-field display correction, was installed
with executable/manifest readback verification. A final live check confirmed GPU
tab access, unchanged saved settings during that installation, absence of the
removed `+1` suffixes, and both engines running/connected without a GPU fault.
See `logs/ui-grid/gpu-settings-final-status.json`.

## Grid UI verification — 2026-10-17

Installed the rebuilt grid UI on `PPSA99531` after an authorized stop/drain and
app close. Upload SHA-256 readback and metadata refresh passed. Both saved
configuration files retained their original hashes through installation and testing.

- Captured and visually inspected the actual console dashboard in stopped and
  mining states, plus the separate Settings screen and local configuration QR.
- Settings open/cancel, pending thread edit/discard, and system keyboard
  open/cancel passed on-console without changing saved settings.
- All 11 checks in `scripts/validate-controls.py` passed, including independent
  web and L1/R1 controls, GPU drain/restart, running-config rejection, and shared
  Square stop/restart. Square has no dashboard hint, as requested.
- Final state: both engines running and connected; CPU 1,412 H/s with four
  configured threads, GPU 3.623 graphs/s, four accepted shares and zero rejected.
  These are short observations, not a benchmark or long-term stability result.
- Width-based truncation and extreme input were checked with the host Settings
  preview under AddressSanitizer/UndefinedBehaviorSanitizer. GPU fault states
  were tested on host; no hardware fault was induced on the console.

Evidence: `logs/ui-grid/console-running.png`, `console-stopped.png`,
`console-settings.png`, `console-final-status.json`, and
`logs/combined-controls.json`. The Settings capture contains private runtime
configuration and should not be included in public release artifacts.

Validated the combined `PPSA99531` title on the existing PS5 on 2026-10-14. The executable links XMRig CPU mining and the C29 Vulkan backend. No C30 backend is linked.

## Simultaneous mining

Final observation from that test run:

| Engine | Rate | Accepted | Rejected |
|---|---:|---:|---:|
| XMRig RandomX, 8 workers | 1,953 H/s | 44 | 0 |
| C29 GPU | 2.674 graphs/s | 4 | 0 |

Both pool connections were active. During that run, both miners ran without graph or runtime limits. These are short live observations, not a controlled performance comparison or long-term stability test. The CPU and GPU share memory bandwidth; their rates must not be summed.

The existing MoneroOcean wallet and SOCKS5 settings were copied only into the combined title's private runtime configuration. CPU uses the verified RandomX endpoint; GPU retains its C29/TLS connection. Existing XMRig/C29 titles and their settings were preserved. The distributable ZIP contains starter settings rather than the private wallet.

## Controls and UI

- GPU configuration changes are refused while GPU work is running.
- GPU stop drains its work while CPU mining continues.
- GPU configuration saves while stopped; GPU restarts beside CPU.
- Square stops both engines and restarts both successfully.
- Captured and visually inspected the native XMRig-based screen with CPU RandomX and GPU C29 status, separate rates, GPU share counts, CPU Options and the web-editor QR.

Evidence: `logs/combined-final.json`, `logs/combined-controls.json`, `logs/combined-status.json`, `logs/combined-mining.log`, and `logs/combined-screen.png`.

## Build and host checks

- Cross-build and SELF integrity inspection passed.
- Console upload passed SHA-256 readback.
- GPU worker lifecycle/publication/drain/fault tests passed.
- Existing CPU Options, JSON validation and HTTP framing/origin tests passed.
- Browser JavaScript syntax and Python helper syntax checks passed.

Integration fixes isolate C29's Blake2 dispatch, use RADV's allocation/thread wrappers, route mappings through native APIs, skip unavailable TTY detection and directly link the SDK's `sceNet` resolver implementation. The manifest fingerprints the integration sources and the verified binary.
