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

#define SKY_RLE_START_INDEX  (SKY_H0 - SKY_TRANSITION)
#define SKY_RLE_END_INDEX    (SKY_RAMP_SIZE - 1)
#define SKY_RLE_SIZE         (SKY_RLE_END_INDEX - SKY_RLE_START_INDEX + 1)
#define SKY_MAX_RUNS         SKY_RLE_SIZE

typedef struct
{
    u16 color;
    u8 span;                 // scanlines this colour holds
} SkyRun;

static SkyRun skyRuns[SKY_MAX_RUNS];
static u16 skyRunCount;
static s16 horizonY;
static u16 skyBandStart;
static vu16 skyRunIndex;        // current clipped RLE run for this frame
static vu16 skyRunSpan;         // scanlines held by the current clipped run
static vu16 skyLinesLeft;       // scanlines remaining after the current run
static vu16 skySkipLines;        // flat-top lines between SKY_HINT_START and band top
static volatile bool skySkipPending;   // switch to per-line software skip after first HINT
static vu16 skyResetLine;       // restore backdrop colour just below horizon
static volatile bool skyResetPending;
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

static void buildSkyRuns(void)
{
    skyRunCount = 0;

    for (u16 i = SKY_RLE_START_INDEX; i <= SKY_RLE_END_INDEX;)
    {
        const u16 color = ramp[i];
        u16 span = 1;

        while ((i + span) <= SKY_RLE_END_INDEX && ramp[i + span] == color)
            span++;

        if (skyRunCount < SKY_MAX_RUNS)
        {
            SkyRun* run = &skyRuns[skyRunCount++];
            run->color = color;
            run->span = (u8) span;
        }

        i += span;
    }
}

static void selectSkyRuns(u16 startIndex, u16 lineCount)
{
    if (!skyRunCount || !lineCount)
    {
        skyRunIndex = 0;
        skyRunSpan = 0;
        skyLinesLeft = 0;
        return;
    }

    u16 offset = (startIndex > SKY_RLE_START_INDEX)
               ? (u16) (startIndex - SKY_RLE_START_INDEX)
               : 0;
    u16 runIndex = 0;

    while ((runIndex + 1) < skyRunCount && offset >= skyRuns[runIndex].span)
    {
        offset -= skyRuns[runIndex].span;
        runIndex++;
    }

    u16 span = (u16) skyRuns[runIndex].span - offset;
    if (span > lineCount) span = lineCount;

    skyRunIndex = runIndex;
    skyRunSpan = span;
    skyLinesLeft = lineCount - span;
}

// Keep this minimal: it runs near HBlank.
HINTERRUPT_CALLBACK sky_hint(void)
{
    if (skyResetPending)
    {
        *((vu32*) VDP_CTRL_PORT) = 0xC0000000UL | ((u32) (SKY_CRAM_INDEX * 2) << 16);
        *((vu16*) VDP_DATA_PORT) = ramp[SKY_H0];
        VDP_setHIntCounter((u8) (SKY_HINT_START - 1));
        VDP_setHInterrupt(FALSE);
        skyResetLine = FALSE;
        skyResetPending = FALSE;
        return;
    }

    // Flat-top gap: the fixed first HINT landed at SKY_HINT_START; software-
    // count the remaining (variable) lines down to the real band top. No CRAM
    // write on these lines (the VBlank flat-top write still holds index 0).
    if (skySkipLines)
    {
        skySkipLines--;
        if (skySkipPending)                  // this is the first HINT of the frame
        {
            skySkipPending = FALSE;
            VDP_setHIntCounter(0);           // per-line firing from here down
        }
        return;
    }

    // Band boundary - time critical (written during HBlank).
    *((vu32*) VDP_CTRL_PORT) = 0xC0000000UL | ((u32) (SKY_CRAM_INDEX * 2) << 16);
    *((vu16*) VDP_DATA_PORT) = skyRuns[skyRunIndex].color;

    if (skySkipPending) skySkipPending = FALSE;

    const u16 span = skyRunSpan;
    if (skyLinesLeft)
    {
        u16 nextSpan;

        skyRunIndex++;
        nextSpan = skyRuns[skyRunIndex].span;
        if (nextSpan > skyLinesLeft) nextSpan = skyLinesLeft;
        skyRunSpan = nextSpan;
        skyLinesLeft -= nextSpan;
        VDP_setHIntCounter((u8) (span - 1));
        return;
    }

    if (skyResetLine)
    {
        skyResetPending = TRUE;
        VDP_setHIntCounter((u8) (span - 1));
        return;
    }

    // CRITICAL: restore the fixed skip value so the counter is NEVER left at 0
    // going into VBlank. This stops the next frame's first HINT from firing at
    // the top of screen.
    VDP_setHIntCounter((u8) (SKY_HINT_START - 1));
    VDP_setHInterrupt(FALSE);
}

void sky_init(void)
{
    for (u16 i = 0; i < SKY_RAMP_SIZE; i++)
        ramp[i] = keyframeColor((s16) SKY_H0 - (s16) i);
    buildSkyRuns();

    horizonY = 0;
    skyBandStart = 0;
    skyRunIndex = 0;
    skyRunSpan = 1;
    skyLinesLeft = 1;
    skySkipLines = 0;
    skySkipPending = FALSE;
    skyResetLine = FALSE;
    skyResetPending = FALSE;
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
        skyResetPending = FALSE;
        *((vu32*) VDP_CTRL_PORT) = 0xC0000000UL | ((u32) (SKY_CRAM_INDEX * 2) << 16);
        *((vu16*) VDP_DATA_PORT) = ramp[SKY_H0];
        return;
    }

    s16 h = horizonY;
    if (h < 0) h = 0;
    if (h > SKY_H0) h = SKY_H0;

    const u16 start = skyBandStart;
    s16 bottom = h;
    if (bottom > (s16) (SKY_SCREEN_H_MAX - 1))
        bottom = (s16) (SKY_SCREEN_H_MAX - 1);
    const u16 startIndex = (u16) (SKY_H0 - h + start);

    // Flat top (lines 0..band top) is held by this single CRAM write; the HINT
    // does not fire at all until the fixed SKY_HINT_START line.
    *((vu32*) VDP_CTRL_PORT) = 0xC0000000UL | ((u32) (SKY_CRAM_INDEX * 2) << 16);
    *((vu16*) VDP_DATA_PORT) = ramp[0];

    selectSkyRuns(startIndex, (u16) (bottom - (s16) start + 1));
    // Variable gap, software-counted from the FIXED line to the band top.
    // start >= SKY_HINT_START in all legal horizon positions (band top min ~40).
    skySkipLines = (start > SKY_HINT_START) ? (u16) (start - SKY_HINT_START) : 0;
    skyResetLine = (((u16) bottom + 1) < SKY_SCREEN_H_MAX) ? TRUE : FALSE;
    skyResetPending = FALSE;
    skySkipPending = TRUE;

    // Constant skip to the fixed first-HINT line. Constant => immune to the
    // per-frame counter-reload timing that broke the variable version.
    VDP_setHIntCounter((u8) (SKY_HINT_START - 1));
    VDP_setHInterrupt(TRUE);
}
