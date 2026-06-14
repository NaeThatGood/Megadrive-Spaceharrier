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
void sky_setHorizon(s16 y);
void sky_vblank(void);

#endif
