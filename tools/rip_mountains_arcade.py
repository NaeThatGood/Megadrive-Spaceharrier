#!/usr/bin/env python3
"""Extract the Space Harrier arcade mountain horizon from gfx1 tile ROMs.

Source: SH MAME ROM set (epr-7196/7197/7198.ic*, gfx1 region).
Decode follows MAME gfx_8x8x3_planar (segahang.cpp). Mountain tiles live on
tile-sheet row 32 (codes 2048..2111); palette colour code 8 (word index 64).

Outputs:
  res/sprites/reference/arcade_mountains_strip.png  — 512x8 RGB reference
"""

import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_ROM_DIR = os.path.join(ROOT, "SH MAME ROM")
DEFAULT_OUT = os.path.join(ROOT, "res", "sprites", "reference", "arcade_mountains_strip.png")

GFX1_ROMS = ("epr-7196.ic31", "epr-7197.ic46", "epr-7198.ic60")
MAINCPU_ROMS = (
    ("epr-7188a.ic97", "epr-7184a.ic84"),
    ("epr-7189.ic98", "epr-7185.ic85"),
    ("epr-7190.ic99", "epr-7186.ic86"),
    ("epr-7191.ic100", "epr-7187.ic87"),
)

# Mountain horizon tiles (512 px = 64 tiles) on the decoded gfx1 sheet.
MOUNTAIN_TILE_ROW = 32
TILES_WIDE = 64
PALETTE_BASE = 64  # colour code 8 in MAME gfx1 (bright green / black dither)


def _load(path):
    with open(path, "rb") as f:
        return f.read()


def _build_maincpu(rom_dir):
    rom = bytearray(0x40000)
    for bank, (even_name, odd_name) in enumerate(MAINCPU_ROMS):
        even = _load(os.path.join(rom_dir, even_name))
        odd = _load(os.path.join(rom_dir, odd_name))
        base = bank * 0x10000
        for i in range(0x8000):
            rom[base + i * 2] = even[i]
            rom[base + i * 2 + 1] = odd[i]
    return rom


def _pal_rgb(maincpu, word_idx):
    off = word_idx * 2
    w = (maincpu[off] << 8) | maincpu[off + 1]
    r = ((w >> 0) & 0x1f) * 255 // 31
    g = ((w >> 5) & 0x1f) * 255 // 31
    b = ((w >> 10) & 0x1f) * 255 // 31
    return (r, g, b)


def _decode_gfx1(rom_dir):
    planes = [_load(os.path.join(rom_dir, name)) for name in GFX1_ROMS]
    if not all(len(p) == 0x8000 for p in planes):
        raise RuntimeError("gfx1 ROMs must each be 32 KiB")
    num_tiles = len(planes[0]) // 8

    def tile(code):
        out = [[0] * 8 for _ in range(8)]
        for y in range(8):
            for x in range(8):
                val = 0
                bit = 4
                for plane in planes[::-1]:  # MSB plane first (MAME plane order)
                    b = plane[code * 8 + y]
                    if (b >> (7 - x)) & 1:
                        val |= bit
                    bit >>= 1
                out[y][x] = val
        return out

    return num_tiles, tile


def extract_mountain_strip(rom_dir=DEFAULT_ROM_DIR):
    """Return a 512x8 RGB PIL image of the arcade mountain horizon."""
    if not os.path.isdir(rom_dir):
        raise FileNotFoundError(f"ROM directory not found: {rom_dir}")

    maincpu = _build_maincpu(rom_dir)
    _, decode_tile = _decode_gfx1(rom_dir)

    img = Image.new("RGB", (TILES_WIDE * 8, 8), (0, 0, 0))
    px = img.load()
    row_base = MOUNTAIN_TILE_ROW * TILES_WIDE

    for col in range(TILES_WIDE):
        tile = decode_tile(row_base + col)
        for y in range(8):
            for x in range(8):
                pen = tile[y][x]
                if pen:
                    px[col * 8 + x, y] = _pal_rgb(maincpu, PALETTE_BASE + pen)

    return img


def rip(out_path=DEFAULT_OUT, rom_dir=DEFAULT_ROM_DIR):
    img = extract_mountain_strip(rom_dir)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    img.save(out_path)
    print(f"wrote {out_path} ({img.size[0]}x{img.size[1]})")
    return img


if __name__ == "__main__":
    rom = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_ROM_DIR
    out = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_OUT
    rip(out, rom)
