# Cursor prompt: per-stage checkerboard / background palette switching

Paste the block below into Cursor (Composer/agent mode) with `src/engine/ground.c` and
`src/engine/ground.h` in context.

---

## Task

Add per-stage palette switching to the ground engine so each level can recolour the
checkerboard (and optionally the sky) with no tile/tilemap changes — same trick the
arcade Space Harrier uses to signal a new stage. The recolour must be a single cheap
call that takes effect on the next frame.

## Current state (do not break)

In `src/engine/ground.c` the checkerboard is a flat two-tone pattern living entirely in
`PAL0` indices 7..14 (`CHECKER_PAL_BASE = 7`, `CHECKER_ENTRIES = 8`). The two source
colours are compile-time constants:

```c
#define COL_LIGHT  RGB24_TO_VDPCOLOR(0x6098D8)
#define COL_DARK   RGB24_TO_VDPCOLOR(0x204090)
```

`GROUND_init()` builds the gradient tables `blendLD[4]` / `blendDL[4]` once from these
two colours using `lerpColor()`. The per-frame forward-scroll animation at label
`forwardAnim` in `GROUND_update()` rebuilds `checkerCols[]` from `COL_LIGHT`, `COL_DARK`,
`blendLD`, `blendDL` and uploads via `PAL_setColors(CHECKER_PAL_BASE, ...)`. Keep all of
this animation logic working unchanged.

## Required changes

1. **Make the checker colours runtime values.** Replace the `COL_LIGHT` / `COL_DARK`
   `#define`s with two file-scope statics (e.g. `static u16 colLight, colDark;`).
   Update `lerpColor` usage and the `forwardAnim` loop to read the variables.

2. **Factor out the blend-table build.** Move the
   `for (s=0..3) blendLD[s]=...; blendDL[s]=...;` loop out of `GROUND_init` into a small
   static helper `static void rebuildCheckerBlends(void)` that uses the current
   `colLight`/`colDark`. Call it from `GROUND_init` after setting the defaults.

3. **Add a stage palette table + public setter.** Define a small table of stage colour
   sets near the top of `ground.c`:

   ```c
   typedef struct { u16 light; u16 dark; } CheckerColors;
   static const CheckerColors STAGE_CHECKER[] = {
       { RGB24_TO_VDPCOLOR(0x6098D8), RGB24_TO_VDPCOLOR(0x204090) }, // 0: blue (current)
       { RGB24_TO_VDPCOLOR(0x60C060), RGB24_TO_VDPCOLOR(0x206020) }, // 1: green
       { RGB24_TO_VDPCOLOR(0xD89060), RGB24_TO_VDPCOLOR(0x903020) }, // 2: amber/desert
       // add more stages here
   };
   #define STAGE_CHECKER_COUNT  (sizeof(STAGE_CHECKER)/sizeof(STAGE_CHECKER[0]))
   ```

   Add a public function declared in `ground.h`:

   ```c
   // Recolour the checkerboard for the given stage. Index wraps modulo the
   // number of defined stages. Cheap: rebuilds blend tables and re-uploads the
   // 8 checker palette entries; no VRAM tile changes. Takes effect next frame.
   void GROUND_setStagePalette(u16 stageIndex);
   ```

   Implement it in `ground.c`: pick `STAGE_CHECKER[stageIndex % STAGE_CHECKER_COUNT]`,
   assign `colLight`/`colDark`, call `rebuildCheckerBlends()`, then push the new colours
   immediately by rebuilding `checkerCols[]` for the current `fwdAcc` phase and calling
   `PAL_setColors(CHECKER_PAL_BASE, checkerCols, CHECKER_ENTRIES, DMA_QUEUE)` (so the
   change shows even if `GROUND_update` isn't called that frame). Reuse the existing
   `forwardAnim` colour-selection logic — extract it into a static helper
   `static void buildCheckerCols(void)` if that avoids duplication.

4. **Default init.** In `GROUND_init`, set the stage-0 colours before building blends so
   behaviour with no caller is identical to today (still blue).

## Constraints

- SGDK C (genesis.h). Match the existing code style; no new libraries.
- Do not touch tilemaps, tilesets, or `drawMirroredGround()` — palette only.
- Keep the per-frame animation cost the same; `GROUND_setStagePalette` is called only on
  stage transitions, not per frame.
- `RGB24_TO_VDPCOLOR` is the existing macro for colour literals; use it for all colours.

## Optional (only if trivial)

If clean, extend the stage table and setter to also swap the sky (`PAL3`, currently set
from `img_sky.palette`) per stage, so the whole horizon shifts. Skip if it requires
reworking how the sky palette is loaded.

## Verify

- Project still builds with the existing Makefile / SGDK toolchain.
- With no `GROUND_setStagePalette` call, the ground looks identical to before (blue).
- Calling `GROUND_setStagePalette(1)` then `(2)` recolours the checkerboard to green then
  amber, and the forward-scroll animation still flows correctly in the new colours.
