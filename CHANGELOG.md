# Changelog

Changes to this adaptation are recorded here. The upstream project history
remains in Git.

## 0.1.1 - 2026-10-03

- Prepare the first public firmware release with a stable `0.1.1` identity
  across the C3, round-display, S3-headless and JC3636W518C profiles.
- Publish the manifest, checksums and application-only OTA images through the
  GitHub Release update contract, with third-party notices included alongside
  distributed binaries.
- Keep the dashboard QR instructions inside the round display's narrow lower
  edge by shortening the Wi-Fi and return captions; the QR itself remains
  unchanged.
- Document the tested ESP32-S3 hardware path and the C3 compile-only profile;
  physical installation and optical QR scanning remain device-validation work.

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
