#include <genesis.h>
#include "resources.h"
#include "../../engine/world.h"

// ---------------------------------------------------------------------------
// Runtime-scaling renderer: 68000 software sprite scaling.
//
// Source: 64x64 master sprite stored in ROM as packed 4bpp (2 px/byte,
// high nibble = left pixel), row-major (enemy_src.bin).
//
// Each object owns a fixed VRAM slot of 64 tiles arranged as a 2x2 grid of
// 32x32 hardware sprites ("quads", via the spr_quad32 shell whose ROM tiles
// are never uploaded). The scaler nearest-neighbour resamples the source to
// any size 8..64 px, writing directly in VDP tile layout (column-major tiles,
// 2 px/byte) into a RAM buffer which is then DMA-queued to the object's slot.
//
// CPU budget: at most ONE object is rescaled per frame (the one with the
// largest visual error, biased towards near/large objects). Other objects
// keep their cached size until their turn - "semi-real-time" scaling.
// ---------------------------------------------------------------------------

#define SRC_SIZE        64
#define SRC_STRIDE      (SRC_SIZE / 2)

#define RT_MIN_SIZE     8
#define RT_MAX_SIZE     64

#define SLOT_TILES      RT_SLOT_TILES
#define SLOT_BYTES      (SLOT_TILES * 32)
#define QUAD_BYTES      (16 * 32)

// scale buffer: one object slot worth of tile data
static u8 scaleBuf[SLOT_BYTES];

// per-rescale lookup tables: dest x -> source byte offset / shift
static u8 srcOff[RT_MAX_SIZE];
static u8 srcShift[RT_MAX_SIZE];

static u16 vramBase;
static u8  rtMaxSlots;                      // limited by user VRAM below sprite pool
static u8  slotsUsed;                       // bitmask, rtMaxSlots <= MAX_OBJECTS

// rescale candidate for this frame
static WObj* candObj;
static u16   candScore;
static u8    candSize;

#define RT_NO_SLOT  0xFF

static u8 allocSlot(void)
{
    for (u8 i = 0; i < rtMaxSlots; i++)
    {
        if (!(slotsUsed & (1 << i)))
        {
            slotsUsed |= (1 << i);
            return i;
        }
    }
    return RT_NO_SLOT;
}

static void freeSlot(WObj* o)
{
    if (o->vramIndex < vramBase) return;

    const u16 rel = o->vramIndex - vramBase;
    if (rel >= (u16) (rtMaxSlots * SLOT_TILES)) return;

    slotsUsed &= ~(1 << (rel / SLOT_TILES));
}

u8 RUNTIME_slotCapacity(void)
{
    return rtMaxSlots;
}

u16 RUNTIME_spriteVramBudget(void)
{
    const u16 rtEnd = TILE_USER_INDEX + img_ground.tileset->numTile
                    + RT_RESERVED_TILES - 1;

    if (rtEnd >= TILE_MAX_NUM) return 420;

    u16 sprVram = (u16) (TILE_MAX_NUM - rtEnd - 1);
    if (sprVram < 256) sprVram = 256;
    if (sprVram > 420) sprVram = 420;
    return sprVram;
}

// --- The scaler -------------------------------------------------------------

// Extract one dest pixel pair from the source row via the column tables.
#define PAIR(srow, x) \
    (u8) ((((srow[srcOff[x]] >> srcShift[x]) & 0xF) << 4) | \
          ((srow[srcOff[(x) + 1]] >> srcShift[(x) + 1]) & 0xF))

static void scaleInto(const u8* src, u8 size, u16 clearBytes)
{
    // dest x -> source pixel mapping (one multiply per column, here only)
    const u16 step = (SRC_SIZE << 8) / size;
    {
        u16 acc = step >> 1;
        for (u8 x = 0; x < size; x++)
        {
            const u8 sx = acc >> 8;
            srcOff[x] = sx >> 1;
            srcShift[x] = (sx & 1) ? 0 : 4;
            acc += step;
        }
    }

    // clear stale data (only the region that will be uploaded)
    memsetU32((u32*) scaleBuf, 0, clearBytes / 4);

    // Dest layout maths: byte offset of pixel (x, y) decomposes into
    //   quadCol(x>>5)*512 + tileCol((x>>3)&3)*128  -> uniformly +128 per
    //                                                  8px column (4*128=512)
    //   quadRow(y>>5)*1024 + tileRow((y>>3)&3)*32 + (y&7)*4
    const u8 fullCols = size >> 3;     // whole 8-px tile columns
    const u8 tail = size & 7;          // remaining pixels

    u16 accY = step >> 1;
    for (u8 y = 0; y < size; y++)
    {
        const u8* srow = src + (u16) (accY >> 8) * SRC_STRIDE;
        accY += step;

        u8* dst = scaleBuf
            + ((y & 32) ? 2 * QUAD_BYTES : 0)
            + (((y >> 3) & 3) << 5)
            + ((y & 7) << 2);

        u8 x = 0;
        for (u8 c = 0; c < fullCols; c++)
        {
            // one tile line: 8 pixels = 4 bytes
            dst[0] = PAIR(srow, x);
            dst[1] = PAIR(srow, x + 2);
            dst[2] = PAIR(srow, x + 4);
            dst[3] = PAIR(srow, x + 6);
            x += 8;
            dst += 4 * 32;   // next tile column (also valid across quads)
        }
        for (u8 t = 0; t < tail; t += 2)
        {
            u8 p0 = (srow[srcOff[x + t]] >> srcShift[x + t]) & 0xF;
            u8 p1 = 0;
            if ((u8) (t + 1) < tail)
                p1 = (srow[srcOff[x + t + 1]] >> srcShift[x + t + 1]) & 0xF;
            dst[t >> 1] = (u8) ((p0 << 4) | p1);
        }
    }
}

