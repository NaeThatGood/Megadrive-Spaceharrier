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
//   384x352 board, horizon at image row 160, on-screen horizon at row 96
//   when V-scroll = BOARD_PAD_TOP (neutral pitch). Only the left 192 px are
//   stored; the right half is drawn with VDP horizontal tile flip attributes.
//   The centered 320 px view gets 32 px of side margin so line scroll does not
//   expose plane wrapping.
#define PITCH_RANGE        64              // must match gen_assets.py
#define BOARD_PAD_TOP      64              // must match gen_assets.py PAD_TOP
#define BOARD_W            384              // IMG_W in gen_assets.py
#define BOARD_H            352              // IMG_H in gen_assets.py
#define BOARD_TILES_W      (BOARD_W / 8)
#define BOARD_TILES_H      (BOARD_H / 8)
#define BOARD_HALF_TILES_W (BOARD_TILES_W / 2)
#define BOARD_HORIZON      (GROUND_HORIZON + BOARD_PAD_TOP)
#define CHECKER_START_PAD  2
#define VIEW_W             320
#define HSCROLL_MIN        (VIEW_W - BOARD_W)
#define HSCROLL_MAX        0
#define HSCROLL_CENTER     (HSCROLL_MIN / 2)
#define GROUND_SWAY_SHIFT  10
#define GROUND_SWAY_ROUND  (1 << (GROUND_SWAY_SHIFT - 1))
#define VANISH_SHIFT       3

#define MAX_LINES          240              // maximum V30 height

// Checker palette entries (PAL0 indices 7..14, see gen_assets.py)
#define CHECKER_PAL_BASE   7
#define CHECKER_ENTRIES    8
#define COL_LIGHT          RGB24_TO_VDPCOLOR(0x6098D8)
#define COL_DARK           RGB24_TO_VDPCOLOR(0x204090)

s16 GROUND_horizon;

static s16 lineScroll[MAX_LINES];
static u16 screenH;
static u16 prevCheckerStartY;
static s16 lastSwayX;
static s16 lastB;
static s16 lastHorizonY;
static bool lineScrollValid;
static u16 fwdAcc;                          // 8.8 fixed, 1.0 = quarter cell
static u16 blendLD[4];                      // light -> dark, 4 sub-steps
static u16 blendDL[4];                      // dark -> light, 4 sub-steps
static u16 checkerCols[CHECKER_ENTRIES];
static u16 groundRow[BOARD_TILES_W];

static s16 clampHScroll(s16 scroll)
{
    if (scroll < HSCROLL_MIN) return HSCROLL_MIN;
    if (scroll > HSCROLL_MAX) return HSCROLL_MAX;
    return scroll;
}

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

static void drawMirroredGround(void)
{
    TileMap* unpacked = NULL;
    const TileMap* tm = img_ground.tilemap;
    const u16 base = TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, TILE_USER_INDEX);

    VDP_loadTileSet(img_ground.tileset, TILE_USER_INDEX, DMA);

    if (tm->compression != COMPRESSION_NONE)
    {
        unpacked = unpackTileMap(tm, NULL);
        if (!unpacked)
        {
            VDP_clearPlane(BG_B, TRUE);
            return;
        }
        tm = unpacked;
    }

    for (u16 y = 0; y < BOARD_TILES_H; y++)
    {
        const u16* src = tm->tilemap + y * BOARD_HALF_TILES_W;

        for (u16 x = 0; x < BOARD_HALF_TILES_W; x++)
            groundRow[x] = src[x];

        for (u16 x = 0; x < BOARD_HALF_TILES_W; x++)
            groundRow[BOARD_TILES_W - 1 - x] = src[x] ^ TILE_ATTR_HFLIP_MASK;

        VDP_setTileMapDataRectEx(BG_B, groundRow, base,
                                  0, y, BOARD_TILES_W, 1, BOARD_TILES_W,
                                  CPU);
    }

    if (unpacked) MEM_free(unpacked);
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

    // Wider/taller board (48x44 tiles); 64x64 plane fits 384x352 px.
    VDP_setPlaneSize(64, 64, TRUE);
    drawMirroredGround();

    memset(lineScroll, 0, sizeof(lineScroll));
    prevCheckerStartY = 0;
    lastSwayX = 0;
    lastB = 0;
    lastHorizonY = 0;
    lineScrollValid = FALSE;
    GROUND_update(0, 0, 0, 0);
}

void GROUND_update(s16 swayX, s16 pitchY, s16 vanishX, u16 speed)
{
    // --- Vertical pitch: scroll the tall board to move the horizon --------
    s16 vs = BOARD_PAD_TOP + ((pitchY * PITCH_RANGE) >> 7);
    // If pitch goes the wrong way, negate vs.
    VDP_setVerticalScroll(BG_B, vs);

    // --- Per-line H scroll: fixed empty headroom + gentle perspective ramp --
    const s16 b = vanishX >> VANISH_SHIFT;  // convergence follows player X

    const s16 horizonY = BOARD_HORIZON - vs;
    GROUND_horizon = horizonY;

    if (lineScrollValid && swayX == lastSwayX && b == lastB &&
        horizonY == lastHorizonY)
        goto forwardAnim;

    s16 checkerStartY = horizonY + CHECKER_START_PAD;
    if (checkerStartY < 0) checkerStartY = 0;
    if (checkerStartY > (s16) screenH) checkerStartY = (s16) screenH;

    const u16 startY = (u16) checkerStartY;
    u16 uploadStartY = 0;

    if (lineScrollValid)
    {
        uploadStartY = (startY < prevCheckerStartY) ? startY : prevCheckerStartY;

        for (u16 y = prevCheckerStartY; y < startY; y++)
            lineScroll[y] = HSCROLL_CENTER;
    }
    else
    {
        for (u16 y = 0; y < startY; y++)
            lineScroll[y] = HSCROLL_CENTER;
    }

    s32 swayAcc = ((s32) swayX * ((s16) startY - horizonY))
               + GROUND_SWAY_ROUND;
    const s32 swayStep = swayX;

    for (u16 y = startY; y < screenH; y++)
    {
        lineScroll[y] = clampHScroll(
            HSCROLL_CENTER
            - ((swayAcc >> GROUND_SWAY_SHIFT) + b));
        swayAcc += swayStep;
    }

    if (uploadStartY < screenH)
    {
        VDP_setHorizontalScrollLine(BG_B, uploadStartY, &lineScroll[uploadStartY],
                                    screenH - uploadStartY, DMA_QUEUE);
    }
    prevCheckerStartY = startY;
    lastSwayX = swayX;
    lastB = b;
    lastHorizonY = horizonY;
    lineScrollValid = TRUE;

forwardAnim:
    // --- Forward motion: rotate the checker palette entries -------------
    fwdAcc += speed * 21;       // speed 22 ~= one checker cell every 5 frames @ 30 Hz

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
    PAL_setColors(CHECKER_PAL_BASE, checkerCols, CHECKER_ENTRIES, DMA_QUEUE);
}
