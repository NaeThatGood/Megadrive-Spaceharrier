#include <genesis.h>
#include "resources.h"
#include "engine/ground.h"
#include "engine/world.h"
#include "engine/hud.h"

// --- Player -----------------------------------------------------------------

#define PLAYER_W        32
#define PLAYER_H        48

#define PLAYER_MIN_X    8
#define PLAYER_MAX_X    (320 - PLAYER_W - 8)
#define PLAYER_MIN_Y    (GROUND_HORIZON - 8)
#define PLAYER_MAX_Y    (224 - PLAYER_H - 4)

#define PLAYER_SPEED    3
#define PLAYER_CENTER_X ((320 - PLAYER_W) / 2)

// Bottom of a ground object when right at the player plane
#define NEAR_GROUND_Y   (GROUND_HORIZON + WORLD_GROUND_DEPTH)

#define SHOT_SPEED      110
#define SPAWN_INTERVAL  40

static Sprite* player;
static s16 playerX;
static s16 playerY;

static WObj  objects[MAX_OBJECTS];
static WShot shots[MAX_SHOTS];

static const Renderer* renderer;
static u16 hits;
static u16 spawnTimer;
static u16 hurtFlash;
static u16 prevJoy;

// --- Helpers ----------------------------------------------------------------

static s16 playerWorldX(void)
{
    return playerX + (PLAYER_W / 2) - 160;
}

static s16 playerWorldY(void)
{
    // Height of the player's torso above the ground plane, world units
    return NEAR_GROUND_Y - (playerY + PLAYER_H - 10);
}

static u16 countObjects(void)
{
    u16 n = 0;
    for (u16 i = 0; i < MAX_OBJECTS; i++)
        if (objects[i].active) n++;
    return n;
}

// --- Objects ----------------------------------------------------------------

static void spawnObject(void)
{
    for (u16 i = 0; i < MAX_OBJECTS; i++)
    {
        WObj* o = &objects[i];
        if (o->active) continue;

        o->active = TRUE;
        o->wx = (s16) (random() % 281) - 140;
        o->wy = (random() & 1) ? 0 : (s16) (20 + (random() % 51));
        o->z  = WORLD_Z_FAR;
        o->vx = (s16) (random() % 5) - 2;
        o->vz = 24 + (random() & 15);
        renderer->spawn(o);
        return;
    }
}

static void killObject(WObj* o)
{
    renderer->despawn(o);
    o->active = FALSE;
}

static void updateObjects(void)
{
    const s16 pwx = playerWorldX();
    const s16 pwy = playerWorldY();

    for (u16 i = 0; i < MAX_OBJECTS; i++)
    {
        WObj* o = &objects[i];
        if (!o->active) continue;

        o->wx += o->vx;

        if (o->z <= WORLD_Z_NEAR + o->vz)
        {
            // Reached the player plane: collision check, then leave
            if (abs(o->wx - pwx) < 40 && abs(o->wy - pwy) < 48)
                hurtFlash = 16;
            killObject(o);
            continue;
        }
        o->z -= o->vz;

        const s16 sx = WORLD_screenX(o->wx, o->z);
        const s16 sy = WORLD_screenYBottom(o->wy, o->z);
        renderer->update(o, sx, sy, WORLD_sizePx(o->z));
    }
}

// --- Shots ------------------------------------------------------------------

static void fireShot(void)
{
    for (u16 i = 0; i < MAX_SHOTS; i++)
    {
        WShot* s = &shots[i];
        if (s->active) continue;

        s->active = TRUE;
        s->wx = playerWorldX();
        s->wy = playerWorldY() + 14;   // cannon height
        s->z  = WORLD_Z_NEAR;
        s->spr = SPR_addSprite(&spr_shot, -16, -16,
                               TILE_ATTR(PAL2, 0, FALSE, FALSE));
        return;
    }
}

static void killShot(WShot* s)
{
    SPR_releaseSprite(s->spr);
    s->spr = NULL;
    s->active = FALSE;
}

