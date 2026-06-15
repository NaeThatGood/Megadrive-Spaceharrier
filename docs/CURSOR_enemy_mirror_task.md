# Task: Halve enemy VRAM by mirroring (stored renderer), like the trees

## Background
The trees already use a VRAM-saving trick: only the LEFT half of each scale frame
is stored in VRAM, and a second hardware sprite displays the mirror via the VDP
H-flip bit, sharing the same tiles. See `initTreePair()` / `setTreePairFrame()` /
`setTreePairPosition()` in `src/main.c`, and `write_scaled_tree_half_strip()` in
`tools/gen_scale_frames.py`.

The enemy is left-right symmetric but the STORED renderer currently stores FULL-width
frames (`spr_enemy_scaled_16/32/64`, one sprite per object). Apply the tree trick to
the enemy: store left-half frames, display each object as a left + mirrored-right pair.
This halves the enemy's VRAM footprint AND halves the tile DMA streamed on each
size-bucket change (which reduces the CPU spikes we're chasing).

Do NOT touch the RUNTIME renderer or the trees. Runtime uses `enemy_src_4bpp` and only
reads `spr_enemy_scaled_64.palette` — leave that intact.

## Part 1 — Asset generator (`tools/gen_scale_frames.py`)
The enemy master `enemy_src.png` is 64x64 and symmetric. Generate half-width enemy
strips instead of full-width:

- Add a `write_scaled_half_strip(src_name, out_name, sizes, canvas_w, canvas_h)` that
  mirrors `write_scaled_tree_half_strip`'s approach but for the square enemy (no crop):
  - half canvas width = `canvas_w // 2`
  - per frame: `h = round(size * src_h / src_w)` (= size for the 64x64 master),
    `half_w = (size + 1) // 2` (round up so the centre column is in the left half),
    resize the LEFT half of the frame to `(half_w, h)`,
    anchor to the INNER edge: `x = i*(canvas_w//2) + ((canvas_w//2) - half_w)`,
    bottom anchor: `y = canvas_h - h`.
- Change the enemy generation in `main()` to call it for the three tiers with the
  SAME tier sizes/heights as today but HALF widths:
  - `enemy_scaled_16.png`: half canvas 8 wide, 16 tall
  - `enemy_scaled_32.png`: half canvas 16 wide, 32 tall
  - `enemy_scaled_64.png`: half canvas 32 wide, 64 tall
- Keep the same palette and the same frame ordering/counts (`SIZES`, tier split).

## Part 2 — Resource defs (`res/resources.res`)
The PNGs are now half width. Update the SPRITE tile dimensions (width in tiles halves,
height unchanged):
- `spr_enemy_scaled_16`: `2 2` -> `1 2`
- `spr_enemy_scaled_32`: `4 4` -> `2 4`
- `spr_enemy_scaled_64`: `8 8` -> `4 8`
Filenames stay the same. (Compare to `spr_tree_scaled "..." 4 27` — already a 32px-wide
half def.)

## Part 3 — Stored renderer (`src/prototypes/stored_frames/render_stored.c`)
Display each object as a LEFT + mirrored-RIGHT pair, mirroring how trees do it.

- `FRAME_TIERS[].canvasPx` stays the FULL display width (16/32/64). The SpriteDefinition
  is now half that width; track `halfPx = canvasPx / 2`.
- In `setStoredFrame`, when the tier changes (the realloc branch):
  - release BOTH `o->sprs[0]` (left) and `o->sprs[1]` (right) if present;
  - add LEFT with auto-alloc: `o->sprs[0] = SPR_addSprite(tier->def, -128,-128, TILE_ATTR(PAL2,0,FALSE,FALSE));`
  - capture `sharedTile = o->sprs[0]->attribut & ~TILE_ATTR_MASK;`
  - add RIGHT sharing tiles, H-flipped, no VRAM/upload (flag 0), exactly like
    `initTreePair`'s right sprite but PAL2:
    `o->sprs[1] = SPR_addSpriteEx(tier->def, -128,-128, TILE_ATTR_FULL(PAL2,0,FALSE,TRUE,sharedTile), 0);`
  - guard against NULL on either add (release the other, set `vramIndex = TIER_NONE`).
- On frame change, call `SPR_setFrame(..., frame - tier->firstFrame)` on BOTH halves.
- In `st_update` positioning, place the pair so the full object stays centred on `sx`
  with ground contact at `syBottom` (unchanged):
  - left:  `SPR_setPosition(o->sprs[0], sx - (canvasPx/2),           syBottom - canvasPx);`
  - right: `SPR_setPosition(o->sprs[1], sx - (canvasPx/2) + halfPx,  syBottom - canvasPx);`
    (i.e. right left-edge at `sx`; seam on the object's vertical centreline.)
- `st_despawn` already loops `sprs[0..3]` releasing non-NULL — keep, it now frees both.
- `st_spawn` is unchanged in spirit (sets frame 0, which allocates the pair).

## Part 4 — Build
Regenerate assets then rebuild (macOS / SGDK):
1. `tools/venv/bin/python tools/gen_scale_frames.py`  (must reprint the 3 enemy strips)
2. `make clean && make`

## Acceptance tests
1. Builds clean, no new warnings. ROM boots in STORED mode.
2. Enemy looks identical to before at all distances — symmetric, no visible seam or
   doubled centre column, ground-contact point unchanged (shadows still line up).
3. Enemy sprites scale through all 50 steps with no missing/garbled right half on
   tier crossings (16->32->64) or rapid approach.
4. On the HUD, the CPU spikes when enemies change size bucket are smaller than before
   (half the tile DMA per change); FPS holds 59-60 more consistently.
5. Press C into RUNTIME mode: still works unchanged (it doesn't use these sheets).
