#include <genesis.h>
#include "resources.h"
#include "engine/ground.h"

// --- Player -----------------------------------------------------------------

#define PLAYER_W        32
#define PLAYER_H        48

#define PLAYER_MIN_X    8
#define PLAYER_MAX_X    (320 - PLAYER_W - 8)
#define PLAYER_MIN_Y    (GROUND_HORIZON - 8)
#define PLAYER_MAX_Y    (224 - PLAYER_H - 4)

#define PLAYER_SPEED    3
#define PLAYER_CENTER_X ((320 - PLAYER_W) / 2)

static Sprite* player;
static s16 playerX;
static s16 playerY;

static void handleInput(void)
{
    const u16 joy = JOY_readJoypad(JOY_1);

    if (joy & BUTTON_LEFT)  playerX -= PLAYER_SPEED;
    if (joy & BUTTON_RIGHT) playerX += PLAYER_SPEED;
    if (joy & BUTTON_UP)    playerY -= PLAYER_SPEED;
    if (joy & BUTTON_DOWN)  playerY += PLAYER_SPEED;

    if (playerX < PLAYER_MIN_X) playerX = PLAYER_MIN_X;
    if (playerX > PLAYER_MAX_X) playerX = PLAYER_MAX_X;
    if (playerY < PLAYER_MIN_Y) playerY = PLAYER_MIN_Y;
    if (playerY > PLAYER_MAX_Y) playerY = PLAYER_MAX_Y;
}

int main(bool hardReset)
{
    (void) hardReset;

    VDP_setScreenWidth320();
    SPR_init();

    GROUND_init();

    PAL_setPalette(PAL1, spr_player.palette->data, CPU);
    playerX = PLAYER_CENTER_X;
    playerY = PLAYER_MAX_Y - 16;
    player = SPR_addSprite(&spr_player, playerX, playerY,
                           TILE_ATTR(PAL1, 0, FALSE, FALSE));

    VDP_drawText("SH PROTO - MILESTONE A", 1, 0);

    while (TRUE)
    {
        handleInput();

        GROUND_update(playerX - PLAYER_CENTER_X, 4);

        SPR_setPosition(player, playerX, playerY);
        SPR_update();
        SYS_doVBlankProcess();
    }

    return 0;
}
