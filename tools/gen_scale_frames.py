#!/usr/bin/env python3
"""Pre-render scale frames for the stored-frame prototype.

Reads a 64x64 master sprite (res/sprites/enemy_src.png) and emits a single
horizontal sprite-sheet strip (res/sprites/enemy_scaled.png) with one frame
per scale step. Every frame sits on a 64x64 canvas, anchored bottom-centre,
so the engine can position all sizes identically. rescomp trims empty 8x8
tiles per frame, so small frames cost little ROM despite the fixed canvas.

Also emits 25-level tree strips from res/sprites/tree_src.png: a 64x216
near strip and a 64x96 far strip.

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
TREE_CANVAS_W = 64
TREE_CANVAS_H = 216
TREE_FAR_CANVAS_H = 96
TREE_SRC_CROP = (0, 24, 64, 240)
TREE_FRAME_COUNT = 25
TREE_SIZES = [
    round(8 + 56 * i / (TREE_FRAME_COUNT - 1))
    for i in range(TREE_FRAME_COUNT)
]


def write_scaled_strip(src_name, out_name, sizes, canvas_w=CANVAS,
                       canvas_h=CANVAS, src_crop=None):
    src = Image.open(os.path.join(SPRITES, src_name))
    assert src.mode == "P", "master sprite must be indexed"
    if src_crop is not None:
        src = src.crop(src_crop)
    src_w, src_h = src.size
    assert src_w == canvas_w

    strip = Image.new("P", (canvas_w * len(sizes), canvas_h), 0)
    strip.putpalette(src.getpalette())

    total_px = 0
    for i, size in enumerate(sizes):
        h = round(size * src_h / src_w)
        frame = src.resize((size, h), Image.NEAREST)
        # bottom-centre anchor within the fixed-size cell
        x = i * canvas_w + (canvas_w - size) // 2
        y = canvas_h - h
        strip.paste(frame, (x, y))
        total_px += size * h

    strip.save(os.path.join(SPRITES, out_name))

    # Rough VRAM cost: 4bpp tiles, rounded up to the non-transparent tile bbox.
    tile_bytes = 0
    for size in sizes:
        h = min(round(size * src_h / src_w), canvas_h)
        tile_bytes += ((size + 7) // 8) * ((h + 7) // 8) * 32
    print(f"wrote {out_name}: {len(sizes)} frames, "
          f"{total_px} px, ~{tile_bytes} bytes of tiles (uncompressed)")


def main():
    write_scaled_strip("enemy_src.png", "enemy_scaled.png", SIZES)
    write_scaled_strip("tree_src.png", "tree_scaled.png", TREE_SIZES,
                       TREE_CANVAS_W, TREE_CANVAS_H, TREE_SRC_CROP)
    write_scaled_strip("tree_src.png", "tree_scaled_far.png", TREE_SIZES,
                       TREE_CANVAS_W, TREE_FAR_CANVAS_H, TREE_SRC_CROP)


if __name__ == "__main__":
    main()
