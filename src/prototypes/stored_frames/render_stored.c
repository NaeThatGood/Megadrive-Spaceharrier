#include <genesis.h>
#include "resources.h"
#include "../../engine/world.h"

// Stored-frame renderer.
// 25 pre-rendered scale steps (8..64 px, ~2.3 px apart) live in ROM as
// frames of one sprite sheet, every frame on a 64x64 canvas anchored
// bottom-centre (see tools/gen_scale_frames.py). One step per 25 Hz tick
// during mid-range approach; the sprite engine streams the frame's tiles
// to VRAM only when the frame index changes.

#define FRAME_COUNT     25
#define FRAME_CANVAS    64

static const u8 FRAME_SIZES[FRAME_COUNT] =
{
     8, 10, 13, 15, 17, 19, 22, 24, 27, 29,
    31, 34, 36, 38, 41, 43, 45, 48, 50, 52,
    55, 57, 59, 61, 64
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
    PAL_setPalette(PAL2, spr_enemy_scaled.palette->data, CPU);
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
