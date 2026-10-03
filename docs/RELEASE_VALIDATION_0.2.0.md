# AdBlock 0.2.0 release validation

Validation recorded on 2026-10-03. This report distinguishes local checks,
hosted distribution and physical device behavior.

## Local regression and build checks

PASS: 30 native tests, 14 Python tests and 13 dashboard Node tests. All four
PlatformIO environments compile with the existing partition layouts:

| Environment | Static RAM | Firmware flash / slot |
| --- | ---: | ---: |
| c3 | 123,188 bytes | 1,134,680 / 1,376,256 bytes |
| round-display | 124,900 bytes | 1,219,988 / 1,376,256 bytes |
| s3-headless | 129,044 bytes | 1,097,201 / 2,097,152 bytes |
| jc3636w518c | 131,868 bytes | 1,200,605 / 2,097,152 bytes |

These are local build figures; ESP image packaging adds its own header/alignment.
Both workflow YAML files parse and the diff passes whitespace checks.

## Curated list distribution

PASS: [Curated blocklists run 37141660262](https://github.com/felipedelpozo/adblock/actions/runs/37141660262)
built and published the separate `blocklists` prerelease. Public assets were
downloaded and their SHA-256, five-byte alignment, counts and strict numeric
sort/uniqueness verified independently. The source archive and SHA256SUMS
also match their manifest entries.

Pinned HaGeZi commit: `913ee73a7b41aee70c943e422e19e16b7be55c47`.

| Profile | Domains | Binary bytes |
| --- | ---: | ---: |
| Light | 51,311 | 256,555 |
| PRO / balanced | 227,420 | 1,137,100 |
| PRO++ / strict | 248,298 | 1,241,490 |

The source archive includes exact source texts, attribution and GPL-3.0 license.
The daily schedule is configured; elapsed future scheduled executions are not
claimed as already tested.

## Physical device checks

The observed Guition JC3636W518C has ESP32-S3 rev0.2, 16 MB flash and 8 MB
PSRAM. A private read-only LittleFS backup preceded installation. Manual
browser OTA to the locally built 0.2.0 passed and retained Wi-Fi, settings and
the existing 99,643-domain custom list. USB identification/reset did not erase flash.

PASS: CSRF and malformed-domain rejection; exception precedence and removal;
unsorted aligned list rejection; rejection of multiple file parts and empty
uploads; old-list retention on failed uploads. A temporary exception survived
reboot and was removed afterward. No temporary test rule remains installed.

The first live curated download exposed `EBUSY` on activation: ESP LittleFS
refuses to replace an open destination. The DNS reader now closes immediately
before the atomic rename and reopens the surviving path on success or failure.
The old list continued serving during the rejected activation. The firmware
also retries temporary manifest 404/server failures during GitHub asset refresh;
the publisher verifies bytes before reusing existing immutable assets.

A second download activated PRO but the legacy probe queried `doubleclick.net`,
which is intentionally absent from HaGeZi Light/PRO. The test now queries
`googlesyndication.com`, present in all three pinned source profiles. Separately,
staging now reserves capacity once rather than running a filesystem-size
traversal on every received chunk, preserving headroom and checking byte
budgets/write results throughout the download.

PASS: the corrected PRO refresh from the public GitHub manifest completed,
activated 227,420 domains and retained DNS blocking throughout. The test took
183 DNS samples, median 32.65 ms and p95 81.39 ms, with no failed queries.

## Limits

C3 profiles are compile-verified, without matching physical C3 hardware tests.
The display/gesture code is unchanged in this release. Optical phone-camera
QR scanning remains unverified. Actual Android application ad coverage needs
app-specific testing: shared advertising/content domains and alternate DNS
resolvers can limit DNS filtering.

Administration is intended for a trusted LAN; Origin/CSRF checks do not provide
user authentication. Releases have no independent signatures or automatic
post-boot health rollback. C3 devices may reject large profiles when there is
insufficient room to stage both the active and replacement list.
