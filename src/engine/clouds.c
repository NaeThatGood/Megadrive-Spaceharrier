#include "clouds.h"

#include "resources.h"

#define CLOUD_PAL_BASE        12
#define CLOUDS_DISABLED       1
#define SCREEN_W              320

#define CLOUD_VARIANTS        3
#define QUARTER_TILES_W       2
#define QUARTER_TILES_H       1
#define QUARTER_W             (QUARTER_TILES_W * 8)
#define QUARTER_H             (QUARTER_TILES_H * 8)
#define CLOUD_FULL_W          (QUARTER_W * 2)
#define CLOUD_FULL_H          (QUARTER_H * 2)

#define FG_CLOUD_GROUP_COUNT  3
#define FG_CLOUDS_PER_GROUP   3
#define FG_CLOUD_QUADS        4
#define FG_CLOUD_WRAP_LEFT    (-72)
#define FG_CLOUD_WRAP_RIGHT   (SCREEN_W + 24)
#define FG_CLOUD_OFFSCREEN_X  (SCREEN_W + 64)
#define FG_CLOUD_EVICT_FRAMES 10
#define FG_CLOUD_DEPTH        300

typedef struct
{
    Sprite* sprs[FG_CLOUD_QUADS];
    s16 groupOffsetX;
    s16 horizonOffsetY;
} ForeCloud;

typedef struct
{
    ForeCloud clouds[FG_CLOUDS_PER_GROUP];
    s32 x;                   // 8.8 fixed
    s16 speed;               // 8.8 fixed
} ForeCloudGroup;

static ForeCloudGroup foreCloudGroups[FG_CLOUD_GROUP_COUNT];
static u8 cloudEvictFrame;

typedef enum
{
    CLOUD_STATE_NORMAL,
    CLOUD_STATE_EVICTING,
    CLOUD_STATE_HIDDEN
} CloudState;

static CloudState cloudState;

static Sprite* addCloudSprite(const SpriteDefinition* def, u16 sharedTile,
                              bool flipV, bool flipH)
{
    Sprite* spr = SPR_addSpriteEx(def, -128, -128,
                                  TILE_ATTR_FULL(PAL2, TRUE, flipV, flipH,
                                                 sharedTile),
                                  0);
    return spr;
}

static void initForeCloud(ForeCloud* cloud, u8 variant, s16 groupOffsetX,
                          s16 horizonOffsetY)
{
    cloud->groupOffsetX = groupOffsetX;
    cloud->horizonOffsetY = horizonOffsetY;

    cloud->sprs[0] = SPR_addSprite(&spr_cloud_quarter, -128, -128,
                                   TILE_ATTR(PAL2, TRUE, FALSE, FALSE));
    if (!cloud->sprs[0])
        SYS_die("foreground cloud allocation failed");

    const u16 sharedTile = cloud->sprs[0]->attribut & ~TILE_ATTR_MASK;
    cloud->sprs[1] = addCloudSprite(&spr_cloud_quarter, sharedTile,
                                    FALSE, TRUE);
    cloud->sprs[2] = addCloudSprite(&spr_cloud_quarter, sharedTile,
                                    TRUE, FALSE);
    cloud->sprs[3] = addCloudSprite(&spr_cloud_quarter, sharedTile,
                                    TRUE, TRUE);

    for (u8 i = 0; i < FG_CLOUD_QUADS; i++)
    {
        Sprite* spr = cloud->sprs[i];
        if (!spr) SYS_die("foreground cloud allocation failed");
        SPR_setFrame(spr, variant % CLOUD_VARIANTS);
        SPR_setPriority(spr, TRUE);
        SPR_setDepth(spr, FG_CLOUD_DEPTH);
        SPR_setVisibility(spr, VISIBLE);
    }
}

static void setForeCloudPosition(ForeCloud* cloud, s32 groupX, s16 horizonY)
{
    const s16 x = (s16) (groupX >> 8) + cloud->groupOffsetX;
    const s16 y = horizonY + cloud->horizonOffsetY;

    if (cloud->sprs[0]) SPR_setPosition(cloud->sprs[0], x, y);
    if (cloud->sprs[1]) SPR_setPosition(cloud->sprs[1], x + QUARTER_W, y);
    if (cloud->sprs[2]) SPR_setPosition(cloud->sprs[2], x, y + QUARTER_H);
    if (cloud->sprs[3])
        SPR_setPosition(cloud->sprs[3], x + QUARTER_W, y + QUARTER_H);
}

static void setForeCloudVisibility(ForeCloud* cloud, SpriteVisibility visibility)
{
    for (u8 i = 0; i < FG_CLOUD_QUADS; i++)
        if (cloud->sprs[i])
            SPR_setVisibility(cloud->sprs[i], visibility);
}

static void initForeCloudGroup(ForeCloudGroup* group, s16 x, s16 speed,
                               s16 horizonOffsetY)
{
    group->x = (s32) x << 8;
    group->speed = speed;

    // Three overlapping 32x16 clouds read as one larger cloud mass.
    initForeCloud(&group->clouds[0], 0, 0, horizonOffsetY);
    initForeCloud(&group->clouds[1], 1, 18, horizonOffsetY + 4);
    initForeCloud(&group->clouds[2], 2, 9, horizonOffsetY - 10);
}

