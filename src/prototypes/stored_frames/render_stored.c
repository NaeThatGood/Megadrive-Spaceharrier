#include <genesis.h>
#include "resources.h"
#include "../../engine/world.h"

// Stored-frame renderer.
// 50 pre-rendered scale steps (8..64 px, ~1.1 px apart) live in ROM as
// frames split across 16x16, 32x32, and 64x64 canvas tiers. The 30 Hz game
// loop maps projected size to the nearest step; the sprite engine streams the
// tier's smaller frame canvas to VRAM only when the frame index changes.

#define FRAME_COUNT     50
#define FRAME_MAX_SIZE  64
#define TIER_COUNT      3
#define TIER_NONE       0xFFFF

static const u8 FRAME_SIZES[FRAME_COUNT] =
{
     8,  9, 10, 11, 13, 14, 15, 16, 17, 18,
    19, 21, 22, 23, 24, 25, 26, 27, 29, 30,
    31, 32, 33, 34, 35, 37, 38, 39, 40, 41,
    42, 43, 45, 46, 47, 48, 49, 50, 51, 53,
    54, 55, 56, 57, 58, 59, 61, 62, 63, 64
};

typedef struct
{
    const SpriteDefinition* def;
    u8 firstFrame;
    u8 canvasPx;
} FrameTier;

static const FrameTier FRAME_TIERS[TIER_COUNT] =
{
    { &spr_enemy_scaled_16,  0, 16 },
    { &spr_enemy_scaled_32,  8, 32 },
    { &spr_enemy_scaled_64, 22, 64 }
};

static u8 frameForSize[FRAME_MAX_SIZE + 1];

static void initFrameLut(void)
{
    for (u16 size = 0; size <= FRAME_MAX_SIZE; size++)
    {
        u8 best = 0;
        u16 bestDist = 0xFFFF;
        for (u8 i = 0; i < FRAME_COUNT; i++)
        {
            const u16 s = FRAME_SIZES[i];
            const u16 dist = (size > s) ? (size - s) : (s - size);
            if (dist < bestDist)
            {
                bestDist = dist;
                best = i;
            }
        }
        frameForSize[size] = best;
    }
}

static u8 sizeToFrame(u16 sizePx)
{
    if (sizePx > FRAME_MAX_SIZE) return FRAME_COUNT - 1;
    return frameForSize[sizePx];
}

static u8 frameToTier(u8 frame)
{
    if (frame < FRAME_TIERS[1].firstFrame) return 0;
    if (frame < FRAME_TIERS[2].firstFrame) return 1;
    return 2;
}

static void setStoredFrame(WObj* o, u8 frame)
{
    const u8 tierIdx = frameToTier(frame);
    const FrameTier* tier = &FRAME_TIERS[tierIdx];

    if (o->vramIndex != tierIdx)
    {
        if (o->sprs[0])
        {
            SPR_releaseSprite(o->sprs[0]);
            o->sprs[0] = NULL;
        }
        if (o->sprs[1])
        {
            SPR_releaseSprite(o->sprs[1]);
            o->sprs[1] = NULL;
        }

        o->sprs[0] = SPR_addSprite(tier->def, -128, -128,
                                   TILE_ATTR(PAL2, 0, FALSE, FALSE));
        if (o->sprs[0])
        {
            const u16 sharedTile = o->sprs[0]->attribut & ~TILE_ATTR_MASK;
            o->sprs[1] =
                SPR_addSpriteEx(tier->def, -128, -128,
                                TILE_ATTR_FULL(PAL2, 0, FALSE, TRUE, sharedTile),
                                0);
        }

        if (!o->sprs[0] || !o->sprs[1])
        {
            if (o->sprs[0])
            {
                SPR_releaseSprite(o->sprs[0]);
                o->sprs[0] = NULL;
            }
            if (o->sprs[1])
            {
                SPR_releaseSprite(o->sprs[1]);
                o->sprs[1] = NULL;
            }
            o->vramIndex = TIER_NONE;
        }
        else
        {
            o->vramIndex = tierIdx;
        }
        o->sizeIdx = 0xFF;
    }

    if (o->sprs[0] && o->sprs[1] && frame != o->sizeIdx)
    {
        const u8 tierFrame = frame - tier->firstFrame;
        o->sizeIdx = frame;
        SPR_setFrame(o->sprs[0], tierFrame);
        SPR_setFrame(o->sprs[1], tierFrame);
    }
}

static void st_init(void)
{
    initFrameLut();
    PAL_setPalette(PAL2, spr_enemy_scaled_64.palette->data, DMA_QUEUE);
}

static void st_spawn(WObj* o)
{
    o->sprs[0] = NULL;
    o->sprs[1] = NULL;
    o->vramIndex = TIER_NONE;
    o->sizeIdx = 0xFF;
    setStoredFrame(o, 0);
}

static void st_update(WObj* o, s16 sx, s16 syBottom, u16 sizePx)
{
    const u8 frame = sizeToFrame(sizePx);
    if (frame != o->sizeIdx)
        setStoredFrame(o, frame);

    if (o->sprs[0] && o->sprs[1] && o->vramIndex != TIER_NONE)
    {
        const u8 canvasPx = FRAME_TIERS[o->vramIndex].canvasPx;
        const u8 halfPx = canvasPx / 2;
        const s16 x = sx - (canvasPx / 2);
        const s16 y = syBottom - canvasPx;
        SPR_setPosition(o->sprs[0], x, y);
        SPR_setPosition(o->sprs[1], x + halfPx, y);
    }
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
