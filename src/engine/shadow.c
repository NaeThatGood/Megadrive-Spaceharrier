#include "shadow.h"

#include "resources.h"
#include "world.h"

#define SHADOW_CANVAS_W       64
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
    Sprite* spr;
    u8 frame;
} ShadowState;

static u8 shadowFrameForSize[SHADOW_FRAME_MAX_SIZE + 1];
static ShadowState shadowStates[SHADOW_REGISTRY_COUNT];
static bool shadowInitialized;

static ShadowState* stateFor(Sprite* s)
{
    if (!s) return NULL;

    for (u8 i = 0; i < SHADOW_REGISTRY_COUNT; i++)
        if (shadowStates[i].spr == s)
            return &shadowStates[i];

    return NULL;
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
            shadowStates[i].spr = NULL;
            shadowStates[i].frame = 0xFF;
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
    Sprite* s = SPR_addSprite(&spr_shadow_scaled, -128, -128,
                              TILE_ATTR(PAL2, 0, FALSE, FALSE));
    if (!s) return NULL;

    SPR_setDepth(s, SHADOW_DEPTH);
    SPR_setVisibility(s, HIDDEN);

    for (u8 i = 0; i < SHADOW_REGISTRY_COUNT; i++)
    {
        if (!shadowStates[i].spr)
        {
            shadowStates[i].spr = s;
            shadowStates[i].frame = 0xFF;
            break;
        }
    }

    return s;
}

void SHADOW_release(Sprite* s)
{
    if (!s) return;

    ShadowState* st = stateFor(s);
    if (st)
    {
        st->spr = NULL;
        st->frame = 0xFF;
    }

    SPR_releaseSprite(s);
}

void SHADOW_hide(Sprite* s)
{
    if (s) SPR_setVisibility(s, HIDDEN);
}

void SHADOW_place(Sprite* s, s16 wx, u16 z, u16 casterSizePx)
{
    if (!s) return;

    if (casterSizePx < SHADOW_MIN_SIZE || z >= SHADOW_FAR_CULL_Z)
    {
        SHADOW_hide(s);
        return;
    }

    const u16 q = WORLD_proj(z);
    const s16 sx = WORLD_screenXq(wx, q);
    const s16 syG = WORLD_screenYBq(0, q) + GROUND_VISIBLE_HORIZON_PAD;
    const u8 frame = sizeToFrame(casterSizePx);
    ShadowState* st = stateFor(s);

    if (!st || frame != st->frame)
    {
        SPR_setFrame(s, frame);
        if (st) st->frame = frame;
    }

    SPR_setPosition(s, sx - (SHADOW_CANVAS_W / 2),
                    syG - (SHADOW_CANVAS_H / 2));
    SPR_setVisibility(s, VISIBLE);
}
