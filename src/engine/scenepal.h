#ifndef ENGINE_SCENEPAL_H
#define ENGINE_SCENEPAL_H

#include <genesis.h>

u16 SCENEPAL_schemeCount(void);
void SCENEPAL_init(void);
void SCENEPAL_cycle(void);
void SCENEPAL_setScheme(u16 index);
void SCENEPAL_update(void);

#endif
