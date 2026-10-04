# Adagotchi Phase 1 — implementation and validation

2026-10-04. Local firmware **0.3.0**, not committed, pushed or published.

The existing repository checkout was reused for this validation.

Base commit: `5237223` (bilingual Wi-Fi setup and QR onboarding). The tree was
clean before this task. The implementation preserves the existing partitions,
Wi-Fi configuration, local dashboard, list pipeline, allowlist, client bans,
captive portal, QR flows and OTA paths. No Muse SDK, AI provider, new production
library, cloud service or full display framebuffer was added.

## Changed files

- `src/pet/PetEngine.h`, `src/pet/PetEngine.cpp`: portable state, feeding,
  cooldowns/quota, active clock, aggregate periods, evolution and serialization.
- `src/pet/pet_runtime.h`, `src/pet/pet_runtime.cpp`: atomic NVS blob and bounded
  asynchronous save queue; controlled reboot/OTA checkpoints.
- `src/ui/pet_ui.h`, `src/ui/pet_ui.cpp`: procedural pet, animated reaction,
  health bars, compact counters and pet status, with English/Spanish labels.
- `src/ui_model.h`, `src/ui.cpp`, `src/display.h`, `src/display.cpp`: seven-page
  navigation, mascot hit bounds, sampled animation and clipped stripe rendering.
  Restored progress establishes the initial reward baseline without a fake feed.
- `src/main.cpp`: DNS event hook, aggregate `/pet.json`, runtime/UI snapshots,
  diagnostic fields and OTA checkpoints. Client bans do not feed the pet.
- `src/github_updater.cpp`, `src/wifi_setup.cpp`: flush pet state at existing
  OTA/setup reboot boundaries. The OTA worker never accesses the live engine.
- `src/firmware_identity.h`: source version 0.3.0; hardware identities unchanged.
- `test/test_pet_engine/test_main.cpp`: eleven pure-engine regression tests.
- `test/test_ui_model/test_ui_model.cpp`: seven-page navigation and pet tap bounds.
- `tools/verify_pet.py`: safe real DNS feeding/duplicate-query check.
- `tools/render_display_previews.py`: pet source previews, custom output directory
  and raw foreground containment checks before applying the round mask.
- `README.md`, `CHANGELOG.md`, `docs/ADAGOTCHI_PHASE1_VALIDATION.md`: behavior,
  exact rules, endpoints, active-time semantics and current verification scope.

## Exact feeding and progression

Accepted domain blocks grant 1 food and 10 XP. A first rewarded domain adds
1 food/1 XP; a first rewarded client adds another 1 food/1 XP. Maximum: 3 food
and 12 XP per event. Novelty is consumed only by accepted rewards; observing an
allowed client does not consume its future feeding bonus.

Same normalized domain: at most one reward per 60 active seconds, across all
clients and DNS query types. Maximum: 30 events per rolling active hour. Both
limits persist through restart and OTA. Novelty and distinct counts use bounded
Bloom filters: false positives and saturation can suppress bonuses or under-count
unique values; the base reward and exact cooldown/quota remain operational.

| XP | Level | Species |
| ---: | ---: | --- |
| 0 | 0 | Egg |
| 50 | 1 | Hatchling |
| 200 | 2 | Blocky |
| 500 | 3 | AdHunter |
| 1,000 | 4 | AdEater |
| 2,000 | 5 | Void |

Fullness (`hunger`) loses 1 point per 15 active minutes; happiness and energy
lose 1 per 30 active minutes. All remain between 20 and 100. Each food restores
4 fullness, 3 happiness and 2 energy. There is no death, offline decay or
wall-clock catch-up. Derived level/species are reconstructed from persistent XP.

The serialized versioned/CRC32-protected state is 1,532 bytes. NVS uses the
separate `adagotchi` namespace. A low-priority task commits serialized copies,
never the mutable engine. A one-entry queue retains the latest pending copy.
Rewards schedule a checkpoint after DNS handling; ordinary counters/elapsed
time checkpoint every 15 minutes, with explicit checkpoints at controlled
reboot boundaries. Unexpected power loss can lose an in-flight reward or up
to 15 minutes of ordinary updates. Invalid/newer records are preserved and
persistence is visibly disabled rather than silently overwriting them.

## Display and REST

Logical 240×240 coordinates support GC9A01 and scale to the S3's 360×360 panel.
Pages: PetHome → Status → Activity → Lists → Network → Controls → PetStatus.
Normal animation is sampled at 4 Hz. The renderer writes at most one clipped
8-row logical stripe per service call. Static pet status does not redraw merely
because the active clock advances. Pet taps produce a short visual reaction
without feeding; horizontal swipes wrap, long presses do nothing. Original
pause/resume and both setup/dashboard QR views remain available.

`GET /pet.json` contains only pet state and aggregates, with no domains, full
URLs or client addresses. It includes lifetime counters and distinct `Today`
fields for blocked/allowed queries, food, reward events and estimated distinct
domains/clients. `/stats.json` keeps its existing dashboard contract.

