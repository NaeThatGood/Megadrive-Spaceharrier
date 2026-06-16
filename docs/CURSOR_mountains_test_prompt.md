# Cursor prompt — implement & test scrolling mountains

Paste the block below into Cursor.

---

Implement scrolling horizon mountains for this SGDK Mega Drive project, then build and test on the emulator.

Follow the full spec in `docs/CURSOR_mountains_task.md` exactly. Key points:

- The artwork already exists: `res/sprites/mountains.png` (512×96, 7 colours, index 0 = transparent). Do NOT regenerate it. The strip is rolling green mountains (dithered distant→foreground green) with a thin dark blue-green base band of tiny trees at strip rows 82–95.
- Put it on the unused `BG_A` plane. `BG_B` is the ground, the sky is the per-line HINT backdrop, the HUD is the WINDOW plane — don't disturb those. Do NOT change `VDP_setScrollingMode` or `VDP_setPlaneSize`.
- Add to `res/resources.res`:  `IMAGE img_mountains "sprites/mountains.png" BEST`
- Create `src/engine/mountains.c` / `.h` exposing:
  - `void MOUNTAINS_init(void);`  — clear BG_A, load the tileset (use a VRAM index allocated after the ground tiles, no collision), set the tilemap at plane rows 0–11, load its palette into a chosen CRAM line (PAL3 or PAL0 spare slots), make the tiles reference that line.
  - `void MOUNTAINS_update(s16 swayX, s16 horizonY);`
- Vertical placement (so the tree band sits on the horizon): `s16 vsA = 96 - horizonY; VDP_setVerticalScroll(BG_A, vsA);` each frame.
- Horizontal parallax (`HSCROLL_LINE`): mountains `swayX >> 3`, tree band `swayX >> 2`. The tree band starts at strip row 82, which lands at screen row `horizonY - 14`. Build the BG_A line-scroll table the same way `ground.c` does for BG_B:
  - rows `(horizonY-96) … (horizonY-15)` → mountain scroll
  - rows `(horizonY-14) … (horizonY-1)`  → tree scroll
- Wire it in `main.c`: call `MOUNTAINS_init()` after `GROUND_init()` / `SPR_initEx(...)`; each frame, after `GROUND_update(...)`, call `MOUNTAINS_update(playerX - PLAYER_CENTER_X, GROUND_horizon + GROUND_VISIBLE_HORIZON_PAD);`

Then test:
1. `make` — must compile with no new warnings.
2. `make run` — launches BlastEm/ares (NTSC).
3. Verify the acceptance criteria in the spec: dithered green mountains with the tiny-tree band sit on the horizon; moving left/right scrolls them; the tree band moves visibly faster than the peaks; scrolling wraps with no visible seam; the range tracks the horizon on pitch; ground/sky/HUD/sprites/palette are all uncorrupted.

If the strip sits or scrolls the wrong way, flip the sign of `vsA` (vertical) or of the scroll shifts (horizontal) and rebuild. Report exactly what you changed and confirm the build and run succeeded.
