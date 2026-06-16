# Cursor prompt: hybrid VBlank palette-scheme fade (compute-once, then playback)

Paste the block below into Cursor (agent mode) with these files in context:
`src/engine/ground.c`, `src/engine/ground.h`, `src/engine/mountains.c`,
`src/engine/mountains.h`, `src/main.c`.

---

## Goal

Add a per-level colour-scheme system with a smooth transition that runs entirely
during VBlank. Pressing **C** cycles to the next scheme; the checkerboard and the
mountain strip fade together to the new scheme over a short, controllable run of
frames. Use the **hybrid** approach: at the moment a fade starts, compute all the
intermediate keyframe colours **once** into a scratch buffer, then just play that
buffer back over the following frames (no per-frame interpolation). Only push a
CRAM write on a frame where a colour actually changes.

## Background on the current code (verify against the live source before editing)

- The **checkerboard** is not a static palette. It is driven by two runtime colours
  in `ground.c`: `colLight` / `colDark`. From those, `rebuildCheckerBlends()` builds
  gradient tables and `buildCheckerCols()` regenerates the 8 CRAM entries
  (`CHECKER_PAL_BASE = 7`, `CHECKER_ENTRIES = 8`) **every frame** as part of the
  forward-scroll animation, uploaded inside `GROUND_update` at the `forwardAnim`
  label via `PAL_setColors(CHECKER_PAL_BASE, checkerCols, CHECKER_ENTRIES, DMA_QUEUE)`.
  => To recolour the checker, set `colLight`/`colDark` and call
  `rebuildCheckerBlends()`. Do **not** add a separate CRAM write for the checker —
  the existing per-frame animation upload already pushes the new colours.

- The **mountains** are a static 6-colour palette: PAL0 indices 1..6
  (`MTN_PAL_FIRST_COLOR = 1`, `MTN_PAL_COLOR_COUNT = 6`), loaded once in
  `MOUNTAINS_init`. => To recolour the mountains, write those 6 CRAM entries.

- Colours use the `RGB24_TO_VDPCOLOR(0x......)` macro. The VDP colour format stores
  3 bits per channel at bit shifts 1, 5, 9 (see the existing `lerpColor` in `ground.c`).

- `DMA_QUEUE` mode defers the CRAM transfer to the next VBlank, which is exactly the
  timing required here.

## Design

Create one small module that owns scheme data and the fade — `src/engine/scenepal.c`
and `src/engine/scenepal.h` ("scene palette"). It is the single source of truth for
runtime scheme swaps and coordinates both engines via thin setters.

### Scheme data (single source of truth)

```c
typedef struct {
    u16 checkerLight;                 // drives the animated checkerboard
    u16 checkerDark;
    u16 mtn[6];                       // PAL0[1..6] mountain strip
} SceneScheme;

static const SceneScheme SCENE_SCHEMES[] =
{
    {   // 0: original look — MUST reproduce the current game exactly
        RGB24_TO_VDPCOLOR(0x6098D8), RGB24_TO_VDPCOLOR(0x204090),
        { RGB24_TO_VDPCOLOR(0x005800), RGB24_TO_VDPCOLOR(0x00A800),
          RGB24_TO_VDPCOLOR(0x28E028), RGB24_TO_VDPCOLOR(0x204090),
          RGB24_TO_VDPCOLOR(0x009000), RGB24_TO_VDPCOLOR(0x00D000) },
    },
    {   // 1: sand / beige desert
        RGB24_TO_VDPCOLOR(0xE8D0A0), RGB24_TO_VDPCOLOR(0xA88048),
        { RGB24_TO_VDPCOLOR(0x604018), RGB24_TO_VDPCOLOR(0xA87830),
          RGB24_TO_VDPCOLOR(0xD8B868), RGB24_TO_VDPCOLOR(0x806840),
          RGB24_TO_VDPCOLOR(0x906828), RGB24_TO_VDPCOLOR(0xC09848) },
    },
};
#define SCENE_SCHEME_COUNT  (sizeof(SCENE_SCHEMES)/sizeof(SCENE_SCHEMES[0]))
```

Verify scheme 0's mountain colours match the current `img_mountains.palette` entries
1..6 and scheme 0's checker colours match the current `GROUND_init` defaults, so the
game looks identical at boot. (These are the values found in the current assets, but
confirm.)

### Fade tuning

```c
#define FADE_KEYFRAMES  6     // intermediate snapshots, last one == exact target
#define FADE_HOLD       6     // frames each keyframe is held on screen
```

### Hybrid mechanism

Keep a mirror of the **currently displayed** colours so a fade always starts from
where the screen actually is (this makes an interrupted/re-pressed fade behave):

```c
static u16 curLight, curDark, curMtn[6];   // what's on screen right now
```

Scratch keyframe buffers, filled **once** per fade start:

```c
static u16 kfLight[FADE_KEYFRAMES], kfDark[FADE_KEYFRAMES];
static u16 kfMtn[FADE_KEYFRAMES][6];
static u16 fadeIdx;     // 0..FADE_KEYFRAMES-1, next keyframe to apply
static u16 holdCount;   // frames held on the current keyframe
static bool fading;
static u16 schemeIndex;
```

