# Documentation image provenance

## Dashboard

`dashboard-overview.jpg`, `dashboard-updates.jpg` and `dashboard-lists.jpg` are browser screenshots
of the HTML/JavaScript in `src/page.h`. They are captured from a local read-only
preview populated with synthetic data, not private network traffic or device
credentials. The example addresses and locally administered MACs are fictional.
The updater image illustrates the transition from installed `0.1.0` to `0.1.1`.
The list image shows firmware 0.2.0 with the PRO profile and fictional domain exceptions.
Screenshots illustrate the UI; release/device validation is recorded separately.

Reproduce the source UI locally:

```sh
python tools/preview_dashboard.py
```

Open `http://127.0.0.1:39417/`. Capture the overview and scroll down to firmware
updates. The tool binds to localhost, uses the firmware HTML unchanged and
rejects all modifying requests. It never contacts an ESP32 or performs OTA.

## Round display

See [DISPLAY_PREVIEWS.md](DISPLAY_PREVIEWS.md) for the source-rendered views,
example state and limitations. Images generated for documentation are distinct
from user-observed physical hardware checks.
