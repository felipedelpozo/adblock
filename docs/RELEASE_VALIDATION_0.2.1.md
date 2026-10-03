# AdBlock 0.2.1 release validation

Recorded on 2026-10-03. Only the final implementation with a scoped reader per
lookup is counted below. The unpublished 0.2.0 candidate and earlier local
0.2.1 builds failed physical filesystem testing and are excluded.

## Regression and build checks

PASS: 30 native tests, 14 Python tests and 13 dashboard Node tests. All four
PlatformIO environments compile with the existing dual-OTA partition layouts.

| Environment | Static RAM | Firmware flash / OTA slot |
| --- | ---: | ---: |
| c3 | 123,156 bytes | 1,135,804 / 1,376,256 bytes |
| round-display | 124,868 bytes | 1,221,120 / 1,376,256 bytes |
| s3-headless | 129,020 bytes | 1,098,433 / 2,097,152 bytes |
| jc3636w518c | 131,844 bytes | 1,201,845 / 2,097,152 bytes |

These are local compiler figures; ESP application-image headers/alignment add
packaging bytes. C3 hardware was not physically tested.

## Published list distribution

PASS: [first publication](https://github.com/felipedelpozo/adblock/actions/runs/37141660262)
and [existing-asset verification](https://github.com/felipedelpozo/adblock/actions/runs/37142118345).
The public manifest, binaries and source archive were downloaded independently
and checked for SHA-256, size, count and sorted unique five-byte little-endian
hashes. Existing immutable assets are verified before reuse.

Pinned HaGeZi revision: `913ee73a7b41aee70c943e422e19e16b7be55c47`.

| Profile | Domains | Binary bytes |
| --- | ---: | ---: |
| Light | 51,311 | 256,555 |
| PRO / balanced | 227,420 | 1,137,100 |
| PRO++ / strict | 248,298 | 1,241,490 |

Source-form lists, attribution and GPL-3.0 notices accompany the separate
`blocklists` prerelease. Daily execution at 03:17 UTC is configured; future
scheduled runs are not claimed as already verified.

## Physical device and filesystem regression

Target: Guition JC3636W518C, ESP32-S3, 16 MB flash and 8 MB PSRAM. The device
retained Wi-Fi provisioning, settings and list data; no filesystem erase or
partition change was used. A private LittleFS backup remains outside the repo.

The discarded candidate reproduced `lfs_file_read_` assertions through a
long-lived reader. A mutex-only attempt still failed. The final implementation
removes that reader and owns one local stream per DNS lookup, including its
open, seek, read and close lifecycle under a shared filesystem mutex. Staging
writers and activation use the same coordinator. A failed lookup returns
SERVFAIL instead of silently forwarding. The precise internal cause of the
stale descriptor in the rejected candidate has not been proven.

PASS: three consecutive public HTTPS profile installations with continuous
single-attempt DNS checks and an unchanged boot nonce during each download:

| Installed profile | DNS samples | Median | p95 |
| --- | ---: | ---: | ---: |
| Light | 85 | 47.97 ms | 93.74 ms |
| PRO++ | 199 | 60.57 ms | 101.75 ms |
| PRO | 198 | 54.99 ms | 100.12 ms |

All 482 queries blocked `googlesyndication.com`, which is present in all three
pinned lists. Each UDP query has a three-second deadline and no retry. List
status polling uses the dashboard's 450 ms cadence. These are end-to-end Wi-Fi
measurements under this test load, not a general latency guarantee.

Two further serial-monitored PRO++ runs each recorded one three-second UDP timeout,
while the boot nonce stayed unchanged. Both later activated successfully. Serial
showed no assertion, unexpected reboot or failed reader. These losses are retained
as unresolved under-load observations, not attributed to radio conditions.
The additional stress PRO++ run had 198 successful queries plus one timeout;
the following PRO run passed 209 queries (median 55.80 ms, p95 94.90 ms).
The following monitored PRO run passed all 190 DNS queries (median 46.95 ms,
p95 92.46 ms). Missing staging/config-file diagnostics are expected cleanup
messages, not read failures. A subsequent 200-query idle baseline passed
(median 40.71 ms, p95 50.85 ms). No UDP retry masks a failure.
The final firmware also logs filesystem and web operations lasting at least
100 ms to support diagnosis without recording queried domain names.
On that final diagnostic build, PRO++ then PRO passed 210 and 191 queries
respectively, with unchanged boot nonces. Median/p95 were 57.52/97.15 ms and
73.23/115.85 ms. Logged lookup delays were approximately 100–157 ms, and test
web requests peaked at 228 ms; no multi-second filesystem stall was recorded.
This successful repeat does not erase the earlier two timeout observations.
Serial contained only the intentional USB reset and application-OTA reboot,
with no LittleFS assertion, unexpected restart or failed-reader diagnostic.
The local diagnostic application SHA-256 was
`12b2e25146dce31fabfd480abcc44415cda107b06566632aa2a0f62624d44bf3`.
The public CI application is verified separately below.

PASS: controlled USB reboot retained the installed profile/list and temporary
custom/allowed rules; DNS exception precedence remained correct. The temporary
rules were removed afterward. PRO remains installed as the recommended default.

PASS: malformed domain/profile rejection; Origin/CSRF checks on new mutations;
allowed-domain precedence and removal; rejection of unsorted, multi-file and
empty list uploads while preserving the active list.

PASS: dashboard, upstream resolution, parent-domain blocking, five/thirty-minute
pause and resume. Blocked-history verification also passed bounded retention,
newest-first order, pagination, search and AAAA logging using a domain present
in HaGeZi profiles. GitHub HTTPS release checks preserved DNS service and rejected
unconfirmed installation, missing/invalid/cross-origin tokens and empty/invalid
manual firmware uploads without reboot. This check ran on 0.2.1 while the latest
public stable release was still 0.1.1; it did not exercise a newer-version
GitHub installation. The prior 0.1.0 to 0.1.1 installation is recorded separately.

## Published firmware and final device checks

PASS: [main CI](https://github.com/felipedelpozo/adblock/actions/runs/37146121112)
and [tag CI](https://github.com/felipedelpozo/adblock/actions/runs/37146362153)
for source `e5d5f9ac1178ca78c259483fc558dc2cef38f3f4`.
The immutable `v0.2.1` tag points to that source.
The public [0.2.1 release](https://github.com/felipedelpozo/adblock/releases/tag/v0.2.1)
contains four application images, manifest, SHA256SUMS and license archive.
The public files match the tag CI artifact byte-for-byte. The latest stable
endpoint returns `v0.2.1`; the `blocklists` prerelease remains separate.
Existing `v0.1.1` and discarded `v0.2.0` tags were not changed.

| Public image | Packaged bytes | OTA slot bytes |
| --- | ---: | ---: |
| c3 | 1,181,984 | 1,376,256 |
| round-display | 1,280,832 | 1,376,256 |
| s3-headless | 1,098,480 | 2,097,152 |
| jc3636w518c | 1,201,856 | 2,097,152 |

The public JC3636W518C image SHA-256 is
`b35888d3f9833b115ab5726a043432d928a2ede971fccf045fbdd3ac8a83886f`.
Its size, chip, board, profile and embedded 0.2.1 identity were checked before
manual application OTA. After installation, the changed boot nonce and serial
identity confirmed restart into the published image. Wi-Fi, list/profile,
update settings and temporary custom/allowed rules were retained. Exception
precedence still worked; temporary test rules were then removed.

PASS on the exact public image: PRO refresh from the public list manifest,
196 single-attempt DNS queries (median 61.41 ms, p95 106.67 ms), unchanged boot
nonce and 227,420 active domains. New mutation checks and invalid-list retention
also passed. Serial showed only the intentional monitor-opening USB reset and
application-OTA reboot, no assertion, failed read or unexpected restart.
Display initialization and CST816 detection were reported ready.

PASS: GitHub HTTPS check against the published release with 74 DNS queries
(median 33.27 ms, p95 46.24 ms), CSRF/Origin and unconfirmed-install rejection,
and empty/invalid manual firmware rejection without reboot. Dashboard,
upstream, suffix blocking, five/thirty-minute pause and resume also passed.
PRO remains active. This final installation used manual OTA at the same
version; it does not claim a fresh older-to-newer GitHub-button installation.

## Limits

Physical results apply to the S3 board above. This release does not change the
display/gesture design. Phone-camera QR scanning is still unverified. Android
Private DNS, application-owned encrypted DNS and alternate IPv6 resolvers can
bypass the sinkhole; shared ad/content domains cannot be separated by DNS.
Actual application ad coverage needs application-specific verification.

Administration requires a trusted LAN. CSRF is not user authentication.
Independent release signatures and automatic post-boot health rollback are not
implemented. C3 may reject a large list if it cannot stage old and new files
with the required headroom; rejection must preserve the active list.
