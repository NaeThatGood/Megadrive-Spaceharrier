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
static u16 skyBandStart;
static const u16* skyReadPtr;
static vu16 skyLinesLeft;       // scanlines the HINT should still fire this frame
static vu16 skySkipLines;        // flat-top lines between SKY_HINT_START and band top
static volatile bool skyDensePending;  // switch to per-line HINTs after first band line
static vu16 skyResetLine;       // restore backdrop colour just below horizon
static bool enabled;

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

static u16 bandStartForHorizon(s16 h)
{
    s16 start = h - SKY_TRANSITION;
    return (start > 0) ? (u16) start : 0;
}

// Keep this minimal: it runs once per scanline near HBlank.
HINTERRUPT_CALLBACK sky_hint(void)
{
    // Flat-top gap: the fixed first HINT landed at SKY_HINT_START; software-
    // count the remaining (variable) lines down to the real band top. No CRAM
    // write on these lines (the VBlank flat-top write still holds index 0).
    if (skySkipLines)
    {
        skySkipLines--;
        if (skyDensePending)                 // this is the first HINT of the frame
        {
            skyDensePending = FALSE;
            VDP_setHIntCounter(0);           // per-line firing from here down
        }
        return;
    }

    // Band line - time critical (written during HBlank).
    *((vu32*) VDP_CTRL_PORT) = 0xC0000000UL | ((u32) (SKY_CRAM_INDEX * 2) << 16);
    *((vu16*) VDP_DATA_PORT) = *skyReadPtr++;

    if (skyDensePending)                     // band starts exactly at SKY_HINT_START
    {
        skyDensePending = FALSE;
        VDP_setHIntCounter(0);
    }

    if (skyLinesLeft)
    {
        if (--skyLinesLeft == 0)
        {
            skyReadPtr = &ramp[0];           // backdrop colour for the reset line
            if (!skyResetLine)
            {
                // CRITICAL: restore the fixed skip value so the counter is
                // NEVER left at 0 going into VBlank. This is what stops the
                // next frame's first HINT from firing at the top of screen.
                VDP_setHIntCounter((u8) (SKY_HINT_START - 1));
                VDP_setHInterrupt(FALSE);
            }
            // else keep counter 0 so the reset line fires on the next scanline
        }
        return;
    }

    // Reset line written; gate off and restore the fixed skip for next frame.
    VDP_setHIntCounter((u8) (SKY_HINT_START - 1));
    VDP_setHInterrupt(FALSE);
    skyResetLine = FALSE;
}

void sky_init(void)
{
    for (u16 i = 0; i < SKY_RAMP_SIZE; i++)
        ramp[i] = keyframeColor((s16) SKY_H0 - (s16) i);

    horizonY = 0;
    skyBandStart = 0;
    skyReadPtr = &ramp[SKY_H0];
    skyLinesLeft = 1;
    skySkipLines = 0;
    skyDensePending = FALSE;
    skyResetLine = FALSE;
    enabled = TRUE;

    SYS_setHIntCallback(sky_hint);
    VDP_setHIntCounter(SKY_HINT_START - 1);
    VDP_setHInterrupt(TRUE);
}

void sky_setEnabled(bool value)
{
    enabled = value;
    if (!enabled)
        VDP_setHInterrupt(FALSE);
}

bool sky_isEnabled(void)
{
    return enabled;
}

void sky_setHorizon(s16 y)
{
    if (y < 0) y = 0;
    if (y > SKY_H0) y = SKY_H0;
    horizonY = y;
    skyBandStart = bandStartForHorizon(y);
}

void sky_vblank(void)
{
    if (!enabled)
    {
        VDP_setHInterrupt(FALSE);
        *((vu32*) VDP_CTRL_PORT) = 0xC0000000UL | ((u32) (SKY_CRAM_INDEX * 2) << 16);
        *((vu16*) VDP_DATA_PORT) = ramp[0];
        return;
    }

    s16 h = horizonY;
    if (h < 0) h = 0;
    if (h > SKY_H0) h = SKY_H0;

    const u16 start = skyBandStart;
    s16 bottom = h + SKY_BOTTOM_ADJUST;
    if (bottom > (s16) (SKY_RAMP_SIZE - SKY_H0 - 1))
        bottom = (s16) (SKY_RAMP_SIZE - SKY_H0 - 1);
    if (bottom > (s16) (SKY_SCREEN_H_MAX - 1))
        bottom = (s16) (SKY_SCREEN_H_MAX - 1);
    const u16 startIndex = (u16) (SKY_H0 - h + start);

    // Flat top (lines 0..band top) is held by this single CRAM write; the HINT
    // does not fire at all until the fixed SKY_HINT_START line.
    *((vu32*) VDP_CTRL_PORT) = 0xC0000000UL | ((u32) (SKY_CRAM_INDEX * 2) << 16);
    *((vu16*) VDP_DATA_PORT) = ramp[0];

    skyReadPtr   = &ramp[startIndex];
    skyLinesLeft = (u16) (bottom - (s16) start + 1);
    // Variable gap, software-counted from the FIXED line to the band top.
    // start >= SKY_HINT_START in all legal horizon positions (band top min ~40).
    skySkipLines = (start > SKY_HINT_START) ? (u16) (start - SKY_HINT_START) : 0;
    skyResetLine = (((u16) bottom + 1) < SKY_SCREEN_H_MAX) ? TRUE : FALSE;
    skyDensePending = TRUE;

    // Constant skip to the fixed first-HINT line. Constant => immune to the
    // per-frame counter-reload timing that broke the variable version.
    VDP_setHIntCounter((u8) (SKY_HINT_START - 1));
    VDP_setHInterrupt(TRUE);
}
