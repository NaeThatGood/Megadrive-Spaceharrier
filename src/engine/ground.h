#ifndef ENGINE_GROUND_H
#define ENGINE_GROUND_H

#include <genesis.h>

// On-screen horizon scanline (fixed).  Vertical pitch is done by V-scrolling
// a taller board bitmap; world projection uses GROUND_horizon (set each frame).
#define GROUND_HORIZON  96

// The checker art intentionally leaves the first few rows below the projection
// horizon transparent to avoid near-horizon moire. Use this when anchoring
// screen-space colour effects to the first visible ground row.
#define GROUND_VISIBLE_HORIZON_PAD  14

// Current on-screen horizon scanline; used by the world projection so
// sprites stay glued to the floor as the horizon moves.
extern s16 GROUND_horizon;

void GROUND_init(void);

// swayX:   lateral offset in pixels (positive = world moves left, i.e.
//          player moved right)
// pitchY:  vertical offset from mid-travel (positive = player high on screen)
// vanishX: player world X for vanishing-point tracking
// speed:   forward speed driving the checker palette animation (use
//          GROUND_FORWARD_SPEED for arcade-like baseline)
void GROUND_update(s16 swayX, s16 pitchY, s16 vanishX, u16 speed);

#endif
