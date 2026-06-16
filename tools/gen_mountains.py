#!/usr/bin/env python3
"""Generate a seamless arcade-style mountain horizon strip for BG_A.

Style reference: Space Harrier (arcade, 1985) "Fantasy Zone" horizon - a vivid
green silhouette of rolling mounds against the sky.

Layout (single BG_A plane, wraps every 512 px):
  - Strip is STRIP_W x STRIP_H, indexed PNG. Index 0 = transparent backdrop, so
    the sky HINT gradient shows through ABOVE the mountain crest only.
  - Rolling mountain mass: a vertical DITHERED gradient from darker "distant"
    green at the top into brighter foreground green lower down. No transparent
    holes inside the mass.
  - Bottom: a solid dark band (the tree band) that runs the full width. Tiny
    trees sit along the base of the mountains INSIDE this band, kept short so no
    canopy pokes above the band. The band is its own tile rows so the engine can
    line-scroll it slightly FASTER than the mountains for parallax.

Seamlessness: every horizontal feature is periodic with a period that divides
STRIP_W, so left and right edges line up exactly when the plane wraps.

Output:
  res/sprites/mountains.png   512x96 indexed
"""

import math
import os

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPRITES = os.path.join(ROOT, "res", "sprites")

STRIP_W = 512          # one plane width (64 tiles) -> exact horizontal wrap
STRIP_H = 96           # 12 tile rows; sky shows through the transparent top
GRAD_TOP = 10          # highest the gradient/crest can reach
# BAND_TOP (top edge of the dark band) is computed from the trees so the band
# stops exactly at the top of the tallest tiny tree - see gen_mountains().

# Palette - vivid arcade greens + a dark base band, snapped to the Mega Drive
# 3-bit colour grid (channel values multiples of 8 quantise cleanly enough).
TRANSPARENT = (0, 0, 0)        # 0 - backdrop, never drawn (sky shows through)
GREEN_FAR   = (0, 88, 0)       # 1 - distant/dark green (top of the mass)
GREEN_NEAR  = (0, 168, 0)      # 2 - foreground green (lower in the mass)
CREST       = (40, 224, 40)    # 3 - sunlit crest highlight (top edge)
BAND        = (32, 64, 144)    # 4 - dark band: matches ground.c COL_DARK 0x204090
TREE        = (0, 144, 0)      # 5 - tree foliage (reads against the dark band)
TREE_HI     = (0, 208, 0)      # 6 - tree highlight

PALETTE = [TRANSPARENT, GREEN_FAR, GREEN_NEAR, CREST, BAND, TREE, TREE_HI]
T, FAR, NEAR, HI, BND, TRE, TRH = range(7)

# Ordered 4x4 Bayer matrix for the distant->foreground dither.
BAYER4 = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def periodic_ridge(x, w, terms):
    """Sum of integer-cycle sines -> seamless rolling height profile in [0,1]."""
    v = 0.0
    amp_total = 0.0
    for cycles, amp, phase in terms:
        v += amp * math.sin(2.0 * math.pi * cycles * x / w + phase)
        amp_total += amp
    return 0.5 + 0.5 * (v / amp_total)


def grad_index(x, y, band_top):
    """Dithered green: FAR near the top, NEAR near the band. Bayer threshold."""
    span = max(1, band_top - GRAD_TOP)
    t = (y - GRAD_TOP) / span          # 0 at top -> 1 at band
    if t < 0.0:
        t = 0.0
    if t > 1.0:
        t = 1.0
    return NEAR if (t * 16.0) > BAYER4[y & 3][x & 3] else FAR


