#!/usr/bin/env python3
"""Pre-render scale frames for the stored-frame prototype.

Reads a 64x64 master sprite (res/sprites/enemy_src.png) and emits a single
horizontal sprite-sheet strip (res/sprites/enemy_scaled.png) with one frame
per scale step. Every frame sits on a 64x64 canvas, anchored bottom-centre,
so the engine can position all sizes identically. rescomp trims empty 8x8
tiles per frame, so small frames cost little ROM despite the fixed canvas.

Scale steps must match FRAME_SIZES[] in render_stored.c:
  round(8 + 56 * i / 49)  for i in 0..49  (50 frames, 8..64 px)
"""

import os

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPRITES = os.path.join(ROOT, "res", "sprites")

CANVAS = 64
FRAME_COUNT = 50
SIZES = [round(8 + 56 * i / (FRAME_COUNT - 1)) for i in range(FRAME_COUNT)]


def main():
    src = Image.open(os.path.join(SPRITES, "enemy_src.png"))
    assert src.mode == "P", "master sprite must be indexed"
    assert src.size == (CANVAS, CANVAS)

    strip = Image.new("P", (CANVAS * len(SIZES), CANVAS), 0)
    strip.putpalette(src.getpalette())

    total_px = 0
    for i, size in enumerate(SIZES):
        frame = src.resize((size, size), Image.NEAREST)
        # bottom-centre anchor within the 64x64 cell
        x = i * CANVAS + (CANVAS - size) // 2
        y = CANVAS - size
        strip.paste(frame, (x, y))
        total_px += size * size

    strip.save(os.path.join(SPRITES, "enemy_scaled.png"))

    # Rough ROM cost: 4bpp = half a byte per pixel, rounded up to 8x8 tiles
    tile_bytes = sum(((s + 7) // 8) ** 2 * 32 for s in SIZES)
    print(f"wrote enemy_scaled.png: {len(SIZES)} frames, "
          f"{total_px} px, ~{tile_bytes} bytes of tiles (uncompressed)")


if __name__ == "__main__":
    main()
