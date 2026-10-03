# Documentation image provenance

These images document the current local **0.2.4** feature build. The latest
public GitHub release remains **v0.2.1**; the images do not imply that v0.2.4
has been published.

## Dashboard captures

`dashboard-overview.jpg`, `dashboard-lists.jpg`, `dashboard-updates.jpg`,
`dashboard-wifi.jpg` and `dashboard-en.jpg` are browser captures of the
unchanged dashboard HTML embedded in `src/page.h`. They use a localhost-only,
read-only preview with synthetic addresses, counters, domains, SSID and
firmware status. They contain no private network data, device credentials,
live telemetry or external web assets. The update panel shows the local 0.2.4
build alongside the public v0.2.1 status instead of inventing an installable
future release.

The locale and output mapping is:

| Image | Locale and view |
| --- | --- |
| `dashboard-overview.jpg` | Spanish overview and blocked-query history |
| `dashboard-en.jpg` | English overview and blocked-query history |
| `dashboard-lists.jpg` | Spanish HaGeZi profiles and persistent allowlist |
| `dashboard-updates.jpg` | Spanish firmware, remote list and GitHub status |
| `dashboard-wifi.jpg` | Spanish safe Wi-Fi reconfiguration panel |

Start the local preview from the repository root:

```sh
python3 tools/preview_dashboard.py --port 39430 --language es
python3 tools/preview_dashboard.py --port 39431 --language en
```

Open the corresponding `http://127.0.0.1:<port>/` address in a browser and
capture the overview, lists, updates and Wi-Fi panels after the synthetic data
has loaded. The English overview is captured from port 39431. The server binds
only to loopback, serves `src/page.h` unchanged, rejects modifying requests,
and never contacts an ESP32 or performs OTA. Stop each preview server after
the capture.

## Round display renders

The display PNGs are source-rendered host previews of the firmware drawing
code, not photographs or physical-panel captures. The renderer uses the
current display, QR, Wi-Fi QR and language sources and writes the six-page
montage plus the three-page setup-flow montage in both locales:

```sh
python3 tools/render_display_previews.py
python3 tools/render_display_previews.py --language en
```

`display-pages*.png` keeps the original six-page montage. The setup flow is
kept separately as `display-setup-flow*.png`; standalone pages use the same
locale suffixes. The generation run checks QR matrix dimensions, quiet zones,
caption containment and, when the already-installed macOS Vision decoder is
available, payload decoding. See [DISPLAY_PREVIEWS.md](DISPLAY_PREVIEWS.md)
for exact synthetic state, renderer limits and verification output.

## Hero illustration

`hero-device.png` is a generative product illustration prepared for the
README. See [HERO.md](HERO.md) for the exact prompt and provenance. Its
caption identifies it as AI-generated and it must not be read as a photograph
or hardware validation evidence. Device and display claims remain grounded in
the repository's hardware and validation documents.
