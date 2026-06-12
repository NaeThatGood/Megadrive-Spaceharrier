# Asset Budget — 16 Mbit (2 MB) Cartridge

Early Mega Drive games shipped on 4 Mbit (512 KB). This prototype assumes a
16 Mbit (2,097,152 byte) cartridge, which was commonplace by 1991-92 and is
trivial for modern homebrew. This document costs out what that buys.

## Measured costs in this prototype

From the rescomp build log (all values from the actual build):

| Resource | Uncompressed | In ROM (FAST/BEST packed) |
| --- | --- | --- |
| Enemy scale strip, 15 frames 8..64 px | 13,024 B tiles | ~6.5 KB |
| Enemy 64x64 master (runtime mode, raw 4bpp) | 2,048 B | 2,048 B (uncompressed by design) |
| Ground screen image 320x224 (tiles + map) | ~17 KB | 8.4 KB |
| Player sprite 32x48 | 768 B | <1 KB |
| "Get ready!" voice, 0.7 s @ 13.3 kHz 8-bit | 9,472 B | 9,472 B |
| Code + SGDK library + font/system | — | ~100 KB |

Whole prototype ROM: 128 KB (one 8th of one 16 Mbit cart... padded; actual
content is ~60 KB).

## Stored-frame cost model

A scale strip of sizes 8..64 px in 4 px steps, 4bpp, tile-rounded:

- Sum of tile bytes = 13,024 B per *pose* (measured ~50% compressible for
  flat-shaded art; assume 7 KB packed conservatively).
- An animated enemy (4 animation frames per scale step) = ~52 KB raw,
  ~28 KB packed.
- A large boss with steps up to 128 px roughly quadruples that: ~110 KB
  packed per animated boss.

## What fits in 2 MB

Working allocation:

| Slice | Budget |
| --- | --- |
| Code + library + tables | 128 KB |
| Backgrounds (4 stage themes, sky/ground variants) | 256 KB |
| Voice + PCM SFX | 256 KB |
| FM music (XGM2, ~8 tracks) | 128 KB |
| Headroom / padding | 128 KB |
| **Enemy + object scale frames** | **~1,200 KB** |

That enemy budget supports, mixed and matched:

- ~170 *static* poses (7 KB each), or
- **~40 fully animated enemy/object types** (28 KB each), or
- ~30 animated types + 2-3 large animated bosses.

For reference, the arcade Space Harrier has roughly 25-30 distinct
enemy/obstacle types — comfortably inside this budget, with animation,
on a 16 Mbit cart. ROM is not the constraint; VRAM and sprites-per-scanline
are (see comparison-notes.md).

## Voice / sample quality tradeoffs

XGM2 plays 8-bit signed PCM at 13.3 kHz (it can mix while FM music plays).
Cost is linear:

| Rate / quality | Cost per second | 256 KB voice budget buys |
| --- | --- | --- |
| 6.65 kHz (muffled, period-typical) | 6.7 KB/s | ~38 s |
| 13.3 kHz (this prototype, clearly intelligible) | 13.3 KB/s | ~19 s |
| 16 kHz via PCM driver (no music mixing) | 16 KB/s | ~16 s |

Early 4 Mbit games could afford only a handful of one-second clips at low
rates; the jump to 16 Mbit makes ~20 seconds of *good* 13.3 kHz speech
practical — wave announcements, boss taunts, "Get ready!" style callouts —
costing only an eighth of the cart.

A full looped PCM music track is still off the table (13.3 KB/s = 800 KB/min);
FM (XGM2) music + PCM voice/SFX accents remains the right split.
