# README hero provenance

`hero-device.png` is an AI-generated product illustration created with the
built-in `image_gen` tool on 2026-10-04. It is not a photograph of the connected
appliance and is not evidence of hardware or display validation.

The round white shell and proportions were referenced from the manufacturer's
**JC3636W518 Specifications-EN** manual, pages 7-8 (58 mm diameter, 11 mm height).
The on-screen content was referenced from the English `display-status-en.png`
view rendered by `tools/render_display_previews.py` from the firmware source.
The generated image approximates that UI and the industrial design; the
separate source-rendered previews remain the precise layout reference.

No manufacturer page, logo or contact details are embedded in the final asset.
The exact generation prompt follows; the input names describe local reference
images used during generation, not dependencies required to render the README.

```text
+Use case: compositing / product-mockup.
Create one polished landscape GitHub README hero banner, roughly 16:9, for the open-source project "ESP AdBlock Round".
Input image 1 (/work/jc3636-hero-detail-8.png): hardware form reference ONLY. It shows the actual Guition JC3636W518C small circular white puck, 58 mm diameter and 11 mm thick, circular glossy black glass screen, slim white shell. Preserve this squat round puck form and thin proportions, NOT a watch, not a square dev board, no stand, no invented bulky casing. Do not reproduce the manual's page, company logo, measurements or phone number.
Input image 2 (display-status-en.png): precise display content reference. Use this real firmware screen inside the puck glass: nearly black circular screen, tiny STATUS top center, thin teal shield/check, large pixel-font ACTIVE, smaller PROTECTION ACTIVE, five tiny page dots with first teal. Match this content closely and keep it sharp and legible; no invented counters or UI.
Scene: premium restrained product illustration on a deep charcoal/navy desktop-like studio surface with subtle soft teal light and realistic soft contact shadow. One device only on the right half, almost front-on with a gentle elevated 3/4 angle so screen remains easily readable, lower rim visible. Thin white shell. Quiet negative space on left.
On the left add only these exact English words in clean modern sans-serif, generous spacing:
"ESP AdBlock"
"Round"
small supporting line: "Local DNS ad & tracker blocking"
small restrained footer: "ESP32-S3  /  Wi-Fi  /  Touch"
The main title should be large, off-white, with Round in muted teal. Device occupies around 40% of canvas width with roomy outer padding. No exaggerated perspective, busy cyber circuit patterns, floating objects, extra devices, cable tangle, stock badges, manufacturer logo or watermark.
Make clear polished editorial product artwork rather than pretending this is an actual hardware photograph. Accurate real-device shape and real provided firmware UI are crucial.
```
