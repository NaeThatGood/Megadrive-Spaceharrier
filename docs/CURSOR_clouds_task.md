# Task: Add shaded clouds to the sky (BG_A, above the mountains)

## Background
The tiled sky gradient is on BG_B. Add clouds on BG_A (the mountains plane): it isn't
mirrored, already vertical-scrolls to track the horizon, and its rows above the mountain
silhouette are transparent (sky shows through). Clouds get a gentle horizontal sway for
free because the upper strip lines already use the mountain line-scroll.

Palette: enemies/shot/shadow (PAL2) use indices 0-11, so PAL2 **12, 13, 14, 15** are free.
Use them for cloud shading (shadow / mid / light / white) and tag cloud cells PAL2. No
collision with enemies.

## Part 1 - Cloud art (`tools/gen_assets.py` or `gen_mountains.py`)
- Generate `res/sprites/clouds.png`: a full-plane-width strip, 64 tiles wide x ~5 tiles tall
  (40px), indexed. Paint a few stylised clouds spread across the width; leave everything else
  index 0 (transparent). Clouds use ONLY indices 12-15:
  15 = white (tops), 14 = light, 13 = mid, 12 = shadow (undersides). rescomp dedups the
  mostly-transparent strip down to just the cloud tiles.
- Keep clouds in the TOP ~40px so they sit above the mountain peaks (tune so no cloud cell
  overlaps the silhouette).

## Part 2 - Resource + VRAM
- Add `IMAGE img_clouds "sprites/clouds.png" NONE` to `res/resources.res`.
- Load after the sky tiles: `CLOUD_TILE_INDEX = SKY_TILE_INDEX + img_sky.tileset->numTile`,
  `VDP_loadTileSet(img_clouds.tileset, CLOUD_TILE_INDEX, DMA)` in `MOUNTAINS_init`.
- IMPORTANT: update `runtimeVramBase()` in `render_runtime.c` to ALSO add
  `img_clouds.tileset->numTile` (chain: ground + mountains + sky + clouds), so runtime slots
  start after the clouds.

## Part 3 - Palette (watch the renderer-switch clobber)
- Set PAL2 indices 12-15 to the cloud greys/white, e.g.
  `0x303848` (shadow), `0x707888`, `0xB0B8C8`, `0xF0F4FF` (white).
- CRITICAL: `st_init` and `rt_init` both call `PAL_setPalette(PAL2, ...enemy...)`, which
  overwrites all 16 PAL2 entries including 12-15. So re-apply the four cloud colours AFTER
  every PAL2 load. Add a small `CLOUDS_applyPalette()` and call it at the end of `st_init`
  and `rt_init` (and once at boot). Otherwise clouds lose their colours on a C-toggle.

## Part 4 - Write the cloud band into BG_A (`src/engine/mountains.c`)
In `MOUNTAINS_init`, AFTER the existing mountains `VDP_setTileMapEx` (PAL0) and after
loading the cloud tileset, write the cloud strip into the TOP rows of BG_A with PAL2:
- `VDP_setTileMapEx(BG_A, img_clouds.tilemap,
     TILE_ATTR_FULL(PAL2, FALSE, FALSE, FALSE, CLOUD_TILE_INDEX),
     0, 0, 0, 0, 64, <cloudRowsTall>, CPU);`
- This sits in the strip's transparent upper rows; transparent cloud cells leave the sky
  showing. Make sure `<cloudRowsTall>` stays above the mountain silhouette rows so the band's
  transparent cells don't erase any peaks.
- Clouds scroll vertically with BG_A (tracks the horizon) and sway gently via the existing
  upper-strip line-scroll - no extra code needed.

## Part 5 - Build & verify
1. `make clean && make` - clean build, no new warnings.
2. Clouds appear in the sky above the mountains, shaded (not flat), reading well against the
   gradient. No clipping into the mountain peaks.
3. Pan left/right: clouds sway gently (slower than the ground) for parallax; no tearing.
4. Climb/descend: clouds track the horizon with the mountains.
5. Enemies/shots/shadows still render correctly (PAL2 0-11 untouched).
6. Press C to toggle STORED/RUNTIME a few times: cloud colours persist (Part 3 re-apply
   works); RUNTIME still gets VRAM (updated `runtimeVramBase()`), or falls back to STORED
   without crashing.

## Fallback (if multi-tone is fiddly)
For a simpler first pass: flat single-colour clouds baked straight into `img_mountains`'s
upper rows using PAL0's one free entry (index 6 = white). No new tileset/palette/VRAM
changes - just art + `PAL_setColor((PAL0<<4)+6, white)`. Upgrade to the shaded version above
later. Stylised flat clouds actually suit the look.
