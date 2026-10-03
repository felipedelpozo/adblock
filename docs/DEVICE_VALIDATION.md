# Device validation — 2026-10-03

Status: implementation, USB installation, live DNS/dashboard/HTTP OTA and persistence
checks complete. The user confirmed the screen and all three touch controls on the device.

## Confirmed target

- User-provided model: Guition **JC3636W518C**, stock SW **V0.9.1**.
- USB: `/dev/cu.usbmodem83201`, VID:PID `303a:1001`.
- Read-only esptool identification: **ESP32-S3 QFN56 revision 0.2**,
  **16 MB quad flash**, **8 MB embedded OPI PSRAM**, 40 MHz crystal.
- Actual firmware strings: **ST77916 QSPI**, **CST816**, 360-pixel media assets.
- Manufacturer demo and independent source agree on the pinout in `README.md`.
- No ESP32-C3 was connected. C3 profiles are compile-only verified.

## Backup and preservation

A complete **16,777,216-byte** pre-write flash backup is retained privately at:

`/Users/felipedelpozo/Documents/Codex/2026-10-03/referenced-chatgpt-conversation-this-is-an/work/device-backup/original-flash.bin`

SHA-256: `0c924c6a8696a90ad0ebf6dd8f6f7bc483b9496bf169b3cb584672109e3632d8`.

It is excluded from the repository and distributable archive because it can
contain personal settings. File permissions are restricted.

The original partition table was read from the backup. NVS remains at
`0x9000/0x5000`, app0 at `0x10000/0x200000`, app1 at `0x210000/0x200000`,
and coredump at `0xff0000/0x10000`. The original `ffat` region at
`0x410000/0xbe0000` contained **zero non-0xff bytes**. Only that erased region
was converted to LittleFS and initially populated. There was no whole-flash
format/erase, NVS erase, credential clearing or SD-card access.

Firmware upload erase ranges exclude NVS and LittleFS. Subsequent firmware
uploads retained the 99,643-domain blocklist. The original factory app was
replaced as requested; its complete image remains recoverable from the backup.
Restoring the whole backup later would also replace settings saved since then.

## Validation results

| Check | Result | Evidence / limitation |
|---|---|---|
| Chip / flash / model identification | PASS | esptool reads + model supplied by user |
| Full pre-write backup | PASS | 16 MB file and SHA-256 above |
| `c3` build | PASS | 1,038,654 linked flash bytes; 54,596 static RAM bytes |
| `round-display` C3 build | PASS | 1,112,882 linked flash bytes; 56,148 static RAM bytes |
| `s3-headless` build | PASS | 1,000,801 linked flash bytes; 60,460 static RAM bytes |
| `jc3636w518c` display build | PASS | 1,092,657 linked flash bytes; 63,132 static RAM bytes |
| Native Unity tests | PASS | 6 tests: pause/resume, rollover, input boundaries, percentage |
| Blocklist generator tests | PASS | 2 tests: normalized sorted unique hashes, preserve old output on empty input |
| Blocklist generation | PASS | 99,643 hashes; 498,215 bytes; both current sources downloaded |
| USB firmware and initial FS writes | PASS | esptool verified hashes of written data |
| Runtime LittleFS/blocklist | PASS | 99,643 domains loaded after repeated firmware updates |
| ST77916 transport initialization | PASS | `display=ok`; actual pixels still require physical confirmation |
| CST816 probe | PASS | actual chip ID `0xB6`, `supported=yes`, `touch=ready` |
| Incremental UI cost | PASS | portal-mode maximum stripe time 7,484 μs; free heap 264,908 bytes |
| Wi-Fi captive portal startup | PASS | AP `C3-AdBlock-F9E0`, address `192.168.4.1` |
| Wi-Fi provisioning | PASS | saved portal credentials subsequently connected; captive probe handling not separately exercised |
| Station Wi-Fi and sinkhole/upstream DNS | PASS | station IP 192.168.31.107; blocked, suffix and upstream queries passed |
| Web dashboard / HTTP firmware OTA | PASS | real dashboard response; same 1,093,024-byte firmware uploaded via its multipart HTTP endpoint and rebooted |
| Arduino OTA live | PASS | espota upload returned OK; device reconnected with blocklist intact and DNS response verified |
| Physical display visibility / three actual touch actions | PASS | user explicitly confirmed image, 5 MIN, 30 M and RESUME; physical interaction validation is user-observed |
| Persistent Wi-Fi/custom rules through OTA reboot | PASS | saved Wi-Fi reconnected; 99,643-domain list and temporary custom rule survived HTTP OTA; rule removed afterward |
| C3 physical display/touch | UNAVAILABLE | only S3 hardware was connected |

Runtime evidence from the final firmware:

