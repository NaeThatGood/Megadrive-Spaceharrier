#ifndef ENGINE_VRAM_LAYOUT_H
#define ENGINE_VRAM_LAYOUT_H

#include <genesis.h>
#include "resources.h"

// Static background tiles are loaded before the sprite engine reserves its pool.
// Keep this chain in one place so sky and mountains cannot overlap.
#define VRAM_GROUND_TILE_INDEX     TILE_USER_INDEX
#define VRAM_MOUNTAIN_TILE_INDEX   (VRAM_GROUND_TILE_INDEX + img_ground.tileset->numTile)
#define VRAM_SKY_TILE_INDEX        (VRAM_MOUNTAIN_TILE_INDEX + img_mountains.tileset->numTile)
#define VRAM_SKY_UP35_TILE_INDEX   (VRAM_SKY_TILE_INDEX + img_sky.tileset->numTile)
#define VRAM_STATIC_TILE_END       (VRAM_SKY_UP35_TILE_INDEX + img_sky_up35.tileset->numTile)

#endif
