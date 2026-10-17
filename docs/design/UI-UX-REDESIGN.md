# XMR Miner UI/UX design

Design proposal · 2026-10-17 · No implementation changes

## Refined direction: two engines, one glance

The user requested a simpler interface, then asked to add more information. Retain the two-row structure and restore useful secondary monitoring details. This refinement supersedes the three exploratory overview layouts below.

Home answers only: Is each engine working? What is its speed? How do I stop or start it?

- Modest app title; no oversized overall status headline.
- Two equal rows: CPU / GPU, explicit state, rate with its own unit, and L1 / R1 action label. Secondary text identifies CPU algorithm and configured thread count, GPU algorithm, and each pool's connection state. GPU also shows explicitly labeled accepted/rejected share counts.
- Footer: Square Start all / Stop all, and Options Settings. No repeated action buttons or extra navigation.
- Keep full pool addresses, wallet strings, phone setup and editable technical settings inside Settings → CPU / GPU details or Settings → Connect phone. Home presents monitoring details only, with smaller but couch-readable secondary text.
- Prefer “Mining”, “Connecting…”, “Stopped”, “Stopping…” and “Needs attention” to internal engine terminology. Never infer mining solely from pool connection. Existing telemetry limits still apply.

For mixed states, show each state independently. Square remains Stop all when either engine is active; L1/R1 starts a stopped engine independently. A global summary adds no information and can conceal mixed states, so omit it.

Place an actionable error immediately beneath the affected row. For example: “GPU stopped unexpectedly. Relaunch XMR Miner.” Allow that row to grow rather than clipping or replacing the other engine. During GPU stopping, replace the action with “Finishing current work…” and suppress repeated commands. When an engine is stopped or data is stale, show an em dash for its current speed instead of a misleading old value.

First launch is the intentional exception: if no engine is configured, show “Set up mining” with “Set up on phone” and “Set up with controller”. Phone setup reveals the actual local URL and QR code. Support setup for just one engine; do not block a configured CPU because GPU setup is incomplete. Preserve the existing rule that launching or saving never starts mining automatically.

Settings has three entry points: CPU, GPU, and Connect phone. Each engine's settings starts with human-readable fields; advanced configuration and diagnostics are disclosed only when needed. Keep Back and explicit Save predictable. Do not require users to learn JSON or cryptic button combinations for ordinary changes.

The web companion mirrors the two engine rows, using one state-dependent Start/Stop button per engine. Put Settings beneath the overview; keep diagnostics within the relevant engine's advanced details instead of making a top-level destination. Mobile uses stacked rows, and action feedback stays beside the action.

Simplification is about reducing decisions and repeated information, not removing recovery, configuration or independent control. The refined image illustrates the normal running state; first-run and error behavior are specified here and still require future usability validation.

### Information hierarchy after adding detail

Latest visual revision: remove the two annotated blocks beneath the rates—both “Pool connected” labels and the GPU accepted/rejected counts. Keep algorithms, CPU thread count, Mining state, rates, and controls unchanged. Pool details and share counts remain available in engine details; this revision supersedes their Home placement in the table below.

### Grid revision

Use an invisible 12-column grid on the 1920 × 1080 TV canvas, with 96 px side margins, 24 px gutters and an 8 px spacing scale. Both engine rows have equal height and matching internal padding. Columns 1–4 contain identity and state, 5–8 contain the rate, and 9–12 contain the controller action. Align numeric left edges, rate baselines, action glyphs and action labels across both rows. Use the same numeric type size for both rates; reduce both together if the longer GPU unit needs space. Header, dividers and footer share the same outer edges.

Remove the bottom-left Square / Stop all hint completely. The footer contains only Settings at the right. Independent L1/R1 actions remain visible. This is a visual design change; it does not itself change the existing Square shortcut in the running app. Earlier proposals to display a shared Home control are superseded by this revision.

| Engine | Identification | Primary information | Secondary information | Action |
| --- | --- | --- | --- | --- |
| CPU | CPU; RandomX; configured thread count | State; H/s | Pool connected/disconnected | L1 Start/Stop CPU |
| GPU | GPU; C29 | State; graphs/s | Pool connected/disconnected; accepted/rejected shares | R1 Start/Stop GPU |

Keep units separate, label both share counts and avoid ambiguous “2 / 0” notation. Thread count is configuration, not a measurement of active workers. The native interface already has this configuration; the web companion can read it from its configuration response. Do not invent CPU share counts or add unsupported power, temperature, earnings or uptime metrics. The concept's four threads and all displayed rates/counts remain sample data.

