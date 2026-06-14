#include "hud.h"

static u16 frameCnt;

void HUD_init(void)
{
    VDP_setWindowOnTop(2);
    VDP_setTextPlane(WINDOW);
    VDP_setTextPalette(PAL0);
    VDP_setTextPriority(TRUE);

    // Text uses PAL0 index 15: force it to white (ground palette leaves it free)
    PAL_setColor(15, RGB24_TO_VDPCOLOR(0xFFFFFF));
    frameCnt = 0;
}

void HUD_update(const char* modeName, u16 objCount, u16 hits, u16 enemySpeedPct)
{
    // SYS_getFPS must be sampled every frame: it counts its own invocations
    const u16 fps = (u16) SYS_getFPS();

    frameCnt++;
    if ((frameCnt & 7) != 0) return;

    char line[41];

    sprintf(line, "%s OBJ:%2u CPU:%3u%% FPS:%2u  ",
            modeName, objCount, SYS_getCPULoad(), fps);
    VDP_drawText(line, 1, 0);

    sprintf(line, "HITS:%3u SPD:%3u%%", hits, enemySpeedPct);
    VDP_drawText(line, 1, 1);
}