```text
[adblock] booting chip=ESP32-S3 flash=16777216 psram=8386295
[round-ui] display=ok touch=scheduled max_render_us=0 free_heap=308560
blocklist: 99643 domains
custom: 0, banned: 0
[setup] No WiFi. Join open network "C3-AdBlock-F9E0" ... http://192.168.4.1
[round-touch] chip=0xB6 supported=yes
[round-ui] touch=ready render_max_us=7484 heap=264908 wifi=portal ip=192.168.4.1
```

Arduino logs missing `custom.txt`, `banned.txt`, `update.cfg` and absent NVS
Wi-Fi namespace on the initial empty installation. These are expected defaults,
not failed blocklist mounting. An early touch-reset recovery timing issue was
corrected; the final boot detects the chip without the initial I²C read error.

## Commands used

Tooling was installed into the workspace's `work/tooling` Python 3.12 venv.
Below are the equivalent commands when that environment is active:

```sh
git clone https://github.com/M-Abozaid/esp32-c3-adblock outputs/esp32-c3-adblock-round
python -m serial.tools.list_ports -v
esptool --port /dev/cu.usbmodem83201 --baud 115200 chip-id
esptool --port /dev/cu.usbmodem83201 --baud 115200 flash-id
esptool --port /dev/cu.usbmodem83201 --baud 460800 read-flash 0 ALL work/device-backup/original-flash.bin
# From the cloned repository:
python tools/build_blocklist.py data/blocklist.bin
pio run -e jc3636w518c -e c3 -e round-display -e s3-headless -j 4
pio test -e native
python -m unittest discover -s tests -v
pio run -e s3-headless -t upload --upload-port /dev/cu.usbmodem83201
# One initial filesystem upload, after verifying that region was erased:
pio run -e s3-headless -t uploadfs --upload-port /dev/cu.usbmodem83201
pio run -e jc3636w518c -t upload --upload-port /dev/cu.usbmodem83201
git diff --check
```

A bounded pyserial reader at 115200 baud captured the real boot and diagnostics;
`pio device monitor --port /dev/cu.usbmodem83201 --baud 115200` is the standard
interactive equivalent.

## Reproducing the live validation

1. Join **C3-AdBlock-F9E0** from a phone and open **http://192.168.4.1**.
   Enter Wi-Fi credentials only in that local portal, never in chat.
2. Read the resulting station IP and run `python tools/verify_device.py DEVICE_IP`.
3. Confirm the screen visually and capture actual 5 MIN, 30 M and RESUME presses.
4. Exercise the dashboard and upload the same `firmware.bin` through browser OTA;
   verify the inactive OTA slot boots and NVS/LittleFS settings remain intact.

The original synchronous DNS upstream timeout and blocking remote-download
behavior remain. During 227 blocked DNS queries over eight seconds, the maximum
recorded UI stripe was 6,066 microseconds. Local query RTT median was 22.56 ms
and maximum 33.09 ms (single client, 10 ms pacing); this is a bounded smoke test,
not a capacity benchmark.
The original blocklist updater may fail open after an interrupted replacement.
See `README.md` for the preserved behavior and safe update instructions.

Final flashed firmware.bin: 1,093,024 bytes; SHA-256 `cd3f9f33aff3dd22691245e401e143085d9a1d030e55c1f7798e315da54ee343`.

## Live network evidence

`python tools/verify_device.py 192.168.31.107` passed twice, including after HTTP OTA.
It verified dashboard HTTP, domain and parent-suffix sinkholes, upstream DNS,
5/30-minute pauses, resume, and restoring the initial pause state.

A temporary `round-ota-validation.invalid` custom rule was added through
`/addblock`, the exact final firmware uploaded to `/update` using Python urllib
with multipart form data, and its rule/blocklist/Wi-Fi persistence checked after
reboot. The temporary rule was removed and absence rechecked.

```text
WiFi up: 192.168.31.107
dashboard: http://c3adblock.local
DNS :53 + dashboard :80 + OTA up
[round-ui] touch=ready render_max_us=6066 heap=247164 wifi=connected ip=192.168.31.107
PASS: dashboard, block/suffix, upstream, 5/30 minute pauses and resume
PASS: HTTP dashboard firmware OTA accepted 1093024 bytes
PASS: Wi-Fi, blocklist, custom rule and update config persisted after OTA reboot
PASS: temporary validation rule removed
```

The UI and all three touch controls were confirmed by the human user. Serial
confirmed a physical contact with controller coordinates; automation cannot
observe the LCD pixels directly. The HTTP OTA API was exercised directly,
rather than through a browser file picker.

Arduino OTA also passed: `pio run -e jc3636w518c -t upload --upload-port 192.168.31.107` returned **Result: OK / SUCCESS** (17.25 seconds). After its reboot, Wi-Fi reconnected, the list still contained 99,643 domains, no temporary rule remained, and the sinkhole returned `0.0.0.0`. PlatformIO selects espota automatically for a network upload port.
