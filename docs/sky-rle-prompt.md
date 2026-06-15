# Cursor task: run-length the sky H-interrupt

## Context

This is an SGDK (Sega Genesis Dev Kit) Space Harrier–style game. The sky gradient
is drawn in `src/engine/sky.c` by an H-interrupt callback (`sky_hint`) that rewrites
CRAM index 0 (the backdrop colour) once per scanline across a gradient band of
`SKY_TRANSITION` (40) lines. Toggling it on (`BUTTON_Y`, `sky_setEnabled`) costs
several FPS.

The flat region *above* the band is already skipped with zero interrupts down to the
fixed line `SKY_HINT_START`, using the `skySkipLines` software counter plus
`VDP_setHIntCounter`. **Inside the band, however, the handler currently calls
`VDP_setHIntCounter(0)` — one interrupt on every line (~41/frame).**

Mega Drive CRAM is 3 bits per channel (8 levels). When the keyframe gradient in
`SKY_KEYFRAMES` is interpolated and quantised to hardware colour, the whole 41-line
band resolves to only **8 distinct hardware colours** — so ~33 of the 41 per-line
CRAM writes set a value identical to the previous line. They are invisible work and
pure interrupt overhead.

## Goal

Fire an H-interrupt **only at each colour boundary**, not every line. Drop interrupt
count from ~41 to ~8 per frame with **pixel-identical output**. Do not change the
visual result, the public API in `sky.h`, or the flat-top behaviour.

## Approach

1. **Build a run-length table of the gradient in VDP colour space.** The full ramp is
   already precomputed in `ramp[]` (in `sky_init`, via `keyframeColor`). Add a static
   RLE table, e.g.:

   ```c
   typedef struct { u16 color; u8 span; } SkyRun;   // span = scanlines this colour holds
   ```

   Walk `ramp[]` over the band range once at init, coalescing consecutive identical
   `u16` VDP colours into `(color, span)` runs. Store into a fixed-size array
   (`SKY_MAX_RUNS`, safe upper bound e.g. 16). Record the run count.

2. **Drive the HINT from the RLE table instead of per-line.** Keep the existing flat-top
   skip to `SKY_HINT_START` unchanged. Once inside the band, each HINT should:
   - write the current run's `color` to CRAM index `SKY_CRAM_INDEX` during HBlank
     (keep the existing raw `VDP_CTRL_PORT` / `VDP_DATA_PORT` write — it's the
     time-critical line),
   - advance to the next run,
   - reprogram `VDP_setHIntCounter` to `span - 1` so the *next* interrupt fires at the
     next colour boundary rather than the next line,
   - when the last run is consumed, do the existing reset-line / gate-off sequence
     (restore `SKY_HINT_START - 1` and `VDP_setHInterrupt(FALSE)` so the counter is
     **never** left at 0 going into VBlank).

3. **Per-frame setup in `sky_vblank`.** The horizon moves each frame, which shifts where
   the band starts and how many of its lines are on-screen. The RLE *colours and spans*
   are constant (gradient shape doesn't change), so the table can be built once at
   `sky_init`. In `sky_vblank`, select the starting run + intra-run offset from the
   current `startIndex` / `bottom`, mirroring how `skyReadPtr` and `skyLinesLeft` are
   derived today. Keep `skyResetLine` and the flat-top `skySkipLines` logic intact.

## Hard constraints / gotchas

- **Counter-reload timing is fragile here.** A prior "variable counter" version broke;
  the current code is deliberately built around a *constant* first-HINT line plus a
  software skip. Reintroducing per-boundary `VDP_setHIntCounter` writes revisits that
  path. Apply the same off-by-one margin already used at `SKY_HINT_START`, and make sure
  the counter is never 0 at VBlank entry.
- The CRAM write on a band line is written **during HBlank** and must stay minimal — do
  not add work to the hot path beyond advancing the run index and one counter write.
- Do not change `sky.h`'s public functions, `SKY_KEYFRAMES`, `SKY_CRAM_INDEX`,
  `SKY_HINT_START`, or `SKY_BOTTOM_ADJUST` semantics.
- `vu16`/`vu16*` volatility on the HINT-shared state must be preserved.

## Acceptance criteria

- Sky looks identical to the current build at every horizon position (sweep altitude
  up/down with the D-pad; compare against `BUTTON_Y` off baseline).
- HUD CPU meter shows a clear drop with sky enabled; interrupts/frame in the band ~8
  rather than ~41.
- No flicker, no stray coloured line at the top of the screen on the frame after the
  band ends (the classic symptom of the counter being left at 0 into VBlank).
- Builds clean with the existing `Makefile` (SGDK, Mac/Docker).

## Verification

Build and run in Genesis Plus GX. A/B with sky off using the in-ROM HUD CPU/FPS meter,
and confirm the gradient is byte-identical by eye across a full altitude sweep.
