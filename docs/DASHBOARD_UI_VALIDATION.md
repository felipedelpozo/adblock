# Dashboard and round UI validation — 2026-10-03

This revision adds a recent blocked-query history and refreshes both user interfaces.
The original hardware bring-up report remains in `DEVICE_VALIDATION.md`.

## Implementation

- `src/query_log.h`: fixed ring of 64 blocked DNS requests (17,420 bytes on ESP32), newest first, bounded domain copies and allocation-free capture.
- `src/blocked_log.cpp/.h`: JSON history endpoint, 16-row pagination, case-insensitive domain/client search, DNS type, reason and relative age. JSON escapes arbitrary DNS bytes.
- `src/main.cpp`: records blocklist/custom/client blocks and exposes `/blocked.json`; web and touch pause state remains shared.
- `src/page.h`: offline-capable Spanish dashboard with readable activity table, filtering/paging, safe DOM rendering, action error handling, preservation of unsaved settings and mobile layout.
- `src/display.cpp`: teal/ink circular layout, central percentage, compact counters, network/IP/RSSI panel, state-aware controls and portal status. Eight-row clipping and frozen snapshots retained; round robin scheduling avoids starving other regions.
- `test/test_query_log/`: retention, overwrite/order, copy bounds, search and JSON byte escaping. Touch tests cover exact control boundaries.
- `tools/verify_history.py`: repeatable live checks; its test queries intentionally replace volatile history without changing saved settings.

The history resets on reboot and does not write to flash. There is no LVGL, full-screen framebuffer or new library dependency. Partitions, both OTA slots, saved Wi-Fi, custom rules and the 99,643-domain list are preserved.

## Validation

| Check | Result | Evidence |
|---|---|---|
| C3 headless build | PASS | 1,058,708 linked flash bytes; 72,020 static RAM bytes |
| C3 round display build | PASS | 1,135,794 linked flash bytes; 73,588 static RAM bytes |
| S3 headless build | PASS | 1,020,689 linked flash bytes; 77,868 static RAM bytes |
| JC3636W518C build | PASS | 1,115,301 linked flash bytes; 80,556 static RAM bytes |
| Native tests | PASS | 11 cases including bounded history, control boundaries and pause/resume/portal repaint regression |
| Python blocklist tests | PASS | 2 cases |
| Embedded browser JavaScript syntax | PASS | Node syntax check |
| Live DNS/dashboard/pause/resume | PASS | `verify_device.py` and actual browser controls |
| Live recent history | PASS | 80 blocked requests, capacity 64, pagination, search, limits and AAAA |
| Custom reason and JSON escaping | PASS | a temporary hostile-looking custom domain round-tripped as text; rule removed |
| Browser injection regression | PASS | the literal HTML-looking rule displayed as text; zero injected image nodes and no dialog |
| Saved settings through OTA | PASS | Wi-Fi reconnected, list/custom rules matched pre-update snapshot |
| Actual LCD visibility/touch | PASS | user confirmed new UI readability and working controls |
| Incremental display timing | PASS | maximum observed stripe 8,873 microseconds in network session; final boot 8,075 microseconds; roughly 230 KB free heap |
| Final network firmware upload | PASS | Arduino OTA returned OK and rebooted |
| Mobile layout | PASS | 390 px viewport, page width 375 px after table/accessibility clipping fix; horizontal scroll confined to tables |

All four firmware profiles still fit their existing application slots. The C3 profiles remain compile-only validated because no C3 hardware is attached.

## Commands

From the repository with the workspace tooling environment active:

```sh
pio test -e native
python -m unittest discover -s tests -v
pio run -e c3 -e round-display -e s3-headless -e jc3636w518c -j 4
pio run -e jc3636w518c -t upload --upload-port 192.168.31.107
python tools/verify_device.py 192.168.31.107
python tools/verify_history.py 192.168.31.107
git diff --check
```

Browser checks exercised actual table rows, next-page navigation, domain filtering, pause/resume, and a temporary custom rule. No filesystem upload, credential clearing, partition change or whole-chip erase was performed in this revision. The final reboot cleared synthetic history test entries; subsequent requests are captured normally.

Final firmware: **1,115,664 bytes**, SHA-256 `1aef64d379a6f14c8d1b6a26caa5cac58eaf2abc85eccf4a674143f0225d7f8f`. Dashboard: http://192.168.31.107/.

## Limits

Only the latest 64 blocked queries are retained; this is a recent activity window, not persistent analytics. Rows may shift between pages as new requests arrive. The upstream synchronous DNS timeout and remote-download behavior are unchanged. The initial updater's interrupted-replacement limitation remains documented in README. DNS filtering cannot reliably remove YouTube ads when advertising and video content share domains.

The final installed dashboard was checked again at 390 px: document width 375 px (vertical scrollbar excluded), with no page-level horizontal overflow. Screenshot: `outputs/dashboard-historial.jpg`. The final browser console showed no JavaScript errors.
