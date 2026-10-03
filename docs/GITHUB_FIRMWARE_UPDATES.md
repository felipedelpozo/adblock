# GitHub firmware updates

The dashboard uses stable, public releases from
[felipedelpozo/adblock](https://github.com/felipedelpozo/adblock).
**Comprobar actualizaciones** only checks for a compatible version. When a newer
version is ready, **Instalar VERSION** asks for confirmation and installs that
specific checked version. It does not silently switch to a later release.
There is no automatic background installation.

The ESP fetches the GitHub API, release manifest and application image over
HTTPS, validating certificates against embedded USERTrust RSA/ECC, DigiCert
Global Root G2 and ISRG Root X1 trust anchors. It synchronizes time with SNTP first; unavailable
NTP, invalid certificates, rate limiting or network failures leave the active
firmware unchanged. The browser does not download release assets, so GitHub
asset CORS and WebCrypto on local HTTP origins are not requirements.

A FreeRTOS worker performs downloads, yielding between bounded stream writes.
The C3 shares its single core with the main loop; the S3 worker uses core 0.
The existing synchronous upstream DNS/blocklist paths retain their original
latency limits. HTTPClient decodes chunked responses. Release metadata is capped
at 32 KiB, the manifest at 8 KiB; a filtered JSON parser discards release-body
metadata. Socket/connect timeouts are 8 seconds, text streams have a 30-second
deadline and firmware streams a 120-second deadline (a stalled socket can add
its timeout before returning). Display rendering remains incremental.

## Release contract

Tags must be stable `vMAJOR.MINOR.PATCH`, with no leading zeros or prerelease
suffix. Each component has at most six digits. `/releases/latest` selects a
stable release; 404 reports **No hay releases publicadas**.

Publish these six files together under the matching release tag:

- `manifest.json`
- `SHA256SUMS`
- `firmware-c3.bin`
- `firmware-round-display.bin`
- `firmware-s3-headless.bin`
- `firmware-jc3636w518c.bin`

The schema-1 manifest contains `repository`, `version` and a `builds` array.
Each build declares `profile`, `version`, `chip`, `board`, `asset`, canonical
GitHub `url`, integer byte `size` and lowercase hexadecimal `sha256`.
`tools/package_release.py` creates this contract from all four compiled images.
It rejects wrong ESP image chip IDs, wrong compiled profile/version markers,
images larger than their existing OTA slots and compile-time `src/secrets.h`.
All images are validated before output files are written.

The device verifies the exact repository, release tag, schema, profile,
chip/board, asset name and URL. It rejects missing or duplicated matching
profiles, old/equal versions, invalid sizes, SHA values and slot overflow.
The streamed image must also contain the compiled identity
`ADBLOCK_ID:PROFILE:VERSION` including its NUL terminator, even across chunks.
Size, SHA-256 and identity must match **before** OTA activation. A failed write
or verification aborts the inactive-slot transaction. Updates preserve NVS,
Wi-Fi settings and LittleFS; they never upload a partition table, bootloader or
filesystem. This is pre-activation verification, not automatic post-boot health
rollback. Authenticity relies on HTTPS and control of the GitHub repository;
SHA-256 is an integrity check, not an independent release signature.

## Build release assets

```sh
ADBLOCK_FW_VERSION=0.1.0 pio run -e c3 -e round-display -e s3-headless -e jc3636w518c
pio test -e native
python -m unittest discover -s tests -v
node --test tests/test_dashboard_firmware.cjs
python tools/package_release.py --version 0.1.0 --output release
```

For another version, use the same version in the build environment and package
command. The embedded header default is used when no override is provided.
The packager rejects relabeling an old binary with a new release version.

`.github/workflows/firmware.yml` validates the tag version, compiles it into all
four profiles, runs tests and uploads the six release files as a GitHub Actions
artifact along with `LICENSE` and third-party notices. Branch/PR builds use the header default. The workflow has read-only
repository permissions and does not publish releases. A maintainer reviews the
artifact, creates the matching stable release and attaches its six files.
The inherited browser installer and its stale C3 binaries have been removed;
`docs/index.html` links to the source and installation instructions.

## API and concurrency

`/stats.json` exposes `fwVersion`, `fwProfile`, `githubStatus`, `githubVersion`,
`githubBusy`, `githubCanInstall`, `githubProgress` and a boot-scoped
`githubNonce`. `POST /github/check` and `POST /github/install?v=VERSION` require
that nonce in `X-CSRF-Token` and an `Origin` matching the device IP or
`http://c3adblock.local`. The install request must match the checked version.
A new check clears the previous candidate, including when the new check fails.
The nonce is a CSRF defense; it is not an administrator login.

A mutex coordinates all three firmware methods. Browser uploads are activated
only after their full request completes. The complete `ArduinoOTA.handle()`
call is guarded because ArduinoOTA starts writing before its start callback.
An upload rejected as busy never aborts another transaction. The GitHub
installer retains the OTA lock through successful reboot.

The existing dashboard/manual OTA remain unauthenticated for trusted LAN use.
Do not expose administrative endpoints to the Internet. The QR on Network
contains only the local dashboard address and requires the same Wi-Fi/AP.
