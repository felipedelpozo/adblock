# AdBlock firmware 0.2.0

Choose HaGeZi Light, PRO or PRO++ from the dashboard, refresh on demand and
keep the selected profile updated on the configured schedule. Existing
installations retain their current list as Custom until a profile is applied.

Persistent domain exceptions override domain filtering for the specified
name and its subdomains. The blocked-query history offers a shortcut to allow
a domain; client bans still take precedence. Exceptions survive restart and OTA.

Curated lists are published in a separate daily GitHub prerelease, with
content-addressed binaries, pinned source archives and GPL license notices.
The firmware checks HTTPS, repository URLs, size, domain count, SHA-256 and
sorted unique hashes. It stages a new file while continuing to use the old
list; failed downloads and insufficient storage leave the active list intact.
The existing dual OTA and LittleFS partition layouts are unchanged.

This release includes application-only OTA images for `c3`, `round-display`,
`s3-headless` and `jc3636w518c`, together with the manifest, SHA256SUMS and
license archive. Select the image matching an already compatible installation.
Use PlatformIO for a first USB installation.

DNS filtering can block app advertising and tracking on separate domains.
It cannot distinguish ads from content served through the same domain, and
Android Private DNS or application-owned encrypted DNS can bypass the device.
PRO++ may require more exceptions and more free flash space than Light.

The physically tested board is the Guition JC3636W518C ESP32-S3. The ESP32-C3
profiles are compile-verified. Administration remains intended for a trusted
LAN, with no user authentication, independent release signatures or automatic
post-boot health rollback. Optical phone-camera QR scanning remains unverified.

See [blocklist profiles](https://github.com/felipedelpozo/adblock/blob/main/docs/BLOCKLIST_PROFILES.md)
and [release validation](https://github.com/felipedelpozo/adblock/blob/main/docs/RELEASE_VALIDATION_0.2.0.md)
for the update contract, license provenance and recorded checks.
