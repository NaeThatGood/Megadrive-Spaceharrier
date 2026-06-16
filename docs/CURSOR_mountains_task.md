# Cursor task: add scrolling horizon mountains (BG_A)

You are working in an **SGDK** (Sega Mega Drive) C project, a Space Harrier-style
pseudo-3D shooter. Build with `make` (wraps SGDK's `makefile.gen`); run with
`make run` (BlastEm/ares, NTSC). Implement the feature below, then build and
verify it runs.

## What already exists (do not regenerate)

The horizon mountain artwork is **already created and committed**. Use it as-is.

- **Asset:** `res/sprites/mountains.png` — a **512 x 96** indexed PNG, **7 colours**
  (fits one CRAM line). Index 0 = transparent.
  - Designed to **wrap horizontally** seamlessly at 512 px (one plane width).
  - **Strip rows 0–81:** a solid rolling mountain mass with a vertical **dithered**
    gradient — distant/dark green at the crest fading into brighter foreground
    green lower down — topped by a 2px sunlit crest. Transparent (index 0) appears
    **only above the crest**, where the sky shows through. No holes inside the mass.
  - **Strip rows 82–95 (bottom band):** a thin solid dark blue-green base band
    that stops level with the tree tops, with a line of tiny trees sitting in it.
    This band is on its own tile rows so it can be line-scrolled **faster** than
    the mountains for parallax.
- Reproducible generator (only if you ever need to tweak the art):
  `tools/gen_mountains.py` (`python3 tools/gen_mountains.py`).
- `res/sprites/mountains_wrap_preview.png` is a visual wrap-check only — **do not**
  add it to the build.

## Engine facts you must respect

- **Plane usage today:** `BG_B` = the perspective checker ground (`src/engine/ground.c`).
  The sky is a per-line HINT gradient on the **backdrop** (CRAM index 0, `src/engine/sky.c`).
  The HUD is on the `WINDOW` plane. **`BG_A` is completely unused** — put the
  mountains there.
- **Scroll mode** is set in `GROUND_init()`:
  `VDP_setScrollingMode(HSCROLL_LINE, VSCROLL_PLANE);`
  So: `BG_A` horizontal scroll is **per-scanline** (line table), `BG_A` vertical
  scroll is **one value for the whole plane**. Keep this mode.
- **Plane size** is already `VDP_setPlaneSize(64, 64, TRUE)` (512 x 512 px). `BG_A`
  shares it.
- **Horizon:** `GROUND_HORIZON` = 96 (on-screen scanline). The *current* horizon
  each frame is the global `s16 GROUND_horizon` (it moves up/down with pitch).
  `GROUND_VISIBLE_HORIZON_PAD` = 14 is the gap to the first solid ground row.
  Define `HORIZON_Y = GROUND_horizon + GROUND_VISIBLE_HORIZON_PAD`.
- **Lateral motion** is already computed in `main.c` and passed to the ground as
  `swayX` (param 1 of `GROUND_update(swayX, pitchY, vanishX, speed)`), where
  `swayX = playerX - PLAYER_CENTER_X`. Reuse this same value to drive the mountain
  parallax. Positive `swayX` = player moved right = world should slide left.

## Goal

Mountains sit on the horizon and **parallax-scroll horizontally** as the player
moves left/right. The **tree band scrolls slightly faster** than the mountains
(it is nearer). The mountains' tree-line **bottom edge lands on the horizon**, so
the peaks rise above it into the sky.

## Implementation

Create `src/engine/mountains.c` and `src/engine/mountains.h` and wire them into
`main.c` / `GROUND_update`. Suggested API:

```c
void MOUNTAINS_init(void);                 // load tiles+map+palette onto BG_A
void MOUNTAINS_update(s16 swayX, s16 horizonY); // call once per frame
```

### 1. Resource

Add to `res/resources.res`:

```
IMAGE  img_mountains "sprites/mountains.png" BEST
```

This gives `img_mountains` with `.tileset`, `.tilemap`, `.palette` (SGDK `Image`).

### 2. Palette

All four CRAM lines are nominally in use (PAL0 ground, PAL1 player, PAL2 shot,
PAL3 trees), but the strip needs only **5 colours + transparent**. Cleanest option:
load the mountain palette into a chosen line and set the BG_A tiles to that line.
**Recommended:** give the mountains **PAL3** (it is the trees' green palette and is
visually compatible) **or** load the five mountain colours into PAL0's spare slots
1–5 (the ground reserves only 0, 6–15). Pick one, load with `PAL_setPalette` /
`PAL_setColors` at init, and make the BG_A tilemap reference that palette line.
Index 0 stays transparent so the sky backdrop shows through.

### 3. Place the tilemap on BG_A

At init:

```c
VDP_clearPlane(BG_A, TRUE);
VDP_loadTileSet(img_mountains.tileset, MTN_TILE_INDEX, DMA);
// 512x96 px = 64 x 12 tiles, placed at plane rows 0..11; vscroll slides it down.
VDP_setTileMapEx(BG_A, img_mountains.tilemap,
                 TILE_ATTR_FULL(MTN_PAL, FALSE, FALSE, FALSE, MTN_TILE_INDEX),
                 0, 0, 0, 0, 64, 12, CPU);
```

Choose `MTN_TILE_INDEX` so it does not collide with the ground tileset or the
sprite VRAM budget — allocate it **after** the ground tiles (see how `TILE_USER_INDEX`
and `RUNTIME_spriteVramBudget()` are used). Verify no VRAM overlap.

### 4. Vertical position (where it lands)

`VSCROLL_PLANE`: one value. Map screen row `r` to plane row `(r + vsA) mod 512`.
We want strip **bottom (row 96)** at `HORIZON_Y`, i.e. strip row 0 at screen row
`HORIZON_Y - 96`. So:

```c
s16 vsA = 96 - horizonY;          // = -(HORIZON_Y - 96); negative auto-wraps
VDP_setVerticalScroll(BG_A, vsA);
```

Call this every frame with the live `horizonY` so the range rises/falls with pitch
like the arcade. (Fallback if it looks busy: pass a constant horizon to pin it.)
**Verify the sign on emulator** — if the strip sits the wrong way, negate `vsA`.

### 5. Horizontal parallax (two speeds, per-line)

`HSCROLL_LINE`: build a `BG_A` line-scroll table. Mountains slow, trees faster:

```c
s16 mtnScroll  = swayX >> 3;      // distant peaks: slow
s16 treeScroll = swayX >> 2;      // nearer trees:  faster
```

The band boundary in **screen space** follows the vertical scroll: the tree band
starts at strip row 82, which appears at screen row `horizonY - 14`. So for each
scanline `r`:

- `r <  (horizonY - 96)`  -> above the strip, leave as-is (sky).
- `(horizonY-96) <= r < (horizonY-14)` -> mountain rows -> `mtnScroll`.
- `(horizonY-14) <= r <  horizonY`      -> tree band     -> `treeScroll`.
- `r >= horizonY` -> ground takes over.

Write the table with `VDP_setHorizontalScrollLine(BG_A, startLine, buf, count, DMA)`
(the ground already does this for `BG_B` in `ground.c` — mirror that). Only the
~96 lines the strip occupies need updating.

**Sign/direction:** match the ground's `swayX` convention; if the mountains drift
the wrong way relative to the floor, flip the sign of `mtnScroll`/`treeScroll`.
Tune the shifts (`>>3`, `>>2`) to taste — bigger shift = slower = more distant.

### 6. Wire into the frame loop

- Call `MOUNTAINS_init()` in `main()` after `GROUND_init()` / `SPR_initEx(...)`
  (so VRAM/palette allocation order is settled) and before the main loop.
- In the loop, after `GROUND_update(...)` sets `GROUND_horizon`, call
  `MOUNTAINS_update(playerX - PLAYER_CENTER_X, GROUND_horizon + GROUND_VISIBLE_HORIZON_PAD);`
- Mountains must render **behind** the ground and trees. BG_A vs BG_B priority:
  the ground occupies scanlines at/below the horizon and the mountains sit above
  it, so they should not fight, but confirm the mountain band is not drawn over
  the checker. Use tile priority / plane order if needed.

## Acceptance criteria

1. Project builds (`make`) with no warnings from the new module and runs (`make run`).
2. A green mountain range with a tiny-tree band is visible sitting on the horizon,
   peaks rising into the sky; the tree baseline meets the ground horizon line.
3. Moving left/right scrolls the mountains horizontally; the **tree band moves
   visibly faster** than the peaks; scrolling **wraps with no visible seam**.
4. The range tracks the horizon as the player pitches up/down (or is cleanly
   pinned if you chose the fallback).
5. No corruption of the ground, sky gradient, HUD, sprites, or palette — i.e. no
   VRAM/CRAM collisions.

## Gotchas

- Don't change `VDP_setScrollingMode` or `VDP_setPlaneSize` — the ground depends
  on them.
- Don't add the `*_wrap_preview.png` to `resources.res`.
- This game runs the logic at 30 Hz (two VBlanks per frame on NTSC); keep
  `MOUNTAINS_update` cheap — it's just a couple of scroll writes plus the line
  table, no per-frame tile uploads (the wrap handles infinite repetition for free).
