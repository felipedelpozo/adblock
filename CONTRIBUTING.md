# Contributing

Thanks for helping improve ESP AdBlock Round. Keep pull requests focused, describe the user-visible effect, and include the profile and hardware used when firmware behavior is involved.

## Development setup

The project uses PlatformIO with Arduino, Python 3.12 or newer, and Node.js 22 or newer. The CI workflow pins Python 3.12, Node 22, and `platformio==6.1.19`; use those versions when reproducing CI results.

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install platformio==6.1.19 esptool pyserial
pio run -e c3 -e round-display -e s3-headless -e jc3636w518c
pio test -e native
python -m unittest discover -s tests -v
node --test tests/test_dashboard_*.cjs
```

The display build supplies the bundled encoder used by the host QR test. A host
C/C++ compiler is required; check for skipped Python tests on a fresh machine.

The four profiles are:

| Profile | Target |
| --- | --- |
| `jc3636w518c` | Confirmed Guition ESP32-S3 with ST77916 QSPI display and CST816 touch; default |
| `s3-headless` | The same ESP32-S3 hardware without display features |
| `c3` | Original ESP32-C3, headless |
| `round-display` | ESP32-2424S012C with GC9A01 display and CST816 touch |

USB identification reports the chip and flash, not the PCB or LCD pinout. Select a profile from the physical board documentation and README, never from a USB vendor ID alone. The C3 display profile is compile-verified; physical C3 display testing requires the matching board.

## Tests and hardware reports

Native, Python, and Node tests can run without a device. A successful build or device boot proves compilation and initialization only. Display visibility, touch gestures, Wi-Fi behavior, DNS blocking, OTA, and captive-portal behavior require the corresponding physical hardware and should be reported separately, including the board, firmware version, and test method.

Do not commit `src/secrets.h`, credentials, flash backups, or device-specific private data. Flash backups may contain saved settings and belong outside the repository.

## Pull requests

Before opening a pull request, run the relevant tests and builds, update documentation when behavior or release procedures change, and explain any hardware checks that were unavailable. Keep source code and technical documentation in English. Review the generated diff for unrelated changes.

The dashboard and manual OTA endpoints are unauthenticated and intended for a trusted LAN. Do not expose a device or its administrative endpoints to the public Internet while testing or demonstrating a change.

## Releases

CI validates stable `vMAJOR.MINOR.PATCH` tags, builds all four profiles, runs the test suites, and uploads a release directory as an artifact. A maintainer manually reviews that artifact and publishes a GitHub release containing these six files together:

- `manifest.json`
- `SHA256SUMS`
- `firmware-c3.bin`
- `firmware-round-display.bin`
- `firmware-s3-headless.bin`
- `firmware-jc3636w518c.bin`

See [`docs/GITHUB_FIRMWARE_UPDATES.md`](docs/GITHUB_FIRMWARE_UPDATES.md) for the manifest contract and packaging commands. Releases are not published automatically by CI. The update path validates HTTPS, profile, version, size, SHA-256, and embedded identity before activation; it has no independent release signatures or automatic post-boot rollback.

## Curated blocklists

The daily `blocklists.yml` workflow publishes a separate prerelease using
content-addressed binaries and pinned HaGeZi source/license archives. See
[blocklist profiles](docs/BLOCKLIST_PROFILES.md) for format, retention, space
checks and physical verification. Never overwrite an existing firmware release
to refresh domain lists.
