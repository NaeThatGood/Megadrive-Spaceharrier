#include <genesis.h>
#include "resources.h"
#include "../../engine/world.h"

// Stored-frame renderer.
// 15 pre-rendered scale steps (8..64 px in 4 px increments) live in ROM as
// frames of one sprite sheet, every frame on a 64x64 canvas anchored
// bottom-centre (see tools/gen_scale_frames.py). Rendering = pick frame
// from projected size; the sprite engine streams the frame's tiles to VRAM
// only when the frame index changes.

#define FRAME_COUNT     15
#define FRAME_MIN_SIZE  8
#define FRAME_STEP      4
#define FRAME_CANVAS    64

static u8 sizeToFrame(u16 sizePx)
{
    if (sizePx <= FRAME_MIN_SIZE) return 0;
    u16 idx = (sizePx - FRAME_MIN_SIZE + (FRAME_STEP / 2)) / FRAME_STEP;
    if (idx >= FRAME_COUNT) idx = FRAME_COUNT - 1;
    return (u8) idx;
}

static void st_init(void)
{
    PAL_setPalette(PAL2, spr_enemy_scaled.palette->data, CPU);
}

static void st_spawn(WObj* o)
{
    o->spr = SPR_addSprite(&spr_enemy_scaled, -128, -128,
                           TILE_ATTR(PAL2, 0, FALSE, FALSE));
    o->sizeIdx = 0;
    SPR_setFrame(o->spr, 0);
}

static void st_update(WObj* o, s16 sx, s16 syBottom, u16 sizePx)
{
    const u8 frame = sizeToFrame(sizePx);
    if (frame != o->sizeIdx)
    {
        o->sizeIdx = frame;
        SPR_setFrame(o->spr, frame);
    }
    // all frames: 64x64 canvas, bottom-centre anchor
    SPR_setPosition(o->spr, sx - (FRAME_CANVAS / 2), syBottom - FRAME_CANVAS);
}

static void st_despawn(WObj* o)
{
    if (o->spr)
    {
        SPR_releaseSprite(o->spr);
        o->spr = NULL;
    }
}

const Renderer RENDER_stored =
{
    "STORED ", st_init, st_spawn, st_update, st_despawn
};
