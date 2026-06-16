# Task: Pool sprites & shadows (stop allocation spikes in STORED mode)

## Background
After the 60 Hz + enemy-mirror changes, the remaining CPU spikes are allocation
churn: `SPR_addSprite`/`SPR_releaseSprite` + shadow add/release on every spawn, kill,
shot, and enemy tier crossing (16->32->64). The mirror change actually doubled the
per-tier-crossing cost (two sprites now). The trees already avoid this — they create
their sprite pairs once in `initTrees`/`initTreePair` and recycle them. Do the same for
enemies, shots, and shadows: pre-create at boot, then toggle visibility.

Target: zero `SPR_addSprite`/`SPR_releaseSprite`/`SHADOW_add`/`SHADOW_release` calls
during gameplay (spawn, kill, fire, tier change). RUNTIME mode stays as-is and may
degrade gracefully (see Part 4).

## Part 1 — Give each object a stable slot id (`src/engine/world.h`, `src/main.c`)
- Add `u8 slot;` to `WObj` (renderer bookkeeping, like `sprs[]`).
- In `spawnObject()`, set `o->slot = (u8) i;` where `i` is the objects[] loop index.

## Part 2 — Pre-create shadows & shot sprites at boot (`src/main.c`)
Shadows: the shadow registry is already sized for everything
(`SHADOW_REGISTRY_COUNT = MAX_OBJECTS + MAX_SHOTS + 1 + 3 = 11`). Move all `SHADOW_add()`
calls to boot so none happen during play:
- At init, create one permanent shadow per object slot: `static Sprite* objShadows[MAX_OBJECTS];`
  filled via `SHADOW_add()` once (after `SHADOW_init`). Player and trees already do this.
- In `spawnObject()`: set `o->shadow = objShadows[o->slot];` (do NOT call `SHADOW_add`).
- In `killObject()`: call `SHADOW_hide(o->shadow)` and leave the pointer; do NOT call
  `SHADOW_release`.

Shots: pre-create the shot sprite AND shadow for every shot slot at boot:
- At init, for each `shots[i]`: `shots[i].spr = SPR_addSprite(&spr_shot, -16,-16, TILE_ATTR(PAL2,0,FALSE,FALSE));`
  set it HIDDEN; `shots[i].shadow = SHADOW_add();` (uses 4 of the registry).
- In `fireShot()`: reuse `shots[i].spr` — reposition + `SPR_setVisibility(VISIBLE)`; do NOT
  `SPR_addSprite` or `SHADOW_add`.
- In `killShot()`: `SPR_setVisibility(s->spr, HIDDEN)` + `SHADOW_hide(s->shadow)`; do NOT
  release either. Set `active = FALSE`.

`src/engine/shadow.c` needs NO changes — `SHADOW_add` is now only ever called at boot.

## Part 3 — Pre-create enemy tier pairs, no realloc (`src/prototypes/stored_frames/render_stored.c`)
Replace per-object create/release with a persistent per-slot, per-tier pool built once.
This SUPERSEDES storing the left/right pair in `o->sprs[]` (free those for RUNTIME's use).

- Add a static pool and a build guard:
      static Sprite* objLeft [MAX_OBJECTS][TIER_COUNT];
      static Sprite* objRight[MAX_OBJECTS][TIER_COUNT];
      static bool poolBuilt;
- In `st_init`, after the palette/LUT setup, build the pool ONCE (guard with `poolBuilt`):
  for each slot, for each tier, create the LEFT sprite (auto-alloc) and the mirrored
  RIGHT sprite sharing its tiles (H-flip, flag 0) — exactly like `initTreePair` but PAL2:
      objLeft[s][t]  = SPR_addSprite(FRAME_TIERS[t].def, -128,-128, TILE_ATTR(PAL2,0,FALSE,FALSE));
      sharedTile     = objLeft[s][t]->attribut & ~TILE_ATTR_MASK;
      objRight[s][t] = SPR_addSpriteEx(FRAME_TIERS[t].def, -128,-128,
                                       TILE_ATTR_FULL(PAL2,0,FALSE,TRUE,sharedTile), 0);
  Hide both. (Right shares tiles => no extra VRAM; only the LEFT reserves tiles.)
- Track the active tier per object in `o->vramIndex` (as today: tier index, `TIER_NONE` = none).
- `st_spawn(o)`: `o->vramIndex = TIER_NONE; o->sizeIdx = 0xFF; setStoredFrame(o, 0);`
- `setStoredFrame(o, frame)`:
  - compute `tier = frameToTier(frame)`;
  - if `tier != o->vramIndex`: hide the old active pair (`objLeft/Right[o->slot][old]`) if any,
    set `o->vramIndex = tier`, make the new pair VISIBLE, and force a frame reload (`sizeIdx=0xFF`);
  - if `frame != o->sizeIdx`: `o->sizeIdx = frame;` and
    `SPR_setFrame(objLeft[o->slot][tier], frame - tier->firstFrame)` + same for `objRight`.
  - NO `SPR_addSprite` / `SPR_releaseSprite` anywhere.
- `st_update(o, sx, syBottom, sizePx)`: after `setStoredFrame`, position the ACTIVE pair
  (`tier = o->vramIndex`, `canvasPx = FRAME_TIERS[tier].canvasPx`, `halfPx = canvasPx/2`):
      left : SPR_setPosition(objLeft [o->slot][tier], sx - (canvasPx/2),          syBottom - canvasPx);
      right: SPR_setPosition(objRight[o->slot][tier], sx - (canvasPx/2) + halfPx, syBottom - canvasPx);
- `st_despawn(o)`: hide ALL tier pairs for `o->slot`; set `o->vramIndex = TIER_NONE`.
  Do NOT release. Do NOT touch `o->sprs[]` (RUNTIME owns those).

## Part 4 — Build & verify
1. `make clean && make` — builds clean, no new warnings.
2. VRAM/sprite budget: pre-creating the pool reserves sprite-engine VRAM permanently
   (enemy LEFT sprites across 3 tiers/slot + the boot shadows). Confirm NO sprite
   allocation returns NULL at boot (add a temporary assert/log if needed). If the sprite
   VRAM budget is exceeded, raise it in `RUNTIME_spriteVramBudget()` / the boot
   `SPR_initEx(...)` call, or reduce reserved shadow frames — do not silently ship NULLs.
3. STORED gameplay: enemies spawn, approach, cross tiers (16->32->64), and die with no
   missing/garbled halves and no flicker. Shadows and shots behave exactly as before.
4. HUD: the CPU spikes on spawn/kill/tier-crossing are gone or much smaller; FPS holds
   59-60 far more consistently; no dropped frames in normal waves.
5. RUNTIME mode (press C): must still switch without crashing. If the stored pool leaves
   too little VRAM for runtime slots, the existing `RUNTIME_slotCapacity()==0` fallback in
   `setRenderer()` should keep it in STORED — verify that path doesn't crash. (RUNTIME is a
   debug mode; graceful degradation is acceptable, a crash is not.)

## Notes
- Hidden SGDK sprites emit nothing to the hardware sprite list, so the idle pool costs
  ~nothing per scanline; it only costs reserved VRAM (modest: LEFT halves only).
- Keep all magic numbers/behaviour otherwise identical; this is a churn-removal refactor,
  not a gameplay change.
