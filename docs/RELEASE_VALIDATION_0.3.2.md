# AdBlock 0.3.2 release validation

Recorded on 2026-10-04 for the `v0.3.2` source and four application-only OTA
images. The release includes the Adagotchi Phase 1 engine, dashboard pet
customization and the contracted-pose playback correction.

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
| `c3` | 126,772 | 1,220,720 | 1,376,256 | 155,536 |
| `round-display` | 128,836 | 1,328,928 | 1,376,256 | 47,328 |
| `s3-headless` | 132,660 | 1,130,976 | 2,097,152 | 966,176 |
| `jc3636w518c` | 135,812 | 1,241,664 | 2,097,152 | 855,488 |

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
| `firmware-c3.bin` | 1,220,720 | `7c46b3fe5a6bf5ffcfe70cac626550df1fd866f86553c232584188d22dbc79ea` |
| `firmware-round-display.bin` | 1,328,928 | `d0807ff0a24f1a5fd6d8f0fd3ebed15f4b4af61488a5a486dc74e4b73cf39fce` |
| `firmware-s3-headless.bin` | 1,130,976 | `66a438532df54f7dbf41745df557687c4bc9131685dc2f30bc6e802de2de9eae` |
| `firmware-jc3636w518c.bin` | 1,241,664 | `d18ede21f18919bb9221d302dc0be754af6fc3c9c4238202ffda0b64cd540a07` |

`manifest.json` SHA-256: `f5e1c511d5770c4307d8505820c4c648a83cdf50790a76778588675efd102be6`.
`SHA256SUMS` SHA-256: `35d3f5b3032cd46e251f5fd9190763d75ff433acdf3de7ed366135ae9885d2b6`.
The GitHub Release also carries `LICENSE` and the complete `licenses/` notices.

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
