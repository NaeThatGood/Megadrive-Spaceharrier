#!/usr/bin/env python3
"""Generate placeholder / ripped art for the Space Harrier-style prototype.

Ground plane checkerboard is procedural. Outputs indexed PNGs for SGDK rescomp.

Outputs:
  res/sprites/ground.png   384x352 perspective checkerboard plane (PAL0)
  res/sprites/player.png   32x48 player character sprite sheet, 1 frame (PAL1)
"""

import math
import os
import sys

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPRITES = os.path.join(ROOT, "res", "sprites")
sys.path.insert(0, os.path.join(ROOT, "tools"))
from rip_player_x68 import rip as rip_player

SCREEN_W, SCREEN_H = 320, 240  # PAL H40/V30


def make_palette(colors):
    """Build a 768-entry PIL palette from a list of (r, g, b)."""
    pal = []
    for c in colors:
        pal.extend(c)
    pal.extend((0, 0, 0) * (256 - len(colors)))
    return pal


def gen_ground():
    """384x352 BG_B board with vertical pitch headroom, SH2 / Burning Force style.

    - Width gives a 32 px side margin around the centered 320 px viewport.
      Height is 224 + 2*PITCH_RANGE so V-scroll can move the horizon without
      wrapping; neutral pitch shows
      rows [PAD_TOP .. PAD_TOP+224).
    - Per-line H-scroll skews the checker (perspective table); forward motion
      is pure palette animation on the static plane.
    - Lateral checker uses world-space parity (floor((x-cx)*z/(P*CELL)) +
      floor(z/CELL)) so diagonals converge cleanly; rows where w(y) < 4 px are
      filled with horizon haze instead of 1-px moire.

    Palette layout (PAL0), must match src/engine/ground.c:
      0      transparent/backdrop
      1..5   spare colours
      6      spare horizon colour
      7..14  checker phase entries (SH2 blues; rotated at runtime)
      15     white (HUD text)
    """
    HORIZON_ONSCREEN = 96
    PITCH_RANGE = 64
    PAD_TOP = PITCH_RANGE
    PAD_BOTTOM = PITCH_RANGE
    IMG_W = 384
    IMG_H = 224 + PAD_TOP + PAD_BOTTOM      # 352
    HORIZON = HORIZON_ONSCREEN + PAD_TOP    # 160 inside the tall image
    PHASES = 4          # forward-motion phase bands per checker cell
    CHECKER_BASE = 7    # first checker palette index

    # SH2-style palette: two-tone blue checker. Indices 7..14 are distinct for
    # rescomp but ground.c overwrites them at runtime with the light/dark pair.
    checker_shades = [
        (88, 152, 216),   # 7  light blue (phase 0)
        (96, 160, 224),   # 8  light
        (80, 144, 208),   # 9  light
        (104, 168, 232),  # 10 light
        (32, 64, 144),    # 11 dark blue (phase 0)
        (40, 72, 152),    # 12 dark
        (24, 56, 136),    # 13 dark
        (48, 80, 160),    # 14 dark
    ]
    colors = [
        (0, 0, 0),        # 0 transparent/backdrop
        (0, 0, 0),        # 1 spare
        (0, 0, 0),        # 2 spare
        (0, 0, 0),        # 3 spare
        (0, 0, 0),        # 4 spare
        (248, 236, 176),  # 5 spare horizon colour
        (248, 236, 176),  # 6 spare horizon colour
    ] + checker_shades + [
        (255, 255, 255),  # HUD text white
    ]
    img = Image.new("P", (IMG_W, IMG_H), 0)
    img.putpalette(make_palette(colors))
    px = img.load()

    # Rows above the horizon are intentionally left as index 0 so the ground
    # image has no baked sky while preserving the same pitch-scroll geometry.

    # Ground: perspective checkerboard with phase-band colour indices.
    #
    # Depth and projection must match src/engine/world.h (Z_NEAR = 256) so the
    # floor grid and sprite projection share one vanishing point.  F is chosen
    # so z = Z_NEAR at the player contact line (GROUND_HORIZON + GROUND_DEPTH
    # on screen, with neutral pitch and V-scroll = PAD_TOP).
    #
    # Checker parity is evaluated in world space — do NOT quantise a per-line
    # cell width to integer pixels.  That was the main horizon warp: near the
    # vanishing point w(y) falls to 1 px, so (x-cx)//w moire-shimmers and the
    # diagonals no longer meet cleanly. Rows where that would happen stay
    # transparent/backdrop instead of drawing unstable checker cells.
    Z_NEAR = 256.0
    GROUND_DEPTH = 110.0
    GROUND_HORIZON_SCREEN = float(HORIZON_ONSCREEN)
    VSCROLL_NEUTRAL = float(PAD_TOP)
    contact_plane_y = GROUND_HORIZON_SCREEN + GROUND_DEPTH + VSCROLL_NEUTRAL
    F = Z_NEAR * (contact_plane_y - HORIZON)
    P = Z_NEAR
    CELL = 32.0         # world-space checker period (matches SH2-ish density)
    MIN_CELL_W = 4      # below this, rows are haze-only (SH2 horizon mask)
    cx = IMG_W // 2
    for y in range(HORIZON + 2, IMG_H):
        d = float(y - HORIZON)
        z = F / d
        w = CELL * P / z
        if w < MIN_CELL_W:
            continue

        sub = int(math.floor(z * PHASES / CELL)) % PHASES
        for x in range(IMG_W):
            cell_x = int(math.floor((x - cx) * z / (P * CELL)))
            cell_z = int(math.floor(z / CELL))
            rel = ((cell_x + cell_z) % 2 * PHASES + sub) % (2 * PHASES)
            px[x, y] = CHECKER_BASE + rel

    img.save(os.path.join(SPRITES, "ground.png"))
    print("wrote ground.png")


def gen_player():
    """64x96 player sprite — must match resources.res (8x12 tiles) and main.c."""
    try:
        rip_player()
        return
    except (FileNotFoundError, RuntimeError) as exc:
        print(f"warn: X68000 player rip unavailable ({exc}), using procedural fallback")

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
    W, H = 64, 96
    img = Image.new("P", (W, H), 0)
    img.putpalette(make_palette(colors))
    small = Image.new("P", (32, 48), 0)
    small.putpalette(make_palette(colors))
    d = ImageDraw.Draw(small)

    # Simple original "flying man with cannon" placeholder, rear 3/4 view.
    d.ellipse([12, 2, 20, 10], fill=1, outline=2)
    d.rectangle([12, 2, 20, 5], fill=9)
    d.polygon([(10, 10), (22, 10), (24, 26), (8, 26)], fill=3, outline=2)
    d.line([(16, 10), (16, 26)], fill=4)
    d.polygon([(8, 11), (4, 22), (7, 24), (11, 14)], fill=3, outline=2)
    d.polygon([(22, 11), (28, 16), (26, 20), (20, 15)], fill=4, outline=2)
    d.rectangle([24, 14, 30, 30], fill=7, outline=8)
    d.rectangle([26, 12, 28, 14], fill=8)
    d.polygon([(10, 26), (15, 26), (13, 44), (8, 42)], fill=5, outline=2)
    d.polygon([(17, 26), (22, 26), (23, 45), (18, 46)], fill=6, outline=2)
    d.rectangle([8, 42, 14, 46], fill=2)
    d.rectangle([18, 44, 24, 47], fill=2)
    d.point((18, 6), fill=10)

    img.paste(small.resize((W, H), Image.NEAREST), (0, 0))
    img.save(os.path.join(SPRITES, "player.png"))
    print(f"wrote player.png ({W}x{H})")


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
