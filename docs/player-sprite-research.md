# Space Harrier player sprite research

Research date: 2026-06-12. Goal: locate the original player ("Harrier") sprite for
reference or use in this Mega Drive prototype.

## Summary

| Source | Player sheet available? | Notes |
|--------|-------------------------|-------|
| **Arcade (1985, Hang-On hardware)** | **No public rip** | Harrier not yet on [TSR arcade page](https://www.spriters-resource.com/arcade/spaceharrier/); only Enemies, Bosses, Logo |
| Sharp X68000 port | Yes | Closest home port to arcade art; 384×168 sheet |
| Master System port | Yes | Full animation set; 8-bit simplified colours |
| **Space Harrier II (Genesis)** | Yes | Best match for this MD project; 376×392 sheet |
| NES / CPC / X1 | Yes | Lower fidelity ports |

Reference PNGs downloaded to `res/sprites/reference/`:

- `harrier_x68000.png` — [TSR #21413](https://www.spriters-resource.com/sharp_x68000/spaceharrier/asset/21413/)
- `harrier_ms.png` — [TSR #21412](https://www.spriters-resource.com/master_system/sharrierms/asset/21412/)
- `harrier_sh2_genesis.png` — [TSR #35153](https://www.spriters-resource.com/sega_genesis/spacehar2/asset/35153/)

## Arcade original — why it is hard

Space Harrier runs on **Sega Hang-On / Space Harrier hardware** (Super Scaler).
Sprite pixels live in dedicated sprite ROMs; **palettes live in the 68000 program
ROM**, not beside the pixel data. MAME's tile viewer does not decode these sprites
directly — rippers use [djyt/system16_sprite_viewer](https://github.com/djyt/system16_sprite_viewer).

TSR comment (Dolphman, 2022): *"I really should get to ripping the Harrier himself"* —
the arcade player sheet simply has not been published yet.

### Arcade sprite ROM layout (MAME `sharrier` / `sharrier1`)

32× 32 KB EPROMs, interleaved. Example filenames from MAME:

```
epr-7230.ic36 … epr-7199.ic1   (sprite ROMs)
epr-6844.ic123                 (zoom table, separate)
```

The viewer config is in the tool repo: `res/config/sharrier.xml` (format **2** =
Space Harrier-specific). Palette file `res/config/palettes/sharrier.pal` ships with
the viewer (dumped from program ROM offset `$c368`, length `$1fc0`).

### Ripping workflow (requires your own legally obtained ROM set)

1. Install/build **system16_sprite_viewer**:
   ```bash
   git clone https://github.com/djyt/system16_sprite_viewer.git
   cd system16_sprite_viewer && mkdir build && cd build
   cmake .. && make
   ```

2. Copy MAME ROM files into `roms/sharrier/` using the names in `sharrier.xml`.

3. Copy `res/config/palettes/sharrier.pal` to `roms/sharrier/sharrier.pal`.

4. Run:
   ```bash
   ./s16_viewer ../res/config/sharrier.xml
   ```

5. Scroll through sprite ROM — Harrier frames are **rear ¾ view, red jacket, blue
   trousers, blond hair, cannon under right arm**. Multiple sizes exist because the
   hardware scales sprites in flight; pick the frame closest to on-screen size at
   the playfield (~32–48 px tall on a 224-line display).

6. Export/screenshot the desired frame, index to 4bpp, and map to a Mega Drive palette
   (max 15 colours + transparent).

**This machine had no MAME install or ROM directory at research time**, so an arcade
ROM rip was not performed here.

## Character reference (all versions)

Consistent design across ports:

- Blond male pilot, red flight suit/jacket, blue pants, boots
- Large under-arm plasma cannon (grey/silver)
- Rear-view running/flying animation (bank left/right, tumble on hit)
- Space Harrier II adds front-facing victory poses and a "Dark Harrier" palette swap

## Fit for this prototype

Current build uses `res/sprites/player.png` at **32×48** (4×6 tiles, PAL1), generated
by `tools/gen_assets.py` as an original placeholder.

| Reference | Typical frame size | Fit |
|-----------|-------------------|-----|
| SH2 Genesis | ~32×40 px per cell | Good — same platform lineage as SH2 MD |
| X68000 | ~24×32 px | Good arcade-adjacent proportions |
| Master System | ~16×24 px | Needs upscale; 8-bit palette |
| Arcade (ROM) | Variable (hardware scaled) | Authentic but needs manual rip + downscale |

Recommended path for this project: extract a **rear-view neutral frame** from
`harrier_sh2_genesis.png` or `harrier_x68000.png`, remap colours to PAL1, and replace
`player.png`. For authentic 1985 arcade pixels, rip via s16_viewer once ROMs are available.

## Legal note

Sprite sheets from TSR / Sprite Database are fan rips of Sega copyrighted material.
Use for private reference and technical prototyping only. ROM ripping requires ROMs
you are entitled to possess (e.g. own the board, use MAME with licensed content).
