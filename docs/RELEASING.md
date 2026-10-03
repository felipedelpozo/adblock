# Release checklist

Releases are reviewed and published by a maintainer. CI builds artifacts with
read-only permissions; it does not create a release or push source changes.

## Before publication

- Review the source diff, changelog, licenses and documented limitations.
- Keep compile-time Wi-Fi credentials, `.env` files, full-flash backups, generated
  files and private logs out of commits. Use the captive portal for provisioning.
- Run native/Python/dashboard tests and compile all four PlatformIO environments.
  Python QR tests require a built display profile and a host C/C++ compiler.
- Confirm display, touch/swipes and phone QR scanning on the actual target board.
  State clearly when a profile is compile-only verified.
- Verify DNS, web, pause/resume and settings preservation on a test device.
- Select a stable `MAJOR.MINOR.PATCH` version. Build with `ADBLOCK_FW_VERSION`
  set to that exact value and use the same value for packaging. Do not relabel
  an existing binary. A test of the GitHub installation path needs a version
  newer than the one installed on the device (currently `0.1.0`).

```sh
ADBLOCK_FW_VERSION=0.1.1 pio run -e c3 -e round-display -e s3-headless -e jc3636w518c
pio test -e native
python -m unittest discover -s tests -v
node --test tests/test_dashboard_firmware.cjs
python tools/package_release.py --version 0.1.1 --output release
```

`0.1.1` above is an example, not a published version. When running locally,
include `LICENSE` and `licenses/` with a distributed binary archive. CI copies
them into its release artifact automatically.

## Publish and verify

1. Commit the reviewed source and changelog, push `main`, and inspect the first
   GitHub Actions run. Local checks do not prove the hosted workflow passed.
2. Create the matching stable tag, for example `v0.1.1`, on the reviewed commit.
   Wait for its firmware workflow; download and review the artifact.
3. Create a public, stable GitHub Release at that tag. Attach the six required
   files from the artifact: `manifest.json`, `SHA256SUMS` and all four
   `firmware-PROFILE.bin` files. Include license notices with binary distributions
   and link the source/tag in the release notes.
4. Describe supported/tested hardware, known limitations and any migration steps.
   The application-only binaries are for compatible OTA layouts; they are not
   complete first-install flash images.
5. From the old firmware, check the release in the dashboard, confirm the exact
   offered version and install. Verify the rebooted identity, DNS/web, Wi-Fi,
   lists/settings and touch. Record actual results before marking the release
   installation path validated.

See [the update contract](GITHUB_FIRMWARE_UPDATES.md). Failed GitHub checks or
image verification must leave the active firmware unchanged. HTTPS and GitHub
repository control provide authenticity; independent signing and automatic
post-boot health rollback are not implemented.
