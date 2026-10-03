# AdBlock firmware 0.2.1

Choose HaGeZi Light, PRO or PRO++ from the dashboard and refresh on demand or
on the configured schedule. Existing installations retain their list as Custom
until a curated profile is applied. PRO is the recommended starting point.

Persistent domain exceptions cover a name and its subdomains; the blocked
history offers an Allow shortcut. Client bans take precedence. Exceptions,
Wi-Fi settings and the chosen profile survive restart and compatible OTA.

A separate daily GitHub prerelease distributes content-addressed list binaries,
pinned source archives and GPL notices. Firmware verifies TLS, repository URLs,
size, domain count, SHA-256 and sorted unique five-byte hashes before activation.
Failed downloads and insufficient staging space preserve the active list.
Dual OTA and LittleFS partition layouts are unchanged.

Physical testing rejected the unpublished 0.2.0 candidate after LittleFS reader
assertions. This version eliminates the long-lived list reader: each lookup
opens and closes an owned reader under the filesystem mutex. Read errors return
DNS SERVFAIL rather than silently forwarding a potentially blocked domain.
Repeated profile updates and preservation checks are recorded in the report.
Additional monitored runs observed isolated UDP timeouts during download
without reboot or a filesystem error. Its cause is not established; update-load
DNS responses are not guaranteed under every network condition.

The assets are application-only OTA images for `c3`, `round-display`,
`s3-headless` and `jc3636w518c`, with the manifest, SHA256SUMS and license archive.
Use the exact matching image for an already compatible installation; use
PlatformIO for the first USB installation.

The physically tested board is Guition JC3636W518C ESP32-S3. C3 profiles are
compile-verified. DNS cannot separate ads and content on the same domain;
Android Private DNS and application-owned encrypted DNS can bypass the device.
PRO++ may need more exceptions and free flash. Administration requires a trusted
LAN; user authentication, independent release signatures and automatic health
rollback are not implemented. Optical phone-camera QR scanning is unverified.

See [profiles](https://github.com/felipedelpozo/adblock/blob/main/docs/BLOCKLIST_PROFILES.md)
and [validation](https://github.com/felipedelpozo/adblock/blob/main/docs/RELEASE_VALIDATION_0.2.1.md).