## Product and user needs

XMR Miner runs independent XMRig RandomX CPU and C29 Vulkan GPU miners on a PS5. A controller interface supports viewing and controlling mining; a local browser supports configuration. These are complementary surfaces of one product.

Based on the current source and README, the likely primary user is a console owner who wants to configure a pool, confirm useful work is happening, and control resource use without accidentally interrupting the other engine. This is a design hypothesis, not an interview finding.

Priorities:

1. Know what each engine is doing and whether its pool is connected.
2. Start or stop either engine independently, or stop both immediately.
3. Configure wallet, pool and worker details without needing JSON expertise.
4. Recover from connection failures and invalid settings without losing edits.
5. Read the console from a couch; use a phone for long text entry.

## Evidence from this project

- `homebrew/main.cpp`: fixed 1920 × 1080 renderer; CPU rate dominates while GPU is a smaller secondary row. CPU options occupy half the overview. Independent L1/R1 controls and shared Square control already exist.
- `homebrew/web/index.html` and `style.css`: CPU/GPU controls, status and large JSON editors share one long page. GPU configuration precedes CPU configuration.
- `homebrew/web/app.js`: browser connection failure changes the connection label but leaves the last metrics visible. Reload can replace unsaved editor content.
- `homebrew/main.cpp`: CPU configuration has revision conflict checking; GPU configuration requires stopping before saving. GPU faults can require relaunch.
- `logs/combined-screen.png`: historical visual reference, not current runtime proof. All concept metrics are sample data.
- No SwiftUI implementation exists here. Apply the supplied skill's design principles, not its platform-specific APIs or deployment requirements.

## Shared design system

Retain the orange app identity and charcoal background. Use mint for positive state, amber for attention and red for errors, always accompanied by a symbol and explicit text. Stopped is neutral, not an error.

Use clean system-style sans serif typography, tabular numerals and separate rate units. Never combine hashes/s with graphs/s. Prefer spacing and thin separators over nested panels, heavy borders and glass effects.

For the 1080p TV proposal: approximately 90 px safe margins, 30–36 px body labels, 44–56 px section headings and 80–112 px principal rates. Validate at real couch distance before finalizing these sizes. Focus needs a distinct outline and label; color alone is insufficient. Avoid moving metric layouts and decorative animation.

For the web companion: system fonts, flexible rem-based text, minimum 44 × 44 CSS-pixel action targets, visible keyboard focus, semantic labels, 200% zoom support, and reduced-motion support. Design normal text for at least 4.5:1 contrast and large text/control boundaries for applicable 3:1 thresholds; measure final colors during implementation.

## TV information architecture

Overview → selected engine details → engine settings.

Overview always exposes CPU and GPU state and separate rates. Keep full pool addresses, wallet strings, JSON, logs and technical options out of the principal monitoring view. Connection details provide the local web URL and a real generated QR code; do not use decorative QR imagery.

Three visual directions explore the same product:

- Quiet Dual Engine: equal CPU/GPU columns, immediate comparison and independent controls. Recommended starting point for everyday monitoring.
- Focused Engine: a persistent engine list and one large detail pane. Better when controller-based inspection and settings are frequent.
- Calm Overview: a strong overall status headline above two wide engine rows. Best for distance reading and occasional intervention.

These are alternatives, not three screens to add together.

## Controller behavior

Preserve L1 for CPU and R1 for GPU. Labels describe the available action: “Start CPU”, “Stop CPU”, “Start GPU”, “Stop GPU”. Show “Stopping GPU…” while it drains; prevent another start during that state.

Square means “Stop all” when either engine is active and “Start all” only when both are stopped and ready. A mixed state must never cause an already-running engine to restart. Keep the shared command available without navigating away.

D-pad moves focus predictably; Cross activates the focused item; Circle returns or cancels. Options opens Settings. In an editor, use an explicit “Save changes” action; avoid making Options silently change meaning to confirmation. Returning from details restores the prior focus.

Starting and stopping do not need confirmation dialogs. Discarding dirty edits does. Relaunch-required faults show instructions; do not imply that a relaunch button already exists.

## Web companion

Use three destinations: Overview, Settings, and Diagnostics. On narrow screens stack engine sections vertically and retain all actions. No horizontal scrolling for the normal workflow.

Overview mirrors TV terminology and exposes independent engine controls. Show connection freshness separately from miner/pool state. HTTP API configuration remains web-only, in advanced settings, separate from the local editor's availability.

