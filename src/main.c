#include <genesis.h>
#include "resources.h"
#include "engine/ground.h"
#include "engine/clouds.h"
#include "engine/world.h"
#include "engine/hud.h"
#include "engine/shadow.h"
#include "engine/mountains.h"
#include "engine/scenepal.h"

// --- Player -----------------------------------------------------------------

#define PLAYER_W        64
#define PLAYER_H        72

#define PLAYER_MIN_X    8
#define PLAYER_MAX_X    (320 - PLAYER_W - 8)
#define PLAYER_MIN_Y    16
#define PLAYER_FEET_Y   PLAYER_H          // cropped sprite ends at the feet
#define PLAYER_GROUND_SINK  8             // feet dip into checker when low

#define PLAYER_CENTER_X ((320 - PLAYER_W) / 2)
#define HURT_FLASH_FRAMES  16
#define HURT_FLASH_PHASE   4
#define PLAYER_SPEED_DEFAULT  4
#define PLAYER_SPEED_STEP_COUNT  9
#define ENEMY_SPEED_DEFAULT  50
#define ENEMY_SPEED_STEP_COUNT  20
#define INPUT_STARTUP_SETTLE_FRAMES 8
#define MAX_TREES            4
#define TREE_VISIBLE_COUNT   3
#define TREE_NEAR_SPRITES    1
#define TREE_FAR_SPRITES     (TREE_VISIBLE_COUNT - TREE_NEAR_SPRITES)
#define TREE_FRAME_COUNT     25
#define TREE_FRAME_MAX_SIZE  64
#define TREE_FRAME_CANVAS_W  64
#define TREE_HALF_CANVAS_W   (TREE_FRAME_CANVAS_W / 2)
#define TREE_HALF_COUNT      2
#define TREE_HALF_LEFT       0
#define TREE_HALF_RIGHT      1
#define TREE_NEAR_CANVAS_H   216
#define TREE_FAR_CANVAS_H    96
#define TREE_Z_SPACING       ((WORLD_Z_FAR - WORLD_Z_NEAR) / MAX_TREES)
#define TREE_DEPTH           200
#define PLAYER_DEPTH         0
#define SHOT_SHADOW_SIZE     12
#define SPRITE_POOL_TILES    640
#define BOSS_SQUILLA_SEGMENTS 3
#define BOSS_SQUILLA_HISTORY 32
#define BOSS_SQUILLA_HEAD    0
#define BOSS_SQUILLA_BODY    1
#define BOSS_SQUILLA_TAIL    2
#define BOSS_SQUILLA_FAR_Z   WORLD_Z_FAR
#define BOSS_SQUILLA_NEAR_Z  768
#define BOSS_SQUILLA_WX_MAX  430
#define BOSS_SQUILLA_WY_MIN  36
#define BOSS_SQUILLA_WY_MAX  128
#define BOSS_SQUILLA_SPAWN_WY 84
#define BOSS_SQUILLA_ENTER_VZ 96
#define BOSS_SQUILLA_ROAM_VZ  72
#define BOSS_SQUILLA_ROAM_FAR_Z 3000
#define BOSS_SQUILLA_NEAR_HOLD_FRAMES 45
#define BOSS_SQUILLA_LEAVE_VZ 72
#define BOSS_SQUILLA_LEAVE_CENTER_STEP 16
#define BOSS_SQUILLA_DEPTH   120
#define BOSS_SQUILLA_BUCKETS 7
#define BOSS_SQUILLA_BUCKET_NONE 0xFF

static Sprite* player;
static Sprite* playerShadow;
static Sprite* objShadows[MAX_OBJECTS];
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

typedef struct
{
    s16 wx;
    s16 wy;
    u16 z;
} BossPathSample;

typedef struct
{
    Sprite* spr;
    u8 bucket;
} SquillaSegment;

static WTree trees[MAX_TREES];
static Sprite* treeNearSprs[TREE_NEAR_SPRITES][TREE_HALF_COUNT];
static Sprite* treeFarSprs[TREE_FAR_SPRITES][TREE_HALF_COUNT];
static Sprite* treeShadowSprs[TREE_VISIBLE_COUNT];
static u8 treeNearFrameIdx[TREE_NEAR_SPRITES];
static u8 treeFarFrameIdx[TREE_FAR_SPRITES];
static u8 treeFrameForSize[TREE_FRAME_MAX_SIZE + 1];

static const Renderer* renderer;
static u16 hits;
static u16 spawnTimer;
static u16 hurtFlash;
static u16 playerSpeed;
static u8 playerSpeedStep;
static u16 enemySpeedPct;
static u8 enemySpeedStep;
static u8 gFramesPerUpdate = 2;
static u16 scrollPhase;
static u16 prevJoy;
static u8 inputSettleFrames;
static bool paused;
static bool enemySpawningEnabled;
static bool enemyPoolReleased;
static bool bossRequestArmed;

typedef enum
{
    BOSS_STATE_OFF,
    BOSS_STATE_PENDING,
    BOSS_STATE_ACTIVE,
    BOSS_STATE_LEAVING,
    BOSS_STATE_RESTORE_WAIT
} BossState;

