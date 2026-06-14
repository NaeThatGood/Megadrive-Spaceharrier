#include <genesis.h>
#include "resources.h"
#include "../../engine/world.h"

// Stored-frame renderer.
// 50 pre-rendered scale steps (8..64 px, ~1.1 px apart) live in ROM as
// frames of one sprite sheet, every frame on a 64x64 canvas anchored
// bottom-centre (see tools/gen_scale_frames.py). The 50 Hz game loop maps
// projected size to the nearest step; the sprite engine streams the frame's
// tiles to VRAM only when the frame index changes.

#define FRAME_COUNT     50
#define FRAME_CANVAS    64

static const u8 FRAME_SIZES[FRAME_COUNT] =
{
     8,  9, 10, 11, 13, 14, 15, 16, 17, 18,
    19, 21, 22, 23, 24, 25, 26, 27, 29, 30,
    31, 32, 33, 34, 35, 37, 38, 39, 40, 41,
    42, 43, 45, 46, 47, 48, 49, 50, 51, 53,
    54, 55, 56, 57, 58, 59, 61, 62, 63, 64
};

static u8 sizeToFrame(u16 sizePx)
{
    u8 best = 0;
    u16 bestDist = 0xFFFF;
    for (u8 i = 0; i < FRAME_COUNT; i++)
    {
        const u16 s = FRAME_SIZES[i];
        const u16 dist = (sizePx > s) ? (sizePx - s) : (s - sizePx);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = i;
        }
    }
    return best;
}

static void st_init(void)
{
    PAL_setPalette(PAL2, spr_enemy_scaled.palette->data, DMA_QUEUE);
}

static void st_spawn(WObj* o)
{
    o->sprs[0] = SPR_addSprite(&spr_enemy_scaled, -128, -128,
                           TILE_ATTR(PAL2, 0, FALSE, FALSE));
    o->sizeIdx = 0;
    SPR_setFrame(o->sprs[0], 0);
}

static void st_update(WObj* o, s16 sx, s16 syBottom, u16 sizePx)
{
    const u8 frame = sizeToFrame(sizePx);
    if (frame != o->sizeIdx)
    {
        o->sizeIdx = frame;
        SPR_setFrame(o->sprs[0], frame);
    }
    // all frames: 64x64 canvas, bottom-centre anchor
    SPR_setPosition(o->sprs[0], sx - (FRAME_CANVAS / 2), syBottom - FRAME_CANVAS);
}

static void st_despawn(WObj* o)
{
    for (u8 q = 0; q < 4; q++)
    {
        if (o->sprs[q])
        {
            SPR_releaseSprite(o->sprs[q]);
            o->sprs[q] = NULL;
        }
    }
    o->vramIndex = 0;
}

const Renderer RENDER_stored =
{
    "STORED ", st_init, st_spawn, st_update, st_despawn, NULL
};
