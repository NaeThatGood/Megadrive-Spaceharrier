#ifndef ENGINE_SHADOW_H
#define ENGINE_SHADOW_H

#include <genesis.h>

void    SHADOW_init(void);
void    SHADOW_beginFrame(void);
Sprite* SHADOW_add(void);
void    SHADOW_release(Sprite* s);
void    SHADOW_place(Sprite* s, s16 wx, u16 z, u16 casterSizePx);
void    SHADOW_hide(Sprite* s);

#endif
