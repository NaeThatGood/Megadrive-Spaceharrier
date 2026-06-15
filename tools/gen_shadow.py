#!/usr/bin/env python3
"""Generate the shared scaled ground-shadow strip.

Outputs:
  res/sprites/shadow_scaled.png  16 frames, each on a 64x24 canvas

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

CANVAS_W = 64
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

    strip = Image.new("P", (CANVAS_W * FRAME_COUNT, CANVAS_H), 0)
    strip.putpalette(make_palette(PALETTE))
    strip.info["transparency"] = 0

    px = strip.load()
    cy = CANVAS_H // 2
    cx = CANVAS_W // 2

    for frame, width in enumerate(WIDTHS):
        height = max(2, round(width / 3))
        rx = width / 2.0
        ry = height / 2.0
        ox = frame * CANVAS_W
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

    out = os.path.join(SPRITES, "shadow_scaled.png")
    strip.save(out)
    print(f"wrote shadow_scaled.png: {FRAME_COUNT} frames, {CANVAS_W}x{CANVAS_H}")


if __name__ == "__main__":
    main()
