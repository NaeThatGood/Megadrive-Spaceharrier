# Cursor task: close the gap between the sky gradient bottom and the horizon

## Context

`src/engine/sky.c` draws a horizon-anchored sky gradient by rewriting CRAM index 0
(the backdrop) per scanline via an H-interrupt. It was recently optimised from a
per-line CRAM write to a **run-length (RLE)** scheme: equal-colour scanlines are
coalesced into `SkyRun { u16 color; u8 span; }` runs (`buildSkyRuns`), and each HINT
writes one colour then reprograms `VDP_setHIntCounter(span - 1)` to *hold* it instead
of firing every line. This restored the lost FPS.

Regression introduced by that change: **there is now a gap between the bottom of the
gradient and the visible ground horizon.** A wrong-coloured strip appears just above
the horizon at some altitudes.

### Why the gap exists

`ramp[]` is built so that below the keyframe band (index >= `SKY_H0`) it is a flat run
of `KF[0]`, the gradient's **bottom** colour. The old per-line renderer wrote a fresh
ramp colour on *every* scanline down to the horizon, so the bottom colour always
reached the horizon for free — the per-line redraw masked any sizing error.

The RLE renderer collapses that flat bottom region into a **single trailing run** that
must be held all the way to the horizon. Its length is currently governed by a
hand-tuned constant `SKY_BOTTOM_ADJUST` (`bottom = h + SKY_BOTTOM_ADJUST`,
`SKY_RLE_END_INDEX = SKY_H0 + SKY_BOTTOM_ADJUST`). Because the horizon moves on the Y
axis every frame, a fixed constant is wrong at some altitudes, leaving the trailing run
too short. When the held run runs out, index 0 falls back to the **reset line colour**,
which writes `ramp[0]` — the gradient's **top** colour, not the bottom — so the gap
shows as a top-colour strip above the horizon.

## Goal

Make the gradient's bottom colour always meet the visible horizon at every altitude,
with no per-altitude hand tuning, while keeping the RLE performance win. Do not change
the public API in `sky.h` or the visible gradient shape.

## Required changes

### 1. Drive the trailing fill from the live horizon, not a constant

- The visible ground horizon is `GROUND_horizon` (runtime, exported from
  `src/engine/ground.h`); the sky is currently told `h = GROUND_horizon +
  GROUND_VISIBLE_HORIZON_PAD` via `sky_setHorizon`. Keep that 14px pad as deliberate
  overlap (gradient bottom should reach *past* the ground top by the pad, so it tucks
  under the ground tiles).
- In `sky_vblank`, after the gradient's changing runs, ensure the final `KF[0]`
  (bottom-colour) run is extended so the band bottom lands on (or just past) the live
  horizon every frame. Compute the trailing span from the current scanline to the
  horizon target instead of from `SKY_BOTTOM_ADJUST`. The RLE table still holds the
  gradient's changing runs; only the *last* run's on-screen span needs to track `h`.
- `selectSkyRuns` / the HINT run-walk already clip the last run to `lineCount` — keep
  that, but make `lineCount` (and the RLE table's trailing `KF[0]` capacity) large
  enough that the clip, not the table, decides where the bottom colour ends. The
  trailing run must never be the limiting factor across the full legal horizon range
  (`GROUND_horizon` min..max + pad).

### 2. Make the reset/backdrop default the bottom colour, not the top

- The reset line and the `!enabled` path currently write `ramp[0]`, which is the
  gradient **top** colour. Change the reset-line write (the one that fires just below
  the band, and the gate-off fallback) to write the **horizon/bottom** colour
  (`ramp[SKY_H0]`, i.e. `KF[0]`) instead. This way any residual one-line timing slop
  near the horizon is the *same colour as the horizon* and therefore invisible.
- Leave the **flat-top** fill (the single CRAM write at the top of `sky_vblank` that
  holds lines above the band) writing `ramp[0]` (top colour) — that one is correct.

## Hard constraints / gotchas

- Preserve the counter-timing safety rules already in the file: the HINT counter must
  **never** be left at 0 going into VBlank (it re-arms to `SKY_HINT_START - 1`), and
  the fixed first-HINT-at-`SKY_HINT_START` + software `skySkipLines` skip must stay
  intact. Do not reintroduce a horizon-dependent *first* counter value.
- Keep the band CRAM write (`VDP_CTRL_PORT` / `VDP_DATA_PORT`) as the only work on the
  time-critical HBlank path; don't add computation inside `sky_hint`.
- Keep `vu16`/volatile qualifiers on HINT-shared state.
- If `SKY_BOTTOM_ADJUST` becomes redundant after tying the fill to the live horizon,
  remove it (and the `SKY_RLE_END_INDEX` dependence on it) or repurpose it as a small
  fixed overlap margin only — don't leave two competing length controls.

## Acceptance criteria

- No gap or wrong-colour strip between the gradient bottom and the horizon at any
  altitude — sweep the player up and down through the full pitch range and watch the
  horizon line; the bottom colour meets it cleanly throughout.
- Gradient shape, colours, and FPS are unchanged vs the current RLE build (HUD CPU/FPS
  meter; interrupts/frame still ~8 in the band).
- No flicker and no stray coloured line at the top of screen on the frame after the
  band ends.
- Builds clean with the existing `Makefile` (SGDK, Mac/Docker).

## Verification

Build and run in Genesis Plus GX. Toggle sky with BUTTON_Y to A/B against the backdrop,
and pitch the horizon up/down across its whole range confirming the bottom colour stays
locked to the horizon with no seam.