def gen_mountains():
    img = Image.new("P", (STRIP_W, STRIP_H), T)
    px = img.load()

    base_y = STRIP_H - 2                  # trunks rest near the bottom (row 94)
    SPACING = 16                          # 512 / 16 = 32 trees -> exact wrap
    n = STRIP_W // SPACING

    # --- Pre-compute trees (positions/sizes UNCHANGED) so the band can be -----
    # narrowed to start exactly at the top of the tallest tiny tree.
    trees = []
    band_top = base_y
    for i in range(n):
        cx = i * SPACING + (SPACING // 2) + (((i * 5) % 3) - 1)
        h = 6 + ((i * 7) % 4)             # 6..9 px tall (unchanged)
        rad = 2 + (i % 2)                 # 2..3 px canopy radius
        cy = base_y - h
        top = cy - rad                    # highest canopy pixel of this tree
        trees.append((cx, cy, rad, i))
        if top < band_top:
            band_top = top                # band starts at the tallest tree top
    BAND_TOP = band_top

    # --- Rolling silhouette: a single bold ridge line. ----------------------
    ridge_terms = [(3, 1.0, 0.0), (4, 0.6, 2.2), (6, 0.4, 0.4),
                   (7, 0.22, 4.1), (2, 0.5, 3.3)]
    ridge_base = 34        # mean crest row (px from top)
    ridge_amp = 20

    for x in range(STRIP_W):
        top = int(round(ridge_base - ridge_amp * (2 * periodic_ridge(
            x, STRIP_W, ridge_terms) - 1)))
        if top < GRAD_TOP:
            top = GRAD_TOP

        # Solid mass from the crest down to the (now lower) dark band.
        for y in range(top, BAND_TOP):
            px[x, y] = grad_index(x, y, BAND_TOP)
        # 2px sunlit crest highlight along the top edge.
        for y in range(top, min(BAND_TOP, top + 2)):
            px[x, y] = HI

    # --- Dark base band (full width, solid) - now stops at the tree tops -----
    for x in range(STRIP_W):
        for y in range(BAND_TOP, STRIP_H):
            px[x, y] = BND

    # --- Tiny trees (drawn in place; not moved up) --------------------------
    for (cx, cy, rad, i) in trees:
        # Trunk
        px[cx, base_y] = BND if cy < base_y - 1 else TRE
        px[cx, base_y - 1] = TRE

        # Rounded canopy (lollipop)
        for dy in range(-rad, rad + 1):
            yy = cy + dy
            if yy < BAND_TOP or yy > base_y - 1:
                continue
            w = rad - (abs(dy) // 2)
            for dx in range(-w, w + 1):
                px[(cx + dx) % STRIP_W, yy] = TRE
        if i % 2 == 0 and cy - rad >= BAND_TOP:
            px[cx % STRIP_W, cy - rad] = TRH

    # Flatten palette and attach.
    flat = []
    for c in PALETTE:
        flat += list(c)
    flat += [0, 0, 0] * (256 - len(PALETTE))
    img.putpalette(flat)

    out = os.path.join(SPRITES, "mountains.png")
    img.save(out)
    print("wrote", out, img.size, "palette colours:", len(PALETTE))

    # Seamless-wrap preview (outputs only, not part of the build): tile x2 and
    # composite over a flat blue so the transparent sky + dark band read true.
    preview = Image.new("RGB", (STRIP_W * 2, STRIP_H), (24, 40, 96))
    rgb = img.convert("RGBA")
    # Treat index 0 as transparent for the preview.
    datas = rgb.getdata()
    newdata = [(r, g, b, 0) if (r, g, b) == TRANSPARENT else (r, g, b, 255)
               for (r, g, b, a) in datas]
    rgb.putdata(newdata)
    preview.paste(rgb, (0, 0), rgb)
    preview.paste(rgb, (STRIP_W, 0), rgb)
    prev_dir = os.environ.get("PREVIEW_DIR", SPRITES)
    prev_path = os.path.join(prev_dir, "mountains_wrap_preview.png")
    preview.resize((STRIP_W * 2 * 2, STRIP_H * 2), Image.NEAREST).save(prev_path)
    print("wrote", prev_path)


if __name__ == "__main__":
    gen_mountains()
