# Wi-Fi reconfiguration validation

This historical validation was recorded on 2026-10-04 for locally built
firmware 0.2.2. The current source/local build is 0.2.4. This document does
not claim a public release or tag; the public 0.2.1 release remains unchanged.

## Automated checks

PASS: 34 native tests, 20 dashboard Node tests and 14 Python tests. The seven
new dashboard checks cover escaped/text-only network rendering, offline and
busy states, canceled confirmation, protected POST, duplicate clicks, conflict
recovery, and an in-flight stats response arriving after setup starts.

PASS: all four PlatformIO environments, retaining existing dual-OTA/LittleFS
partitions with no new library dependency.

| Environment | Static RAM bytes | Firmware flash bytes / OTA slot |
| --- | ---: | ---: |
| c3 | 123,324 | 1,148,934 / 1,376,256 |
| round-display | 125,036 | 1,234,236 / 1,376,256 |
| s3-headless | 129,188 | 1,111,905 / 2,097,152 |
| jc3636w518c | 132,012 | 1,215,369 / 2,097,152 |

The compiler figures exclude image packaging alignment. The installed display
application SHA-256 is
`a13fbf67ffcdd0dcf5693db918dda695e5177914cfe05409012452347b07fb2e`.

## Connected device

Target: Guition JC3636W518C, ESP32-S3, 16 MB flash and 8 MB PSRAM. The display
application was installed through its existing manual dashboard OTA endpoint,
without an erase, partition upload, or filesystem upload.

PASS: version/profile, reboot, Wi-Fi, 227,420 domains, balanced profile,
update settings, custom rules and exceptions retained after installation.

PASS: missing/invalid tokens and cross-origin requests rejected without reboot.
The old credential-erasing endpoint returns HTTP 410 without changing state.

PASS: dashboard setup request enters the AP+STA portal. The portal was reached
through the previous LAN address, without changing the host's Wi-Fi. Its form
and status API were verified. Invalid credentials (including an embedded NUL)
and invalid/cross-origin portal tokens were rejected.

PASS: a randomly named nonexistent open network failed to associate, the old
station connection recovered, and the portal showed failure. Cancel returned
to normal operation with saved Wi-Fi, list profile/data and settings retained.

PASS: dashboard, sinkhole/suffix blocking, upstream DNS, 5/30-minute pauses,
resume, bounded query history and invalid OTA rejection after recovery. A
public HTTPS release check also completed while 79 DNS samples succeeded
(median 33.36 ms, p95 41.21 ms). These are measurements for this check only.

PASS: an intentional USB monitor reset returned to normal operation; serial
reported firmware identity, display initialization, supported CST816 touch,
Wi-Fi, dashboard, DNS and OTA startup. Absent optional legacy settings/staging
files produced existing diagnostics; no filesystem assertion was observed.

## Limits

A successful change to a different real network has not been physically
verified; no real replacement password was supplied. Credential encoding and
validation are covered by native tests. Initial provisioning from empty NVS,
the ten-minute idle return, and C3 hardware were not physically exercised in
this change. Physical touch interactions were previously user-confirmed;
this change only reuses the display/touch service during setup.

The dashboard documentation screenshot uses synthetic network data. Source
and this local firmware are not claimed as published to GitHub.
