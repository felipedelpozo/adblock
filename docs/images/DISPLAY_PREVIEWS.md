# Round display previews (English)

These previews are deterministic host renders of the repository's actual round
display renderer. The tool copies `src/display.cpp`, `src/display_qr.cpp`,
`src/wifi_qr.h`, the current `src/i18n.h`, and their model headers into a
temporary directory, compiles the renderer unchanged with a small
Arduino/LovyanGFX drawing shim and a host locale implementation matching the
current i18n API, and uses the bundled LovyanGFX `glcdfont.h` bitmap font and
`lgfx_qrcode.c` encoder. Rendering still follows the firmware's 240 px logical
geometry, 360 px S3 scaling, 8 px clipped stripes, palette, controls, locale,
and QR layout. The six original pages retain the 3-by-2 `display-pages*.png`
montage. The three setup-flow pages are separate 3-by-1
`display-setup-flow*.png` montages, with matching standalone PNGs.

The six source pages use one explicit demonstration snapshot: 99,643 loaded
domains, 132,684 blocked requests, 45,123 allowed requests, 3 clients, -52 dBm,
and `192.168.1.50`. The controls page is intentionally paused with 298 seconds
remaining so the resume control is visible. The QR page encodes exactly
`http://192.168.1.50/` through the same C encoder used by firmware. These values
are demonstration data and are not a device telemetry capture.

The setup-flow snapshot models the captive portal as `portal=true`, IP
`192.168.4.1`, and AP `C3-AdBlock-ABCD`. Its network page shows both the
`CONECTAR WIFI`/`CONNECT WIFI` and `ABRIR PORTAL`/`OPEN PORTAL` buttons. The
setup QR encodes `WIFI:T:nopass;S:C3-AdBlock-ABCD;;` as a 29x29 matrix at
logical left 46, top 38. The portal URL QR encodes `http://192.168.4.1/` as a
25x25 matrix using the normal dashboard geometry.

There is no framebuffer or readback path on the hardware, so these files are
host-rendered source previews, not photographs or optical panel captures. The
host shim reproduces RGB565 primitives and bitmap glyphs; physical panel
controller timing, electrical artifacts, touch response, and optical appearance
remain unrepresented. The outside of each 360 px canvas is masked to a neutral
background to show the circular panel boundary.

QR squares are checked directly in the unmasked source framebuffer for complete
bounds and an intact four-module quiet zone. Instructional captions are checked
for rendered pixels and containment against the circular panel mask. On macOS,
the renderer uses the already-installed Vision barcode detector when available;
otherwise it uses `zbarimg` when installed and records decoder availability.
The independent decoder is optional and adds no production dependency.

Regenerate both locales from the repository root with:

```sh
python3 tools/render_display_previews.py
python3 tools/render_display_previews.py --language en
```

The host clock is a deterministic `micros()` stub used only to exercise the
bounded stripe loop. `host_stub_clock_ticks` and `renders` are control-flow
evidence, not hardware timing. Physical panel timing, touch response, electrical
artifacts, optical appearance, and real captive-portal behavior still require
device validation.

Verification output from the generation run:

- `scenario=status language=en mid_switch=no renders=33 ready=yes host_stub_clock_ticks=37`
- `scenario=activity language=en mid_switch=no renders=33 ready=yes host_stub_clock_ticks=37`
- `scenario=lists language=en mid_switch=no renders=33 ready=yes host_stub_clock_ticks=37`
- `scenario=network language=en mid_switch=no renders=33 ready=yes host_stub_clock_ticks=37`
- `scenario=controls-paused language=en mid_switch=no renders=33 ready=yes host_stub_clock_ticks=37`
- `scenario=dashboard-qr language=en mid_switch=no renders=33 ready=yes host_stub_clock_ticks=37`
- `qr_matrix=25x25 dark_modules=330 payload=http://192.168.1.50/`
- `qr_square=198x198 quiet_zone_pixels=16704 clipped=no`
- `qr_caption_pixels=1484 outside_circle=0`
- `qr_decode=macos_vision payload=http://192.168.1.50/`
- `scenario=network-portal language=en mid_switch=no renders=33 ready=yes host_stub_clock_ticks=37`
- `scenario=setup-wifi-qr language=en mid_switch=no renders=33 ready=yes host_stub_clock_ticks=37`
- `qr_matrix=29x29 dark_modules=408 payload=WIFI:T:nopass;S:C3-AdBlock-ABCD;;`
- `qr_square=222x222 quiet_zone_pixels=19008 clipped=no`
- `qr_caption_pixels=2136 outside_circle=0`
- `qr_decode=macos_vision payload=WIFI:T:nopass;S:C3-AdBlock-ABCD;;`
- `scenario=portal-dashboard-qr language=en mid_switch=no renders=33 ready=yes host_stub_clock_ticks=37`
- `qr_matrix=25x25 dark_modules=334 payload=http://192.168.4.1/`
- `qr_square=198x198 quiet_zone_pixels=16704 clipped=no`
- `qr_caption_pixels=1484 outside_circle=0`
- `qr_decode=macos_vision payload=http://192.168.4.1/`
- `scenario=dashboard-qr language=en mid_switch=yes renders=37 ready=yes host_stub_clock_ticks=37`
- `mid_render_switch=es->en page=dashboard-qr qr_modules=330 preserved=yes`
- `scenario=setup-wifi-qr language=en mid_switch=yes renders=37 ready=yes host_stub_clock_ticks=37`
- `mid_render_switch=es->en page=setup-wifi-qr qr_modules=408 preserved=yes`
- `scenario=portal-dashboard-qr language=en mid_switch=yes renders=37 ready=yes host_stub_clock_ticks=37`
- `mid_render_switch=es->en page=portal-dashboard-qr qr_modules=334 preserved=yes`

The Spanish run completed with the same source geometry and checks:

- `scenario=status/activity/lists/network/controls-paused/dashboard-qr/network-portal/setup-wifi-qr/portal-dashboard-qr language=es renders=33 ready=yes`
- `setup-wifi-qr qr_matrix=29x29 qr_square=222x222 qr_caption_pixels=2320 outside_circle=0 qr_decode=macos_vision`
- `dashboard-qr qr_matrix=25x25 qr_square=198x198 qr_caption_pixels=1544 outside_circle=0 qr_decode=macos_vision`
- `portal-dashboard-qr qr_matrix=25x25 qr_square=198x198 qr_caption_pixels=1544 outside_circle=0 qr_decode=macos_vision`
