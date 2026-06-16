# Task: Replace the HINT sky with a tiled gradient on BG_B (remove the H-interrupt)

## Background
The sky is currently a per-scanline H-interrupt (`src/engine/sky.c`, `sky_hint`) that
rewrites CRAM index 0 every line. It costs ~20% CPU. Replace it with a static tiled
gradient baked into the BG_B board above the horizon. The gradient is horizontally
uniform, so it dedups to a single ~20-tile column repeated across the plane (near-zero
VRAM). BG_B already vertical-scrolls to track the horizon, so the gradient stays pinned
to the horizon for free. Net: delete the HINT, recover ~20%, and open the door to clouds.

Palette: the trees (PAL3) only use indices 0-3 and 6-11, so PAL3 indices **4, 5, 12, 13,
14, 15** are free. The sky tiles use ONLY those indices and are tagged PAL3 in the
tilemap, so they share the line with the trees without collision.

## Part 1 - Generate the sky gradient column (`tools/gen_assets.py`)
- The board horizon sits at image row `BOARD_HORIZON = GROUND_HORIZON + BOARD_PAD_TOP`
  (~160px ~ 20 tile rows above the on-screen horizon). Generate an 8px-wide x `BOARD_HORIZON`
  -tall indexed PNG `res/sprites/sky_gradient.png`:
  - Vertical gradient from a deep zenith colour at the TOP to a pale near-horizon colour
    at the BOTTOM (row `BOARD_HORIZON-1`, i.e. just above the horizon line).
  - Use ONLY palette indices 4, 5, 12, 13, 14, 15 for the 6 gradient stops. Never index 0
    (must be opaque). Map stops top->bottom to indices [12, 13, 14, 15, 5, 4].
  - Dither at the transitions between stops with an 8px-PERIODIC ordered pattern (e.g. 2x2
    Bayer) so a single tile per row still tiles seamlessly across the plane width.
- The image only needs to be 8px wide (one tile column); it will be repeated horizontally.

## Part 2 - Resource + VRAM (`res/resources.res`, `src/engine/*`)
- Add `IMAGE img_sky "sprites/sky_gradient.png" NONE` (or TILESET) to `resources.res`.
- Reserve a VRAM region for the sky tiles. Current layout is ground tiles at
  `TILE_USER_INDEX`, then mountains at `MTN_TILE_INDEX`. Add the sky tileset AFTER the
  mountains: define `SKY_TILE_INDEX = MTN_TILE_INDEX + img_mountains.tileset->numTile`,
  and load it in `GROUND_init` (or `sky` init) with `VDP_loadTileSet(img_sky.tileset, SKY_TILE_INDEX, DMA)`.
- IMPORTANT: update `runtimeVramBase()` in `render_runtime.c` to also add
  `img_sky.tileset->numTile`, so the runtime slots/sprite pool start after the sky tiles.

## Part 3 - Palette init
After the trees load PAL3 (`initTrees` does `PAL_setPalette(PAL3, spr_tree_scaled.palette...)`),
set the 6 sky entries WITHOUT disturbing the tree colours:
- `PAL_setColor((PAL3<<4)+12, <zenith>); ...+13, +14, +15, +4, +5` down to the near-horizon
  colour, matching the PNG's top->bottom index mapping exactly.
- Also set PAL0 index 0 (backdrop) to the zenith colour as a safety fill for the top edge
  at maximum pitch.
- Suggested stops (tune to taste), zenith->horizon:
  `0x2A4B8D, 0x3A63B0, 0x4E80CC, 0x6CA0E0, 0x97C2EE, 0xC2E0F5`.

## Part 4 - Write the sky into the board tilemap (`src/engine/ground.c`)
In `drawMirroredGround`, the board is written row by row. Compute
`skyRows = BOARD_HORIZON / 8` (tile rows above the horizon). For each board tile row `y`:
- if `y < skyRows`: fill the ENTIRE row (all `BOARD_TILES_W` columns, no mirror/flip needed -
  it's uniform) with the sky tile for that height:
  `TILE_ATTR_FULL(PAL3, FALSE, FALSE, FALSE, SKY_TILE_INDEX + y)`.
- else: write the ground checker tiles exactly as today (PAL0, mirrored).
This replaces the old index-0 (transparent) rows above the horizon with the opaque gradient.
Mountains (BG_A) still render in front of BG_B and are transparent above their silhouette,
so they correctly occlude the sky and the sky shows through above them.

## Part 5 - Remove the HINT sky (`src/main.c`, delete `src/engine/sky.c` & `sky.h`)
- Delete `src/engine/sky.c` and `src/engine/sky.h`.
- In `main.c`: remove `#include "engine/sky.h"`, the `sky_init()` call, both `sky_setHorizon()`
  calls, the direct `sky_vblank()` call, and `SYS_setVIntCallback(sky_vblank)` (pass NULL or
  remove if nothing else uses the VInt callback).
- Remove the `BUTTON_Y` sky toggle in `handleInput` (and `sky_setEnabled`/`sky_isEnabled`
  usage). Leave Y unbound or repurpose later.
- Confirm nothing else references `GROUND_VISIBLE_HORIZON_PAD` via the sky; the ground/
  mountain horizon wiring is unchanged.

## Part 6 - (OPTIONAL, after Part 1-5 verified) Clouds on BG_A
- Extend the mountains art (`gen_mountains.py` / `img_mountains`) to add a few stylised cloud
  shapes ABOVE the mountain strip, using PAL3 sky indices (e.g. 15 = white, 14 = light) so
  they read against the gradient. Tag those BG_A cells PAL3 in the tilemap.
- BG_A isn't mirrored (clouds stay asymmetric) and already tracks the horizon vertically.
- Optionally extend the BG_A above-strip H-scroll lines with a slow drift for cloud parallax.
- Keep cloud tile count modest (~20-60 tiles).

## Part 7 - Build & verify
1. `make clean && make` - clean build; `sky.c` gone, no dangling references.
2. Sky renders as a smooth (dithered) vertical gradient above the horizon, no banding seams,
   no flicker, no per-scanline artefacts. Trees still render correctly (shared PAL3).
3. Climb/descend: the gradient stays pinned to the horizon and scrolls with it; no index-0
   backdrop shows at the screen top across the full pitch range.
4. Mountains still sit correctly in front of the sky; checker unchanged below the horizon.
5. HUD: CPU is ~20% lower than with the HINT, and flat (no per-line interrupt cost). FPS 59-60.
6. Press C -> RUNTIME still works (verify the updated `runtimeVramBase()` left it enough VRAM;
   the `RUNTIME_slotCapacity()==0` fallback should keep STORED if not - must not crash).

## Notes
- The gradient column is ~20 tiles total regardless of width (horizontal repeat); the only
  real VRAM spend is optional clouds.
- Vertical resolution is per-pixel within each 8px tile, so the gradient is smooth; banding
  comes only from the 6 colours - dithering hides it.
