# Language validation

This historical language validation used local firmware **0.2.3** on
2026-10-04. The current source/local build is **0.2.4**; this document records
the earlier build and appliance installation, and does not announce a public
release.
The connected target is the Guition JC3636W518C ESP32-S3 with ST77916 display
and CST816 touch. Existing Wi-Fi, balanced/HaGeZi PRO profile, 227,420 domains,
custom settings and exceptions were retained.

## Automated checks

| Check | Result |
| --- | --- |
| Native models, including strict locale codes and status templates | PASS, 41 tests |
| Dashboard tests, including drafts, stale responses, save failures and nonce gating | PASS, 28 tests |
| Python tooling tests | PASS, 14 tests |
| Spanish/English source display previews | PASS, six views per locale |
| Language change during incremental QR rendering | PASS, same final pixels and QR matrix |
| Diff whitespace checks | PASS |

Host renderer clock values are synthetic and do not measure hardware latency.

All four firmware builds passed with the existing dual-OTA partitions:

| Profile | Static RAM | Application flash |
| --- | ---: | ---: |
| `c3` | 123,324 / 327,680 bytes | 1,184,184 / 1,376,256 bytes |
| `round-display` | 125,044 / 327,680 bytes | 1,270,580 / 1,376,256 bytes |
| `s3-headless` | 129,196 / 327,680 bytes | 1,147,093 / 2,097,152 bytes |
| `jc3636w518c` | 132,020 / 327,680 bytes | 1,251,401 / 2,097,152 bytes |

The installed JC3636W518C application SHA-256 is
`f2431d5049ded94f886612f2f7a3239259bf2c1c415ea1bf4d1ed0e20699fe53`.
Only the application was installed through manual OTA; NVS, LittleFS,
bootloader and partition tables were preserved.

## Live-device checks

- PASS: unsupported/malformed language codes rejected; missing, invalid and
  cross-origin CSRF rejected; GET cannot change the preference.
- PASS: Spanish/English switching without reboot or loss of DNS blocking.
- PASS: English preference survives a controlled USB restart.
- PASS: Wi-Fi portal inherits the language, switches both ways, rejects invalid
  requests and safely cancels back to the saved network.
- PASS: English portal rejects malformed credentials and a nonexistent network,
  recovers the previous connection and preserves the preference on cancel.
- PASS: dashboard selector changes the actual device; an unsaved URL draft
  survives the change; reload keeps English and discards the unsaved draft.
- PASS: sinkhole/suffix matching, upstream forwarding, 5/30-minute pauses and resume.
- PASS: bounded history, ordering, pagination, search, request data and AAAA entries.
- PASS: GitHub HTTPS check while DNS remains responsive; invalid/empty manual
  OTA images rejected without reboot. DNS median 33.66 ms / p95 47.66 ms over
  75 samples during this check; this is a local observation, not a benchmark.
- PASS: serial boot reports `ADBLOCK_ID:jc3636w518c:0.2.3`, `display=ok`,
  touch chip `0xB6` supported, populated blocklist and Wi-Fi/DNS/web/OTA startup.

The first live history run failed a cross-request count comparison while the
appliance was serving traffic. A repeat of the unchanged verifier passed;
its comparison assumes the volatile ring does not change between requests.
The GitHub verifier initially assumed Spanish status messages. It now checks
the exact expected prefixes for the current language and passed in English.

## Limits

Only the S3 round-display profile was installed on hardware. C3 and headless
profiles were compiled. Documentation screenshots use synthetic data;
display images are source renders, not photographs. Physical readability
and swipe interaction with the new translations await user confirmation.
Native file-picker captions follow the browser/OS locale.

No Git commit, push, tag or public release was created for this change.
Private device logs and backups remain outside the repository.
The appliance was left in Spanish with blocking active and 227,420 domains.
