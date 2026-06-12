# Stored Frames vs Runtime Scaling — Comparison Notes

Measurements taken in Genesis Plus GX (cycle-accurate enough for this
purpose) with the in-ROM HUD: object count, CPU load (% of one 60 Hz frame;
>100% means missed frames) and FPS. Same wave pattern in both modes,
up to 8 simultaneous objects, sizes 8..64 px.

## Headline numbers

| Metric | STORED | RUNTIME |
| --- | --- | --- |
| Frame rate | 60 FPS, locked | 39-44 FPS typical, worse in heavy waves |
| CPU load | 21-23% | 106-242% (load >100% = dropped frames) |
| Size granularity | 15 steps (4 px) | 1 px (when rescale budget allows) |
| Worst-case cost per object | table lookup + frame switch | 4096 px resample + 2 KB DMA |
| ROM per enemy pose | ~13 KB raw, ~6.5 KB compressed | 2 KB (one 64x64 master) |
| VRAM per on-screen object | only the current frame's tiles (1..64) | fixed 64-tile slot (2 KB) |

The prototype's C scaler rescales at most one object per frame, prioritised
by visual error. Even so, a single 64 px rescale costs more than a whole
frame of 68000 time, which is exactly what the CPU meter shows.

## Why runtime scaling loses on this hardware

- The 68000 runs at 7.67 MHz, roughly 128k cycles per NTSC frame. Touching
  every destination pixel of a 64x64 sprite (4096 px, ~2 px/byte) plus
  clearing and DMA-ing a 2 KB buffer simply does not fit alongside game
  logic, even with table-driven C and tile-layout direct writes.
- Hand-written 68000 assembly (see technical-plan.md) could plausibly buy
  2-3x, which still caps practical throughput at ~1-2 large rescales per
  frame. Smooth growth of several near objects at once is out of reach.
- DMA bandwidth is also finite: VBlank DMA moves ~7 KB/frame (NTSC). Two
  large objects rescaled per frame (4 KB) would already crowd out scroll
  tables and sprite list updates.
- VRAM: each runtime object needs its full 64-tile slot reserved (8 objects
  = 512 tiles = 16 KB, a quarter of VRAM), whereas stored frames only keep
  each object's *current* frame in VRAM.

## Where runtime scaling is still attractive

- **Per-pixel size continuity.** Stored frames step every 4 px; runtime mode
  grows in 1 px increments and never "pops" between buckets, which looks
  noticeably smoother on slow, distant approaches where rescales are cheap
  (an 8-24 px rescale costs only a few hundred pixels).
- **ROM economy per pose.** One 2 KB master replaces a 13 KB strip; for a
  game with hundreds of distinct poses on a small cart this could matter.
  With a 16 Mbit budget it does not (see asset-budget.md).
- **One-off effects.** A single zooming object (title logo, boss intro,
  death zoom) is well within budget; this is how period games actually used
  software scaling — sparingly.

## Hardware-sprite limits (both modes)

- 80 hardware sprites, max 20 sprites / 320 px per scanline. A 64 px object
  is 2 sprites wide on a scanline (runtime mode quads) or up to 2-4 cut
  sprites in stored mode. Four 64 px objects on the same scanline band is
  near the wall in either mode; the design answer is the classic one —
  spread enemies vertically and cap simultaneous near objects.
- Sprite engine VRAM streaming (stored mode) only uploads on frame change;
  steady state is almost free. A worst case (all 8 objects changing bucket
  in one frame, ~3 KB of tiles) stays within VBlank DMA capacity.

## Expected real-hardware behaviour

Genesis Plus GX is timing-faithful for CPU/DMA at this level of detail, so:

- STORED mode should hold 60 FPS on a stock console; all updates ride the
  DMA queue inside VBlank.
- RUNTIME mode will drop frames on real hardware exactly as in the emulator
  (the bottleneck is raw 68000 throughput, not emulation artefacts).
- The line-scroll ground and palette cycling are bog-standard VDP features,
  hardware-safe.

## Verdict

**Stored frames are the right approach for a Mega Drive Space Harrier-style
game**, full stop. A 16 Mbit cartridge removes the one historical argument
for runtime scaling (ROM scarcity): ~40 fully animated enemy types fit
comfortably (asset-budget.md). The recommended hybrid: stored frames for all
gameplay objects, with the software scaler kept for occasional single-object
showpieces (boss intro zooms, title effects) where a transient frame-rate
cost is acceptable or can be hidden.