Settings begins with an explicit CPU/GPU selector. Use labeled fields for pool address, wallet/pool user, worker name and applicable credentials. CPU exposes a bounded thread count. Preserve engine-specific schemas and unknown supported fields. Offer an explicit “Use CPU wallet” convenience only when the user chooses it; never silently copy pool endpoints between protocols.

Keep proxy, donation, HTTP API and raw JSON under Advanced. CPU and GPU edits remain independent. Show Saved / Unsaved changes / Saving / Saved and applied inline. Before CPU save, explain that applying settings can briefly interrupt CPU mining. GPU save explains “Stop GPU before saving; CPU continues.” A proposed “Stop GPU and save” convenience must wait for confirmed completion of stopping and must not silently restart it.

Reload and navigation preserve dirty drafts or ask before discarding. CPU revision conflicts retain the draft and offer comparison/reload; never overwrite silently. GPU revision protection would require backend work and is not currently guaranteed.

Diagnostics holds logs and actionable error details. Do not force users to understand internal codes before showing a recovery action.

## First-run flow

1. Show “Set up mining”; both engines remain stopped.
2. Choose CPU, GPU or both. Explain that GPU needs a C29-compatible pool.
3. Enter settings on the console or open the phone configuration page via real QR/link.
4. Validate required fields and placeholder wallet values locally; describe these as format checks, not proof of pool compatibility.
5. Save per engine, then offer an explicit Start action. Saving never starts mining unexpectedly.
6. Show Connecting until the pool connection is reported; report work rate separately. A connected pool alone is not proof of an accepted share.

## State and recovery design

| State | Presentation | Available next step |
| --- | --- | --- |
| Not configured | “Add wallet and pool” | Open settings |
| Stopped | Neutral status; no live-rate implication | Start engine |
| Starting / connecting | Explicit progress text; no success checkmark | Stop; inspect settings if it fails |
| Mining | Rate with units and separate pool state | Stop engine |
| Pool disconnected | “Pool disconnected”; qualify retained rate | Inspect connection/settings |
| Stopping | “Stopping GPU…”; explain current work is finishing | Wait; no repeated start |
| GPU fault | “GPU unavailable — relaunch XMR Miner” | Read details and relaunch manually |
| Browser connection lost | “Cannot reach PS5”; last-updated time, metrics labeled stale | Reconnect; keep drafts |
| Save rejected | Inline reason next to relevant field where possible | Correct without losing input |

The overview summary is derived from both engines. “Both miners are running” is inappropriate when only one is active. Never hide a GPU fault behind a general green connection indicator.

## Data and implementation boundaries

Current `/status` provides CPU enabled state, pool connection and rate; GPU running/stopping/connection/fault/result, graphs/s, graph count and accepted/rejected shares; and HTTP API state/port. CPU accepted-share counts are not in this endpoint, so omit them from the base design.

Detailed startup phases, reliable CPU/GPU health beyond these fields, historical charts and CPU share counters require additional telemetry or explicit inference labels. Browser last-updated time can describe its last successful response. Do not invent temperatures, wall power, earnings, efficiency or an aggregate rate.

TV typography, navigation and guided forms are proposed changes. The concepts are not validated running interfaces. No console deployment, mining command or configuration mutation is part of this design work.

## Design validation before implementation acceptance

- From couch distance, identify each engine's state and stop the intended engine without stopping the other.
- With CPU running and GPU stopped, predict the effect of each shortcut correctly.
- Configure a first-run wallet without opening JSON; recognize that saving does not start mining.
- On a narrow phone layout and at 200% zoom, reach all settings and actions.
- Unplug/reconnect the network: stale values cannot be mistaken for current telemetry; drafts survive.
- Trigger invalid configuration, a save conflict, stopping and fault states; each exposes an honest next action.
- Check browser keyboard navigation and screen-reader labels; announce action results without reading changing rates every second.
- Measure contrast, verify reduced motion and test long labels before declaring accessibility complete.

## Design sources

- Supplied SwiftUI Pro skill: design, accessibility and resizability principles, adapted to the actual C++/web surfaces.
- [Apple HIG — Focus and selection](https://developer.apple.com/design/human-interface-guidelines/focus-and-selection/): make the focused control recognizable and navigation predictable.
- [Apple HIG — Accessibility](https://developer.apple.com/design/human-interface-guidelines/accessibility): readable text, contrast, multimodal cues and accessible interactions.
- [Nielsen Norman Group — Ten usability heuristics](https://www.nngroup.com/articles/ten-usability-heuristics/): visibility of system status, user control, consistency, recognition over recall and error prevention.
