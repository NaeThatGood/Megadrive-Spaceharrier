#include "mountains.h"

#include "resources.h"

#define MTN_TILE_INDEX        (TILE_USER_INDEX + img_ground.tileset->numTile)
#define MTN_PAL              PAL0
#define MTN_PAL_FIRST_COLOR  1
#define MTN_PAL_COLOR_COUNT  5

#define MTN_PLANE_W_TILES    64
#define MTN_STRIP_W_TILES    64
#define MTN_STRIP_H_TILES    12
#define MTN_STRIP_H          96
#define MTN_TREE_START       64
#define MTN_TREE_H           (MTN_STRIP_H - MTN_TREE_START)
#define MAX_LINES            240

static s16 lineScroll[MAX_LINES];
static u16 screenH;
static s16 prevStartY;
static s16 prevEndY;
static s16 lastSwayX;
static s16 lastHorizonY;
static bool lineScrollValid;

static s16 clampLine(s16 y)
{
    if (y < 0) return 0;
    if (y > (s16) screenH) return (s16) screenH;
    return y;
}

static void clearLineRange(s16 startY, s16 endY)
{
    startY = clampLine(startY);
    endY = clampLine(endY);

    for (s16 y = startY; y < endY; y++)
        lineScroll[y] = 0;
}

void MOUNTAINS_init(void)
{
    screenH = VDP_getScreenHeight();
    if (screenH > MAX_LINES) screenH = MAX_LINES;

    PAL_setColors(MTN_PAL_FIRST_COLOR,
                  img_mountains.palette->data + MTN_PAL_FIRST_COLOR,
                  MTN_PAL_COLOR_COUNT,
                  CPU);

    VDP_clearPlane(BG_A, TRUE);
    VDP_loadTileSet(img_mountains.tileset, MTN_TILE_INDEX, DMA);
    VDP_setTileMapEx(BG_A, img_mountains.tilemap,
                     TILE_ATTR_FULL(MTN_PAL, FALSE, FALSE, FALSE, MTN_TILE_INDEX),
                     0, 0, 0, 0,
                     MTN_STRIP_W_TILES, MTN_STRIP_H_TILES,
                     CPU);

    memset(lineScroll, 0, sizeof(lineScroll));
    if (screenH > 0)
        VDP_setHorizontalScrollLine(BG_A, 0, lineScroll, screenH, DMA_QUEUE);

    prevStartY = 0;
    prevEndY = 0;
    lastSwayX = 0;
    lastHorizonY = 0;
    lineScrollValid = FALSE;
}

void MOUNTAINS_update(s16 swayX, s16 horizonY)
{
    const s16 stripStartY = horizonY - MTN_STRIP_H;
    const s16 treeStartY = horizonY - MTN_TREE_H;
    const s16 stripEndY = horizonY;
    const s16 visibleStartY = clampLine(stripStartY);
    const s16 visibleEndY = clampLine(stripEndY);

    VDP_setVerticalScroll(BG_A, MTN_STRIP_H - horizonY);

    if (lineScrollValid && swayX == lastSwayX && horizonY == lastHorizonY)
        return;

    s16 uploadStartY = visibleStartY;
    s16 uploadEndY = visibleEndY;

    if (lineScrollValid)
    {
        if (prevStartY < uploadStartY) uploadStartY = prevStartY;
        if (prevEndY > uploadEndY) uploadEndY = prevEndY;
        clearLineRange(prevStartY, prevEndY);
    }

    clearLineRange(visibleStartY, visibleEndY);

    const s16 mtnScroll = -(swayX / 8);
    const s16 treeScroll = -(swayX / 4);
    const s16 visibleTreeStartY = clampLine(treeStartY);

    for (s16 y = visibleStartY; y < visibleTreeStartY && y < visibleEndY; y++)
        lineScroll[y] = mtnScroll;

    for (s16 y = visibleTreeStartY; y < visibleEndY; y++)
        lineScroll[y] = treeScroll;

    uploadStartY = clampLine(uploadStartY);
    uploadEndY = clampLine(uploadEndY);

    if (uploadStartY < uploadEndY)
    {
        VDP_setHorizontalScrollLine(BG_A, uploadStartY,
                                    &lineScroll[uploadStartY],
                                    uploadEndY - uploadStartY,
                                    DMA_QUEUE);
    }

    prevStartY = visibleStartY;
    prevEndY = visibleEndY;
    lastSwayX = swayX;
    lastHorizonY = horizonY;
    lineScrollValid = TRUE;
}
