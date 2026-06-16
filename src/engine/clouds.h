#ifndef ENGINE_CLOUDS_H
#define ENGINE_CLOUDS_H

#include <genesis.h>

void CLOUDS_init(void);
void CLOUDS_release(void);
void CLOUDS_applyPalette(void);
void CLOUDS_update(s16 horizonY);
void CLOUDS_evict(void);
void CLOUDS_resume(void);
bool CLOUDS_areOffscreen(void);

#endif