void CLOUDS_applyPalette(void)
{
    static const u16 colors[4] =
    {
        RGB24_TO_VDPCOLOR(0x303848),
        RGB24_TO_VDPCOLOR(0x707888),
        RGB24_TO_VDPCOLOR(0xB0B8C8),
        RGB24_TO_VDPCOLOR(0xF0F4FF),
    };

    // Queue after any full PAL2 upload so enemy palette refreshes do not
    // overwrite the cloud-only entries before VBlank flushes DMA.
    PAL_setColors((PAL2 << 4) + CLOUD_PAL_BASE, colors, 4, DMA_QUEUE);
}

void CLOUDS_init(void)
{
#if CLOUDS_DISABLED
    cloudState = CLOUD_STATE_HIDDEN;
    cloudEvictFrame = 0;
    return;
#endif

    CLOUDS_applyPalette();
    cloudState = CLOUD_STATE_NORMAL;
    cloudEvictFrame = 0;

    initForeCloudGroup(&foreCloudGroups[0], -40, 0x80, -96);
    initForeCloudGroup(&foreCloudGroups[1], 110, 0xA0, -82);
    initForeCloudGroup(&foreCloudGroups[2], 260, 0xC0, -68);
}

void CLOUDS_release(void)
{
    for (u8 groupIdx = 0; groupIdx < FG_CLOUD_GROUP_COUNT; groupIdx++)
    {
        ForeCloudGroup* group = &foreCloudGroups[groupIdx];
        for (u8 cloudIdx = 0; cloudIdx < FG_CLOUDS_PER_GROUP; cloudIdx++)
        {
            ForeCloud* cloud = &group->clouds[cloudIdx];
            for (u8 quad = FG_CLOUD_QUADS; quad > 0; quad--)
            {
                const u8 quadIdx = quad - 1;
                if (cloud->sprs[quadIdx])
                {
                    SPR_releaseSprite(cloud->sprs[quadIdx]);
                    cloud->sprs[quadIdx] = NULL;
                }
            }
        }
    }

    cloudState = CLOUD_STATE_HIDDEN;
    cloudEvictFrame = 0;
}

void CLOUDS_update(s16 horizonY)
{
    if (cloudState == CLOUD_STATE_HIDDEN) return;

    bool allOffscreen = TRUE;

    for (u8 groupIdx = 0; groupIdx < FG_CLOUD_GROUP_COUNT; groupIdx++)
    {
        ForeCloudGroup* group = &foreCloudGroups[groupIdx];

        if (cloudState == CLOUD_STATE_EVICTING)
        {
            const u16 rampFrame = (cloudEvictFrame > FG_CLOUD_EVICT_FRAMES)
                                ? FG_CLOUD_EVICT_FRAMES
                                : cloudEvictFrame;
            const s32 rampedSpeed =
                ((s32) group->speed * (FG_CLOUD_EVICT_FRAMES + rampFrame)) /
                FG_CLOUD_EVICT_FRAMES;
            group->x += rampedSpeed;
        }
        else
        {
            group->x += group->speed;
            if ((group->x >> 8) > FG_CLOUD_WRAP_RIGHT)
                group->x = (s32) FG_CLOUD_WRAP_LEFT << 8;
        }

        if ((group->x >> 8) <= FG_CLOUD_OFFSCREEN_X)
            allOffscreen = FALSE;

        for (u8 cloudIdx = 0; cloudIdx < FG_CLOUDS_PER_GROUP; cloudIdx++)
            setForeCloudPosition(&group->clouds[cloudIdx], group->x, horizonY);
    }

    if (cloudState == CLOUD_STATE_EVICTING)
    {
        if (cloudEvictFrame < FG_CLOUD_EVICT_FRAMES)
            cloudEvictFrame++;

        if (allOffscreen)
        {
            for (u8 groupIdx = 0; groupIdx < FG_CLOUD_GROUP_COUNT; groupIdx++)
                for (u8 cloudIdx = 0; cloudIdx < FG_CLOUDS_PER_GROUP; cloudIdx++)
                    setForeCloudVisibility(&foreCloudGroups[groupIdx].clouds[cloudIdx],
                                           HIDDEN);
            cloudState = CLOUD_STATE_HIDDEN;
        }
    }
}

void CLOUDS_evict(void)
{
    if (cloudState == CLOUD_STATE_HIDDEN) return;

    cloudState = CLOUD_STATE_EVICTING;
    cloudEvictFrame = 0;
}

void CLOUDS_resume(void)
{
    cloudState = CLOUD_STATE_NORMAL;
    cloudEvictFrame = 0;

    foreCloudGroups[0].x = -((s32) 40 << 8);
    foreCloudGroups[1].x = (s32) 110 << 8;
    foreCloudGroups[2].x = (s32) 260 << 8;

    for (u8 groupIdx = 0; groupIdx < FG_CLOUD_GROUP_COUNT; groupIdx++)
        for (u8 cloudIdx = 0; cloudIdx < FG_CLOUDS_PER_GROUP; cloudIdx++)
            setForeCloudVisibility(&foreCloudGroups[groupIdx].clouds[cloudIdx],
                                   VISIBLE);
}

bool CLOUDS_areOffscreen(void)
{
    return cloudState == CLOUD_STATE_HIDDEN;
}
