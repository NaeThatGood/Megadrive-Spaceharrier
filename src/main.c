#include <genesis.h>
#include "resources.h"
#include "engine/ground.h"
#include "engine/world.h"
#include "engine/hud.h"
#include "engine/sky.h"

// --- Player -----------------------------------------------------------------

#define PLAYER_W        64
#define PLAYER_H        96

#define PLAYER_MIN_X    8
#define PLAYER_MAX_X    (320 - PLAYER_W - 8)
#define PLAYER_MIN_Y    16
#define PLAYER_FEET_Y   (PLAYER_H - 20)   // matches playerWorldY() reference
#define PLAYER_GROUND_SINK  8             // feet dip into checker when low

#define PLAYER_CENTER_X ((320 - PLAYER_W) / 2)
#define HURT_FLASH_FRAMES  16
#define HURT_FLASH_PHASE   4
#define PLAYER_SPEED_DEFAULT  7
#define PLAYER_SPEED_STEP_COUNT  9
#define ENEMY_SPEED_DEFAULT  50
#define ENEMY_SPEED_MIN      10
#define ENEMY_SPEED_MAX      200
#define ENEMY_SPEED_STEP     10
#define MAX_TREES            4
#define TREE_VISIBLE_COUNT   3
#define TREE_NEAR_SPRITES    1
#define TREE_FAR_SPRITES     (TREE_VISIBLE_COUNT - TREE_NEAR_SPRITES)
#define TREE_FRAME_COUNT     25
#define TREE_FRAME_CANVAS_W  64
#define TREE_NEAR_CANVAS_H   216
#define TREE_FAR_CANVAS_H    96
#define TREE_Z_SPACING       ((WORLD_Z_FAR - WORLD_Z_NEAR) / MAX_TREES)
#define TREE_DEPTH           200
#define PLAYER_DEPTH         0

static Sprite* player;
static s16 playerX;
static s16 playerY;
static s16 playerMaxY;     // lowest Y (ground hover); depends on screen height

static WObj  objects[MAX_OBJECTS];
static WShot shots[MAX_SHOTS];

typedef struct
{
    s16     wx;
    u16     z;
} WTree;

static WTree trees[MAX_TREES];
static Sprite* treeNearSprs[TREE_NEAR_SPRITES];
static Sprite* treeFarSprs[TREE_FAR_SPRITES];
static u8 treeNearFrameIdx[TREE_NEAR_SPRITES];
static u8 treeFarFrameIdx[TREE_FAR_SPRITES];

static const Renderer* renderer;
static u16 hits;
static u16 spawnTimer;
static u16 hurtFlash;
static u16 playerSpeed;
static u8 playerSpeedStep;
static u16 enemySpeedPct;
static u16 prevJoy;
static bool paused;

// --- Helpers ----------------------------------------------------------------

static const u16 PLAYER_SPEED_PCTS[PLAYER_SPEED_STEP_COUNT] =
{
    100, 125, 150, 175, 200, 0, 25, 50, 75
};

static s16 playerWorldX(void)
{
    return playerX + (PLAYER_W / 2) - 160;
}

static s16 playerWorldY(void)
{
    // Height of the player's torso above the ground plane, world units.
    // GROUND_horizon + WORLD_GROUND_DEPTH = bottom of a ground object
    // right at the player plane.
    return (GROUND_horizon + WORLD_GROUND_DEPTH) - (playerY + PLAYER_H - 20);
}

static u16 countObjects(void)
{
    u16 n = 0;
    for (u16 i = 0; i < MAX_OBJECTS; i++)
        if (objects[i].active) n++;
    return n;
}

static u16 enemyStepVz(const WObj* o)
{
    const u16 vz = (u16) ((((u32) o->vz * enemySpeedPct) + 50) / 100);
    return (vz == 0) ? 1 : vz;
}

static void setPlayerSpeedStep(u8 step)
{
    playerSpeedStep = step % PLAYER_SPEED_STEP_COUNT;
    playerSpeed = (u16) ((((u32) PLAYER_SPEED_DEFAULT *
                           PLAYER_SPEED_PCTS[playerSpeedStep]) + 50) / 100);
}

// --- Trees -------------------------------------------------------------------