static BossState bossState;
static SquillaSegment bossSquilla[BOSS_SQUILLA_SEGMENTS];
static BossPathSample bossHead;
static BossPathSample bossHistory[BOSS_SQUILLA_HISTORY];
static u8 bossHistoryCursor;
static u16 bossNearHoldFrames;
static bool bossApproaching;
static s16 bossVx;
static s16 bossVy;
static s16 bossVz;

// --- Helpers ----------------------------------------------------------------

static const u16 ENEMY_SPEED_PCTS[ENEMY_SPEED_STEP_COUNT] =
{
    10, 20, 30, 40, 50, 60, 70, 80, 90, 100,
    110, 120, 130, 140, 150, 160, 170, 180, 190, 200
};

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
    return (GROUND_horizon + WORLD_GROUND_DEPTH) - (playerY + PLAYER_FEET_Y);
}

static u16 countObjects(void)
{
    u16 n = 0;
    for (u16 i = 0; i < MAX_OBJECTS; i++)
        if (objects[i].active) n++;
    return n;
}

static s16 playerPitchY(void)
{
    return ((PLAYER_MIN_Y + playerMaxY) / 2) - playerY;
}

static void refreshEnemyStep(WObj* o)
{
    const u16 vz = (u16) ((((u32) o->vz * enemySpeedPct) + 50) / 100);
    const u16 stepVz = (vz == 0) ? 1 : vz;
    o->stepVz = stepVz * gFramesPerUpdate;
}

static void refreshEnemySteps(void)
{
    for (u16 i = 0; i < MAX_OBJECTS; i++)
        if (objects[i].active)
            refreshEnemyStep(&objects[i]);
}

