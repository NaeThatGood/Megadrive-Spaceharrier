#!/usr/bin/env python3
"""Extract a rear-view Harrier frame from the X68000 reference sheet.

Source: res/sprites/reference/harrier_x68000.png (TSR rip by Yawackhary).
Output: res/sprites/player.png — 64x96 indexed, colour 0 = transparent (PAL1).
"""

import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "res", "sprites", "reference", "harrier_x68000.png")
OUT = os.path.join(ROOT, "res", "sprites", "player.png")

OUT_W, OUT_H = 64, 96
# Frame 1: neutral mid-stride rear view (x columns in the top running row).
FRAME_X0, FRAME_X1 = 40, 67
BG_INDEX = 0
TRANSPARENT = (255, 0, 255)


def src_palette_rgb(im):
    pal = im.getpalette()
    return [tuple(pal[i * 3 : i * 3 + 3]) for i in range(256)]


def tight_bbox(im, x0, x1, y_max):
    px = im.load()
    xs, ys = [], []
    for y in range(y_max):
        for x in range(x0, x1):
            if px[x, y] != BG_INDEX:
                xs.append(x)
                ys.append(y)
    if not xs:
        raise RuntimeError("empty frame region")
    return min(xs), min(ys), max(xs) + 1, max(ys) + 1


def indexed_to_rgba(im, colors):
    px = im.load()
    w, h = im.size
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    opx = out.load()
    for y in range(h):
        for x in range(w):
            idx = px[x, y]
            if idx == BG_INDEX:
                continue
            r, g, b = colors[idx]
            opx[x, y] = (r, g, b, 255)
    return out


def fit_canvas(rgba, out_w, out_h):
    w, h = rgba.size
    scale = min(out_w / w, out_h / h)
    nw = max(1, int(round(w * scale)))
    nh = max(1, int(round(h * scale)))
    scaled = rgba.resize((nw, nh), Image.Resampling.NEAREST)
    canvas = Image.new("RGBA", (out_w, out_h), (0, 0, 0, 0))
    canvas.paste(scaled, ((out_w - nw) // 2, out_h - nh))
    return canvas


def rgba_to_indexed(rgba, transparent=TRANSPARENT):
    """Pack non-transparent pixels into indices 1..N, index 0 = key colour."""
    px = rgba.load()
    w, h = rgba.size
    rgb_to_idx = {transparent: 0}
    palette = [transparent]
    out = Image.new("P", (w, h), 0)
    opx = out.load()

    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < 128:
                opx[x, y] = 0
                continue
            rgb = (r, g, b)
            if rgb not in rgb_to_idx:
                if len(palette) >= 16:
                    raise RuntimeError("more than 15 opaque colours in frame")
                rgb_to_idx[rgb] = len(palette)
                palette.append(rgb)
            opx[x, y] = rgb_to_idx[rgb]

    flat = []
    for c in palette:
        flat.extend(c)
    flat.extend((0, 0, 0) * (256 - len(palette)))
    out.putpalette(flat)
    return out


def rip(frame_x0=FRAME_X0, frame_x1=FRAME_X1):
    im = Image.open(SRC)
    colors = src_palette_rgb(im)
    x0, y0, x1, y1 = tight_bbox(im, frame_x0, frame_x1, y_max=100)
    crop = im.crop((x0, y0, x1, y1))
    rgba = indexed_to_rgba(crop, colors)
    fitted = fit_canvas(rgba, OUT_W, OUT_H)
    indexed = rgba_to_indexed(fitted)
    indexed.save(OUT)
    print(f"wrote {OUT} ({OUT_W}x{OUT_H}, {len(indexed.getcolors(maxcolors=256))} colours used)")


if __name__ == "__main__":
    x0 = int(sys.argv[1]) if len(sys.argv) > 1 else FRAME_X0
    x1 = int(sys.argv[2]) if len(sys.argv) > 2 else FRAME_X1
    rip(x0, x1)