static const u8 TREE_FRAME_SIZES[TREE_FRAME_COUNT] =
{
     8, 10, 13, 15, 17,
    20, 22, 24, 27, 29,
    31, 34, 36, 38, 41,
    43, 45, 48, 50, 52,
    55, 57, 59, 62, 64
};

static u8 treeSizeToFrame(u16 sizePx)
{
    u8 best = 0;
    u16 bestDist = 0xFFFF;
    for (u8 i = 0; i < TREE_FRAME_COUNT; i++)
    {
        const u16 s = TREE_FRAME_SIZES[i];
        const u16 dist = (sizePx > s) ? (sizePx - s) : (s - sizePx);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = i;
        }
    }
    return best;
}

static s16 treeLane(u8 i)
{
    static const s16 lanes[MAX_TREES] = { -280, 240, -190, 310 };
    return lanes[i & (MAX_TREES - 1)];
}

static void resetTree(WTree* t, u8 slot, u16 z)
{
    t->wx = treeLane(slot);
    t->z = z;
}

static void initTrees(void)
{
    PAL_setPalette(PAL3, spr_tree_scaled.palette->data, CPU);

    for (u8 i = 0; i < TREE_NEAR_SPRITES; i++)
    {
        treeNearSprs[i] = SPR_addSprite(&spr_tree_scaled, -128, -128,
                                        TILE_ATTR(PAL3, 0, FALSE, FALSE));
        treeNearFrameIdx[i] = 0xFF;
        if (treeNearSprs[i])
        {
            SPR_setDepth(treeNearSprs[i], TREE_DEPTH);
            SPR_setVisibility(treeNearSprs[i], HIDDEN);
        }
    }

    for (u8 i = 0; i < TREE_FAR_SPRITES; i++)
    {
        treeFarSprs[i] = SPR_addSprite(&spr_tree_scaled_far, -128, -128,
                                       TILE_ATTR(PAL3, 0, FALSE, FALSE));
        treeFarFrameIdx[i] = 0xFF;
        if (treeFarSprs[i])
        {
            SPR_setDepth(treeFarSprs[i], TREE_DEPTH);
            SPR_setVisibility(treeFarSprs[i], HIDDEN);
        }
    }

    for (u8 i = 0; i < MAX_TREES; i++)
        resetTree(&trees[i], i, WORLD_Z_FAR - (u16) i * TREE_Z_SPACING);
}

