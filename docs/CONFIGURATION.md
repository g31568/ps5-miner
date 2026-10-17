# Configuration reference

The combined app copies starter configs into its private sandbox on first use. Edit through the controller or the local editor. Files in `config/` are legacy CPU payload examples, not the combined app's production defaults. Release defaults are generated in `scripts/build-homebrew.py`.

| Item | CPU | GPU |
| --- | --- | --- |
| Saved config | `/download0/xmrig-native.json` | `/download0/c29-pool.json` |
| Starter pool | `gulf.moneroocean.stream:128` | host `gulf.moneroocean.stream`, port `10128` |
| Payout key | `pools[0].user` | `login` |
| Worker/password | `pools[0].pass` | `pass` |
| Proxy | `pools[0].socks5` (`host:port` or null) | `proxy_host` + `proxy_port` |
| TLS | Unavailable in this CPU build | `is_tls: true`, `tls_verify: true` |
| CA bundle | — | `/app0/assets/cacert.pem` |

Starter endpoints are examples, not a guarantee of current pool availability. A pool password is not a wallet seed. SOCKS5 routing does not encrypt a plaintext pool connection.

CPU defaults: 8 workers (editable 1–16), RandomX fast dataset, hardware AES, interpreter, prefetch mode 3, yielding enabled, donation level 0 (editable). Higher worker counts may compete with GPU cycle search. Use measured comparisons on your console rather than assuming more threads are faster.

GPU defaults: 68 trim rounds, 1,024 trim threads, 256/256 seed/redistribution threads, stagger 32, output cache 0. These are compiled tuning parameters, not general user pool fields. `runtime_seconds: 0` and `max_graphs: 0` mean unlimited. Stop GPU before saving its config. The optional `.perf.json` sidecar caches measured graph performance, not credentials.

The web editor includes independent CPU/GPU controls and `/status` reports a separate `gpu` object. HTTP API options are web-only. The editor itself is always an unauthenticated LAN interface while the app runs; see [security boundaries](../SECURITY.md). Browser automation requires the exact Host/Origin and `X-XMRig-Control: 1` with JSON for POST. These headers do not authenticate a client.

Settings persist atomically after validation. Invalid or duplicate JSON fields are rejected. CPU changes do not rewrite GPU settings. A stopped GPU is required for GPU configuration changes, and a fence timeout requires relaunch before another GPU start.
