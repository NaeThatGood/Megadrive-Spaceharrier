#include <genesis.h>
#include "resources.h"
#include "../../engine/world.h"

// Runtime-scaling renderer.
// Interim stub: same fixed sprite as the stored renderer. Replaced by the
// 68000 software scaler in the runtime-scaling milestone.

static void rt_init(void)
{
    PAL_setPalette(PAL2, spr_enemy16.palette->data, CPU);
}

static void rt_spawn(WObj* o)
{
    o->spr = SPR_addSprite(&spr_enemy16, -64, -64,
                           TILE_ATTR(PAL2, 0, FALSE, FALSE));
    o->sizeIdx = 0;
}

static void rt_update(WObj* o, s16 sx, s16 syBottom, u16 sizePx)
{
    (void) sizePx;
    SPR_setPosition(o->spr, sx - 8, syBottom - 16);
}

static void rt_despawn(WObj* o)
{
    if (o->spr)
    {
        SPR_releaseSprite(o->spr);
        o->spr = NULL;
    }
}

const Renderer RENDER_runtime =
{
    "RUNTIME", rt_init, rt_spawn, rt_update, rt_despawn
};
