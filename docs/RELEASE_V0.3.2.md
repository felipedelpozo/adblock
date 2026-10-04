# AdBlock firmware 0.3.2

This release adds the local Adagotchi pet and dashboard customization to the
existing DNS sinkhole, Wi-Fi setup, bilingual UI and GitHub OTA flows.

The dashboard supports persistent names, three built-in skins and conversion of
Codex v1/v2 WebP sprite sheets into a bounded BPT1 animation asset. Appearance
is independent of pet progression. During playback, contracted poses below 90%
of the largest opaque pose in their state repeat the preceding stable pose; the
stored asset bytes and 4 Hz timing remain unchanged.

The release contains application-only OTA images for `c3`, `round-display`,
`s3-headless` and `jc3636w518c`, plus `manifest.json`, `SHA256SUMS`, `LICENSE`
and third-party license notices. Use the exact profile with an existing,
compatible partition layout; a first installation still requires a PlatformIO
build and partition review.

The physically observed target is the Guition JC3636W518C ESP32-S3 with its
ST77916 360×360 display and CST816 touch controller. The other profiles are
compile-verified. USB serial capture and physical display/touch observation
were unavailable during this release validation, so generated previews and
host tests do not substitute for those checks.

DNS filtering remains subject to shared ad/content domains, alternate resolvers,
VPNs, browser encrypted DNS and application-owned encrypted DNS. Administration
is intended for a trusted LAN; authentication, independent release signatures
and automatic post-boot rollback are not implemented.

See [release validation](RELEASE_VALIDATION_0.3.2.md),
[Adagotchi Phase 1 validation](ADAGOTCHI_PHASE1_VALIDATION.md) and
[customization validation](ADAGOTCHI_CUSTOMIZATION_VALIDATION.md).
