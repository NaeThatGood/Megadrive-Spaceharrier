#include "ground.h"
#include "resources.h"

// Perspective ground using the techniques of the original Mega Drive
// pseudo-3D floors:
//
//  - Space Harrier II: the floor is a tile plane skewed by a per-scanline
//    horizontal scroll table (its "perspective table"), and up/down player
//    movement vertically scrolls a taller-than-screen tilemap to move the
//    horizon. We do both here.
//  - Burning Force: forward motion is pure palette animation on a static
//    plane. The checkerboard is pre-rendered with 8 colour indices (each
//    depth cell split into 4 phase bands, cell parity folded in); rotating
//    those entries makes the board rush toward the camera in quarter-cell
//    steps. The entry currently crossing a band boundary is colour-blended,
//    so the motion reads as a smooth sweep, not discrete flips. Cost per
//    frame: 8 CRAM writes, zero tile updates (SH2 instead rewrote ~11 KB of
//    tile data per frame - unnecessary for a flat two-tone checker).
//
// Plane geometry (must match tools/gen_assets.py gen_ground):
//   320x352 board, horizon at image row 160, on-screen horizon at row 96
//   when V-scroll = BOARD_PAD_TOP (neutral pitch).
#define PITCH_RANGE        64              // must match gen_assets.py
#define BOARD_PAD_TOP      64              // must match gen_assets.py PAD_TOP
#define BOARD_H            352              // IMG_H in gen_assets.py

#define MAX_LINES          240              // PAL V30

// Checker palette entries (PAL0 indices 7..14, see gen_assets.py)
#define CHECKER_PAL_BASE   7
#define CHECKER_ENTRIES    8
#define COL_LIGHT          RGB24_TO_VDPCOLOR(0x6098D8)
#define COL_DARK           RGB24_TO_VDPCOLOR(0x204090)

s16 GROUND_horizon;

static s16 lineScroll[MAX_LINES];
static u16 screenH;
static u16 fwdAcc;                          // 8.8 fixed, 1.0 = quarter cell
static u16 blendLD[4];                      // light -> dark, 4 sub-steps
static u16 blendDL[4];                      // dark -> light, 4 sub-steps
static u16 checkerCols[CHECKER_ENTRIES];

// Linear blend between two VDP colours, num/4 of the way from a to b
static u16 lerpColor(u16 a, u16 b, u16 num)
{
    u16 out = 0;
    for (u16 shift = 1; shift <= 9; shift += 4)
    {
        const s16 av = (a >> shift) & 7;
        const s16 bv = (b >> shift) & 7;
        out |= (u16) (av + (((bv - av) * (s16) num) >> 2)) << shift;
    }
    return out;
}

void GROUND_init(void)
{
    PAL_setPalette(PAL0, img_ground.palette->data, CPU);

    screenH = VDP_getScreenHeight();
    GROUND_horizon = GROUND_HORIZON;
    fwdAcc = 0;

    for (u16 s = 0; s < 4; s++)
    {
        blendLD[s] = lerpColor(COL_LIGHT, COL_DARK, s);
        blendDL[s] = lerpColor(COL_DARK, COL_LIGHT, s);
    }

    VDP_setScrollingMode(HSCROLL_LINE, VSCROLL_PLANE);

    // Taller-than-screen board (44 tile rows); 64x64 plane fits 352 px height.
    VDP_setPlaneSize(64, 64, TRUE);
    VDP_drawImageEx(BG_B, &img_ground,
                    TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, TILE_USER_INDEX),
                    0, 0, FALSE, TRUE);

    memset(lineScroll, 0, sizeof(lineScroll));
    GROUND_update(0, 0, 0, 0);
}

void GROUND_update(s16 swayX, s16 pitchY, s16 vanishX, u16 speed)
{
    GROUND_horizon = GROUND_HORIZON;

    // --- Vertical pitch: scroll the tall board to move the horizon --------
    s16 vs = BOARD_PAD_TOP + ((pitchY * PITCH_RANGE) >> 7);
    // If pitch goes the wrong way, negate vs.
    VDP_setVerticalScroll(BG_B, vs);

    // --- Per-line H scroll: sky parallax + perspective ramp -------------
    const s16 skyScroll = -(swayX >> 3);
    const s16 a = swayX;            // full-strength lean
    const s16 b = vanishX >> 1;     // convergence point follows player X

    for (u16 y = 0; y < (u16) GROUND_HORIZON; y++)
        lineScroll[y] = skyScroll;

    for (u16 y = GROUND_HORIZON; y < screenH; y++)
    {
        const s16 d = (s16) y - GROUND_HORIZON;
        lineScroll[y] = -(((a * d) >> 5) + b);
    }

    VDP_setHorizontalScrollLine(BG_B, 0, lineScroll, screenH, DMA_QUEUE);

    // --- Forward motion: rotate the checker palette entries -------------
    fwdAcc += speed * 21;       // speed 20 ~= one checker cell every 5 frames @ 25 Hz

    const u16 tQ = (fwdAcc >> 8) & 7;      // quarter-band counter
    const u16 sub = (fwdAcc >> 6) & 3;     // blend sub-step within a band
    for (u16 j = 0; j < CHECKER_ENTRIES; j++)
    {
        const u16 q = (j + tQ) & 7;
        checkerCols[j] = (q < 3) ? COL_LIGHT
                       : (q == 3) ? blendLD[sub]
                       : (q < 7) ? COL_DARK
                                 : blendDL[sub];
    }
    PAL_setColors(CHECKER_PAL_BASE, checkerCols, CHECKER_ENTRIES, CPU);
}
