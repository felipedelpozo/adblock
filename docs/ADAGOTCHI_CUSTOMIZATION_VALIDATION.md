# Adagotchi Phase 1 + local customization — validation

Date: 2026-10-04. Local firmware **0.3.1**, unpublished. Existing checkout was reused on base `5237223`; all Phase 1 changes were preserved. No commit, push, release, filesystem format, NVS erase or Wi-Fi reset was performed.

## Delivered behavior

The dashboard settings link opens `/pet`. Names, Classic/Amber/Violet skins and imported custom artwork are saved independently of progress. A demo Debug Duck is installed as custom artwork, with the original name Adagotchi.

Download a pet from https://codex-pets.net/, unzip it and select `spritesheet.webp`. Verified standard formats are Codex v1 1536×1872 (8×9 cells) and v2 1536×2288 (8×11 cells), with 192×208 cells. Source rows 0/3/4/6 (idle/waving/jumping/waiting) map to idle/reaction/feeding/low-energy states. Row/grid/frame controls are available under the advanced disclosure. PNG/WebP and converted BPT1 import are supported; direct ZIP/catalog import is not included.

Browser-only conversion preserves aspect ratio at 48×48, samples six frames per state, uses 15 RGB565 colors plus transparency and produces a fixed **27,696-byte** BPT1 file with CRC32. Four states play at 4 Hz. Preview shows the quantized artwork. The device needs no image decoder, LVGL, full-screen framebuffer, Muse or external asset server. Color/detail and original animation timing are reduced; custom art does not change at evolution thresholds, while species/level continue to update.

Appearance persistence uses an atomic 41-byte NVS record (`pet-look`) containing name, skin, revision and selected asset slot. Uploaded assets are validated in `.tmp`, renamed into the inactive LittleFS slot, then selected through the NVS commit. The active file/RAM image is untouched until success. Render reads only RAM; PSRAM is preferred and was verified by `assetMemory: psram`. A corrupt/newer appearance record is preserved and persistence disabled. The actual board's file mutex must be initialized before sprite restoration; the reboot test caught and verified the correction.

## Exact Phase 1 rules

- One eligible blocked-domain event awards **1 food + 10 XP**. A novel rewarded domain adds **1 food + 1 XP**; a novel rewarded client adds the same. Maximum **3 food / 12 XP** per event.
- A normalized domain receives at most one reward every **60 active seconds**, across clients and query types. Exact rolling quota: **30 reward events per active hour**, persisted through reboot/OTA. Approximate novelty filters can produce conservative false positives and eventually saturate.
- Client bans, allowed/paused queries and failed list lookups never feed the pet. Tap reactions never award food.
- XP thresholds: Egg 0; Hatchling 50; Blocky 200; AdHunter 500; AdEater 1000; Void 2000. Levels 0–5 follow these stages.
- Fullness (named `hunger`) decays 1 per 15 active minutes; happiness/energy 1 per 30 active minutes. Floor 20, maximum 100; food restores 4/3/2 points. Offline time is frozen and the pet cannot die.
- `bornAt`, `lastFed` and `activeMs` use accumulated active uptime, never an invented wall clock. Today is an anchored 24-hour active period, not a calendar day. Distinct-domain/client counts are approximate; ordinary counters/time checkpoint every 15 minutes, rewards asynchronously and controlled reboot/OTA explicitly. Abrupt loss can lose uncheckpointed ordinary counters/time.

## Interaction and REST

Pet home shows artwork/name/species/level, fullness/energy and food/blocked counts for the active period. Swipe horizontally through the existing appliance pages and Pet Status; tap the pet for a presentation-only reaction. Existing pause/resume and QR controls remain. Long press is reserved. LovyanGFX renders one clipped eight-row logical stripe per loop, at 4 Hz for animation; top-of-sprite repaint across y=60 has an executable regression test.

- `GET /pet.json`: aggregate state, chosen name/skin, period/time basis and diagnostics. No browsing history or client addresses.
- `GET /pet/appearance.json`: name/skin, custom availability, bytes, memory kind, revision and storage readiness.
- `POST /pet/appearance?name=...&skin=...`: save appearance only. Names 1–16 ASCII letters/digits/spaces/_/-. Same-origin plus boot nonce in `X-CSRF-Token`.
- `GET /pet/sprite`: exact BPT1 download; 404 if absent. `POST /pet/sprite`: exactly one multipart file, with the same origin/token checks.
- Existing `/stats.json`, dashboard, DNS, OTA, portal and blocklist APIs remain available.

## Validation results

| Scope | Result |
| --- | --- |
| Four firmware profiles | PASS |
| Native logic/model tests | PASS — 62/62, including 11 PetEngine and 4 PetAsset |
| Dashboard JavaScript tests | PASS — 34/34 |
| Python tests | PASS — 16 passed, 1 optional OpenCV decoder skipped (17 total) |
| Pure parser C++11 strict warnings | PASS — Wall/Wextra/Werror/pedantic |
| Renderer animation-boundary regression | FAIL before correction; PASS after correction |
| Compressed dashboard roundtrip/generation | PASS |
| Live browser import | PASS — actual Debug Duck v2 WebP → BPT1 → authenticated upload |
| Live browser name/style save and restoration | PASS |
| Browser BPT1 download | PASS — identical bytes to device/source fixture conversion |
| Browser 390px responsive layout | PASS — no horizontal overflow |
| Invalid names/skins, absent or wrong-origin CSRF | PASS — settings unchanged |
| CRC, truncated, oversized and multiple-file uploads | PASS — committed asset/appearance retained |
| Dashboard, sinkhole/suffix, upstream, 5/30-minute pause and resume | PASS |
| Final application-only OTA/reboot | PASS — appearance, exact asset bytes, progress and configuration retained |
| USB serial | UNAVAILABLE — port exists, 25-second capture returned zero bytes |
| Physical screen/finger interaction | UNAVAILABLE — readiness is reported by firmware; no physical observation |
| Power-cut/fault injection | UNAVAILABLE — atomic selection design reviewed, controlled restart tested |

