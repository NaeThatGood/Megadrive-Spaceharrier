#ifndef ENGINE_HUD_H
#define ENGINE_HUD_H

#include <genesis.h>

void HUD_init(void);

// Cheap debug overlay; internally rate-limited to every 8 frames.
void HUD_update(const char* modeName, u16 (*countObjects)(void), u16 hits,
                u16 enemySpeedPct);

#endif
