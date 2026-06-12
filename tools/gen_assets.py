#!/usr/bin/env python3
"""Generate original placeholder art for the Space Harrier-style prototype.

All art is produced procedurally (no copyrighted material). Outputs indexed
PNGs compatible with SGDK's rescomp (16 colours max per image, colour 0 =
transparent for sprites / backdrop for images).

Outputs:
  res/sprites/ground.png   320x224 sky + perspective checkerboard (PAL0)
  res/sprites/player.png   32x48 player character sprite sheet, 1 frame (PAL1)
"""

import math
import os

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPRITES = os.path.join(ROOT, "res", "sprites")

SCREEN_W, SCREEN_H = 320, 224
HORIZON = 96  # first ground scanline


def make_palette(colors):
    """Build a 768-entry PIL palette from a list of (r, g, b)."""
    pal = []
    for c in colors:
        pal.extend(c)
    pal.extend((0, 0, 0) * (256 - len(colors)))
    return pal


def gen_ground():
    # Palette layout (PAL0):
    #  0 backdrop (black)         1..4 sky gradient blues
    #  5 checker light            6 checker dark
    #  7 horizon haze
    colors = [
        (0, 0, 0),
        (32, 64, 160),    # sky deep
        (48, 96, 192),
        (80, 128, 224),
        (128, 176, 240),  # sky near horizon
        (96, 192, 64),    # checker light green
        (32, 112, 32),    # checker dark green
        (176, 224, 240),  # horizon haze
    ]
    img = Image.new("P", (SCREEN_W, SCREEN_H), 0)
    img.putpalette(make_palette(colors))
    px = img.load()

    # Sky: 4 horizontal bands.
    band_h = HORIZON // 4
    for y in range(HORIZON):
        idx = 1 + min(3, y // band_h)
        for x in range(SCREEN_W):
            px[x, y] = idx

    # Thin haze line right at the horizon.
    for x in range(SCREEN_W):
        px[x, HORIZON] = 7
        px[x, HORIZON + 1] = 7

    # Ground: true perspective projection of an infinite checkerboard.
    #   z(y) = F / (y - HORIZON), worldX = (x - cx) * z / P
    F = 9000.0          # depth scale
    P = 160.0           # projection scale
    CELL = 96.0         # checker cell size in world units
    cx = SCREEN_W / 2.0
    for y in range(HORIZON + 2, SCREEN_H):
        z = F / (y - HORIZON)
        for x in range(SCREEN_W):
            wx = (x - cx) * z / P
            parity = (math.floor(wx / CELL) + math.floor(z / CELL)) & 1
            px[x, y] = 5 if parity else 6

    img.save(os.path.join(SPRITES, "ground.png"))
    print("wrote ground.png")


def gen_player():
    # Palette layout (PAL1): 0 transparent, then character colours.
    colors = [
        (255, 0, 255),    # 0 transparent (magenta key, index 0 unused anyway)
        (236, 188, 156),  # 1 skin
        (60, 60, 68),     # 2 dark outline
        (200, 40, 40),    # 3 jacket red
        (140, 24, 24),    # 4 jacket shadow
        (48, 72, 160),    # 5 trousers blue
        (32, 48, 112),    # 6 trousers shadow
        (200, 200, 208),  # 7 gun light grey
        (120, 120, 132),  # 8 gun dark grey
        (244, 224, 96),   # 9 hair blond
        (255, 255, 255),  # 10 highlight
    ]
    W, H = 32, 48
    img = Image.new("P", (W, H), 0)
    img.putpalette(make_palette(colors))
    d = ImageDraw.Draw(img)

    # Simple original "flying man with cannon" placeholder, rear 3/4 view.
    # Head
    d.ellipse([12, 2, 20, 10], fill=1, outline=2)
    d.rectangle([12, 2, 20, 5], fill=9)           # hair
    # Torso (jacket)
    d.polygon([(10, 10), (22, 10), (24, 26), (8, 26)], fill=3, outline=2)
    d.line([(16, 10), (16, 26)], fill=4)
    # Arms: left arm down, right arm holding cannon forward
    d.polygon([(8, 11), (4, 22), (7, 24), (11, 14)], fill=3, outline=2)
    d.polygon([(22, 11), (28, 16), (26, 20), (20, 15)], fill=4, outline=2)
    # Cannon under right arm
    d.rectangle([24, 14, 30, 30], fill=7, outline=8)
    d.rectangle([26, 12, 28, 14], fill=8)
    # Legs (flying pose, trailing)
    d.polygon([(10, 26), (15, 26), (13, 44), (8, 42)], fill=5, outline=2)
    d.polygon([(17, 26), (22, 26), (23, 45), (18, 46)], fill=6, outline=2)
    # Boots
    d.rectangle([8, 42, 14, 46], fill=2)
    d.rectangle([18, 44, 24, 47], fill=2)
    # Visor highlight
    d.point((18, 6), fill=10)

    img.save(os.path.join(SPRITES, "player.png"))
    print("wrote player.png")


if __name__ == "__main__":
    os.makedirs(SPRITES, exist_ok=True)
    gen_ground()
    gen_player()
