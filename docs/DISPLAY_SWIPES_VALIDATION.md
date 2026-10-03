# Five-page display validation — 2026-10-03

This revision implements the approved round-screen proposal. The dashboard
and blocked-query history are unchanged; the preceding revision is recorded
in `DASHBOARD_UI_VALIDATION.md`.

## Implementation

- `src/display.cpp/.h`: Status, Activity, Lists, Network and Controls pages;
  large main values, Spanish labels, circular safe margins and five position
  dots. `setPage` cancels the old page's paint work. `pageReady` tracks the
  first complete repaint independently of ongoing statistics changes.
- `src/touch_model.h`: allocation-free contact tracking, horizontal swipes
  and release-only taps. Vertical/curved drags, long presses and interrupted
  contacts cannot press a pause button. Clock wraparound is supported.
- `src/touch.cpp/.h`: bounded 20 ms CST816 coordinate polling throughout
  contact, chip-ID probing and error cancellation.
- `src/ui.cpp` and `src/ui_model.h`: cyclic page navigation and controls only
  on the Controls page. The renderer and hit tests share button rectangles.
  Start and end positions must hit the same enabled button.
- `test/test_ui_model/`: gesture, page order, control bounds, pause-state,
  percentage overflow and clock-wrap coverage.

Swiping left advances; swiping right returns. Navigation wraps at either end
and starts on Status after reboot. Pause and resume remain shared with the
web dashboard. Indefinite web pauses are displayed without a misleading
automatic-resume countdown. Portal controls are disabled.

Rendering remains one clipped eight-row logical stripe per loop pass, with
frozen regional snapshots and visible-page dirty masks. There is no LVGL,
full-screen framebuffer, new dependency, extra rendering task or partition
change. DNS, OTA and persistent storage code were not changed.

## Build and automated validation

| Check | Result | Evidence |
| --- | --- | --- |
| Native tests | PASS | 18 cases across UI/gestures, blocking state and query history |
| Python blocklist tests | PASS | 2 cases |
| C3 headless | PASS | 1,058,892 linked flash bytes; 72,092 static RAM bytes |
| C3 round display | PASS | 1,137,826 linked flash bytes; 73,684 static RAM bytes |
| S3 headless | PASS | 1,020,857 linked flash bytes; 77,940 static RAM bytes |
| JC3636W518C | PASS | 1,117,301 linked flash bytes; 80,644 static RAM bytes |
| Diff whitespace | PASS | `git diff --check` |

The S3 display build uses 53.3% of its 2 MiB OTA application slot. Both OTA
slots and the 11.875 MiB LittleFS partition are retained. C3 profiles are
compile-verified; no C3 board is connected.

## Connected device

Read-only identification confirmed the existing JC3636W518C's ESP32-S3
revision 0.2, 16 MB quad flash and 8 MB PSRAM on `/dev/cu.usbmodem83201`.
USB firmware upload succeeded and verified the written data hash. No whole
flash erase, filesystem upload or Wi-Fi reset was performed.

| Live check | Result | Evidence |
| --- | --- | --- |
| Firmware write | PASS | USB upload; 1,117,664-byte application; verified hash |
| Display/touch initialization | PASS | `display=ok`, CST chip `0xB6`, `touch=ready` |
| Saved Wi-Fi | PASS | Reconnected at `192.168.31.107` |
| Stored blocklist | PASS | 99,643 domains before and after upload |
| DNS, web and OTA startup | PASS | `DNS :53 + dashboard :80 + OTA up` |
| DNS and shared pause state | PASS | Sinkhole, suffix blocking, upstream resolution, web 5/30-minute pauses and resume |
| Dashboard preservation | PASS | Served HTML byte-identical before and after update |
| Recent blocked history | PASS | Endpoint returns all three sinkhole-test entries with client/type/reason |
| Stripe timing at initial Status view | PASS | Maximum observed 4,693 microseconds; about 230 KB free heap |
| Actual screen readability/swipes/buttons | PENDING | Awaiting user confirmation on the physical panel |

Firmware SHA-256:
`502ec3f7570a1e60e9a30471e105116e05c9caf95d3a37d2c0513d4b9b001f25`.

## Reproduce

```sh
pio test -e native
python -m unittest discover -s tests -v
pio run -e c3 -e round-display -e s3-headless -e jc3636w518c
pio run -e jc3636w518c -t upload --upload-port /dev/cu.usbmodem83201
pio device monitor --port /dev/cu.usbmodem83201 --baud 115200
python tools/verify_device.py 192.168.31.107
git diff --check
```

Swipe and tap serial messages include the resulting page and action. Physical
validation should visit all five pages in both directions, check wraparound,
press 5 minutes / 30 minutes / resume, and confirm that dragging over a pause
button changes page without pausing DNS. Captive portal provisioning and an
actual OTA write were not repeated in this display-only revision; their code
and the partition layout are unchanged.
