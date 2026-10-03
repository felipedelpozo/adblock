# Third-party display initialization

The ST77916 register initialization in `src/st77916_init.h` follows the
Espressif ESP32_Display_Panel driver distributed with the Guition JC3636W518EN
manufacturer examples. That source identifies its license as Apache-2.0.
See `Apache-2.0.txt` and the provenance comment in the header.

Reference package: https://github.com/td0034/JC3636W518
Driver upstream: https://github.com/esp-arduino-libs/ESP32_Display_Panel

LovyanGFX is a pinned external dependency, distributed under its own BSD license:
https://github.com/lovyan03/LovyanGFX/blob/master/license.txt

The AdBlock engine remains under the original MIT license in `LICENSE`.
