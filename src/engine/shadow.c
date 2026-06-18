#include "shadow.h"

#include "resources.h"
#include "world.h"

#define SHADOW_RESCALE_BUDGET  2
#define SHADOW_MAX_SKIP        3
#define SHADOW_CANVAS_W       64
#define SHADOW_HALF_CANVAS_W  (SHADOW_CANVAS_W / 2)
#define SHADOW_CANVAS_H       24
#define SHADOW_FRAME_COUNT    16
#define SHADOW_FRAME_MAX_SIZE 64
#define SHADOW_MIN_SIZE       4
#define SHADOW_FAR_CULL_Z     (WORLD_Z_FAR - 128)
#define SHADOW_DEPTH          250
#define SHADOW_PAL_INDEX      11
#define SHADOW_REGISTRY_COUNT (MAX_OBJECTS + MAX_SHOTS + 1 + 3)

static const u8 SHADOW_FRAME_SIZES[SHADOW_FRAME_COUNT] =
{
     8, 11, 15, 18, 22, 25, 29, 32,
    36, 39, 43, 46, 50, 53, 57, 60
};

typedef struct
{
    Sprite* sprLeft;
    Sprite* sprRight;
    u8 frame;
    u8 skips;
} ShadowState;

static u8 shadowRescaleCredits;

static u8 shadowFrameForSize[SHADOW_FRAME_MAX_SIZE + 1];
static ShadowState shadowStates[SHADOW_REGISTRY_COUNT];
static bool shadowInitialized;

static ShadowState* stateFor(Sprite* s)
{
    if (!s) return NULL;

    for (u8 i = 0; i < SHADOW_REGISTRY_COUNT; i++)
        if (shadowStates[i].sprLeft == s)
            return &shadowStates[i];

    return NULL;
}

static void hidePair(ShadowState* st)
{
    if (st->sprLeft) SPR_setVisibility(st->sprLeft, HIDDEN);
    if (st->sprRight) SPR_setVisibility(st->sprRight, HIDDEN);
}

static void initFrameLut(void)
{
    for (u16 size = 0; size <= SHADOW_FRAME_MAX_SIZE; size++)
    {
        u8 best = 0;
        u16 bestDist = 0xFFFF;
        for (u8 i = 0; i < SHADOW_FRAME_COUNT; i++)
        {
            const u16 s = SHADOW_FRAME_SIZES[i];
            const u16 dist = (size > s) ? (size - s) : (s - size);
            if (dist < bestDist)
            {
                bestDist = dist;
                best = i;
            }
        }
        shadowFrameForSize[size] = best;
    }
}

static u8 sizeToFrame(u16 sizePx)
{
    if (sizePx > SHADOW_FRAME_MAX_SIZE) return SHADOW_FRAME_COUNT - 1;
    return shadowFrameForSize[sizePx];
}

void SHADOW_init(void)
{
    if (!shadowInitialized)
    {
        initFrameLut();

        for (u8 i = 0; i < SHADOW_REGISTRY_COUNT; i++)
        {
            shadowStates[i].sprLeft = NULL;
            shadowStates[i].sprRight = NULL;
            shadowStates[i].frame = 0xFF;
            shadowStates[i].skips = 0;
        }

        shadowInitialized = TRUE;
    }

    // PAL2 index 11 is unused by the generated enemy/shot palette; shots use
    // indices 8..10, so this does not clobber their colours. Renderer switches
    // reload PAL2, so callers may safely invoke SHADOW_init() again to reapply
    // only this colour without resetting sprite state.
    PAL_setColor((PAL2 << 4) + SHADOW_PAL_INDEX,
                 RGB24_TO_VDPCOLOR(0x101828));
}

Sprite* SHADOW_add(void)
{
    ShadowState* slot = NULL;

    for (u8 i = 0; i < SHADOW_REGISTRY_COUNT; i++)
    {
        if (!shadowStates[i].sprLeft)
        {
            slot = &shadowStates[i];
            break;
        }
    }
    if (!slot) return NULL;

    slot->sprLeft = SPR_addSprite(&spr_shadow_scaled, -128, -128,
                                  TILE_ATTR(PAL2, 0, FALSE, FALSE));
    slot->sprRight = NULL;

    if (slot->sprLeft)
    {
        const u16 sharedTile = slot->sprLeft->attribut & ~TILE_ATTR_MASK;
        slot->sprRight =
            SPR_addSpriteEx(&spr_shadow_scaled, -128, -128,
                            TILE_ATTR_FULL(PAL2, 0, FALSE, TRUE, sharedTile),
                            0);
    }

    if (!slot->sprLeft || !slot->sprRight)
    {
        if (slot->sprRight)
        {
            SPR_releaseSprite(slot->sprRight);
            slot->sprRight = NULL;
        }
        if (slot->sprLeft)
        {
            SPR_releaseSprite(slot->sprLeft);
            slot->sprLeft = NULL;
        }
        return NULL;
    }

    SPR_setDepth(slot->sprLeft, SHADOW_DEPTH);
    SPR_setDepth(slot->sprRight, SHADOW_DEPTH);
    hidePair(slot);
    slot->frame = 0xFF;
    slot->skips = 0;

    return slot->sprLeft;
}

void SHADOW_release(Sprite* s)
{
    if (!s) return;

    ShadowState* st = stateFor(s);
    if (!st) return;

    if (st->sprRight)
    {
        SPR_releaseSprite(st->sprRight);
        st->sprRight = NULL;
    }
    if (st->sprLeft)
    {
        SPR_releaseSprite(st->sprLeft);
        st->sprLeft = NULL;
    }
    st->frame = 0xFF;
    st->skips = 0;
}

void SHADOW_beginFrame(void)
{
    shadowRescaleCredits = SHADOW_RESCALE_BUDGET;
}

void SHADOW_hide(Sprite* s)
{
    ShadowState* st = stateFor(s);
    if (st) hidePair(st);
}

void SHADOW_place(Sprite* s, s16 wx, u16 z, u16 casterSizePx)
{
    if (!s) return;

    ShadowState* st = stateFor(s);
    if (!st || !st->sprLeft || !st->sprRight) return;

    if (casterSizePx < SHADOW_MIN_SIZE || z >= SHADOW_FAR_CULL_Z)
    {
        hidePair(st);
        return;
    }

    const u16 q = WORLD_proj(z);
    const s16 sx = WORLD_screenXq(wx, q);
    const s16 syG = WORLD_screenYBq(0, q) + GROUND_VISIBLE_HORIZON_PAD;
    const u8 frame = sizeToFrame(casterSizePx);

    if (frame != st->frame)
    {
        const u8 force = (st->frame == 0xFF);
        const u8 allow = force ||
                         (shadowRescaleCredits > 0) ||
                         (st->skips >= SHADOW_MAX_SKIP);
        if (allow)
        {
            SPR_setFrame(st->sprLeft, frame);
            SPR_setFrame(st->sprRight, frame);
            st->frame = frame;
            st->skips = 0;
            if (!force && shadowRescaleCredits > 0)
                shadowRescaleCredits--;
        }
        else
        {
            st->skips++;
        }
    }
    else
    {
        st->skips = 0;
    }

    const s16 x = sx - (SHADOW_CANVAS_W / 2);
    const s16 y = syG - (SHADOW_CANVAS_H / 2);
    SPR_setPosition(st->sprLeft, x, y);
    SPR_setPosition(st->sprRight, x + SHADOW_HALF_CANVAS_W, y);
    SPR_setVisibility(st->sprLeft, VISIBLE);
    SPR_setVisibility(st->sprRight, VISIBLE);
}
