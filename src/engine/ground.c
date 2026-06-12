#include "ground.h"
#include "resources.h"

// Perspective ground illusion on a static tile plane:
//  - lateral sway: per-scanline horizontal scroll, ramping linearly with
//    distance below the horizon (mathematically correct for a ground plane:
//    shifting world X by W needs a screen shift of W/z, and 1/z is linear
//    in screen Y for a flat floor)
//  - forward motion: cycling the two checker palette colours flips the board
//    by half a cell, the classic cheap "rushing floor" effect
//  - sky: small constant parallax scroll

#define SCREEN_H  224

static s16 lineScroll[SCREEN_H];
static u16 cycleTimer;
static u16 cyclePhase;
static u16 checkerColA;
static u16 checkerColB;

void GROUND_init(void)
{
    PAL_setPalette(PAL0, img_ground.palette->data, CPU);

    // Checker colours live at PAL0 indices 5 and 6 (see gen_assets.py)
    checkerColA = PAL_getColor(5);
    checkerColB = PAL_getColor(6);
    cycleTimer = 0;
    cyclePhase = 0;

    VDP_setScrollingMode(HSCROLL_LINE, VSCROLL_PLANE);
    VDP_drawImageEx(BG_B, &img_ground,
                    TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, TILE_USER_INDEX),
                    0, 0, FALSE, TRUE);

    memset(lineScroll, 0, sizeof(lineScroll));
}

void GROUND_update(s16 swayX, u16 speed)
{
    // Sky: gentle constant parallax
    const s16 skyScroll = -(swayX >> 3);
    for (u16 y = 0; y < GROUND_HORIZON; y++)
        lineScroll[y] = skyScroll;

    // Ground: linear ramp, stronger the closer to the camera
    for (u16 y = GROUND_HORIZON; y < SCREEN_H; y++)
        lineScroll[y] = -((swayX * (s16) (y - GROUND_HORIZON)) >> 6);

    VDP_setHorizontalScrollLine(BG_B, 0, lineScroll, SCREEN_H, DMA_QUEUE);

    // Forward motion: flip checker colours; faster speed = faster flips
    if (speed > 0)
    {
        cycleTimer += speed;
        if (cycleTimer >= 24)
        {
            cycleTimer = 0;
            cyclePhase ^= 1;
            PAL_setColor(5, cyclePhase ? checkerColB : checkerColA);
            PAL_setColor(6, cyclePhase ? checkerColA : checkerColB);
        }
    }
}
