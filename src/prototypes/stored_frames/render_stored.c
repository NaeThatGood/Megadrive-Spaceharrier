#include <genesis.h>
#include "resources.h"
#include "../../engine/clouds.h"
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
#define SCREEN_W        320

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
static Sprite* objLeft[MAX_OBJECTS][TIER_COUNT];
static Sprite* objRight[MAX_OBJECTS][TIER_COUNT];
static bool poolBuilt;
static s16 screenH;

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

static void hidePair(u8 slot, u8 tier)
{
    if (objLeft[slot][tier])
        SPR_setVisibility(objLeft[slot][tier], HIDDEN);
    if (objRight[slot][tier])
        SPR_setVisibility(objRight[slot][tier], HIDDEN);
}

static void setPairVisibility(u8 slot, u8 tier, SpriteVisibility visibility)
{
    if (objLeft[slot][tier])
        SPR_setVisibility(objLeft[slot][tier], visibility);
    if (objRight[slot][tier])
        SPR_setVisibility(objRight[slot][tier], visibility);
}

static bool pairReady(u8 slot, u8 tier)
{
    return objLeft[slot][tier] && objRight[slot][tier];
}

static void buildObjectPool(void)
{
    if (poolBuilt) return;

    for (u8 slot = 0; slot < MAX_OBJECTS; slot++)
    {
        for (u8 tier = 0; tier < TIER_COUNT; tier++)
        {
            const SpriteDefinition* def = FRAME_TIERS[tier].def;

            objLeft[slot][tier] = SPR_addSprite(def, -128, -128,
                                                TILE_ATTR(PAL2, 0, FALSE, FALSE));
            objRight[slot][tier] = NULL;

            if (objLeft[slot][tier])
            {
                const u16 sharedTile = objLeft[slot][tier]->attribut & ~TILE_ATTR_MASK;
                objRight[slot][tier] =
                    SPR_addSpriteEx(def, -128, -128,
                                    TILE_ATTR_FULL(PAL2, 0, FALSE, TRUE, sharedTile),
                                    0);
            }

            hidePair(slot, tier);
            if (!pairReady(slot, tier))
                SYS_die("stored enemy pool allocation failed");
        }
    }

    poolBuilt = TRUE;
}

static void setStoredFrame(WObj* o, u8 frame)
{
    const u8 tierIdx = frameToTier(frame);
    const FrameTier* tier = &FRAME_TIERS[tierIdx];

    if (o->vramIndex != tierIdx)
    {
        if (o->vramIndex != TIER_NONE)
            hidePair(o->slot, (u8) o->vramIndex);

        if (pairReady(o->slot, tierIdx))
        {
            o->vramIndex = tierIdx;
            setPairVisibility(o->slot, tierIdx, VISIBLE);
        }
        else
        {
            o->vramIndex = TIER_NONE;
        }
        o->sizeIdx = 0xFF;
    }

    if (pairReady(o->slot, tierIdx) && frame != o->sizeIdx)
    {
        const u8 tierFrame = frame - tier->firstFrame;
        o->sizeIdx = frame;
        SPR_setFrame(objLeft[o->slot][tierIdx], tierFrame);
        SPR_setFrame(objRight[o->slot][tierIdx], tierFrame);
    }
}

static void st_init(void)
{
    initFrameLut();
    PAL_setPalette(PAL2, spr_enemy_scaled_64.palette->data, DMA_QUEUE);
    CLOUDS_applyPalette();
    screenH = VDP_getScreenHeight();
    buildObjectPool();
}

static void st_spawn(WObj* o)
{
    o->vramIndex = TIER_NONE;
    o->sizeIdx = 0xFF;
    setStoredFrame(o, 0);
}

static void st_update(WObj* o, s16 sx, s16 syBottom, u16 sizePx)
{
    const u8 frame = sizeToFrame(sizePx);
    if (frame != o->sizeIdx)
        setStoredFrame(o, frame);

    if (o->vramIndex != TIER_NONE && pairReady(o->slot, (u8) o->vramIndex))
    {
        const u8 tier = (u8) o->vramIndex;
        const u8 canvasPx = FRAME_TIERS[tier].canvasPx;
        const u8 halfPx = canvasPx / 2;
        const s16 x = sx - (canvasPx / 2);
        const s16 y = syBottom - canvasPx;
        if (x <= -(s16) canvasPx || x >= SCREEN_W ||
            y <= -(s16) canvasPx || y >= screenH)
        {
            hidePair(o->slot, tier);
            return;
        }

        SPR_setPosition(objLeft[o->slot][tier], x, y);
        SPR_setPosition(objRight[o->slot][tier], x + halfPx, y);
        setPairVisibility(o->slot, tier, VISIBLE);
    }
}

static void st_despawn(WObj* o)
{
    for (u8 tier = 0; tier < TIER_COUNT; tier++)
        hidePair(o->slot, tier);
    o->vramIndex = TIER_NONE;
}

void RENDER_storedRelease(void)
{
    if (!poolBuilt) return;

    for (u8 slot = 0; slot < MAX_OBJECTS; slot++)
    {
        for (u8 tier = 0; tier < TIER_COUNT; tier++)
        {
            if (objRight[slot][tier])
            {
                SPR_releaseSprite(objRight[slot][tier]);
                objRight[slot][tier] = NULL;
            }
            if (objLeft[slot][tier])
            {
                SPR_releaseSprite(objLeft[slot][tier]);
                objLeft[slot][tier] = NULL;
            }
        }
    }

    poolBuilt = FALSE;
}

const Renderer RENDER_stored =
{
    "STORED ", st_init, st_spawn, st_update, st_despawn, NULL
};