“Today” is the current anchored 24-hour period of **accumulated active uptime**,
not a calendar day. The endpoint explicitly sets `calendarDateKnown:false`,
`periodBasis:"active-uptime-24h"`, `periodElapsedMs`, and
`timestampBasis:"active-uptime-ms"`. `bornAt:0` is this clock's origin.

## Build and host validation

PASS: all four PlatformIO firmware environments compiled.
PASS: 58 native model tests, including 11 new engine tests.
PASS: 28 dashboard tests.
PASS: Python suite: 14 passed, 1 optional OpenCV decoding test skipped.
PASS: pure engine C++11 compile with `-Wall -Wextra -Werror`.
PASS: English/Spanish source previews, all three pet views, with zero foreground
pixels outside the circular panel before masking.
PASS: final diff whitespace review. No credentials or unrelated user work were changed.

| Environment | Static RAM / 327,680 | Application .bin | OTA slot | Remaining slot bytes |
| --- | ---: | ---: | ---: | ---: |
| c3 | 126,540 | 1,247,424 | 1,376,256 | 128,832 |
| round-display | 128,604 | 1,354,736 | 1,376,256 | 21,520 |
| s3-headless | 132,428 | 1,159,392 | 2,097,152 | 937,760 |
| jc3636w518c | 135,572 | 1,269,536 | 2,097,152 | 827,616 |

The actual binary size includes image alignment/padding. PlatformIO's ELF flash
summaries were respectively 1,196,000 / 1,288,928 / 1,159,021 / 1,269,165 bytes;
those smaller totals should not be used to estimate OTA image headroom. The
C3 display build fits, with only 21,520 bytes left. Runtime S3 free heap remained
about 150–154 KB. The persistence worker has a 6,144-byte stack and a 1,536-byte
queued image, in addition to the fixed engine/serialization buffers.

## Live appliance evidence

PASS: application-only local browser OTA installed the exact JC3636W518C build
with no filesystem upload or flash erase. A second upload of the same image
exercised normal OTA reboot/persistence.

Current REST diagnostics independently identify ESP32-S3, 16,777,216-byte flash,
8 MB nominal PSRAM, `displayReady:true` and `touchReady:true`. This is the
existing ST77916 QSPI 360×360 board, not a physically connected C3/GC9A01.

PASS: unchanged Wi-Fi/IP, saved language, 227,420-domain list, selected/applied
list profile, custom rules and allowlist after both installs.
PASS: original dashboard, parent-domain sinkhole, upstream resolution,
5/30-minute pauses and resume; the verifier restored the original pause state.
PASS: fresh blocked subdomain awarded 2 food and 11 XP. Forty repeats granted
no further events during that check (`backgroundRewardEvents:0`). Ordinary LAN
traffic also fed the pet and its live evolution reached Blocky.
PASS: before second OTA: 248 XP / 41 food / 23 events.
After reboot: 268 XP / 43 food / 25 events, same birth origin,
continued active clock and Blocky state. Additional valid LAN traffic explains
the increase; progress and daily counters did not reset.
PASS: 70-second post-install observation with eight samples: no changed boot
nonce/unexpected reboot, monotonic active clock, display/touch remaining ready.
Final observed state: 298 XP / 46 food / 28 events.

Forty identical `googlesyndication.com` sinkhole requests before/after:

| Build | Median | P95 | Maximum |
| --- | ---: | ---: | ---: |
| 0.2.4 before | 32.25 ms | 110.64 ms | 233.90 ms |
| 0.3.0 after | 34.95 ms | 44.70 ms | 133.32 ms |

This small LAN sample shows no clear steady-state regression; it is not a
sustained-load guarantee. The subdomain verifier has additional suffix lookups
and therefore is not directly comparable to the single-name baseline.
Initial NVS save reached 208.899 ms; after the second boot the observed maximum
save was 93.641 ms. Maximum observed stripe wall time was
52.180 ms. Background flash commits/preemption can still produce
latency outliers; the existing synchronous upstream timeout is also unchanged.

## Validation limits

UNAVAILABLE: USB bootloader connection and serial log capture. The USB port
exists, but chip identification returned no serial data
and both monitor attempts received no firmware logs. Installation used the
existing local OTA path. Watchdog log absence cannot be claimed; the bounded
REST observation detected no unexpected reboot.

UNVERIFIED: optical appearance on the physical panel and actual finger taps/
swipes. Driver initialization and host gesture tests are verified, but neither
substitutes for physical interaction. Preview PNGs are generated from the actual
renderer with synthetic state, not photographs or hardware screen readback.

COMPILE-ONLY: ESP32-C3 headless/GC9A01 and S3 headless profiles. The connected
hardware evidence covers the S3 display target.

No public release, commit, push or external AI integration was performed.
The original unauthenticated trusted-LAN administration scope remains unchanged.

Installed application SHA-256:
`d059232cb07abeb92fe916a6467608f12ad4d8925719361a13d2e7912909f026`
