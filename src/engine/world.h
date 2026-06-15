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
// Projection (vanishing point at (160, GROUND_horizon)):
//   screenX       = 160 + wx * Z_NEAR / z
//   screenYBottom = GROUND_horizon + (GROUND_DEPTH - wy) * Z_NEAR / z
//   spritePixels  = baseSize * Z_NEAR / z
//
// GROUND_horizon is a runtime value: the ground renderer moves the horizon
// with player altitude (SH2-style V-scroll), and the projection follows so
// objects stay planted on the floor.
// ---------------------------------------------------------------------------

#define WORLD_Z_NEAR    256
#define WORLD_Z_FAR     4096

extern const u16 WORLD_projLUT[];

// World motion distances are per 60 Hz display frame. The main loop multiplies
// them by its renderer cadence so 30 Hz runtime updates keep the same wall speed.
#define GROUND_FORWARD_SPEED    11
#define WORLD_APPROACH_VZ_BASE  48
#define WORLD_APPROACH_VZ_RAND  26      // vz in [BASE, BASE + RAND]
#define WORLD_SHOT_SPEED        220
#define WORLD_SPAWN_INTERVAL    20

// Ground plane depth offset: bottom of a ground object at z = Z_NEAR
// sits at GROUND_horizon + GROUND_DEPTH pixels.
#define WORLD_GROUND_DEPTH  110

#define MAX_OBJECTS     3
#define MAX_SHOTS       4

// Runtime renderer: each object owns this many VRAM tiles (see render_runtime.c).
#define RT_SLOT_TILES       64
#define RT_RESERVED_TILES   (MAX_OBJECTS * RT_SLOT_TILES)

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
    u16     stepVz;     // vz scaled by current enemy speed percentage
    u8      sizeIdx;    // current frame bucket / cached size (renderer data)
    Sprite* sprs[4];    // sprite-engine handles (renderer bookkeeping)
    Sprite* shadow;     // ground contact shadow (owned by object lifetime)
    u16     vramIndex;  // VRAM tile slot (runtime renderer bookkeeping)
} WObj;

typedef struct
{
    bool active;
    s16  wx;
    s16  wy;
    u16  z;
    Sprite* spr;
    Sprite* shadow;
} WShot;

static inline s16 WORLD_screenX(s16 wx, u16 z)
{
    return 160 + (s16) (((s32) wx * WORLD_Z_NEAR) / z);
}

static inline u16 WORLD_proj(u16 z)
{
    return WORLD_projLUT[z];
}

static inline s16 WORLD_screenXq(s16 wx, u16 q)
{
    return 160 + (s16) (((s32) wx * q) >> 8);
}

static inline s16 WORLD_screenYBottom(s16 wy, u16 z)
{
    return GROUND_horizon +
        (s16) (((s32) (WORLD_GROUND_DEPTH - wy) * WORLD_Z_NEAR) / z);
}

static inline s16 WORLD_screenYBq(s16 wy, u16 q)
{
    return GROUND_horizon +
        (s16) (((s32) (WORLD_GROUND_DEPTH - wy) * q) >> 8);
}

static inline u16 WORLD_sizePx(u16 z)
{
    return (u16) (((u32) OBJ_BASE_SIZE * WORLD_Z_NEAR) / z);
}

static inline u16 WORLD_sizePxq(u16 q)
{
    return q >> 2;
}

// --- Renderer interface: implemented by each prototype ----------------------

typedef struct
{
    const char* name;                // shown on the debug HUD
    void (*init)(void);              // mode entered
    void (*spawn)(WObj* o);          // object appeared
    void (*update)(WObj* o, s16 sx, s16 syBottom, u16 sizePx);
    void (*despawn)(WObj* o);        // object removed
    void (*frame)(void);             // once per frame, after all updates (may be NULL)
} Renderer;

extern const Renderer RENDER_stored;
extern const Renderer RENDER_runtime;

// Valid after RENDER_runtime.init(); may be < MAX_OBJECTS when ground tiles
// leave little user VRAM below the sprite-engine pool.
u8 RUNTIME_slotCapacity(void);

// Call after GROUND_init() to size the sprite-engine pool so runtime object
// slots can live after the ground tileset without overlapping it or crashing.
u16 RUNTIME_spriteVramBudget(void);

#endif
