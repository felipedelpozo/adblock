# AdBlock 0.3.2 release validation

Recorded on 2026-10-04 for the `v0.3.2` source and four application-only OTA
images. The release includes the Adagotchi Phase 1 engine, dashboard pet
customization and the contracted-pose playback correction. The published
images are the byte-verified artifacts from the hosted tag workflow
[37236096096](https://github.com/felipedelpozo/adblock/actions/runs/37236096096).

## Regression and build gates

PASS: 63 native tests, including the PetEngine and PetAsset suites.
PASS: 35 dashboard Node.js tests, including `tests/test_pet_dashboard.cjs`.
PASS: 16 Python tests with one optional OpenCV decoder test skipped.
PASS: all four PlatformIO profiles compiled with `ADBLOCK_FW_VERSION=0.3.2`.
PASS: deterministic dashboard and pet gzip headers matched their generators.
PASS: package validation found exactly the four required profiles, matching
ESP image chip IDs, embedded profile/version markers, OTA size limits and
manifest SHA-256 values.

| Profile | Static RAM | Application bytes | OTA slot | Remaining bytes |
| --- | ---: | ---: | ---: | ---: |
| `c3` | 126,772 | 1,220,432 | 1,376,256 | 155,824 |
| `round-display` | 128,836 | 1,328,608 | 1,376,256 | 47,648 |
| `s3-headless` | 132,660 | 1,130,640 | 2,097,152 | 966,512 |
| `jc3636w518c` | 135,812 | 1,241,312 | 2,097,152 | 855,840 |

The C3 round-display image has limited remaining OTA headroom. Keep the
existing partition layout and check the actual padded `.bin` size before adding
firmware features.

## Animation correction

The final playback map is computed once per asset and occupies 24 bytes. A pose
whose opaque width or height is below 90% of the largest pose in its state
repeats the preceding stable pose. The BPT1 asset remains byte-for-byte
unchanged, all four states retain six frames and playback remains 4 Hz. The
correction evidence reports 63/63 native, 35/35 Node and 16 Python passes with
one optional OpenCV skip; physical display observation was unavailable.

## Release assets

The generated `release/manifest.json` contains these application images:

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `firmware-c3.bin` | 1,220,432 | `57558542903a5710fad052501283d2c27816be33ad5f9276700ebe2eaa7dd4da` |
| `firmware-round-display.bin` | 1,328,608 | `609254deb2786eaa605a773fe726a463bfd05546e8811d8b8a0e74662502f60a` |
| `firmware-s3-headless.bin` | 1,130,640 | `ba8a81858677309182b17933706f0c3e31c3147cce3e8ded373475b0ba7e40a3` |
| `firmware-jc3636w518c.bin` | 1,241,312 | `c12083483fe2227e99ae39cfc2537e0963ddbb9d5b29ae352ab5f310e5a2a1fd` |

`manifest.json` SHA-256: `9d4f2d58b4096caaf6895079609847a1d6213b8f96e0a9640ce3714a2d7e838a`.
`SHA256SUMS` SHA-256: `27ca178efeb30dc0e99463513a80f1edcd133323064746b70f25d3c8abcb5090`.
The GitHub Release also carries `LICENSE` and the complete `licenses/` notices.

The local macOS build was used for the pre-publication size and marker checks.
Its firmware bytes are not distributed because framework diagnostic strings can
contain absolute build-machine paths; the table above records the hosted CI
artifacts that were downloaded from the release and verified byte-for-byte.

## Hardware scope and limits

The physically observed target is the Guition JC3636W518C ESP32-S3 with 16 MB
flash, 8 MB PSRAM, ST77916 360×360 QSPI display and CST816 touch. The C3,
round-display and S3-headless profiles are compile-verified. USB serial capture
and physical display/touch observation were unavailable in this release run;
host renderer and gesture tests do not substitute for those checks.

The application images are OTA payloads for compatible existing partitions.
They do not include a bootloader, partition table or filesystem image. DNS
filtering remains subject to shared ad/content domains, alternate resolvers,
VPNs, browser encrypted DNS and application-owned encrypted DNS.