static void rescaleObject(WObj* o, u8 size)
{
    if (!o->sprs[0]) return;

    // upload: 1 quad when <= 32 px, all 4 otherwise
    const u16 bytes = (size <= 32) ? QUAD_BYTES : SLOT_BYTES;

    scaleInto((const u8*) enemy_src_4bpp, size, bytes);
    o->sizeIdx = size;

    const u32 vramAddr = (u32) (o->vramIndex) * 32;
    const u32 vramEnd = vramAddr + bytes - 1;
    if (vramEnd > (u32) TILE_USER_MAX_INDEX * 32) return;

    DMA_queueDma(DMA_VRAM, scaleBuf, vramAddr, bytes / 2, 2);
}

// --- Renderer interface ------------------------------------------------------

static void rt_init(void)
{
    PAL_setPalette(PAL2, spr_enemy_scaled.palette->data, DMA_QUEUE);

    // Slots sit immediately after the ground tileset so DMA never clobbers
    // floor tiles. Boot calls SPR_initEx(RUNTIME_spriteVramBudget()) so the
    // sprite pool starts just above the last slot (no crash, no banding).
    vramBase = TILE_USER_INDEX + img_ground.tileset->numTile;

    const u16 slotsEnd = vramBase + RT_RESERVED_TILES - 1;
    if (slotsEnd <= TILE_USER_MAX_INDEX)
        rtMaxSlots = MAX_OBJECTS;
    else if (vramBase > TILE_USER_MAX_INDEX)
        rtMaxSlots = 0;
    else
        rtMaxSlots = (u8) ((TILE_USER_MAX_INDEX + 1 - vramBase) / SLOT_TILES);

    slotsUsed = 0;
    candObj = NULL;
    candScore = 0;
}

static void rt_spawn(WObj* o)
{
    const u8 slot = allocSlot();
    if (slot == RT_NO_SLOT)
    {
        o->vramIndex = 0;
        o->sizeIdx = 0;
        for (u8 q = 0; q < 4; q++) o->sprs[q] = NULL;
        return;
    }

    o->vramIndex = vramBase + slot * SLOT_TILES;
    o->sizeIdx = 0;   // forces a rescale as soon as possible

    for (u8 q = 0; q < 4; q++)
    {
        o->sprs[q] = SPR_addSpriteEx(&spr_quad32, -64, -64,
            TILE_ATTR_FULL(PAL2, FALSE, FALSE, FALSE,
                           o->vramIndex + q * 16),
            0);
        if (!o->sprs[q])
        {
            for (u8 r = 0; r < q; r++)
            {
                if (o->sprs[r]) SPR_releaseSprite(o->sprs[r]);
                o->sprs[r] = NULL;
            }
            freeSlot(o);
            o->vramIndex = 0;
            return;
        }
        SPR_setVisibility(o->sprs[q], HIDDEN);
    }
}

static void rt_update(WObj* o, s16 sx, s16 syBottom, u16 sizePx)
{
    if (!o->sprs[0]) return;

    u8 target = (sizePx < RT_MIN_SIZE) ? RT_MIN_SIZE
              : (sizePx > RT_MAX_SIZE) ? RT_MAX_SIZE
              : (u8) sizePx;

    // visual error -> rescale candidate (biggest error first, near objects
    // weighted up since an error on a large sprite is more visible)
    const u8 cached = o->sizeIdx;
    const u8 err = (cached > target) ? (cached - target) : (target - cached);
    if (err > 0)
    {
        const u16 score = ((u16) err << 2) + (target >> 3);
        if (candObj == NULL || score > candScore)
        {
            candObj = o;
            candScore = score;
            candSize = target;
        }
    }

    if (cached == 0)
    {
        // nothing valid in VRAM yet: keep hidden until first rescale
        for (u8 q = 0; q < 4; q++) SPR_setVisibility(o->sprs[q], HIDDEN);
        return;
    }

    // position quads using the cached size (that's what is in VRAM)
    const s16 bx = sx - (cached >> 1);
    const s16 by = syBottom - cached;
    const bool big = (cached > 32);

    // quad order matches the scale buffer layout: TL, TR, BL, BR
    SPR_setPosition(o->sprs[0], bx, by);
    SPR_setVisibility(o->sprs[0], VISIBLE);
    if (big)
    {
        SPR_setPosition(o->sprs[1], bx + 32, by);
        SPR_setPosition(o->sprs[2], bx, by + 32);
        SPR_setPosition(o->sprs[3], bx + 32, by + 32);
        SPR_setVisibility(o->sprs[1], VISIBLE);
        SPR_setVisibility(o->sprs[2], VISIBLE);
        SPR_setVisibility(o->sprs[3], VISIBLE);
    }
    else
    {
        SPR_setVisibility(o->sprs[1], HIDDEN);
        SPR_setVisibility(o->sprs[2], HIDDEN);
        SPR_setVisibility(o->sprs[3], HIDDEN);
    }
}

static void rt_despawn(WObj* o)
{
    for (u8 q = 0; q < 4; q++)
    {
        if (o->sprs[q])
        {
            SPR_releaseSprite(o->sprs[q]);
            o->sprs[q] = NULL;
        }
    }
    freeSlot(o);
    o->vramIndex = 0;
    if (candObj == o) candObj = NULL;
}

static void rt_frame(void)
{
    if (!candObj || !candObj->active || !candObj->sprs[0])
    {
        candObj = NULL;
        candScore = 0;
        return;
    }

    rescaleObject(candObj, candSize);
    candObj = NULL;
    candScore = 0;
}

const Renderer RENDER_runtime =
{
    "RUNTIME", rt_init, rt_spawn, rt_update, rt_despawn, rt_frame
};
