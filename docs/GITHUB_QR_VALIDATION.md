# GitHub updates and dashboard QR validation

Validation date: 2026-10-03. Repository: `felipedelpozo/adblock`.

## Delivered behavior

- Dashboard checks stable GitHub Releases over certificate-validated HTTPS.
- Installation requires a newer compatible release, explicit confirmation and the exact checked version. Image size, SHA-256, chip/profile/board and embedded version identity are checked before activation.
- Firmware downloads run in a bounded background task. A shared OTA lock coordinates GitHub, browser uploads and ArduinoOTA.
- Network displays an **ABRIR DASHBOARD** button. Its QR contains only the current local dashboard URL; tap/swipe dismisses the modal without activating another control. Loss of connectivity closes it.
- Five-page swipe navigation and incremental stripe rendering are retained. The QR uses a cached 79-byte matrix, with no full framebuffer or LVGL.
- Release tooling and read-only GitHub Actions produce four application images, `manifest.json` and `SHA256SUMS`. Publication is a maintainer action.

## Hardware and installed firmware

Guition JC3636W518C, ESP32-S3 revision 0.2, 16 MiB flash, 8 MiB OPI PSRAM, ST77916 360×360 QSPI display and CST816 touch (reported ID `0xB6`). USB port: `/dev/cu.usbmodem83201`. Dashboard during validation: `http://192.168.31.107/`.

Installed application identity: `ADBLOCK_ID:jc3636w518c:0.1.0`.

- Application binary: 1,161,712 bytes.
- SHA-256: `d23fced1bfe02a8c2cd1c7b6636cea55e5877bbdae0468a428838020ac60f9f7`.
- USB upload: PASS, including upload hash verification.
- Final exact binary: PASS through browser-compatible HTTP multipart OTA after the final upload guard correction; response `200`, successful reboot and serial boot verification.
- Saved Wi-Fi, 99,643 domains, custom/banned lists and blocklist update settings were preserved. Partition tables, NVS and LittleFS were not erased or uploaded.

## Automated validation

| Check | Result |
| --- | --- |
| PlatformIO native suites | PASS: 26 cases |
| Python suites | PASS: 7 cases covering blocklist, QR and release packaging |
| Dashboard JavaScript | PASS: 3 cases covering gating, CSRF requests and confirmed/pinned installation |
| Four hardware profile builds | PASS |
| Version override | PASS: compiled `0.1.1` identity verified, then headless build restored to `0.1.0` |
| QR decoding | PASS: actual bundled encoder and adapter matrix decoded by macOS Vision as `http://192.168.31.107/` |
| Diff whitespace check | PASS |

Final build sizes (linked flash differs from packaged ESP image byte size):

| Profile | Static RAM bytes | Linked flash bytes | OTA slot bytes |
| --- | ---: | ---: | ---: |
| c3 | 72,260 | 1,095,950 | 1,376,256 |
| round-display | 73,956 | 1,181,236 | 1,376,256 |
| s3-headless | 78,108 | 1,058,421 | 2,097,152 |
| jc3636w518c | 80,932 | 1,161,353 | 2,097,152 |

Partition layouts are unchanged and retain dual OTA and LittleFS.

## Live device validation

- PASS: web dashboard, DNS sinkhole and suffix blocking, upstream forwarding, 5/30-minute pause and resume.
- PASS: actual device HTTPS request to GitHub. The repository has no published release; the dashboard correctly reports **No hay releases publicadas** and disables installation.
- PASS: missing/invalid nonce and cross-origin update requests rejected with `403`; unconfirmed/mismatched install rejected with `409`.
- PASS: empty/non-multipart and invalid manual firmware uploads rejected with `400`, without reboot. This includes the regression that previously dereferenced a missing upload object.
- PASS: 35 DNS samples during a GitHub check, median 21.08 ms and p95 31.03 ms. These are local observations, not a throughput guarantee.
- PASS: serial display/touch initialization, maximum observed status stripe render 4,356 µs, approximately 219 KB free heap after the check, minimum observed updater task stack margin approximately 2,496 bytes.

## Reproduction

Use the versions in `platformio.ini` and the CI workflow. With PlatformIO/Python/Node available:

```sh
pio test -e native
python -m unittest discover -s tests -v
node --test tests/test_dashboard_firmware.cjs
ADBLOCK_FW_VERSION=0.1.0 pio run -e c3 -e round-display -e s3-headless -e jc3636w518c
python tools/package_release.py --version 0.1.0 --output release
pio run -e jc3636w518c -t upload --upload-port /dev/cu.usbmodem83201
python tools/verify_device.py 192.168.31.107
python tools/verify_github_update.py 192.168.31.107 --reject-invalid-ota
curl --fail-with-body --max-time 60 -F f=@.pio/build/jc3636w518c/firmware.bin http://192.168.31.107/update
```

Upload commands restart the device; run them only when an installation is intended. Do not use `erase_flash` or `uploadfs` for an application update.

## Pending evidence and limitations

- PENDING: physical phone scan of the QR from the display and dismissal by touch/swipe. Host decoding and navigation tests passed; they do not prove optical scanning on the hardware.
- UNAVAILABLE: full installation from a real GitHub Release, because none is published. The current device runs `0.1.0`; a future end-to-end update test needs a newer version, such as `0.1.1`, built with that embedded identity and published with its matching assets.
- Existing LAN administration/manual OTA have no login. The update nonce provides CSRF protection, not authentication.
- Release authenticity relies on HTTPS and repository control. Independent signing and post-boot health rollback are not implemented.
- No source commit, push or GitHub release publication was performed. The source archive includes the existing swipe changes and these additions. Private flash backups, serial logs and credentials are excluded.

See [GitHub firmware update instructions](GITHUB_FIRMWARE_UPDATES.md) for the release contract and security/concurrency behavior.
