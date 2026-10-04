# Changelog

Changes to this adaptation are recorded here. The upstream project history
remains in Git.

## 0.3.2 - 2026-10-04

- Add local dashboard pet customization: persistent names, three built-in skins,
  Codex v1/v2 WebP sheet import and bounded BPT1 animation assets. Keep
  appearance independent of progression, validate uploads and preserve old assets.
- Skip contracted custom-pet poses below 90% of the largest opaque pose in each
  state while preserving the stored BPT1 bytes and playback timing.
- Compress dashboard HTML on the build host to retain C3 OTA flash headroom.

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

The four-profile application-only images, manifest, checksums and license
notices are published with the matching `v0.3.2` GitHub Release. Validation,
including the latest animation correction, is documented in
[RELEASE_VALIDATION_0.3.2.md](docs/RELEASE_VALIDATION_0.3.2.md).

## 0.2.4 feature groundwork - 2026-10-03

- Add a persistent Spanish/English preference shared by the dashboard,
  round-display pages and Wi-Fi setup portal.
- Add dashboard Wi-Fi reconfiguration with test-before-save, cancellation and
  recovery that preserve the saved configuration and device data.
- Add setup-flow QR views for joining the open configuration AP and opening its
  portal, with the portal QR kept separate from the local dashboard QR.
- Add source-rendered Spanish/English display previews, dashboard captures and
  provenance documentation for the current feature build.

## 0.3.0 feature history - 2026-10-04

- Add local Adagotchi Phase 1: persistent NVS pet progress, bounded feeding,
  novelty bonuses, six evolutions and active-time-only health decay.
- Preserve cooldowns and the rolling 30-reward hourly limit across reboot/OTA.
- Add animated pet home/state pages alongside existing AdBlock controls,
  swipe navigation, QR access and Wi-Fi setup; use the same clipped renderer.
- Expose aggregate-only `/pet.json`, including explicit active-uptime period
  semantics, approximate unique counts and persistence/device diagnostics.
- Add host engine/gesture tests, circular preview containment checks and a
  live DNS feeding verifier. Preserve existing settings and partition layouts.

See [Phase 1 validation](docs/ADAGOTCHI_PHASE1_VALIDATION.md) and the
[customization validation](docs/ADAGOTCHI_CUSTOMIZATION_VALIDATION.md) for
historical evidence and the physical-validation limits of that work.

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
