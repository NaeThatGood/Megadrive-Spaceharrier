# Task: Throttle ground & mountain line-scroll to 30 Hz (game stays 60 Hz)

## Background
The game now runs at 60 Hz. Every frame the player moves, both `GROUND_update`
(`src/engine/ground.c`) and `MOUNTAINS_update` (`src/engine/mountains.c`) rebuild a
full per-line horizontal-scroll table and DMA it (BG_B and BG_A). That's the largest
remaining fixed cost after the sky. The lateral sway/perspective is subtle enough to
update at 30 Hz with no visible difference, halving this cost.

CRITICAL: do NOT throttle the whole function. These must stay at 60 Hz:
- the VERTICAL scroll (`VDP_setVerticalScroll`) — it moves the horizon, and sprites are
  pinned to `GROUND_horizon`;
- `GROUND_horizon` assignment in `GROUND_update` (the projection reads it every frame);
- the ground PALETTE forward-rush animation (the `forwardAnim` block in `GROUND_update`).
Only the per-line H-scroll rebuild + `VDP_setHorizontalScrollLine` upload is throttled.

## Part 1 — Add a gate parameter
- `src/engine/ground.h`: `void GROUND_update(s16 swayX, s16 pitchY, s16 vanishX, u16 speed, bool rebuildScroll);`
- `src/engine/mountains.h`: `void MOUNTAINS_update(s16 swayX, s16 horizonY, bool rebuildScroll);`

## Part 2 — ground.c
In `GROUND_update`, keep the existing order: compute `vs`, `VDP_setVerticalScroll(BG_B, vs)`,
`b`, `horizonY`, `GROUND_horizon = horizonY`, then the existing "unchanged" guard. Add:
- `if (!rebuildScroll) goto forwardAnim;` immediately after the existing
  `if (lineScrollValid && ... ) goto forwardAnim;` guard.
This skips the per-line table rebuild + `VDP_setHorizontalScrollLine` on gated frames while
still running the `forwardAnim` palette cycling every frame. The `prev*`/`last*` bookkeeping
is untouched on skipped frames, so the next ungated frame correctly detects any change and
rebuilds (at most 1 frame of lateral latency).
- In `GROUND_init`'s internal call, pass `TRUE` (force initial build):
  `GROUND_update(0, 0, 0, 0, TRUE);`

## Part 3 — mountains.c
In `MOUNTAINS_update`, keep `VDP_setVerticalScroll(BG_A, MTN_STRIP_H - horizonY)` every frame.
Immediately after it (and after the existing unchanged-guard `return`), add:
- `if (!rebuildScroll) return;`
So the vertical scroll still tracks the horizon at 60 Hz, but the H-scroll table rebuild +
upload is gated.

## Part 4 — main.c: drive the gate, offset the two planes
- Add a frame counter advanced once per non-paused update, e.g. `static u16 scrollPhase = 0;`
  incremented at the top of the `if (!paused)` block.
- In the in-loop calls, update the two planes on ALTERNATE frames so only one H-scroll
  upload happens per frame (spreads DMA + CPU):
  - `GROUND_update(playerX - PLAYER_CENTER_X, playerPitchY(), playerWorldX(),
                    GROUND_FORWARD_SPEED, (scrollPhase & 1) == 0);`
  - `MOUNTAINS_update(playerX - PLAYER_CENTER_X, GROUND_horizon + GROUND_VISIBLE_HORIZON_PAD,
                      (scrollPhase & 1) == 1);`
- In the PRE-loop setup calls (before `while (TRUE)`), pass `TRUE` to both so the initial
  tables are fully built.

## Part 5 — Build & verify
1. `make clean && make` — clean build, no new warnings.
2. Ground checker forward-rush and the horizon (altitude) response are still perfectly
   smooth at 60 Hz (these were NOT throttled).
3. Lateral sway/perspective now updates at 30 Hz: pan left/right and confirm it still reads
   as smooth. If the sway looks stepped, fall back to gating only the mountains (pass TRUE to
   GROUND_update, keep the mountain gate) and re-evaluate.
4. HUD: per-frame CPU is lower and flatter during lateral movement; FPS holds 59-60.
5. No tearing or one-frame scroll glitches at the horizon when climbing/descending.

## Notes
- This is purely a cost/latency change, no gameplay change. Worst case is 1 frame of lateral
  scroll latency, which is imperceptible for the gentle sway gain.
