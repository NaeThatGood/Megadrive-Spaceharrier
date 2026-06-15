#!/usr/bin/env python3
"""Generate a seamless arcade-style mountain horizon strip for BG_A.

Style reference: Space Harrier (arcade, 1985) "Fantasy Zone" horizon - a vivid
green silhouette of rolling mounds against the sky, with small tree/arch shapes
along the crest and a denser tiny-tree line low down.

Layout (single BG_A plane, wraps every 512 px):
  - Strip is STRIP_W x STRIP_H, indexed PNG (index 0 = transparent backdrop,
    so the sky HINT gradient shows through above/behind the silhouette).
  - Top ~2/3  : rolling mountains (back ridge + front ridge + crest highlight).
  - Bottom 1/3: band of tiny trees, kept on its own tile rows so the engine can
    line-scroll it slightly FASTER than the mountains for a parallax feel.

Seamlessness: every horizontal feature is periodic with a period that divides
STRIP_W, so the left and right edges line up exactly when the plane wraps.

Output:
  res/sprites/mountains.png   512x96 indexed (PAL: greens on the MD colour grid)
"""

import math
import os

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPRITES = os.path.join(ROOT, "res", "sprites")

STRIP_W = 512          # one plane width (64 tiles) -> exact horizontal wrap
STRIP_H = 96           # 12 tile rows; sky shows through the transparent top
TREE_BAND_H = 32       # bottom third (4 tile rows): the faster-scrolling layer
TREE_TOP = STRIP_H - TREE_BAND_H

# Palette - vivid arcade greens snapped to the Mega Drive 3-bit colour grid
# (channel values are multiples of 32 so rescomp quantises cleanly).
TRANSPARENT = (0, 0, 0)        # index 0 - backdrop, never drawn
MTN_BACK    = (0, 96, 0)       # index 1 - distant ridge (darkest body)
MTN_BODY    = (0, 160, 0)      # index 2 - main mountain body
MTN_CREST   = (32, 224, 32)    # index 3 - sunlit crest highlight
TREE_DARK   = (0, 64, 0)       # index 4 - tree shadow / trunks
TREE_BODY   = (0, 192, 0)      # index 5 - tree foliage

PALETTE = [TRANSPARENT, MTN_BACK, MTN_BODY, MTN_CREST, TREE_DARK, TREE_BODY]
IDX = {name: i for i, name in enumerate(
    ["t", "back", "body", "crest", "tdark", "tbody"])}


def periodic_ridge(x, w, terms):
    """Sum of integer-cycle sines -> seamless rolling height profile in [0,1]."""
    v = 0.0
    amp_total = 0.0
    for cycles, amp, phase in terms:
        v += amp * math.sin(2.0 * math.pi * cycles * x / w + phase)
        amp_total += amp
    return 0.5 + 0.5 * (v / amp_total)


def gen_mountains():
    img = Image.new("P", (STRIP_W, STRIP_H), IDX["t"])
    px = img.load()

    # --- Back ridge: low, gentle, darkest. Sits highest on screen. ----------
    back_terms = [(3, 1.0, 0.0), (5, 0.45, 1.7), (7, 0.25, 4.1)]
    back_base = 26     # mean height (px from top) of back ridge crest
    back_amp = 14

    # --- Front ridge: taller, bolder mounds, drawn over the back ridge. -----
    front_terms = [(4, 1.0, 2.2), (6, 0.5, 0.4), (9, 0.22, 5.0), (2, 0.6, 3.3)]
    front_base = 40
    front_amp = 20

    for x in range(STRIP_W):
        back_h = int(round(back_base - back_amp * (2 * periodic_ridge(
            x, STRIP_W, back_terms) - 1)))
        front_h = int(round(front_base - front_amp * (2 * periodic_ridge(
            x, STRIP_W, front_terms) - 1)))

        # Back ridge fills from its crest down to the tree band.
        for y in range(max(0, back_h), TREE_TOP):
            px[x, y] = IDX["back"]

        # Front ridge body over the top, with a 2px sunlit crest.
        for y in range(max(0, front_h), TREE_TOP):
            px[x, y] = IDX["body"]
        for y in range(max(0, front_h), min(TREE_TOP, front_h + 2)):
            px[x, y] = IDX["crest"]

    # --- Tree band: tiny trees on their own rows (faster parallax layer) -----
    # Each tree is a small rounded canopy on a 2px trunk, with gaps between
    # them so they read as distinct trees rather than a continuous fence.
    base_y = STRIP_H - 2               # trunks sit on this row
    # Thin ground line ties the forest together without becoming a solid bar.
    for x in range(0, STRIP_W, 2):
        px[x, base_y + 1] = IDX["tdark"]

    SPACING = 16                       # 512 / 16 = 32 trees -> exact wrap
    n = STRIP_W // SPACING
    for i in range(n):
        # Slight periodic jitter in x; stays within the cell so wrap is exact.
        cx = i * SPACING + (SPACING // 2) + (((i * 5) % 3) - 1)
        h = 8 + ((i * 7) % 5)                       # 8..12 px canopy height
        rad = 2 + (i % 3)                           # 2..4 px canopy radius
        cy = base_y - h                             # canopy centre row

        # Trunk (2 px tall, 1 px wide).
        px[cx, base_y] = IDX["tdark"]
        px[cx, base_y - 1] = IDX["tdark"]

        # Rounded canopy: stacked rows narrowing toward the top (lollipop).
        for dy in range(-rad, rad + 1):
            yy = cy + dy
            if yy < TREE_TOP or yy > base_y - 1:
                continue
            # width tapers with vertical distance from centre
            w = rad - (abs(dy) // 2)
            for dx in range(-w, w + 1):
                xx = (cx + dx) % STRIP_W           # wrap-safe horizontally
                px[xx, yy] = IDX["tbody"]
        # Dark underside + a crest pixel on alternating trees for depth.
        for dx in range(-rad + 1, rad):
            px[(cx + dx) % STRIP_W, cy + rad] = IDX["tdark"]
        if i % 2 == 0:
            px[cx % STRIP_W, cy - rad] = IDX["crest"]

    # Flatten palette to 256*3 and attach.
    flat = []
    for c in PALETTE:
        flat += list(c)
    flat += [0, 0, 0] * (256 - len(PALETTE))
    img.putpalette(flat)

    out = os.path.join(SPRITES, "mountains.png")
    img.save(out)
    print("wrote", out, img.size, "palette colours:", len(PALETTE))

    # Seamless-wrap preview: tile the strip x2 horizontally so the join at
    # x=512 can be eyeballed. Saved to outputs only (not part of the build).
    preview = Image.new("P", (STRIP_W * 2, STRIP_H), IDX["t"])
    preview.putpalette(flat)
    preview.paste(img, (0, 0))
    preview.paste(img, (STRIP_W, 0))
    prev_path = os.path.join(os.environ.get("PREVIEW_DIR", SPRITES),
                             "mountains_wrap_preview.png")
    # Upscale 3x nearest for easier visual inspection.
    preview.convert("RGB").resize(
        (STRIP_W * 2 * 2, STRIP_H * 2), Image.NEAREST).save(prev_path)
    print("wrote", prev_path)


if __name__ == "__main__":
    gen_mountains()
