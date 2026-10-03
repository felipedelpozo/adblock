# Setup Wi-Fi QR validation

Local firmware **0.2.4**, validated on 2026-10-04. The connected target is
the Guition JC3636W518C ESP32-S3. This change has not been published as a
GitHub release and remains pending a firmware release.

The installed application SHA-256 is
`d4eb161719078a80fdc6220e6af14d38a66e0d3f0a5854b43ec461539d6ab6b0`.
Application-only manual OTA preserved NVS, LittleFS, the current Wi-Fi,
balanced/HaGeZi PRO list with 227,420 domains, settings and exceptions.

## Automated validation

- PASS: 46 native model tests, including open-network payload formatting,
  reserved-character escaping, capacity/control/termination rejection,
  portal-only gating and navigation through both QR steps.
- PASS: the existing QR encoder harness checks version 2 / 25×25 URL and
  version 3 / 29×29 setup matrices, finder patterns, view switching,
  cache reuse and invalidation.
- PASS: Python suite, 15 passed. One optional OpenCV decoding test skipped
  because OpenCV is not installed; this does not validate phone scanning.
- PASS: macOS Vision independently decodes the source-rendered Wi-Fi QR to
  `WIFI:T:nopass;S:C3-AdBlock-ABCD;;` and the portal QR to `http://192.168.4.1/`
  in both languages. QR squares, quiet zones and captions remain inside the
  circle; language switches preserve all three QR matrices.
- PASS: all four firmware profiles compiled with the existing partitions.
- PASS: diff whitespace checks.

| Profile | Static RAM | Application flash |
| --- | ---: | ---: |
| `c3` | 123,324 / 327,680 bytes | 1,184,192 / 1,376,256 bytes |
| `round-display` | 125,092 / 327,680 bytes | 1,272,034 / 1,376,256 bytes |
| `s3-headless` | 129,196 / 327,680 bytes | 1,147,101 / 2,097,152 bytes |
| `jc3636w518c` | 132,076 / 327,680 bytes | 1,252,829 / 2,097,152 bytes |

The QR cache is one 106-byte packed matrix. The display retains its clipped
stripe renderer. No additional production library, framebuffer, partition
change or password access was introduced.

## Live appliance

The serial/web validation enters the Wi-Fi portal without submitting new
credentials, observes the automatic Network / setup-Wi-Fi QR selection with
touch ready, then cancels. Normal connection, language, lists, exceptions
and DNS blocking are checked after recovery.

PASS during the live portal run: the diagnostic reports
`page=network qr=wifi touch=ready`, and cancellation restores sinkhole replies
without changing the saved configuration. No replacement credentials were
submitted.

The final layout correction moves the lower instructions inward after the
host containment test detected five clipped pixels. Both locales then passed
with zero clipped caption pixels. The corrected firmware was rebuilt,
installed, and verified to retain the existing configuration; its navigation
model tests also cover a delayed AP name becoming available.

The standard QR payload only represents the current open setup AP. The
second QR still represents `http://192.168.4.1/`. First-time setup and Wi-Fi
reconfiguration follow the same display flow. Instructions are documented
in [Wi-Fi setup](WIFI_SETUP.md).

## Limits

Hardware validation covers the S3 display target. Other targets are compiled.
Documentation display images are deterministic host renders. Optical scanning
of the physical panel and the phone's join-network prompt require user
confirmation; the phone's captive-portal behavior depends on its OS.
Device logs and private state backups remain outside the repository.
