#ifndef ENGINE_WORLD_H
#define ENGINE_WORLD_H

#include <genesis.h>
#include "ground.h"

// ---------------------------------------------------------------------------
// Pseudo-3D world: objects live at (wx, wy, z).
//   z:  depth, WORLD_Z_NEAR (at the player) .. WORLD_Z_FAR (vanishing point)
//   wx: lateral position in "screen pixels at z = WORLD_Z_NEAR" units
//   wy: height above the ground plane, same units (0 = on the ground)
//
// Projection (vanishing point at (160, GROUND_HORIZON)):
//   screenX       = 160 + wx * Z_NEAR / z
//   screenYBottom = HORIZON + (GROUND_DEPTH - wy) * Z_NEAR / z
//   spritePixels  = baseSize * Z_NEAR / z
// ---------------------------------------------------------------------------

#define WORLD_Z_NEAR    256
#define WORLD_Z_FAR     4096

// Ground plane depth offset: bottom of a ground object at z = Z_NEAR
// sits at HORIZON + GROUND_DEPTH pixels.
#define WORLD_GROUND_DEPTH  110

#define MAX_OBJECTS     8
#define MAX_SHOTS       4

// Object base size in pixels when at z = WORLD_Z_NEAR
#define OBJ_BASE_SIZE   64

typedef struct WObj
{
    bool    active;
    s16     wx;
    s16     wy;
    u16     z;
    s16     vx;
    u16     vz;         // approach speed (subtracted from z each frame)
    u8      sizeIdx;    // current scale bucket (renderer bookkeeping)
    Sprite* spr;        // sprite-engine handle (renderer bookkeeping)
    u16     vramIndex;  // VRAM tile slot (runtime renderer bookkeeping)
} WObj;

typedef struct
{
    bool active;
    s16  wx;
    s16  wy;
    u16  z;
    Sprite* spr;
} WShot;

static inline s16 WORLD_screenX(s16 wx, u16 z)
{
    return 160 + (s16) (((s32) wx * WORLD_Z_NEAR) / z);
}

static inline s16 WORLD_screenYBottom(s16 wy, u16 z)
{
    return GROUND_HORIZON +
        (s16) (((s32) (WORLD_GROUND_DEPTH - wy) * WORLD_Z_NEAR) / z);
}

static inline u16 WORLD_sizePx(u16 z)
{
    return (u16) (((u32) OBJ_BASE_SIZE * WORLD_Z_NEAR) / z);
}

// --- Renderer interface: implemented by each prototype ----------------------

typedef struct
{
    const char* name;                // shown on the debug HUD
    void (*init)(void);              // mode entered
    void (*spawn)(WObj* o);          // object appeared
    void (*update)(WObj* o, s16 sx, s16 syBottom, u16 sizePx);
    void (*despawn)(WObj* o);        // object removed
} Renderer;

extern const Renderer RENDER_stored;
extern const Renderer RENDER_runtime;

#endif
