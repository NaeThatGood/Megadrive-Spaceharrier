#ifndef ENGINE_MOUNTAINS_H
#define ENGINE_MOUNTAINS_H

#include <genesis.h>

void MOUNTAINS_init(void);
void MOUNTAINS_setColors(const u16* colors6);
void MOUNTAINS_update(s16 swayX, s16 horizonY);

#endif
