#ifndef ENGINE_SKY_H
#define ENGINE_SKY_H

#include <genesis.h>

// CRAM entry rewritten by the per-line HINT. The sky is visible because the
// ground image leaves rows above the horizon as palette index 0/backdrop.
#define SKY_CRAM_INDEX    0

#define SKY_SCREEN_H_MAX  240
#define SKY_H0            240
#define SKY_TRANSITION    40
#define SKY_RAMP_SIZE     (SKY_H0 + SKY_SCREEN_H_MAX)

// Stable hardware timing offset between the programmed band bottom and the
// visible horizon. Keep this fixed: it trims the remaining constant gap without
// reintroducing horizon-dependent HINT counter arming.
#define SKY_BOTTOM_ADJUST 36

// Fixed scanline of the first HINT each frame. The gradient band top can
// never rise above ~line 40 (sky horizon min 80 - SKY_TRANSITION 40), so
// we skip all scanlines above this with ZERO interrupts. 4-line margin
// below 40 absorbs the classic MD HINT off-by-one. Lower it for more
// safety margin (tiny extra cost), raise it to skip more (must stay
// <= band-top minimum or the gradient top gets clipped).
#define SKY_HINT_START    36

typedef struct
{
    u8 d;                 // scanlines above the horizon
    u8 r;
    u8 g;
    u8 b;
} SkyKeyframe;

#define SKY_KEYFRAME_COUNT  5

extern const SkyKeyframe SKY_KEYFRAMES[SKY_KEYFRAME_COUNT];

void sky_init(void);
void sky_setEnabled(bool enabled);
bool sky_isEnabled(void);
void sky_setHorizon(s16 y);
void sky_vblank(void);

#endif
