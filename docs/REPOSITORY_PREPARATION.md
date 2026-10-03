# Public repository preparation

Prepared on 2026-10-03 for https://github.com/felipedelpozo/adblock.
This report describes local preparation; no source commit, push or release
publication was performed. The GitHub repository was public and empty when checked.

## Prepared contents

- README with overview, first-install sequence, hardware/pinout table, router DNS
  guidance, build/flash instructions, maintenance links and test-status caveats.
- MIT license retaining the upstream copyright and adding adaptation attribution;
  vendor/QR/LovyanGFX/ArduinoJson notices retained.
- CONTRIBUTING, SECURITY, CHANGELOG, issue forms, pull-request template and
  a maintainer release checklist.
- Read-only four-profile CI with distribution licenses included in its artifact.
- The obsolete inherited C3 web installer, manifest and five stale flash images
  removed from the current source tree. The landing page now links to this
  adaptation's setup/release documentation. Existing Git history is retained.
- Private/generated flash images, credentials, logs and build output ignored.
  The personal absolute backup path removed from the device report.
- Git remotes prepared: `origin` points to this repository; `upstream` points to
  M-Abozaid/esp32-c3-adblock. Branch `main` has no upstream tracking assignment
  until the first push. Default push destination is `origin`.

## Validation

| Check | Result |
| --- | --- |
| Four PlatformIO builds | PASS: c3, round-display, s3-headless, jc3636w518c |
| Native suites | PASS: 26 cases |
| Python suites | PASS: 7 cases, including real bundled QR encoder |
| Dashboard tests | PASS: 3 cases |
| Release packaging | PASS: four images, embedded identities, slot bounds and checksums |
| YAML parse / issue field IDs / read-only workflow | PASS |
| Local Markdown links / ignored private files / focused secret scan | PASS |
| Diff whitespace check | PASS |
| Hosted GitHub Actions | NOT RUN: source has not been pushed |

The focused source audit checks for local personal paths, private key blocks,
GitHub token patterns and generated binary/log files. It is not an exhaustive
security review. Private backups and raw serial logs are outside the source
archive. Historical upstream README content is retained as an archive.

No firmware or device settings were changed during repository preparation.
Previous live DNS/web/OTA results and pending physical QR/GitHub-release update
checks are recorded in [GITHUB_QR_VALIDATION.md](GITHUB_QR_VALIDATION.md).

## Publication handoff

Review and commit the current working tree, then push `main` to `origin`.
The intended repository description is:

> ESP32 DNS ad blocker with a round touchscreen, local dashboard, blocked-query history and GitHub OTA updates.

Suggested topics: `esp32`, `esp32-s3`, `adblock`, `dns`, `platformio`, `lovyangfx`,
`ota`, `jc3636w518c`.

After the first successful hosted CI run, follow [RELEASING.md](RELEASING.md).
Do not publish an untested GitHub installation path as already validated.
