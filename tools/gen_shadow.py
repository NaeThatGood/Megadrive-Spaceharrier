#!/usr/bin/env python3
"""Generate the shared scaled ground-shadow strip.

Outputs:
  res/sprites/shadow_scaled.png  16 frames, each on a 32x24 half canvas

The game draws the second half with the VDP horizontal flip bit, so the visible
shadow is a symmetric ellipse at full 64x24 display size while using half the
VRAM per shadow instance.

Frame ellipse widths, used by src/engine/shadow.c:
   8, 11, 15, 18, 22, 25, 29, 32,
  36, 39, 43, 46, 50, 53, 57, 60

Pixels dither between index 0 (transparent) and index 11. Index 11 is reserved
in PAL2 by src/engine/shadow.c so the shadow does not overwrite shot colours.
"""

import os

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPRITES = os.path.join(ROOT, "res", "sprites")

FULL_CANVAS_W = 64
HALF_CANVAS_W = FULL_CANVAS_W // 2
CANVAS_H = 24
FRAME_COUNT = 16
WIDTHS = [
     8, 11, 15, 18, 22, 25, 29, 32,
    36, 39, 43, 46, 50, 53, 57, 60,
]

PALETTE = [
    (0, 0, 0),       # 0 transparent
]
SHADOW_INDEX = 11
SHADOW_COLOR = (16, 24, 40)


def make_palette(colors):
    pal = [0] * 768
    for i, c in enumerate(colors):
        pal[i * 3:i * 3 + 3] = c
    pal[SHADOW_INDEX * 3:SHADOW_INDEX * 3 + 3] = SHADOW_COLOR
    return pal


def main():
    os.makedirs(SPRITES, exist_ok=True)

    full = Image.new("P", (FULL_CANVAS_W * FRAME_COUNT, CANVAS_H), 0)
    full.putpalette(make_palette(PALETTE))
    full.info["transparency"] = 0

    px = full.load()
    cy = CANVAS_H // 2
    cx = FULL_CANVAS_W // 2

    for frame, width in enumerate(WIDTHS):
        height = max(2, round(width / 3))
        rx = width / 2.0
        ry = height / 2.0
        ox = frame * FULL_CANVAS_W
        min_x = cx - ((width + 1) // 2)
        max_x = cx + (width // 2)
        min_y = cy - ((height + 1) // 2)
        max_y = cy + (height // 2)

        for y in range(min_y, max_y + 1):
            for x in range(min_x, max_x + 1):
                nx = (x + 0.5 - cx) / rx
                ny = (y + 0.5 - cy) / ry
                if nx * nx + ny * ny <= 1.0 and ((x + y) & 1) == 0:
                    px[ox + x, y] = SHADOW_INDEX

    half = Image.new("P", (HALF_CANVAS_W * FRAME_COUNT, CANVAS_H), 0)
    half.putpalette(full.getpalette())
    half.info["transparency"] = 0
    for frame in range(FRAME_COUNT):
        half.paste(full.crop((frame * FULL_CANVAS_W, 0,
                              frame * FULL_CANVAS_W + HALF_CANVAS_W, CANVAS_H)),
                   (frame * HALF_CANVAS_W, 0))

    out = os.path.join(SPRITES, "shadow_scaled.png")
    half.save(out)
    print(f"wrote shadow_scaled.png: {FRAME_COUNT} mirrored half frames, "
          f"{HALF_CANVAS_W}x{CANVAS_H}")


if __name__ == "__main__":
    main()
