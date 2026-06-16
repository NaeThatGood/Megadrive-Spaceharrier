#include "scenepal.h"

#include "ground.h"
#include "mountains.h"

#define MTN_SCHEME_COLORS  6
#define FADE_KEYFRAMES    6
#define FADE_HOLD         6

typedef struct
{
    u16 checkerLight;
    u16 checkerDark;
    u16 mtn[MTN_SCHEME_COLORS];
} SceneScheme;

static const SceneScheme SCENE_SCHEMES[] =
{
    {   // 0: original blue checker and green mountain strip
        RGB24_TO_VDPCOLOR(0x6098D8), RGB24_TO_VDPCOLOR(0x204090),
        { RGB24_TO_VDPCOLOR(0x005800), RGB24_TO_VDPCOLOR(0x00A800),
          RGB24_TO_VDPCOLOR(0x28E028), RGB24_TO_VDPCOLOR(0x204090),
          RGB24_TO_VDPCOLOR(0x009000), RGB24_TO_VDPCOLOR(0x00D000) },
    },
    {   // 1: sand / beige desert
        RGB24_TO_VDPCOLOR(0xE8D0A0), RGB24_TO_VDPCOLOR(0xA88048),
        { RGB24_TO_VDPCOLOR(0x604018), RGB24_TO_VDPCOLOR(0xA87830),
          RGB24_TO_VDPCOLOR(0xD8B868), RGB24_TO_VDPCOLOR(0x806840),
          RGB24_TO_VDPCOLOR(0x906828), RGB24_TO_VDPCOLOR(0xC09848) },
    },
};
#define SCENE_SCHEME_COUNT  (sizeof(SCENE_SCHEMES) / sizeof(SCENE_SCHEMES[0]))

static u16 curLight;
static u16 curDark;
static u16 curMtn[MTN_SCHEME_COLORS];

static u16 kfLight[FADE_KEYFRAMES];
static u16 kfDark[FADE_KEYFRAMES];
static u16 kfMtn[FADE_KEYFRAMES][MTN_SCHEME_COLORS];
static u16 fadeIdx;
static u16 holdCount;
static bool fading;
static u16 schemeIndex;

static u16 lerpColorN(u16 a, u16 b, u16 num, u16 den)
{
    u16 out = 0;

    for (u16 shift = 1; shift <= 9; shift += 4)
    {
        const s16 av = (a >> shift) & 7;
        const s16 bv = (b >> shift) & 7;
        out |= (u16) (av + (((bv - av) * (s16) num) / (s16) den)) << shift;
    }

    return out;
}

u16 SCENEPAL_schemeCount(void)
{
    return (u16) SCENE_SCHEME_COUNT;
}

void SCENEPAL_init(void)
{
    const SceneScheme* scheme = &SCENE_SCHEMES[0];

    schemeIndex = 0;
    curLight = scheme->checkerLight;
    curDark = scheme->checkerDark;

    for (u16 i = 0; i < MTN_SCHEME_COLORS; i++)
        curMtn[i] = scheme->mtn[i];

    GROUND_setCheckerColors(curLight, curDark);
    GROUND_setSkyScrollOffset(0);
    MOUNTAINS_setColors(curMtn);

    fadeIdx = 0;
    holdCount = 0;
    fading = FALSE;
}

void SCENEPAL_setScheme(u16 index)
{
    schemeIndex = index % (u16) SCENE_SCHEME_COUNT;
    const SceneScheme* target = &SCENE_SCHEMES[schemeIndex];

    GROUND_setSkyScrollOffset(schemeIndex ? GROUND_SKY_SCROLL_UP_PX : 0);

    for (u16 k = 0; k < FADE_KEYFRAMES; k++)
    {
        const u16 num = k + 1;

        kfLight[k] = lerpColorN(curLight, target->checkerLight, num, FADE_KEYFRAMES);
        kfDark[k] = lerpColorN(curDark, target->checkerDark, num, FADE_KEYFRAMES);

        for (u16 i = 0; i < MTN_SCHEME_COLORS; i++)
            kfMtn[k][i] = lerpColorN(curMtn[i], target->mtn[i], num, FADE_KEYFRAMES);
    }

    fadeIdx = 0;
    holdCount = 0;
    fading = TRUE;
}

void SCENEPAL_cycle(void)
{
    SCENEPAL_setScheme(schemeIndex + 1);
}

void SCENEPAL_update(void)
{
    if (!fading) return;

    if (holdCount > 0)
    {
        holdCount--;
        return;
    }

    curLight = kfLight[fadeIdx];
    curDark = kfDark[fadeIdx];
    GROUND_setCheckerColors(curLight, curDark);

    bool mtnChanged = FALSE;
    for (u16 i = 0; i < MTN_SCHEME_COLORS; i++)
    {
        const u16 next = kfMtn[fadeIdx][i];
        if (curMtn[i] != next)
        {
            curMtn[i] = next;
            mtnChanged = TRUE;
        }
    }

    if (mtnChanged)
        MOUNTAINS_setColors(curMtn);

    holdCount = FADE_HOLD - 1;
    fadeIdx++;
    if (fadeIdx >= FADE_KEYFRAMES)
        fading = FALSE;
}
