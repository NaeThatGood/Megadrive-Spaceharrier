#ifndef ENGINE_GROUND_H
#define ENGINE_GROUND_H

#include <genesis.h>

// First scanline of the ground area (must match tools/gen_assets.py)
#define GROUND_HORIZON  96

void GROUND_init(void);

// swayX: lateral offset in pixels (positive = world moves left, i.e. player
//        moved right). speed: forward speed 0..8 controlling checker cycling.
void GROUND_update(s16 swayX, u16 speed);

#endif
