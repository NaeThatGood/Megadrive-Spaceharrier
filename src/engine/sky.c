#include "sky.h"

const SkyKeyframe SKY_KEYFRAMES[SKY_KEYFRAME_COUNT] =
{
    {  0, 110, 200, 175 },
    {  6, 165, 205, 195 },
    { 14, 200, 195, 220 },
    { 26, 198, 170, 238 },
    { 40, 190, 158, 242 },
};

static u16 ramp[SKY_RAMP_SIZE];
static s16 horizonY;
static const u16* skyReadPtr;

static u16 rgbToVdp(u8 r, u8 g, u8 b)
{
    return RGB24_TO_VDPCOLOR(((u32) r << 16) | ((u16) g << 8) | b);
}

static u16 keyframeColor(s16 d)
{
    if (d <= 0)
    {
        const SkyKeyframe* k = &SKY_KEYFRAMES[0];
        return rgbToVdp(k->r, k->g, k->b);
    }

    if (d >= SKY_TRANSITION)
    {
        const SkyKeyframe* k = &SKY_KEYFRAMES[SKY_KEYFRAME_COUNT - 1];
        return rgbToVdp(k->r, k->g, k->b);
    }

    for (u16 i = 0; i < SKY_KEYFRAME_COUNT - 1; i++)
    {
        const SkyKeyframe* a = &SKY_KEYFRAMES[i];
        const SkyKeyframe* b = &SKY_KEYFRAMES[i + 1];

        if (d <= b->d)
        {
            const s16 span = (s16) b->d - (s16) a->d;
            const s16 num = d - (s16) a->d;
            const u8 r = (u8) ((s16) a->r + ((((s16) b->r - (s16) a->r) * num) / span));
            const u8 g = (u8) ((s16) a->g + ((((s16) b->g - (s16) a->g) * num) / span));
            const u8 bl = (u8) ((s16) a->b + ((((s16) b->b - (s16) a->b) * num) / span));
            return rgbToVdp(r, g, bl);
        }
    }

    return rgbToVdp(SKY_KEYFRAMES[SKY_KEYFRAME_COUNT - 1].r,
                    SKY_KEYFRAMES[SKY_KEYFRAME_COUNT - 1].g,
                    SKY_KEYFRAMES[SKY_KEYFRAME_COUNT - 1].b);
}

// Keep this minimal: it runs once per scanline near HBlank.
HINTERRUPT_CALLBACK sky_hint(void)
{
    *((vu32*) VDP_CTRL_PORT) = 0xC0000000UL | ((u32) (SKY_CRAM_INDEX * 2) << 16);
    *((vu16*) VDP_DATA_PORT) = *skyReadPtr++;
}

void sky_init(void)
{
    for (u16 i = 0; i < SKY_RAMP_SIZE; i++)
        ramp[i] = keyframeColor((s16) SKY_H0 - (s16) i);

    horizonY = 0;
    skyReadPtr = &ramp[SKY_H0];

    SYS_setHIntCallback(sky_hint);
    VDP_setHIntCounter(0);
    VDP_setHInterrupt(TRUE);
}

void sky_setHorizon(s16 y)
{
    if (y < 0) y = 0;
    if (y > SKY_H0) y = SKY_H0;
    horizonY = y;
}

void sky_vblank(void)
{
    s16 h = horizonY;
    if (h < 0) h = 0;
    if (h > SKY_H0) h = SKY_H0;
    skyReadPtr = &ramp[SKY_H0 - h];
}
