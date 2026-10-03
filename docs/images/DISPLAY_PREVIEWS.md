# Round display previews

These previews are deterministic host renders of the repository's actual round
display renderer. The tool copies `src/display.cpp`, `src/display_qr.cpp`, and
their model headers into a temporary directory, compiles them unchanged with a
small Arduino/LovyanGFX drawing shim, and uses the bundled LovyanGFX
`glcdfont.h` bitmap font and `lgfx_qrcode.c` encoder. Rendering still follows
the firmware's 240 px logical geometry, 360 px S3 scaling, 8 px clipped stripes,
palette, controls, and QR layout.

The six source pages use one explicit demonstration snapshot: 99,643 loaded
domains, 132,684 blocked requests, 45,123 allowed requests, 3 clients, -52 dBm,
and `192.168.1.50`. The controls page is intentionally paused with 298 seconds
remaining so the resume control is visible. The QR page encodes exactly
`http://192.168.1.50/` through the same C encoder used by firmware. These values
are demonstration data and are not a device telemetry capture.

There is no framebuffer or readback path on the hardware, so these files are
host-rendered source previews, not photographs or optical panel captures. The
host shim reproduces RGB565 primitives and bitmap glyphs; physical panel
controller timing, electrical artifacts, touch response, and optical appearance
remain unrepresented. The outside of each 360 px canvas is masked to a neutral
background to show the circular panel boundary.

The two QR instructional captions are checked directly in the unmasked source
framebuffer: both regions contain rendered glyph pixels and the containment
check confirms that zero caption pixels fall outside the circular panel.

Regenerate from the repository root with:

```sh
python3 tools/render_display_previews.py
```

Verification output from the generation run:

- `scenario=status renders=33 ready=yes host_stub_clock_ticks=37`
- `scenario=activity renders=33 ready=yes host_stub_clock_ticks=37`
- `scenario=lists renders=33 ready=yes host_stub_clock_ticks=37`
- `scenario=network renders=33 ready=yes host_stub_clock_ticks=37`
- `scenario=controls-paused renders=33 ready=yes host_stub_clock_ticks=37`
- `scenario=dashboard-qr renders=33 ready=yes host_stub_clock_ticks=37`
- `qr_matrix=25x25 dark_modules=330 url=http://192.168.1.50/`
- `qr_caption_pixels=1544 outside_circle=0`
