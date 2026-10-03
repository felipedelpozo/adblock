# Changelog

Changes to this adaptation are recorded here. The upstream project history
remains in Git. Firmware version `0.1.0` is the current local development
identity; no GitHub release has been published yet.

## Unreleased

- Add Guition JC3636W518C support: ESP32-S3, ST77916 QSPI 360×360 and CST816 touch.
- Retain headless C3/S3 and ESP32-2424S012C GC9A01 profiles in one PlatformIO project.
- Add five incremental display pages with horizontal swipe navigation, pause
  controls and a local dashboard QR.
- Add a searchable, paginated history of the last 64 blocked DNS requests in RAM.
- Add confirmed GitHub Release updates with validated HTTPS, compatible profile
  selection, embedded version identity, image size and SHA-256 verification.
- Coordinate GitHub, browser and Arduino firmware OTA; reject invalid manual
  uploads without rebooting or aborting another update.
- Preserve Wi-Fi, NVS, custom domains, client bans and LittleFS through firmware OTA.
- Add native, Python and dashboard tests, four-profile CI and release packaging.
- Replace the inherited C3 web installer and stale binary images with setup guidance.

Validation and remaining hardware/release evidence are documented in
[GITHUB_QR_VALIDATION.md](docs/GITHUB_QR_VALIDATION.md).
