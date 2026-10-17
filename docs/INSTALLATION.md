# Installation and updates

## Requirements

- Jailbroken PS5 with a compatible homebrew folder loader; runtime evidence is for firmware **13.60** only.
- Sufficient free memory for RandomX fast mode and the C29 GPU graph. Close other apps.
- A Monero payout address and pool access. GPU mining needs the MO-Miner C29 object-login protocol; a RandomX-only P2Pool service is insufficient.
- A trusted LAN. Read [SECURITY.md](../SECURITY.md) before exposing the editor.

## Install

1. Download the app ZIP and `SHA256SUMS` from a published release, or build with [BUILDING.md](../BUILDING.md). GitHub Actions builds are also available under a successful run's **Artifacts** section. Extract the outer Actions artifact first.
2. In the directory containing all release files, run `shasum -a 256 -c SHA256SUMS` (macOS) or `sha256sum -c SHA256SUMS` (Linux). If downloading only the app ZIP, compare its digest with the matching checksum line; missing other files will make the full check fail.
3. Extract `PS5-XMR-Miner-<version>.zip`. Copy the entire `PPSA99531/` folder to the directory scanned by your loader, commonly `/data/homebrew/PPSA99531/`. Use your loader's documented transfer method. This is a folder release, **not a retail PKG**.
4. Rescan in the loader and launch **XMR Miner**. Mining starts stopped. Options opens Settings; scan the QR with a device on the same trusted LAN to open the HTTP editor.
5. Replace `YOUR_WALLET` in both CPU and GPU settings. Set your pools and worker names. Save CPU settings; stop GPU before saving its settings. See [CONFIGURATION.md](CONFIGURATION.md).
6. Start one engine at a time and check connection, accepted/rejected shares, errors, temperature and stability. A connected status or rising hash rate does not prove a pool accepted a share.

## Controller

| Control | Action |
| --- | --- |
| L1 | Start/stop CPU |
| R1 | Start/stop GPU |
| Square | Start/stop both engines |
| Options | Open Settings / save edited settings |
| Left / Right in Settings | Select CPU / GPU |
| Circle in Settings | Cancel |

CPU reports hashes/second; GPU reports graphs/second. Their numbers cannot be added.

## Update / remove

Stop both engines and close the app before replacing its folder. Preserve the title's private saved data: CPU `/download0/xmrig-native.json`, GPU `/download0/c29-pool.json`. `/download0` is an app sandbox path, not the loader folder. Keeping the title identity preserves settings; uninstalling through a loader may remove them, so make a private backup first. Restore only settings you trust.

If the launcher caches an old icon/title, rescan or follow your loader's metadata-refresh procedure. The optional `refresh-title-metadata.py` developer helper directly updates title metadata and is not required for normal installation; see BUILDING.md.

To remove, close the app and use your loader's uninstall procedure. Confirm separately whether it deletes saved settings.

## Troubleshooting

- **App missing:** verify the folder nesting, `sce_sys/param.json`, icon, and loader compatibility.
- **App fails / allocation error:** close other apps, relaunch, and test CPU and GPU separately. Other firmware is unverified.
- **No mining:** replace placeholders, check pool protocol, DNS/proxy settings and connectivity. GPU TLS errors require valid CA roots and hostname verification, not disabling verification.
- **GPU won't restart after fence timeout:** close and relaunch the app; resource ownership is deliberately treated as uncertain after a timeout.
- **Phone cannot connect:** use the same LAN, the exact current QR address and firewall rules that permit only your device. The dynamic port changes between launches.