static void updateTrees(void)
{
    const s16 pwx = playerWorldX();
    u8 sorted[MAX_TREES];
    u8 visible[TREE_VISIBLE_COUNT];
    u8 visibleCount = 0;

    for (u8 i = 0; i < MAX_TREES; i++)
    {
        WTree* t = &trees[i];
        if (t->z <= WORLD_Z_NEAR + GROUND_FORWARD_SPEED)
            resetTree(t, i, WORLD_Z_FAR);
        else
            t->z -= GROUND_FORWARD_SPEED;
        sorted[i] = i;
    }

    // Render the closest logical trees with a small fixed sprite pool:
    // one full-height near sprite plus smaller far sprites for the rest.
    for (u8 i = 1; i < MAX_TREES; i++)
    {
        const u8 idx = sorted[i];
        u8 j = i;
        while (j > 0 && trees[sorted[j - 1]].z > trees[idx].z)
        {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = idx;
    }

    for (u8 i = 0; i < MAX_TREES && visibleCount < TREE_VISIBLE_COUNT; i++)
    {
        const WTree* t = &trees[sorted[i]];
        if (WORLD_sizePx(t->z) <= TREE_FRAME_SIZES[TREE_FRAME_COUNT - 1])
            visible[visibleCount++] = sorted[i];
    }

    for (u8 i = 0; i < TREE_NEAR_SPRITES; i++)
    {
        Sprite* spr = treeNearSprs[i];
        if (!spr) continue;
        if (i >= visibleCount)
        {
            SPR_setVisibility(spr, HIDDEN);
            continue;
        }

        const WTree* t = &trees[visible[i]];
        const u8 frame = treeSizeToFrame(WORLD_sizePx(t->z));
        if (frame != treeNearFrameIdx[i])
        {
            treeNearFrameIdx[i] = frame;
            SPR_setFrame(spr, frame);
        }
        SPR_setPosition(spr,
                        WORLD_screenX(t->wx - pwx, t->z) - (TREE_FRAME_CANVAS_W / 2),
                        WORLD_screenYBottom(0, t->z) + GROUND_VISIBLE_HORIZON_PAD
                            - TREE_NEAR_CANVAS_H);
        SPR_setVisibility(spr, VISIBLE);
    }

    for (u8 i = 0; i < TREE_FAR_SPRITES; i++)
    {
        Sprite* spr = treeFarSprs[i];
        const u8 visibleIdx = TREE_NEAR_SPRITES + i;
        if (!spr) continue;
        if (visibleIdx >= visibleCount)
        {
            SPR_setVisibility(spr, HIDDEN);
            continue;
        }

        const WTree* t = &trees[visible[visibleIdx]];
        const u8 frame = treeSizeToFrame(WORLD_sizePx(t->z));
        if (frame != treeFarFrameIdx[i])
        {
            treeFarFrameIdx[i] = frame;
            SPR_setFrame(spr, frame);
        }
        SPR_setPosition(spr,
                        WORLD_screenX(t->wx - pwx, t->z) - (TREE_FRAME_CANVAS_W / 2),
                        WORLD_screenYBottom(0, t->z) + GROUND_VISIBLE_HORIZON_PAD
                            - TREE_FAR_CANVAS_H);
        SPR_setVisibility(spr, VISIBLE);
    }
}

// --- Objects ----------------------------------------------------------------

static void spawnObject(void)
{
    for (u16 i = 0; i < MAX_OBJECTS; i++)
    {
        WObj* o = &objects[i];
        if (o->active) continue;

        o->active = TRUE;
        o->vramIndex = 0;
        o->sizeIdx = 0;
        for (u8 q = 0; q < 4; q++) o->sprs[q] = NULL;
        o->wx = (s16) (random() % 281) - 140;
        o->wy = (random() & 1) ? 0 : (s16) (20 + (random() % 51));
        o->z  = WORLD_Z_FAR;
        o->vx = (s16) (random() % 5) - 2;
        o->vz = WORLD_APPROACH_VZ_BASE + (random() & WORLD_APPROACH_VZ_RAND);
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

        const u16 stepVz = enemyStepVz(o);

        if (o->z <= WORLD_Z_NEAR + stepVz)
        {
            // Reached the player plane: collision check, then leave
            if (abs(o->wx - pwx) < 80 && abs(o->wy - pwy) < 96)
                hurtFlash = HURT_FLASH_FRAMES;
            killObject(o);
            continue;
        }
        o->z -= stepVz;

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
        s->wy = playerWorldY() + 28;   // cannon height
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

        if (s->z >= WORLD_Z_FAR - WORLD_SHOT_SPEED)
        {
            killShot(s);
            continue;
        }
        s->z += WORLD_SHOT_SPEED;

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

static void playGetReady(void)
{
    XGM2_playPCM(snd_getready, sizeof(snd_getready), SOUND_PCM_CH_AUTO);
}

static void setRenderer(const Renderer* r)
{
    DMA_waitCompletion();
    SPR_update();

    for (u16 i = 0; i < MAX_OBJECTS; i++)
        if (objects[i].active) renderer->despawn(&objects[i]);

    SPR_update();

    renderer = r;
    renderer->init();

    if (renderer == &RENDER_runtime && RUNTIME_slotCapacity() == 0)
    {
        renderer = &RENDER_stored;
        renderer->init();
    }
    else if (renderer == &RENDER_runtime)
    {
        u8 cap = RUNTIME_slotCapacity();
        u8 kept = 0;
        for (u16 i = 0; i < MAX_OBJECTS; i++)
        {
            if (!objects[i].active) continue;
            if (kept >= cap) killObject(&objects[i]);
            else kept++;
        }
    }

    for (u16 i = 0; i < MAX_OBJECTS; i++)
        if (objects[i].active) renderer->spawn(&objects[i]);
}

// --- Input ------------------------------------------------------------------

static void handleInput(void)
{
    const u16 joy = JOY_readJoypad(JOY_1);
    const u16 pressed = joy & ~prevJoy;
    prevJoy = joy;

    if (pressed & BUTTON_START)
        paused = !paused;

    if (pressed & BUTTON_C)
        setRenderer((renderer == &RENDER_stored) ? &RENDER_runtime
                                                 : &RENDER_stored);

    if (paused) return;

    if ((pressed & BUTTON_A) && enemySpeedPct > ENEMY_SPEED_MIN)
        enemySpeedPct -= ENEMY_SPEED_STEP;
    if ((pressed & BUTTON_B) && enemySpeedPct < ENEMY_SPEED_MAX)
        enemySpeedPct += ENEMY_SPEED_STEP;

    if (pressed & BUTTON_Y)
        sky_setEnabled(!sky_isEnabled());
    if (pressed & BUTTON_Z)
        setPlayerSpeedStep(playerSpeedStep + 1);

    if (pressed & BUTTON_X)
        fireShot();

    if (joy & BUTTON_LEFT)  playerX -= playerSpeed;
    if (joy & BUTTON_RIGHT) playerX += playerSpeed;
    if (joy & BUTTON_UP)    playerY -= playerSpeed;
    if (joy & BUTTON_DOWN)  playerY += playerSpeed;

    if (playerX < PLAYER_MIN_X) playerX = PLAYER_MIN_X;
    if (playerX > PLAYER_MAX_X) playerX = PLAYER_MAX_X;
    if (playerY < PLAYER_MIN_Y) playerY = PLAYER_MIN_Y;
    if (playerY > playerMaxY)   playerY = playerMaxY;
}

// --- Main -------------------------------------------------------------------

int main(bool hardReset)
{
    (void) hardReset;

    // NTSC gives a clean 30 Hz game cadence when the loop waits two VBlanks.
    // PAL still gets the taller 240-line mode, but runs at 25 Hz.
    VDP_setScreenWidth320();
    if (IS_PAL_SYSTEM) VDP_setScreenHeight240();
    JOY_setSupport(PORT_1, JOY_SUPPORT_6BTN);

    // Ground first: stabilises TILE_MAX_NUM, then reserve VRAM after the
    // ground tileset for runtime scaling slots before the sprite pool exists.
    GROUND_init();
    SPR_initEx(RUNTIME_spriteVramBudget());

    HUD_init();

    playerMaxY = GROUND_HORIZON + WORLD_GROUND_DEPTH
               - PLAYER_FEET_Y + PLAYER_GROUND_SINK;

    PAL_setPalette(PAL1, spr_player.palette->data, CPU);
    playerX = PLAYER_CENTER_X;
    playerY = playerMaxY - 8;
    setPlayerSpeedStep(0);
    enemySpeedPct = ENEMY_SPEED_DEFAULT;
    player = SPR_addSprite(&spr_player, playerX, playerY,
                           TILE_ATTR(PAL1, 0, FALSE, FALSE));
    if (player) SPR_setDepth(player, PLAYER_DEPTH);
    initTrees();

    renderer = &RENDER_stored;
    renderer->init();

    sky_init();
    sky_setHorizon(GROUND_horizon + GROUND_VISIBLE_HORIZON_PAD);
    sky_vblank();
    SYS_setVIntCallback(sky_vblank);

    playGetReady();

    while (TRUE)
    {
        handleInput();

        if (!paused)
        {
            // Ground first: it sets GROUND_horizon, which the projection of
            // every object and shot below depends on.
            const s16 pitchY = ((PLAYER_MIN_Y + playerMaxY) / 2) - playerY;
            GROUND_update(playerX - PLAYER_CENTER_X, pitchY, playerWorldX(),
                          GROUND_FORWARD_SPEED);
            sky_setHorizon(GROUND_horizon + GROUND_VISIBLE_HORIZON_PAD);

            spawnTimer++;
            if (spawnTimer >= WORLD_SPAWN_INTERVAL)
            {
                spawnTimer = 0;
                spawnObject();
            }

            updateTrees();
            updateObjects();
            updateShots();
            if (renderer->frame) renderer->frame();

            if (hurtFlash)
            {
                hurtFlash--;
                if (player)
                    SPR_setVisibility(player, (hurtFlash & HURT_FLASH_PHASE)
                                               ? HIDDEN
                                               : VISIBLE);
            }
            else
            {
                if (player) SPR_setVisibility(player, VISIBLE);
            }
        }

        HUD_update(renderer->name, countObjects(), hits, enemySpeedPct);

        if (player) SPR_setPosition(player, playerX, playerY);
        SPR_update();
        SYS_doVBlankProcess();
        VDP_waitVSync();   // NTSC 60 Hz display, 30 Hz game/render cadence
    }

    return 0;
}