static void updateShots(void)
{
    for (u16 i = 0; i < MAX_SHOTS; i++)
    {
        WShot* s = &shots[i];
        if (!s->active) continue;

        if (s->z >= WORLD_Z_FAR - SHOT_SPEED)
        {
            killShot(s);
            continue;
        }
        s->z += SHOT_SPEED;

        // Hit test against objects
        bool hit = FALSE;
        for (u16 j = 0; j < MAX_OBJECTS; j++)
        {
            WObj* o = &objects[j];
            if (!o->active) continue;
            if (abs((s16) (o->z - s->z)) < 200 &&
                abs(o->wx - s->wx) < 36 &&
                abs(o->wy - s->wy) < 40)
            {
                killObject(o);
                hit = TRUE;
                hits++;
                break;
            }
        }
        if (hit)
        {
            killShot(s);
            continue;
        }

        SPR_setPosition(s->spr,
                        WORLD_screenX(s->wx, s->z) - 4,
                        WORLD_screenYBottom(s->wy, s->z) - 8);
    }
}

// --- Mode switch ------------------------------------------------------------

static void setRenderer(const Renderer* r)
{
    for (u16 i = 0; i < MAX_OBJECTS; i++)
        if (objects[i].active) renderer->despawn(&objects[i]);

    renderer = r;
    renderer->init();

    for (u16 i = 0; i < MAX_OBJECTS; i++)
        if (objects[i].active) renderer->spawn(&objects[i]);
}

// --- Input ------------------------------------------------------------------

static void handleInput(void)
{
    const u16 joy = JOY_readJoypad(JOY_1);
    const u16 pressed = joy & ~prevJoy;
    prevJoy = joy;

    if (joy & BUTTON_LEFT)  playerX -= PLAYER_SPEED;
    if (joy & BUTTON_RIGHT) playerX += PLAYER_SPEED;
    if (joy & BUTTON_UP)    playerY -= PLAYER_SPEED;
    if (joy & BUTTON_DOWN)  playerY += PLAYER_SPEED;

    if (playerX < PLAYER_MIN_X) playerX = PLAYER_MIN_X;
    if (playerX > PLAYER_MAX_X) playerX = PLAYER_MAX_X;
    if (playerY < PLAYER_MIN_Y) playerY = PLAYER_MIN_Y;
    if (playerY > PLAYER_MAX_Y) playerY = PLAYER_MAX_Y;

    if (pressed & (BUTTON_A | BUTTON_B | BUTTON_C))
        fireShot();

    if (pressed & BUTTON_START)
        setRenderer((renderer == &RENDER_stored) ? &RENDER_runtime
                                                 : &RENDER_stored);
}

// --- Main -------------------------------------------------------------------

int main(bool hardReset)
{
    (void) hardReset;

    VDP_setScreenWidth320();
    SPR_init();

    GROUND_init();
    HUD_init();

    PAL_setPalette(PAL1, spr_player.palette->data, CPU);
    playerX = PLAYER_CENTER_X;
    playerY = PLAYER_MAX_Y - 16;
    player = SPR_addSprite(&spr_player, playerX, playerY,
                           TILE_ATTR(PAL1, 0, FALSE, FALSE));

    renderer = &RENDER_stored;
    renderer->init();

    while (TRUE)
    {
        handleInput();

        spawnTimer++;
        if (spawnTimer >= SPAWN_INTERVAL)
        {
            spawnTimer = 0;
            spawnObject();
        }

        updateObjects();
        updateShots();
        if (renderer->frame) renderer->frame();

        GROUND_update(playerX - PLAYER_CENTER_X, 4);

        if (hurtFlash)
        {
            hurtFlash--;
            PAL_setColor(0, (hurtFlash & 4) ? RGB24_TO_VDPCOLOR(0xC00000)
                                            : RGB24_TO_VDPCOLOR(0x000000));
        }

        HUD_update(renderer->name, countObjects(), hits);

        SPR_setPosition(player, playerX, playerY);
        SPR_update();
        SYS_doVBlankProcess();
    }

    return 0;
}
