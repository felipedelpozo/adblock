# Security policy

## Scope and limitations

ESP AdBlock Round is designed for use on a trusted local network. The dashboard, browser OTA, Arduino OTA, captive portal, and update administration are unauthenticated. Do not expose the device or its administrative endpoints to the public Internet, and do not treat the local QR code as an authentication mechanism.

GitHub firmware updates use certificate-validated HTTPS and verify the repository, release metadata, profile, embedded identity, size, and SHA-256 before activating the inactive OTA slot. SHA-256 provides integrity checking; releases do not currently have independent signatures or automatic post-boot rollback.

## Reporting a vulnerability

If this repository has GitHub private vulnerability reporting enabled, use the **Report a vulnerability** action in the repository's Security tab. If it is unavailable, contact the repository owner through the public GitHub profile at [github.com/felipedelpozo](https://github.com/felipedelpozo) and request a private reporting channel. Do not include sensitive exploit details, credentials, flash dumps, or personal data in a public issue.

Please include the affected release or commit, hardware profile, impact, and a minimal sanitized reproduction when it is safe to do so. Allow maintainers reasonable time to investigate before public disclosure.
