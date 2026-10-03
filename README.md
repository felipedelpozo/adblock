# ESP AdBlock Round

An adaptation of [M-Abozaid/esp32-c3-adblock](https://github.com/M-Abozaid/esp32-c3-adblock)
(upstream `c56456ac535844d9eecd6cfee030a2e23dc797d1`). The same DNS sinkhole,
web dashboard, browser/Arduino OTA, captive portal, persistent client bans and
custom domains, and scheduled blocklist updates run in all hardware profiles.

The device identified over USB on 2026-10-03 is a **Guition JC3636W518C,
SW V0.9.1**, with an **ESP32-S3 revision 0.2, 16 MB flash and 8 MB OPI PSRAM**.
It is a **1.8-inch ST77916 QSPI 360×360** display with **CST816S** touch,
not the initially proposed ESP32-C3/GC9A01 board. Its USB port was
`/dev/cu.usbmodem83201` (VID:PID `303a:1001`). Ports can change after reconnecting.

## Hardware profiles

| Environment | Hardware | Display | Flash |
|---|---|---|---|
| `jc3636w518c` (default) | Confirmed Guition S3 | ST77916 QSPI, 360×360, CST816 | 16 MB |
| `s3-headless` | Same S3, network services only | None | 16 MB |
| `c3` | Original ESP32-C3 | None | 4 MB |
| `round-display` | ESP32-2424S012C | GC9A01 SPI, 240×240, CST816 | 4 MB |

The C3 display profile is compile-verified only; no such board was connected.
USB identifies the chip and flash, not the PCB or LCD pinout. Never select a
profile by USB vendor ID alone. The JC pinout was checked against the supplied
model, factory firmware controller strings, manufacturer demo and schematics
in [td0034/JC3636W518](https://github.com/td0034/JC3636W518) and the independent
[itoulee/jc3636w518-demo](https://github.com/itoulee/jc3636w518-demo).

| Function | JC3636W518C S3 | ESP32-2424S012C C3 |
|---|---:|---:|
| LCD clock | 9 | 6 |
| LCD CS | 10 | 10 |
| LCD D0 / MOSI | 11 | 7 |
| LCD D1 / D2 / D3 | 12 / 13 / 14 | — |
| LCD DC | QSPI command phase | 2 |
| LCD reset | 47 | Unconnected |
| Backlight | 15 | 3 |
| Touch SDA / SCL | 7 / 8 | 4 / 5 |
| Touch reset / interrupt | 40 / 41 | 1 / 0 |
| Touch I²C address | 0x15 | 0x15 |
| BOOT | 0 | 9 |

## Screen and touch

LovyanGFX draws directly to the display: no LVGL, sprite or full-screen
framebuffer. A shared logical 240×240 layout scales to 360×360. The screen
shows ACTIVE/PAUSED, remaining pause time, blocked and allowed queries,
blocked percentage, loaded and custom domains, observed DNS clients, RSSI
and IP. The bottom buttons pause for 5 minutes, 30 minutes, or resume.
Pauses are deliberately volatile and start ACTIVE after a restart; they
share the exact same state as the web controls. Existing per-client bans
remain enforced while domain blocking is paused.

Only changed regions redraw, clipped to one 8-row logical stripe per main-loop pass. State is
sampled every 250 ms. Touch polling is bounded, checks the CST816 chip ID and
emits one action per contact. Portal mode shows its AP name and IP; touch
pause commands have no effect until network services are running. Serial logs
include controller ID, touch coordinates/actions, heap and maximum render time.

Main integration is in `src/main.cpp`; display, UI and touch are separate
modules. `boards/jc3636w518c.json` defines the actual 16 MB/8 MB board;
`src/st77916_qspi.*` supplies the QSPI transport absent in LovyanGFX 1.2.7. `src/blocking_state.h` handles duration limits and clock rollover.

## Recent blocked-query history

The dashboard now includes a searchable, paginated history of the **last 64
blocked DNS requests**, newest first: requested domain, client IP, DNS type,
reason (blocklist, custom domain or banned client), and elapsed time. History
uses approximately 17 KiB of fixed RAM in every profile and is cleared on
reboot. The DNS capture path does not allocate memory or write to flash.
Allowed requests are not logged.

`GET /blocked.json?offset=0&limit=16&q=example` returns up to 16 entries per
page. Search matches domains and client IPs without case sensitivity and is
limited to 63 characters. All DNS bytes are escaped before JSON serialization;
the browser displays received strings as text. No external assets are needed.

The round display uses a central blocking percentage, compact counters, a
network panel, clear status/countdown and state-aware touch controls. A round
robin repaint schedule prevents continuously changing statistics from delaying
network and control regions. Rendering remains clipped to eight logical rows,
without a full-screen framebuffer.

Run `python tools/verify_history.py DEVICE_IP` to check the live history. This
deliberately generates 80 blocked queries and replaces the volatile recent
history; it does not modify persistent settings. DNS blocking cannot reliably
remove YouTube advertisements served from domains shared with video content.

## Build and tests

Install Python 3.12+ and PlatformIO in a virtual environment:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install platformio esptool pyserial
pio run -e jc3636w518c
pio run -e c3 -e round-display -e s3-headless
pio test -e native
python -m unittest discover -s tests -v
```

No `secrets.h` is required. Saved NVS credentials take precedence; otherwise
use the captive portal. Optional compile-time credentials follow
`src/secrets.example.h`; `src/secrets.h` is ignored by Git.

Build a blocklist from StevenBlack and HaGeZi Light:

```sh
mkdir -p data
python tools/build_blocklist.py data/blocklist.bin
```

The HaGeZi default uses the current `wildcard/light-onlydomains.txt` path.
A failed source or empty list stops the build and preserves the old output.
The result is sorted unique 40-bit FNV-1a hashes, matching the original engine.

## Identify, back up and flash

Read-only chip/flash identification must come before firmware writes:

```sh
python -m serial.tools.list_ports -v
esptool --port /dev/cu.usbmodem83201 chip-id
esptool --port /dev/cu.usbmodem83201 flash-id
esptool --port /dev/cu.usbmodem83201 read-flash 0 ALL original-flash.bin
```

Backups can contain private settings; keep them outside the repository.
Do not run `erase-flash`. Firmware upload preserves NVS and the filesystem
provided their partition offsets match the device:

```sh
pio run -e jc3636w518c -t upload --upload-port /dev/cu.usbmodem83201
pio device monitor --port /dev/cu.usbmodem83201 --baud 115200
```

On this device the original factory `ffat` region at `0x410000`, size
`0xbe0000`, was checked in the complete backup and contained only `0xff`.
`partitions-s3.csv` preserves original NVS, both 2 MB OTA slots and coredump,
relabeling that erased region for LittleFS (11.875 MiB). The C3 partition table
is unchanged. LittleFS never auto-formats on mount failure.

**Only for a verified empty initial filesystem**, upload the generated list:

```sh
pio run -e jc3636w518c -t uploadfs --upload-port /dev/cu.usbmodem83201
```

`uploadfs` replaces the whole filesystem. On an already configured device,
use the dashboard's Blocklist Upload instead so custom domains, bans and
update settings are retained. The SD card and audio/media files are unused
and untouched by this project.

## Wi-Fi and live checks

If no saved Wi-Fi connects, join the open AP `C3-AdBlock-XXXX` and open
`http://192.168.4.1`. Enter the network and password in the captive portal.
After reboot, the serial monitor reports the station IP. BOOT requests the
portal without deleting saved credentials; `/forgetwifi` remains an explicit
credential reset in the upstream web interface.

Open `http://c3adblock.local` or the reported IP. Point test devices/router
DNS at that IP. Use it as the only advertised resolver when you want blocking:
a second public DNS server can bypass the sinkhole.

```sh
python tools/verify_device.py DEVICE_IP
# Or individual DNS queries:
dig @DEVICE_IP doubleclick.net
dig @DEVICE_IP example.org
```

The live check validates the dashboard, sinkhole, parent-domain matches,
upstream resolution, web pauses and resume, then restores the initial pause
state. Physical screen visibility and actual touch presses still require
observation on the device; successful initialization alone is not visual proof.

## OTA and persistent updates

Upload `.pio/build/jc3636w518c/firmware.bin` through the dashboard Firmware
section, or use Arduino OTA:

```sh
pio run -e jc3636w518c -t upload --upload-port c3adblock.local
```

PlatformIO automatically selects espota when the upload port is an IP address or hostname.
Use the same hardware environment for OTA. Browser OTA writes the inactive
app slot; firmware updates preserve NVS/LittleFS. Blocklist upload and remote
scheduled updates retain the original API. The original updater removes the
old blocklist to make space before downloading its replacement: interrupted
or invalid updates can leave no list and therefore fail open. Custom domains
continue working even when the flash blocklist is absent.

The upstream synchronous DNS forwarder may wait up to one second for an
unresponsive upstream, and remote list downloads also block the main loop.
The display adds no background task or framebuffer; DNS burst handling has a
10 ms fairness bound between queries, but an individual upstream timeout can
still delay UI/web. The upstream dashboard/OTA are unauthenticated and intended
for a trusted LAN; never expose them to the public Internet.

## Upstream and license

The original README is preserved in [docs/UPSTREAM_README.md](docs/UPSTREAM_README.md).
Original project by M-Abozaid, inspired by s60sc/ESP32_AdBlocker. The engine is MIT,
see [LICENSE](LICENSE); the vendor display table carries Apache-2.0 attribution
in [licenses/NOTICE.md](licenses/NOTICE.md).
