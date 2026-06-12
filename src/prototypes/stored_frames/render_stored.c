#include <genesis.h>
#include "resources.h"
#include "../../engine/world.h"

// Stored-frame renderer.
// Interim version: a single 16x16 sprite for all distances. The next
// milestone swaps this for a strip of pre-rendered scale frames picked
// by Z-derived size bucket.

static void st_init(void)
{
    PAL_setPalette(PAL2, spr_enemy16.palette->data, CPU);
}

static void st_spawn(WObj* o)
{
    o->spr = SPR_addSprite(&spr_enemy16, -64, -64,
                           TILE_ATTR(PAL2, 0, FALSE, FALSE));
    o->sizeIdx = 0;
}

static void st_update(WObj* o, s16 sx, s16 syBottom, u16 sizePx)
{
    (void) sizePx;
    SPR_setPosition(o->spr, sx - 8, syBottom - 16);
}

static void st_despawn(WObj* o)
{
    if (o->spr)
    {
        SPR_releaseSprite(o->spr);
        o->spr = NULL;
    }
}

const Renderer RENDER_stored =
{
    "STORED ", st_init, st_spawn, st_update, st_despawn
};
