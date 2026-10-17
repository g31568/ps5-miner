# Security

This is experimental homebrew for a jailbroken PS5, currently tested on firmware 13.60. Treat the console and its LAN as trusted. A build passing CI does not certify the console runtime or upstream dependencies.

## Network exposure

The local editor binds the console LAN address on a dynamic HTTP port. **It has no authentication or HTTPS. Anyone who can reach that port can read pool settings (including passwords), change payout destinations, and start or stop mining.** Host/Origin checks and the JSON control header protect against common browser cross-origin requests, not direct LAN clients. Use a private network or an isolated VLAN with firewall rules allowing only your configuration device. Never forward the editor, mining API, FTP, debug, or payload-loader ports to the internet.

The CPU core is built with TLS disabled; its pool connection is plaintext, including through SOCKS5 (which is not encryption). Use a trusted network path or an independently encrypted tunnel. GPU TLS and certificate verification are enabled in release defaults; leave verification enabled and use current public CA roots. Pool addresses are operator-selected network destinations.

Saved settings and logs in `/download0` are plaintext. Do not include those files in issues, source commits, or releases. A wallet address is a public payout identifier; a seed phrase or private spend/view key must never be entered into this app.

## Reporting

Use the repository's GitHub **Security → Report a vulnerability** feature when available. Otherwise open an issue requesting private contact without publishing credentials or exploit details. Include the commit, firmware, affected component, prerequisites, and a minimal redacted reproduction. No response-time or support guarantee is offered.

## Review limits

Publication review covers first-party HTTP/configuration code, pool parsing/TLS configuration, dependency downloads, release packaging, and accidental credential exposure. Vendored XMRig, Mesa, SDK and cryptographic implementations require their own upstream review. No claim of a complete dependency audit, penetration test, or long-term mining stability is made.
