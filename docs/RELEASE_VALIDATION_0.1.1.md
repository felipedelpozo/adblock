# AdBlock 0.1.1 release validation

Status: **PASS** for the published GitHub release and the physically observed
Guition JC3636W518C update path. Validation recorded on 2026-10-03.

## Release and hosted CI

- Release: [AdBlock firmware v0.1.1](https://github.com/felipedelpozo/adblock/releases/tag/v0.1.1)
- Source commit and tag target: `0f10e1428c82fd82cec219427f6b547f1b316d4c`
- Tag: `v0.1.1`
- Tag workflow: [Firmware builds run 37132192332](https://github.com/felipedelpozo/adblock/actions/runs/37132192332), conclusion `success`
- CI artifact: `esp32-firmware-0f10e1428c82fd82cec219427f6b547f1b316d4c`

The tag workflow passed native tests, all four PlatformIO builds, Python
tests, dashboard Node tests, release packaging and distribution license
inclusion. The public release uses the tag-CI binaries, not separately built
local binaries.

## Published assets

The public `manifest.json` and `SHA256SUMS` were downloaded from the release and
verified against the four binaries. Every image has the expected ESP image
header and chip ID, profile/board metadata, `0.1.1` embedded identity, size and
SHA-256, and remains within its OTA slot.

| Asset | Size | SHA-256 |
| --- | ---: | --- |
| `manifest.json` | 1,624 bytes | `06fb40c891d59ccdf859422a5741fe7aa35578e552ef2121cb64392758f33b71` |
| `SHA256SUMS` | 357 bytes | `e19e5741727293a6671bbd31d03f45a08ab0ef3c7ecf5d989114b7e70361bc02` |
| `firmware-c3.bin` | 1,138,192 bytes | `a44711a7ee55cf3b94a8e9f2fea26a797c9864c69ea6bec41ebe86f551b1f8ff` |
| `firmware-round-display.bin` | 1,237,008 bytes | `31873bd180072e01a6cb3cb0f7b457dc141ac12e4e0c756c56c7b9c1b9f7d49c` |
| `firmware-s3-headless.bin` | 1,058,480 bytes | `e18572b1de5b71eb3cdcf9a00aee3931f13e30cb2c51ad7d0e088016714f82e7` |
| `firmware-jc3636w518c.bin` | 1,161,344 bytes | `a8c0773dbfdef541a2aa2bcbaad2472235cc7004e357971ddb5d9587e66de4a6` |
| `adblock-firmware-v0.1.1-license-notices.tar.gz` | 7,125 bytes | `1f13ff4e4f041329c997761292fcf1287eb6c3bc547d59cabe2c2de6c2a0eafd` |

The license archive contains `LICENSE` and all files under `licenses/`,
including `NOTICE.md` and the vendor notices.

## Physical update and device behavior

The observed JC3636W518C ESP32-S3 device was updated from firmware `0.1.0`
through the dashboard GitHub check/install endpoints to the published `0.1.1`
release. The install reached 100% with `Firmware verificado; reiniciando`,
then reported a new boot nonce and version `0.1.1`. The device retained its
IP, Wi-Fi connection, 99,643 loaded domains, custom settings, upstream URL and
interval.

After reboot, sequential device checks passed for the dashboard web path,
suffix blocking, upstream resolution, five-minute pause, thirty-minute pause
and resume. The GitHub update verifier with invalid OTA checks also passed:

- current version reported up to date at `0.1.1`;
- invalid, cross-origin and wrong-token CSRF requests returned `403`;
- an unconfirmed install returned `409`;
- empty and invalid manual uploads returned `400` without rebooting;
- 73 DNS timing samples passed with median `27.66 ms` and p95 `33.17 ms`.

One earlier invocation overlapped a pause-mutating check; the harness was then
run sequentially and passed. This was an execution-order issue, not a product
failure. No USB hardware flash was performed.

The C3 profiles remain compile-only verified. Optical phone-camera QR scanning
remains pending; host QR decoding and source-derived display previews do not
constitute physical optical scan evidence.

See the [GitHub update contract](https://github.com/felipedelpozo/adblock/blob/main/docs/GITHUB_FIRMWARE_UPDATES.md)
and [release notes](https://github.com/felipedelpozo/adblock/blob/main/docs/RELEASE_V0.1.1.md)
for the update limitations, including trusted-LAN administration, no
independent artifact signature and no automatic post-boot rollback.
