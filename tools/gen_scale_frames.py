#!/usr/bin/env python3
"""Pre-render scale frames for the stored-frame prototype.

Reads a 64x64 master sprite (res/sprites/enemy_src.png) and emits tiered
horizontal sprite-sheet strips with one frame per scale step. Distant frames
use 16x16 or 32x32 canvases, while near frames keep the 64x64 canvas. All
frames are anchored bottom-centre so the engine can position each tier with
the same ground contact point.

Also emits 25-level tree half-strips from res/sprites/tree_src.png: 32x216
near frames and 32x96 far frames. The game draws the second half with the
VDP horizontal flip bit, so the visible tree is vertically symmetrical.

Scale steps must match FRAME_SIZES[] in render_stored.c:
  round(8 + 56 * i / 49)  for i in 0..49  (50 frames, 8..64 px)
"""

import os

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPRITES = os.path.join(ROOT, "res", "sprites")

FRAME_COUNT = 50
SIZES = [round(8 + 56 * i / (FRAME_COUNT - 1)) for i in range(FRAME_COUNT)]
ENEMY_TIERS = [
    ("enemy_scaled_16.png", 16, [s for s in SIZES if s <= 16]),
    ("enemy_scaled_32.png", 32, [s for s in SIZES if 16 < s <= 32]),
    ("enemy_scaled_64.png", 64, [s for s in SIZES if s > 32]),
]
TREE_CANVAS_W = 64
TREE_HALF_CANVAS_W = TREE_CANVAS_W // 2
TREE_CANVAS_H = 216
TREE_FAR_CANVAS_H = 96
TREE_SRC_CROP = (0, 24, 64, 240)
TREE_FRAME_COUNT = 25
TREE_SIZES = [
    round(8 + 56 * i / (TREE_FRAME_COUNT - 1))
    for i in range(TREE_FRAME_COUNT)
]


def write_scaled_strip(src_name, out_name, sizes, canvas_w=64,
                       canvas_h=64, src_crop=None):
    src = Image.open(os.path.join(SPRITES, src_name))
    assert src.mode == "P", "master sprite must be indexed"
    if src_crop is not None:
        src = src.crop(src_crop)
    src_w, src_h = src.size
    assert all(size <= canvas_w for size in sizes)

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


def write_scaled_tree_half_strip(src_name, out_name, sizes, canvas_h):
    src = Image.open(os.path.join(SPRITES, src_name))
    assert src.mode == "P", "master sprite must be indexed"
    src = src.crop(TREE_SRC_CROP)
    src_w, src_h = src.size
    half_src = src.crop((0, 0, src_w // 2, src_h))

    strip = Image.new("P", (TREE_HALF_CANVAS_W * len(sizes), canvas_h), 0)
    strip.putpalette(src.getpalette())

    total_px = 0
    tile_bytes = 0
    for i, size in enumerate(sizes):
        h = min(round(size * src_h / src_w), canvas_h)
        half_w = (size + 1) // 2
        frame = half_src.resize((half_w, h), Image.NEAREST)
        # Anchor to the inner edge; the paired sprite mirrors from this edge.
        x = i * TREE_HALF_CANVAS_W + (TREE_HALF_CANVAS_W - half_w)
        y = canvas_h - h
        strip.paste(frame, (x, y))
        total_px += half_w * h
        tile_bytes += ((half_w + 7) // 8) * ((h + 7) // 8) * 32

    strip.save(os.path.join(SPRITES, out_name))
    print(f"wrote {out_name}: {len(sizes)} mirrored half frames, "
          f"{total_px} px, ~{tile_bytes} bytes of tiles (uncompressed)")


def main():
    for out_name, canvas, sizes in ENEMY_TIERS:
        write_scaled_strip("enemy_src.png", out_name, sizes, canvas, canvas)
    write_scaled_tree_half_strip("tree_src.png", "tree_scaled.png",
                                 TREE_SIZES, TREE_CANVAS_H)
    write_scaled_tree_half_strip("tree_src.png", "tree_scaled_far.png",
                                 TREE_SIZES, TREE_FAR_CANVAS_H)


if __name__ == "__main__":
    main()