static void setEnemySpeedStep(u8 step)
{
    enemySpeedStep = step % ENEMY_SPEED_STEP_COUNT;
    enemySpeedPct = ENEMY_SPEED_PCTS[enemySpeedStep];
    refreshEnemySteps();
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

static void initTreeFrameLut(void)
{
    for (u16 size = 0; size <= TREE_FRAME_MAX_SIZE; size++)
    {
        u8 best = 0;
        u16 bestDist = 0xFFFF;
        for (u8 i = 0; i < TREE_FRAME_COUNT; i++)
        {
            const u16 s = TREE_FRAME_SIZES[i];
            const u16 dist = (size > s) ? (size - s) : (s - size);
            if (dist < bestDist)
            {
                bestDist = dist;
                best = i;
            }
        }
        treeFrameForSize[size] = best;
    }
}

static void initSkyPalette(void)
{
    PAL_setColor((PAL3 << 4) + 12, RGB24_TO_VDPCOLOR(0x2A4B8D));
    PAL_setColor((PAL3 << 4) + 13, RGB24_TO_VDPCOLOR(0x3A63B0));
    PAL_setColor((PAL3 << 4) + 14, RGB24_TO_VDPCOLOR(0x4E80CC));
    PAL_setColor((PAL3 << 4) + 15, RGB24_TO_VDPCOLOR(0x6CA0E0));
    PAL_setColor((PAL3 << 4) + 5,  RGB24_TO_VDPCOLOR(0x97C2EE));
    PAL_setColor((PAL3 << 4) + 4,  RGB24_TO_VDPCOLOR(0xC2E0F5));
    PAL_setColor(PAL0 << 4, RGB24_TO_VDPCOLOR(0x2A4B8D));
}

static u8 treeSizeToFrame(u16 sizePx)
{
    if (sizePx > TREE_FRAME_MAX_SIZE) return TREE_FRAME_COUNT - 1;
    return treeFrameForSize[sizePx];
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

static void initTreePair(Sprite* sprs[TREE_HALF_COUNT], const SpriteDefinition* def)
{
    sprs[TREE_HALF_LEFT] = SPR_addSprite(def, -128, -128,
                                         TILE_ATTR(PAL3, 0, FALSE, FALSE));
    sprs[TREE_HALF_RIGHT] = NULL;

    if (sprs[TREE_HALF_LEFT])
    {
        const u16 sharedTile = sprs[TREE_HALF_LEFT]->attribut & ~TILE_ATTR_MASK;
        sprs[TREE_HALF_RIGHT] =
            SPR_addSpriteEx(def, -128, -128,
                            TILE_ATTR_FULL(PAL3, 0, FALSE, TRUE, sharedTile),
                            0);
    }

    for (u8 half = 0; half < TREE_HALF_COUNT; half++)
    {
        if (sprs[half])
        {
            SPR_setDepth(sprs[half], TREE_DEPTH);
            SPR_setVisibility(sprs[half], HIDDEN);
        }
    }
}

static void setTreePairFrame(Sprite* sprs[TREE_HALF_COUNT], u8 frame)
{
    for (u8 half = 0; half < TREE_HALF_COUNT; half++)
        if (sprs[half])
            SPR_setFrame(sprs[half], frame);
}

static void setTreePairVisibility(Sprite* sprs[TREE_HALF_COUNT], SpriteVisibility visibility)
{
    for (u8 half = 0; half < TREE_HALF_COUNT; half++)
        if (sprs[half])
            SPR_setVisibility(sprs[half], visibility);
}

static void setTreePairPosition(Sprite* sprs[TREE_HALF_COUNT], s16 x, s16 y)
{
    if (sprs[TREE_HALF_LEFT])
        SPR_setPosition(sprs[TREE_HALF_LEFT], x, y);
    if (sprs[TREE_HALF_RIGHT])
        SPR_setPosition(sprs[TREE_HALF_RIGHT], x + TREE_HALF_CANVAS_W, y);
}

static void initTrees(void)
{
    initTreeFrameLut();
    PAL_setPalette(PAL3, spr_tree_scaled.palette->data, CPU);
    initSkyPalette();

    for (u8 i = 0; i < TREE_NEAR_SPRITES; i++)
    {
        initTreePair(treeNearSprs[i], &spr_tree_scaled);
        treeShadowSprs[i] = SHADOW_add();
        treeNearFrameIdx[i] = 0xFF;
    }

    for (u8 i = 0; i < TREE_FAR_SPRITES; i++)
    {
        initTreePair(treeFarSprs[i], &spr_tree_scaled_far);
        treeShadowSprs[TREE_NEAR_SPRITES + i] = SHADOW_add();
        treeFarFrameIdx[i] = 0xFF;
    }

    for (u8 i = 0; i < MAX_TREES; i++)
        resetTree(&trees[i], i, WORLD_Z_FAR - (u16) i * TREE_Z_SPACING);
}

static void releaseTreePair(Sprite* sprs[TREE_HALF_COUNT])
{
    if (sprs[TREE_HALF_RIGHT])
    {
        SPR_releaseSprite(sprs[TREE_HALF_RIGHT]);
        sprs[TREE_HALF_RIGHT] = NULL;
    }
    if (sprs[TREE_HALF_LEFT])
    {
        SPR_releaseSprite(sprs[TREE_HALF_LEFT]);
        sprs[TREE_HALF_LEFT] = NULL;
    }
}

static void releaseTrees(void)
{
    for (u8 i = 0; i < TREE_NEAR_SPRITES; i++)
    {
        releaseTreePair(treeNearSprs[i]);
        treeNearFrameIdx[i] = 0xFF;
    }

    for (u8 i = 0; i < TREE_FAR_SPRITES; i++)
    {
        releaseTreePair(treeFarSprs[i]);
        treeFarFrameIdx[i] = 0xFF;
    }

    for (u8 i = 0; i < TREE_VISIBLE_COUNT; i++)
    {
        SHADOW_release(treeShadowSprs[i]);
        treeShadowSprs[i] = NULL;
    }
}

static void initObjectShadows(void)
{
    for (u8 i = 0; i < MAX_OBJECTS; i++)
    {
        objShadows[i] = SHADOW_add();
        if (!objShadows[i])
            SYS_die("object shadow allocation failed");
    }
}

static void releaseObjectShadows(void)
{
    for (u8 i = 0; i < MAX_OBJECTS; i++)
    {
        objects[i].shadow = NULL;
        SHADOW_release(objShadows[i]);
        objShadows[i] = NULL;
    }
}

static void initShots(void)
{
    for (u8 i = 0; i < MAX_SHOTS; i++)
    {
        shots[i].active = FALSE;
        if (!shots[i].spr)
            shots[i].spr = SPR_addSprite(&spr_shot, -16, -16,
                                         TILE_ATTR(PAL2, 0, FALSE, FALSE));
        if (shots[i].spr)
            SPR_setVisibility(shots[i].spr, HIDDEN);
        if (!shots[i].shadow)
            shots[i].shadow = SHADOW_add();
        if (!shots[i].spr || !shots[i].shadow)
            SYS_die("shot pool allocation failed");
    }
}

static void releaseShotsForBoss(void)
{
    for (u8 i = 0; i < MAX_SHOTS; i++)
    {
        WShot* s = &shots[i];
        s->active = FALSE;
        if (s->spr) SPR_setVisibility(s->spr, HIDDEN);
        SHADOW_release(s->shadow);
        s->shadow = NULL;
        if (i > 0 && s->spr)
        {
            SPR_releaseSprite(s->spr);
            s->spr = NULL;
        }
    }
}

static void updateTrees(void)
{
    const s16 pwx = playerWorldX();
    const u16 forwardStep = GROUND_FORWARD_SPEED * gFramesPerUpdate;
    u8 sorted[MAX_TREES];
    u8 visible[TREE_VISIBLE_COUNT];
    u16 treeProj[MAX_TREES];
    u16 treeSizePx[MAX_TREES];
    u8 visibleCount = 0;

    for (u8 i = 0; i < MAX_TREES; i++)
    {
        WTree* t = &trees[i];
        if (t->z <= WORLD_Z_NEAR + forwardStep)
            resetTree(t, i, WORLD_Z_FAR);
        else
            t->z -= forwardStep;
        treeProj[i] = WORLD_proj(t->z);
        treeSizePx[i] = WORLD_sizePxq(treeProj[i]);
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
        const u8 idx = sorted[i];
        if (treeSizePx[idx] <= TREE_FRAME_SIZES[TREE_FRAME_COUNT - 1])
            visible[visibleCount++] = idx;
    }

    for (u8 i = 0; i < TREE_NEAR_SPRITES; i++)
    {
        Sprite** sprs = treeNearSprs[i];
        Sprite* shadow = treeShadowSprs[i];
        if (!sprs[TREE_HALF_LEFT] && !sprs[TREE_HALF_RIGHT])
        {
            SHADOW_hide(shadow);
            continue;
        }
        if (i >= visibleCount)
        {
            setTreePairVisibility(sprs, HIDDEN);
            SHADOW_hide(shadow);
            continue;
        }

        const u8 idx = visible[i];
        const WTree* t = &trees[idx];
        const u16 q = treeProj[idx];
        const u8 frame = treeSizeToFrame(treeSizePx[idx]);
        if (frame != treeNearFrameIdx[i])
        {
            treeNearFrameIdx[i] = frame;
            setTreePairFrame(sprs, frame);
        }
        setTreePairPosition(sprs,
                            WORLD_screenXq(t->wx - pwx, q) - (TREE_FRAME_CANVAS_W / 2),
                            WORLD_screenYBq(0, q) + GROUND_VISIBLE_HORIZON_PAD
                                - TREE_NEAR_CANVAS_H);
        setTreePairVisibility(sprs, VISIBLE);
        SHADOW_place(shadow, t->wx - pwx, t->z, treeSizePx[idx]);
    }

    for (u8 i = 0; i < TREE_FAR_SPRITES; i++)
    {
        Sprite** sprs = treeFarSprs[i];
        const u8 visibleIdx = TREE_NEAR_SPRITES + i;
        Sprite* shadow = treeShadowSprs[visibleIdx];
        if (!sprs[TREE_HALF_LEFT] && !sprs[TREE_HALF_RIGHT])
        {
            SHADOW_hide(shadow);
            continue;
        }
        if (visibleIdx >= visibleCount)
        {
            setTreePairVisibility(sprs, HIDDEN);
            SHADOW_hide(shadow);
            continue;
        }

        const u8 idx = visible[visibleIdx];
        const WTree* t = &trees[idx];
        const u16 q = treeProj[idx];
        const u8 frame = treeSizeToFrame(treeSizePx[idx]);
        if (frame != treeFarFrameIdx[i])
        {
            treeFarFrameIdx[i] = frame;
            setTreePairFrame(sprs, frame);
        }
        setTreePairPosition(sprs,
                            WORLD_screenXq(t->wx - pwx, q) - (TREE_FRAME_CANVAS_W / 2),
                            WORLD_screenYBq(0, q) + GROUND_VISIBLE_HORIZON_PAD
                                - TREE_FAR_CANVAS_H);
        setTreePairVisibility(sprs, VISIBLE);
        SHADOW_place(shadow, t->wx - pwx, t->z, treeSizePx[idx]);
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
        o->slot = (u8) i;
        o->vramIndex = 0;
        o->sizeIdx = 0;
        for (u8 q = 0; q < 4; q++) o->sprs[q] = NULL;
        o->shadow = objShadows[o->slot];
        o->wx = (s16) (random() % 281) - 140;
        o->wy = (random() & 1) ? 0 : (s16) (20 + (random() % 51));
        o->z  = WORLD_Z_FAR;
        o->vx = (s16) (random() % 5) - 2;
        o->vz = WORLD_APPROACH_VZ_BASE + (random() % (WORLD_APPROACH_VZ_RAND + 1));
        refreshEnemyStep(o);
        renderer->spawn(o);
        return;
    }
}

static void killObject(WObj* o)
{
    renderer->despawn(o);
    SHADOW_hide(o->shadow);
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

        o->wx += o->vx * gFramesPerUpdate;

        const u16 stepVz = o->stepVz;

        if (o->z <= WORLD_Z_NEAR + stepVz)
        {
            // Reached the player plane: collision check, then leave
            if (abs(o->wx - pwx) < 80 && abs(o->wy - pwy) < 96)
                hurtFlash = HURT_FLASH_FRAMES;
            killObject(o);
            continue;
        }
        o->z -= stepVz;

        const u16 q = WORLD_proj(o->z);
        const s16 groundY = WORLD_screenYBq(0, q) + GROUND_VISIBLE_HORIZON_PAD;
        if (groundY >= GROUND_visibleBottom)
        {
            killObject(o);
            continue;
        }

        const s16 sx = WORLD_screenXq(o->wx, q);
        const s16 sy = WORLD_screenYBq(o->wy, q);
        const u16 sizePx = WORLD_sizePxq(q);
        renderer->update(o, sx, sy, sizePx);
        SHADOW_place(o->shadow, o->wx, o->z, sizePx);
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
        if (s->spr)
        {
            SPR_setPosition(s->spr, -16, -16);
            SPR_setVisibility(s->spr, VISIBLE);
        }
        return;
    }
}

static void killShot(WShot* s)
{
    if (s->spr) SPR_setVisibility(s->spr, HIDDEN);
    SHADOW_hide(s->shadow);
    s->active = FALSE;
}

static void hideGameplaySpritesForBoss(void)
{
    for (u8 i = 0; i < TREE_NEAR_SPRITES; i++)
        setTreePairVisibility(treeNearSprs[i], HIDDEN);
    for (u8 i = 0; i < TREE_FAR_SPRITES; i++)
        setTreePairVisibility(treeFarSprs[i], HIDDEN);
    for (u8 i = 0; i < TREE_VISIBLE_COUNT; i++)
        SHADOW_hide(treeShadowSprs[i]);

    for (u16 i = 0; i < MAX_OBJECTS; i++)
    {
        WObj* o = &objects[i];
        if (!o->active) continue;
        renderer->despawn(o);
        SHADOW_hide(o->shadow);
        o->active = FALSE;
        o->shadow = NULL;
    }

    for (u16 i = 0; i < MAX_SHOTS; i++)
        killShot(&shots[i]);
}

static void updateShots(void)
{
    const u16 step = WORLD_SHOT_SPEED * gFramesPerUpdate;

    for (u16 i = 0; i < MAX_SHOTS; i++)
    {
        WShot* s = &shots[i];
        if (!s->active) continue;

        if (s->z >= WORLD_Z_FAR - step)
        {
            killShot(s);
            continue;
        }
        s->z += step;

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

        const u16 q = WORLD_proj(s->z);
        if (s->spr)
            SPR_setPosition(s->spr,
                            WORLD_screenXq(s->wx, q) - 4,
                            WORLD_screenYBq(s->wy, q) - 8);

        // Shot shadows are easy to disable if lower-scanline sprite budget
        // gets tight during emulator testing.
        if (s->spr) SHADOW_place(s->shadow, s->wx, s->z, SHOT_SHADOW_SIZE);
        else SHADOW_hide(s->shadow);
    }
}

// --- Mode helpers -----------------------------------------------------------

static void playGetReady(void)
{
    XGM2_playPCM(snd_getready, sizeof(snd_getready), SOUND_PCM_CH_AUTO);
}

static const SpriteDefinition* const
SQUILLA_DEFS[BOSS_SQUILLA_SEGMENTS][BOSS_SQUILLA_BUCKETS] =
{
    { &spr_squilla_head_1, &spr_squilla_head_1a, &spr_squilla_head_1b,
      &spr_squilla_head_2, &spr_squilla_head_2a, &spr_squilla_head_3,
      &spr_squilla_head_4 },
    { &spr_squilla_body_1, &spr_squilla_body_1a, &spr_squilla_body_1b,
      &spr_squilla_body_2, &spr_squilla_body_2a, &spr_squilla_body_3,
      &spr_squilla_body_4 },
    { &spr_squilla_tail_1, &spr_squilla_tail_1a, &spr_squilla_tail_1b,
      &spr_squilla_tail_2, &spr_squilla_tail_2a, &spr_squilla_tail_3,
      &spr_squilla_tail_4 }
};

static const u8 SQUILLA_W[BOSS_SQUILLA_SEGMENTS][BOSS_SQUILLA_BUCKETS] =
{
    { 72, 64, 48, 40, 32, 24, 16 },
    { 72, 64, 48, 40, 32, 24, 16 },
    { 120, 104, 80, 64, 48, 40, 24 }
};

static const u8 SQUILLA_H[BOSS_SQUILLA_SEGMENTS][BOSS_SQUILLA_BUCKETS] =
{
    { 112, 96, 80, 64, 48, 40, 24 },
    { 112, 96, 80, 64, 48, 40, 24 },
    { 80, 72, 56, 48, 32, 24, 16 }
};

static const u8 SQUILLA_DELAY[BOSS_SQUILLA_SEGMENTS] = { 0, 8, 16 };
static const u16 SQUILLA_Z_OFFSET[BOSS_SQUILLA_SEGMENTS] = { 0, 180, 360 };

static void initSquillaBossData(void)
{
    for (u8 i = 0; i < BOSS_SQUILLA_SEGMENTS; i++)
    {
        bossSquilla[i].spr = NULL;
        bossSquilla[i].bucket = BOSS_SQUILLA_BUCKET_NONE;
    }
    bossHistoryCursor = 0;
}

static u8 squillaBucketForZ(u16 z)
{
    if (z <= 1000) return 0;
    if (z <= 1300) return 1;
    if (z <= 1700) return 2;
    if (z <= 2200) return 3;
    if (z <= 2700) return 4;
    if (z <= 3400) return 5;
    return 6;
}

static void releaseSquillaSegment(u8 segment)
{
    if (bossSquilla[segment].spr)
    {
        SPR_releaseSprite(bossSquilla[segment].spr);
        bossSquilla[segment].spr = NULL;
    }
    bossSquilla[segment].bucket = BOSS_SQUILLA_BUCKET_NONE;
}

static void releaseSquillaBoss(void)
{
    for (u8 i = 0; i < BOSS_SQUILLA_SEGMENTS; i++)
        releaseSquillaSegment(i);
}

static void hideSquillaBoss(void)
{
    for (u8 i = 0; i < BOSS_SQUILLA_SEGMENTS; i++)
    {
        if (bossSquilla[i].spr)
        {
            SPR_setVisibility(bossSquilla[i].spr, HIDDEN);
            SPR_setPosition(bossSquilla[i].spr, -128, -128);
        }
    }
}

static bool ensureSquillaSegment(u8 segment, u8 bucket)
{
    if (bossSquilla[segment].spr && bossSquilla[segment].bucket == bucket)
        return TRUE;

    releaseSquillaSegment(segment);

    bossSquilla[segment].spr =
        SPR_addSprite(SQUILLA_DEFS[segment][bucket], -128, -128,
                      TILE_ATTR(PAL2, 0, FALSE, FALSE));
    if (!bossSquilla[segment].spr)
        return FALSE;

    bossSquilla[segment].bucket = bucket;
    SPR_setDepth(bossSquilla[segment].spr, BOSS_SQUILLA_DEPTH + segment);
    return TRUE;
}

static void fillSquillaHistory(void)
{
    for (u8 i = 0; i < BOSS_SQUILLA_HISTORY; i++)
        bossHistory[i] = bossHead;
    bossHistoryCursor = 0;
}

static void recordSquillaHead(void)
{
    bossHistory[bossHistoryCursor] = bossHead;
    bossHistoryCursor++;
    if (bossHistoryCursor >= BOSS_SQUILLA_HISTORY)
        bossHistoryCursor = 0;
}

static BossPathSample squillaHistorySample(u8 delay)
{
    u8 idx = bossHistoryCursor;
    const u8 back = delay + 1;
    idx = (idx + BOSS_SQUILLA_HISTORY - back) & (BOSS_SQUILLA_HISTORY - 1);
    return bossHistory[idx];
}

static s16 approachS16(s16 value, s16 target, s16 step)
{
    if (value < target)
    {
        value += step;
        if (value > target) value = target;
    }
    else if (value > target)
    {
        value -= step;
        if (value < target) value = target;
    }
    return value;
}

static void renderSquillaBoss(void)
{
    for (u8 i = 0; i < BOSS_SQUILLA_SEGMENTS; i++)
    {
        BossPathSample p = squillaHistorySample(SQUILLA_DELAY[i]);
        const u16 offsetZ = SQUILLA_Z_OFFSET[i];
        p.z = (p.z > BOSS_SQUILLA_FAR_Z - offsetZ)
            ? BOSS_SQUILLA_FAR_Z
            : (p.z + offsetZ);

        if (bossState == BOSS_STATE_LEAVING &&
            p.z >= BOSS_SQUILLA_FAR_Z - BOSS_SQUILLA_LEAVE_VZ)
        {
            if (bossSquilla[i].spr)
                SPR_setVisibility(bossSquilla[i].spr, HIDDEN);
            continue;
        }

        const u8 bucket = squillaBucketForZ(p.z);
        if (!ensureSquillaSegment(i, bucket))
            continue;

        const u16 q = WORLD_proj(p.z);
        const s16 sx = WORLD_screenXq(p.wx, q);
        const s16 sy = WORLD_screenYBq(p.wy, q);
        const s16 x = sx - (SQUILLA_W[i][bucket] / 2);
        const s16 y = sy - SQUILLA_H[i][bucket];

        SPR_setPosition(bossSquilla[i].spr, x, y);
        SPR_setVisibility(bossSquilla[i].spr, VISIBLE);
    }
}

static bool squillaFullyRetreated(void)
{
    for (u8 i = 0; i < BOSS_SQUILLA_SEGMENTS; i++)
    {
        BossPathSample p = squillaHistorySample(SQUILLA_DELAY[i]);
        const u16 offsetZ = SQUILLA_Z_OFFSET[i];
        p.z = (p.z > BOSS_SQUILLA_FAR_Z - offsetZ)
            ? BOSS_SQUILLA_FAR_Z
            : (p.z + offsetZ);
        if (p.z < BOSS_SQUILLA_FAR_Z - BOSS_SQUILLA_LEAVE_VZ)
            return FALSE;
    }
    return TRUE;
}

static void startBossRequest(void)
{
    if (bossState != BOSS_STATE_OFF) return;

    bossRequestArmed = TRUE;
    bossState = BOSS_STATE_PENDING;
    enemySpawningEnabled = FALSE;
    CLOUDS_evict();
}

static void restoreNormalMode(void)
{
    bossState = BOSS_STATE_OFF;
    bossRequestArmed = FALSE;
    enemySpawningEnabled = TRUE;
    enemyPoolReleased = FALSE;
    spawnTimer = 0;

    SHADOW_init();
    CLOUDS_init();
    initTrees();
    initObjectShadows();
    initShots();
    playerShadow = SHADOW_add();
    if (!playerShadow)
        SYS_die("player shadow allocation failed");
    renderer->init();
    SHADOW_init();
}

static void spawnSquillaBoss(void)
{
    PAL_setPalette(PAL2, spr_squilla_head_1.palette->data, DMA_QUEUE);
    SHADOW_init();

    releaseSquillaBoss();
    bossHead.wx = 0;
    bossHead.wy = BOSS_SQUILLA_SPAWN_WY;
    bossHead.z = BOSS_SQUILLA_FAR_Z;
    bossVx = 4;
    bossVy = 2;
    bossVz = -BOSS_SQUILLA_ENTER_VZ;
    bossNearHoldFrames = 0;
    bossApproaching = TRUE;
    fillSquillaHistory();
    recordSquillaHead();
    bossState = BOSS_STATE_ACTIVE;

    KLog_U1("VRAM tiles free after boss spawn: ", SPR_getFreeVRAM());
    KLog_U1("  sprite handles active: ", SPR_getNumActiveSprite());
}

static void updateBossMode(void)
{
    if (bossState == BOSS_STATE_OFF)
        return;

    if (bossState == BOSS_STATE_PENDING)
    {
        if (!bossRequestArmed) return;

        if (countObjects() == 0 && CLOUDS_areOffscreen())
        {
            if (!enemyPoolReleased)
            {
                hideGameplaySpritesForBoss();
                RENDER_storedRelease();
                CLOUDS_release();
                releaseTrees();
                releaseObjectShadows();
                releaseShotsForBoss();
                SHADOW_release(playerShadow);
                playerShadow = NULL;
                enemyPoolReleased = TRUE;
                return;
            }
            else
            {
                bossRequestArmed = FALSE;
                spawnSquillaBoss();
            }
        }
        return;
    }

    if (bossState == BOSS_STATE_RESTORE_WAIT)
    {
        releaseSquillaBoss();
        restoreNormalMode();
        return;
    }

    if (bossState == BOSS_STATE_ACTIVE)
    {
        if (bossNearHoldFrames)
        {
            bossHead.z = BOSS_SQUILLA_NEAR_Z;
            bossNearHoldFrames = (bossNearHoldFrames > gFramesPerUpdate)
                               ? (bossNearHoldFrames - gFramesPerUpdate)
                               : 0;
            if (!bossNearHoldFrames)
                bossVz = BOSS_SQUILLA_ROAM_VZ;
        }
        else
        {
            const s16 stepVz = bossVz * (s16) gFramesPerUpdate;
            if (stepVz < 0 && bossHead.z <= (u16) -stepVz + BOSS_SQUILLA_NEAR_Z)
            {
                bossHead.z = BOSS_SQUILLA_NEAR_Z;
                bossVz = 0;
                bossNearHoldFrames = BOSS_SQUILLA_NEAR_HOLD_FRAMES;
                bossApproaching = FALSE;
            }
            else if (stepVz > 0 &&
                     bossHead.z >= BOSS_SQUILLA_ROAM_FAR_Z - (u16) stepVz)
            {
                bossHead.z = BOSS_SQUILLA_ROAM_FAR_Z;
                bossVz = -BOSS_SQUILLA_ROAM_VZ;
            }
            else
            {
                bossHead.z = (u16) ((s16) bossHead.z + stepVz);
            }
        }

        if (!bossApproaching || bossHead.z < 3000)
        {
            bossHead.wx += bossVx * (s16) gFramesPerUpdate;
            bossHead.wy += bossVy * (s16) gFramesPerUpdate;

            if (bossHead.wx <= -BOSS_SQUILLA_WX_MAX ||
                bossHead.wx >= BOSS_SQUILLA_WX_MAX)
            {
                bossVx = -bossVx;
                if (bossHead.wx < -BOSS_SQUILLA_WX_MAX)
                    bossHead.wx = -BOSS_SQUILLA_WX_MAX;
                if (bossHead.wx > BOSS_SQUILLA_WX_MAX)
                    bossHead.wx = BOSS_SQUILLA_WX_MAX;
            }

            if (bossHead.wy <= BOSS_SQUILLA_WY_MIN ||
                bossHead.wy >= BOSS_SQUILLA_WY_MAX)
            {
                bossVy = -bossVy;
                if (bossHead.wy < BOSS_SQUILLA_WY_MIN)
                    bossHead.wy = BOSS_SQUILLA_WY_MIN;
                if (bossHead.wy > BOSS_SQUILLA_WY_MAX)
                    bossHead.wy = BOSS_SQUILLA_WY_MAX;
            }
        }
    }
    else if (bossState == BOSS_STATE_LEAVING)
    {
        const u16 stepVz = BOSS_SQUILLA_LEAVE_VZ * gFramesPerUpdate;
        const s16 centerStep = BOSS_SQUILLA_LEAVE_CENTER_STEP *
                               (s16) gFramesPerUpdate;

        bossHead.wx = approachS16(bossHead.wx, 0, centerStep);
        bossHead.wy = approachS16(bossHead.wy, BOSS_SQUILLA_SPAWN_WY, centerStep);
        bossHead.z = (bossHead.z >= BOSS_SQUILLA_FAR_Z - stepVz)
                   ? BOSS_SQUILLA_FAR_Z
                   : (bossHead.z + stepVz);

        if (squillaFullyRetreated())
        {
            hideSquillaBoss();
            bossState = BOSS_STATE_RESTORE_WAIT;
            return;
        }
    }

    recordSquillaHead();
    renderSquillaBoss();
}

// --- Input ------------------------------------------------------------------

static void handleInput(void)
{
    const u16 joy = JOY_readJoypad(JOY_1);

    if (inputSettleFrames)
    {
        prevJoy = joy;
        inputSettleFrames--;
        return;
    }

    const u16 pressed = joy & ~prevJoy;
    prevJoy = joy;

    if (pressed & BUTTON_START)
        paused = !paused;

    if (paused) return;

    if (pressed & BUTTON_A)
        setEnemySpeedStep(enemySpeedStep + 1);

    if (pressed & BUTTON_B)
        playGetReady();

    if (pressed & BUTTON_Z)
        setPlayerSpeedStep(playerSpeedStep + 1);

    if (pressed & BUTTON_C)
        SCENEPAL_cycle();

    if (pressed & BUTTON_X)
        fireShot();

    if (pressed & BUTTON_Y)
    {
        if (bossState == BOSS_STATE_ACTIVE)
            bossState = BOSS_STATE_LEAVING;
        else
            startBossRequest();
    }

    const u16 moveStep = playerSpeed * gFramesPerUpdate;
    if (joy & BUTTON_LEFT)  playerX -= moveStep;
    if (joy & BUTTON_RIGHT) playerX += moveStep;
    if (joy & BUTTON_UP)    playerY -= moveStep;
    if (joy & BUTTON_DOWN)  playerY += moveStep;

    if (playerX < PLAYER_MIN_X) playerX = PLAYER_MIN_X;
    if (playerX > PLAYER_MAX_X) playerX = PLAYER_MAX_X;
    if (playerY < PLAYER_MIN_Y) playerY = PLAYER_MIN_Y;
    if (playerY > playerMaxY)   playerY = playerMaxY;
}

// --- Main -------------------------------------------------------------------

int main(bool hardReset)
{
    (void) hardReset;

    VDP_setScreenWidth320();
    if (IS_PAL_SYSTEM) VDP_setScreenHeight240();
    JOY_setSupport(PORT_1, JOY_SUPPORT_6BTN);

    XGM2_loadDriver(TRUE);

    // Ground first: stabilises TILE_MAX_NUM before the sprite engine reserves
    // the VRAM pool used by stored enemy frames, trees, shadows, shots, player.
    GROUND_init();
    SPR_initEx(SPRITE_POOL_TILES);
    MOUNTAINS_init();
    CLOUDS_init();
    SCENEPAL_init();
    SHADOW_init();

    HUD_init();

    playerMaxY = GROUND_HORIZON + WORLD_GROUND_DEPTH
               - PLAYER_FEET_Y + PLAYER_GROUND_SINK;

    PAL_setPalette(PAL1, spr_player.palette->data, CPU);
    playerX = PLAYER_CENTER_X;
    playerY = playerMaxY - 8;
    setPlayerSpeedStep(0);
    setEnemySpeedStep((ENEMY_SPEED_DEFAULT - 10) / 10);
    prevJoy = JOY_readJoypad(JOY_1);
    inputSettleFrames = INPUT_STARTUP_SETTLE_FRAMES;
    enemySpawningEnabled = TRUE;
    enemyPoolReleased = FALSE;
    bossRequestArmed = FALSE;
    bossState = BOSS_STATE_OFF;
    initSquillaBossData();
    player = SPR_addSprite(&spr_player, playerX, playerY,
                           TILE_ATTR(PAL1, 0, FALSE, FALSE));
    if (!player)
        SYS_die("player sprite allocation failed");
    SPR_setDepth(player, PLAYER_DEPTH);
    playerShadow = SHADOW_add();
    if (!playerShadow)
        SYS_die("player shadow allocation failed");
    initTrees();
    initObjectShadows();
    initShots();

    renderer = &RENDER_stored;
    gFramesPerUpdate = 2;
    renderer->init();
    SHADOW_init();
    CLOUDS_applyPalette();

    KLog_U1("VRAM tiles free after init: ", SPR_getFreeVRAM());
    KLog_U1("  sprite handles active: ", SPR_getNumActiveSprite());

    GROUND_update(playerX - PLAYER_CENTER_X, playerPitchY(), playerWorldX(), 0,
                  TRUE);
    MOUNTAINS_update(playerX - PLAYER_CENTER_X,
                     GROUND_horizon + GROUND_VISIBLE_HORIZON_PAD);
    CLOUDS_update(GROUND_horizon + GROUND_VISIBLE_HORIZON_PAD);

    playGetReady();

    while (TRUE)
    {
        handleInput();
        SHADOW_beginFrame();

        if (!paused)
        {
            scrollPhase++;

            // Ground first: it sets GROUND_horizon, which the projection of
            // every object and shot below depends on.
            SCENEPAL_update();
            GROUND_update(playerX - PLAYER_CENTER_X, playerPitchY(), playerWorldX(),
                          GROUND_FORWARD_SPEED * gFramesPerUpdate,
                          (scrollPhase & 1) == 0);
            MOUNTAINS_update(playerX - PLAYER_CENTER_X,
                             GROUND_horizon + GROUND_VISIBLE_HORIZON_PAD);
            CLOUDS_update(GROUND_horizon + GROUND_VISIBLE_HORIZON_PAD);

            if (enemySpawningEnabled)
            {
                spawnTimer += gFramesPerUpdate;
                if (spawnTimer >= WORLD_SPAWN_INTERVAL)
                {
                    spawnTimer = 0;
                    spawnObject();
                }
            }

            if (bossState == BOSS_STATE_OFF ||
                (bossState == BOSS_STATE_PENDING && !enemyPoolReleased))
            {
                updateTrees();
                updateObjects();
                updateShots();
            }
            updateBossMode();
            if (renderer->frame) renderer->frame();

            if (hurtFlash)
            {
                hurtFlash = (hurtFlash > gFramesPerUpdate)
                          ? (hurtFlash - gFramesPerUpdate)
                          : 0;
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

        HUD_update(renderer->name, countObjects, hits, enemySpeedPct);

        if (playerShadow)
            SHADOW_place(playerShadow, playerWorldX(), WORLD_Z_NEAR, PLAYER_W);
        if (player) SPR_setPosition(player, playerX, playerY);
        SPR_update();
        // First VBlank: SGDK waits for VBlank start, then flushes DMA/services.
        SYS_doVBlankProcess();
        for (u8 i = 1; i < gFramesPerUpdate; i++)
            VDP_waitVSync();
    }

    return 0;
}