After the final reboot, progress was **388 XP / 55 food**, with saved appearance revision 15; the final artifact uses 27,696 bytes in psram. Wi-Fi, language, 227,420-domain list and pause state were preserved. No real calendar date was invented. A 60-second final REST observation checks an unchanged boot nonce, monotonic active time and display/touch readiness; serial watchdog absence cannot be claimed from an empty log.

## Memory and OTA budget

Actual binary size includes ESP image padding. Static RAM excludes heap stacks, NVS queues and the dynamically loaded asset.

| Profile | Static RAM bytes | Actual .bin bytes | OTA slot free bytes |
| --- | ---: | ---: | ---: |
| c3 | 126,756 | 1,219,744 | 156,512 |
| round-display | 128,820 | 1,327,968 | 48,288 |
| s3-headless | 132,636 | 1,130,128 | 967,024 |
| jc3636w518c | 135,788 | 1,240,912 | 856,240 |

Installed S3 image SHA-256: `6490702af527b904017f49dcefeca17161b1897bbc3730665174aa0a01a93e98`. Checksums for all four images are in `firmware/customization-builds.json`. Firmware files are application-only; never upload another profile or erase/replace the filesystem to install them.

The connected hardware is **Guition JC3636W518C ESP32-S3**, 16 MB flash / 8 MB PSRAM, ST77916 QSPI round 360×360 and CST816 touch. GC9A01 240×240/C3 and headless profiles are compile-validated; they were not physically flashed. C3 assets use heap and can be rejected safely if allocation/filesystem headroom is insufficient. One loaded sprite costs 27,696 bytes; replacing it briefly needs a second candidate buffer. Two stored slots plus staging can consume about 83 KB of LittleFS.

## DNS measurements

Unpaired 40-query measurements before and after customization were median 33.38/63.22 ms, p95 37.80/75.69 ms. These do not isolate changing Wi-Fi/traffic/flash activity. The controlled alternating comparison below uses the same installed firmware, host, query and network; each row is 40 sequential sinkhole queries.

| Run | Appearance | Median ms | p95 ms | Max ms |
| --- | --- | ---: | ---: | ---: |
| 1 | classic | 62.89 | 78.29 | 81.67 |
| 2 | custom | 61.49 | 65.72 | 68.95 |
| 3 | classic | 61.50 | 70.69 | 171.38 |
| 4 | custom | 62.31 | 69.15 | 344.38 |

Custom animation showed no material median penalty against the built-in pet in this paired sample. This is a short observation, not a hard real-time or sustained-load guarantee. NVS/filesystem configuration writes and Wi-Fi can cause outliers; uploads/OTA can briefly delay DNS. Existing Phase 1 DNS/feeding validation remains in `adagotchi-phase1-validation.md`.

## Changed files

- `src/pet/PetEngine.*`, `pet_runtime.*`: pure engine, active-clock counters and independent NVS save worker.
- `src/pet/PetAsset.*`, `pet_appearance.*`: bounded sprite parser, palette/frame access, separate atomic appearance record and two asset slots.
- `src/ui/pet_ui.*`, `src/display.*`, `src/ui.cpp`, `src/ui_model.h`: procedural/custom pet rendering, stripe repaint regions, reaction and navigation.
- `src/main.cpp`: DNS feeding hook, aggregate REST, authenticated appearance/upload routes and safe initialization order.
- `src/page.h`, `src/page_gzip.h`, `src/pet_page.h`, `web/pet.html`: dashboard link and bilingual local customization/conversion UI.
- `src/github_updater.cpp`, `src/wifi_setup.cpp`: checkpoint before existing reboot/OTA paths.
- `src/firmware_identity.h`: local build version 0.3.1.
- `test/test_pet_engine`, `test/test_pet_asset`, UI model tests and `tests/test_pet_dashboard.cjs`, `test_dashboard_assets.py`, `test_pet_display.py`: logic, format, browser boundaries, compression and actual renderer regression coverage.
- `tools/build_identity.py`, `build_dashboard_page.py`, `build_pet_page.py`, `render_display_previews.py`, `verify_pet.py`, `verify_pet_appearance.py`, `verify_device.py`: deterministic assets, previews and reproducible live checks.
- README, CHANGELOG and validation documents: usage, exact rules and observed limits.

## Deliverables and provenance

- `adagotchi-0.3.1.patch`: complete change from the existing base, including Phase 1; reverse-apply dry run verified.
- `adagotchi-0.3.1-source.zip`: source snapshot, without secrets, private logs, flash backups or build caches.
- `firmware/adblock-0.3.1-*.bin`: four unpublished local builds.
- `adagotchi-customization-dashboard.png`: actual device-served dashboard screenshot.
- `custom-pet-preview/display-pet.png`: real source-renderer preview with synthetic progress; not a physical photograph.
- `pet-examples/`: original public MIT Debug Duck fixture, source/license and locally converted BPT1. Source revision `27996cafa42119c86db5472af852a3ef185723f4` of https://github.com/portons/codex-pet-share. Nino was inspected only as a format fixture and is not redistributed.