A local 3-bit-channel interpolation helper (same shift pattern as `ground.c`'s
`lerpColor`, but with an arbitrary denominator):

```c
static u16 lerpColorN(u16 a, u16 b, u16 num, u16 den); // num/den of the way a->b
```

### Public API (scenepal.h)

```c
#define SCENE_SCHEME_COUNT_PUBLIC  /* expose the count, or a getter */ 
void SCENEPAL_init(void);          // apply scheme 0 instantly; sets mirrors
void SCENEPAL_cycle(void);         // advance to next scheme, start a fade
void SCENEPAL_setScheme(u16 index);// start a fade to a specific scheme
void SCENEPAL_update(void);        // call once per frame (plays back the fade)
```

### Behaviour

`SCENEPAL_setScheme(index)` (compute-once):
- `schemeIndex = index % SCENE_SCHEME_COUNT;` target = `&SCENE_SCHEMES[schemeIndex]`.
- For `k = 0 .. FADE_KEYFRAMES-1`, fill the scratch buffers by interpolating from the
  current mirror to the target with `num = k+1`, `den = FADE_KEYFRAMES` (so the last
  keyframe equals the exact target). Do this for `checkerLight`, `checkerDark`, and
  each of the 6 mountain entries.
- `fadeIdx = 0; holdCount = 0; fading = TRUE;`
- Do **not** touch CRAM here — only fill scratch. Playback happens in `SCENEPAL_update`.

`SCENEPAL_cycle()`:
- `SCENEPAL_setScheme(schemeIndex + 1);`

`SCENEPAL_update()` (playback, runs every frame; cheap, no interpolation):
- If `!fading` return.
- `if (holdCount > 0) { holdCount--; return; }`  // still holding this keyframe
- Apply keyframe `fadeIdx`:
  - **Checker:** `GROUND_setCheckerColors(kfLight[fadeIdx], kfDark[fadeIdx]);`
    update `curLight/curDark`. (No CRAM write — the ground animation uploads it.)
  - **Mountains:** build the new 6-entry array from `kfMtn[fadeIdx]`. If it differs
    from `curMtn` in any entry, write all 6 with one
    `MOUNTAINS_setColors(...)` (which uses `DMA_QUEUE`) and copy into `curMtn`.
    If nothing changed (common, because 3-bit colour makes consecutive keyframes
    repeat), skip the write entirely — this is the "only write on changed frames" win.
  - `holdCount = FADE_HOLD - 1;`
  - `fadeIdx++; if (fadeIdx >= FADE_KEYFRAMES) fading = FALSE;`

`SCENEPAL_init()`:
- Set mirrors from `SCENE_SCHEMES[0]`, apply checker colours via
  `GROUND_setCheckerColors`, write mountains via `MOUNTAINS_setColors`, `fading = FALSE`.

### Thin setters to add

In `ground.c` / `ground.h`:
```c
// Set the two checker driver colours and rebuild blend tables. Does NOT write
// CRAM directly; the per-frame checker animation in GROUND_update uploads them.
void GROUND_setCheckerColors(u16 light, u16 dark);
// body: colLight = light; colDark = dark; rebuildCheckerBlends();
```
Leave the existing `GROUND_setStagePalette` / `STAGE_CHECKER` alone, but it is now
superseded by scenepal for runtime swaps — if trivial, have `GROUND_setStagePalette`
delegate to `GROUND_setCheckerColors`, otherwise leave it untouched. Do not change
`GROUND_init`'s boot default behaviour.

In `mountains.c` / `mountains.h`:
```c
// Write the 6 mountain palette entries (PAL0[1..6]) during the next VBlank.
void MOUNTAINS_setColors(const u16* colors6);
// body: PAL_setColors(MTN_PAL_FIRST_COLOR, colors6, MTN_PAL_COLOR_COUNT, DMA_QUEUE);
```

### Wiring in main.c

- `#include "engine/scenepal.h"`.
- After `MOUNTAINS_init();` in `main`, call `SCENEPAL_init();`.
- In the main loop's non-paused block, call `SCENEPAL_update();` **before**
  `GROUND_update(...)` so the updated checker colours are used the same frame.
- In `handleInput()`, add (BUTTON_C is currently free):
  ```c
  if (pressed & BUTTON_C) SCENEPAL_cycle();
  ```

## Constraints

- SGDK C (`genesis.h`); match existing code style; no new libraries.
- All CRAM writes go through `DMA_QUEUE` so they land in VBlank. No `CPU`-mode CRAM
  writes during active display.
- Palette only — do not touch tilemaps, tilesets, or scroll code.
- The per-frame cost when **not** fading must be ~zero (an early `return`).
- During a fade, the only added per-frame work is the playback bookkeeping; the
  interpolation is computed once at fade start, not per frame.

## Verify

- Builds with the existing Makefile / SGDK toolchain.
- At boot the game looks identical to before (scheme 0).
- Pressing C fades checker + mountains together to sand, then back to the original on
  the next press; colours land exactly on the target scheme (no drift).
- Confirm (e.g. in an emulator's CRAM/VRAM debugger or by reasoning) that CRAM is
  written only on keyframe-change frames, not every frame, and only during VBlank.
