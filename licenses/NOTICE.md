# Third-party notices

The ST77916 register initialization in `src/st77916_init.h` follows the
Espressif ESP32_Display_Panel driver distributed with the Guition JC3636W518EN
manufacturer examples. That source identifies its license as Apache-2.0.
See `Apache-2.0.txt` and the provenance comment in the header.

Reference package: https://github.com/td0034/JC3636W518
Driver upstream: https://github.com/esp-arduino-libs/ESP32_Display_Panel

LovyanGFX is a pinned external dependency, distributed under its own BSD license:
https://github.com/lovyan03/LovyanGFX/blob/master/license.txt
Its license text is included in `LovyanGFX_BSD.txt`.

LovyanGFX's bundled QR encoder is copyright Richard Moore (2017), modified
by lovyan03 (2020), and distributed under MIT; see `LGFX_QRCODE_MIT.txt`.

ArduinoJson 6.21.5 is an external MIT dependency used for bounded release
manifest parsing; see `ArduinoJson_MIT.txt` and
https://github.com/bblanchon/ArduinoJson.

The AdBlock engine remains under the original MIT license in `LICENSE`.

HaGeZi-derived curated DNS lists are distributed separately under GPL-3.0.
Their release includes pinned source files, attribution and GPL license text.
These data assets are not bundled into the firmware or covered by its MIT
license. See `docs/BLOCKLIST_PROFILES.md`.
