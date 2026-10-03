# AdBlock firmware 0.1.1

This is the first public AdBlock firmware release. It combines DNS blocking,
the local dashboard and blocked-query history with five swipeable display
pages, pause controls, a local dashboard QR and confirmed GitHub OTA checks.
It packages the four application images supported by the GitHub firmware update
contract:

- `firmware-c3.bin` for ESP32-C3 headless hardware;
- `firmware-round-display.bin` for the C3 round-display profile;
- `firmware-s3-headless.bin` for the ESP32-S3 headless profile; and
- `firmware-jc3636w518c.bin` for the Guition JC3636W518C display profile.

Every image embeds `ADBLOCK_ID:<profile>:0.1.1` and is accompanied by the
schema-1 `manifest.json` and `SHA256SUMS`. The images are application-only OTA
assets: they do not include a bootloader, partition table or LittleFS image and
must only be installed on a compatible existing layout. Wi-Fi settings, NVS and
LittleFS are preserved by the OTA flow.

The round dashboard QR page keeps its QR payload and layout, while its two
instruction captions are shortened to `USA LA MISMA WIFI` and `TOCA O DESLIZA`
so they stay within the narrow lower edge of the circular display. The
host-rendered QR preview and the final tag-CI display build must retain these
captions.

## Validation status

The JC3636W518C ESP32-S3 display profile is the physically observed target. Its
display controls and earlier firmware interactions have been exercised on the
device; the release build and host-rendered display screenshots are reviewed as
release evidence. The C3 profiles are compile-only verified in this release
unless a C3 device result is recorded separately. The optical QR scan from a
phone camera remains pending; a host QR decode does not constitute physical
scan confirmation.

The release checks cover the native firmware suite, Python tooling and QR /
release contract tests, dashboard Node tests, four PlatformIO builds, embedded
profile/version markers, ESP image chip IDs, OTA slot sizes, manifest URLs,
SHA-256 checksums and license notices. Hosted tag-CI results and the final
asset hashes are the publication evidence for the release artifact.

## Security and operational limits

The updater uses HTTPS certificate validation, repository and tag checks,
profile/board compatibility, image-size limits, embedded identity and
SHA-256 verification before OTA activation. The dashboard and manual OTA
endpoints remain intended for a trusted LAN and are not authenticated for
Internet exposure. SHA-256 provides integrity checking; the release has no
independent artifact signature. There is no automatic post-boot health rollback.
Failed checks and failed writes leave the active firmware unchanged.

See [GitHub firmware updates](https://github.com/felipedelpozo/adblock/blob/v0.1.1/docs/GITHUB_FIRMWARE_UPDATES.md)
and the [release checklist](https://github.com/felipedelpozo/adblock/blob/v0.1.1/docs/RELEASING.md)
for the complete contract and installation procedure. End-to-end installation
from the published release must be validated from a device running 0.1.0
before it is described as complete.
