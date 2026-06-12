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


# Shared palette for enemy + shot sprites (PAL2 at runtime).
ENEMY_PALETTE = [
    (255, 0, 255),    # 0 transparent
    (24, 16, 24),     # 1 outline
    (208, 56, 48),    # 2 body red
    (144, 32, 32),    # 3 body shadow
    (188, 196, 208),  # 4 dome light
    (120, 132, 148),  # 5 dome dark
    (240, 240, 240),  # 6 eye white
    (32, 32, 80),     # 7 pupil
    (248, 216, 64),   # 8 shot yellow
    (240, 144, 48),   # 9 shot orange
    (255, 255, 255),  # 10 highlight
]


def gen_enemy():
    """64x64 master sprite: original one-eyed hover-drone placeholder."""
    W = H = 64
    img = Image.new("P", (W, H), 0)
    img.putpalette(make_palette(ENEMY_PALETTE))
    d = ImageDraw.Draw(img)

    # Body: red saucer
    d.ellipse([4, 26, 60, 50], fill=2, outline=1)
    d.ellipse([10, 30, 54, 44], fill=3)
    # Dome on top
    d.ellipse([16, 8, 48, 40], fill=4, outline=1)
    d.ellipse([20, 12, 44, 30], fill=5)
    # Big single eye
    d.ellipse([24, 16, 40, 32], fill=6, outline=1)
    d.ellipse([29, 21, 35, 27], fill=7)
    d.point((30, 22), fill=10)
    # Underside thruster nubs
    for cx in (14, 32, 50):
        d.ellipse([cx - 4, 48, cx + 4, 56], fill=5, outline=1)
        d.ellipse([cx - 2, 52, cx + 2, 58], fill=9)

    img.save(os.path.join(SPRITES, "enemy_src.png"))

    # Interim 16x16 version (used before stored scale frames exist)
    small = img.resize((16, 16), Image.NEAREST)
    small.save(os.path.join(SPRITES, "enemy16.png"))
    print("wrote enemy_src.png, enemy16.png")


def gen_runtime_assets():
    """Assets for the runtime-scaling renderer.

    enemy_src.bin: the 64x64 master sprite as packed 4bpp (2 px/byte,
    high nibble = left pixel), row-major. Read directly by the 68000
    software scaler.

    quad32.png: fully opaque 32x32 dummy sprite. Compiled with
    OPTIMIZATION NONE it yields exactly one 32x32 hardware sprite whose
    16 tiles we overwrite at runtime with scaled data (its ROM tiles are
    never uploaded).
    """
    src = Image.open(os.path.join(SPRITES, "enemy_src.png"))
    w, h = src.size
    data = bytearray()
    pix = src.load()
    for y in range(h):
        for x in range(0, w, 2):
            data.append(((pix[x, y] & 0xF) << 4) | (pix[x + 1, y] & 0xF))
    with open(os.path.join(SPRITES, "enemy_src.bin"), "wb") as f:
        f.write(data)

    quad = Image.new("P", (32, 32), 1)
    quad.putpalette(make_palette(ENEMY_PALETTE))
    quad.save(os.path.join(SPRITES, "quad32.png"))
    print(f"wrote enemy_src.bin ({len(data)} bytes), quad32.png")


def gen_shot():
    img = Image.new("P", (8, 8), 0)
    img.putpalette(make_palette(ENEMY_PALETTE))
    d = ImageDraw.Draw(img)
    d.ellipse([0, 0, 7, 7], fill=9)
    d.ellipse([1, 1, 6, 6], fill=8)
    d.ellipse([2, 2, 4, 4], fill=10)
    img.save(os.path.join(SPRITES, "shot.png"))
    print("wrote shot.png")


if __name__ == "__main__":
    os.makedirs(SPRITES, exist_ok=True)
    gen_ground()
    gen_player()
    gen_enemy()
    gen_runtime_assets()
    gen_shot()
