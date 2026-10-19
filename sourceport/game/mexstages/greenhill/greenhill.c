/* Green Hill Zone (Akaneia 1.0.1 and ACE 2.0.0, the same file: internal 85, external 302), the m-ex
 * grFunction block of GrGh.dat: 1,408 instructions with symbols (disassembly:
 * `py -3.12 grdis.py ak GrGh.dat`, run-source/rel09-stages). Sound bank 69 (silent until it loads).
 *
 * Every export is its own (1, 3 to 9). Fifteen map gobjs: 0 Points, 1 Background, 2 Stage (the
 * floor: collision, and the stage's own state), 3 to 5 and 12 to 14 the background characters that
 * run along a path (6), 7 to 9 three breakable walls, 10 the checkpoint, 11 the background colour.
 * The Stage gobj spawns a character every few seconds (or three in a row), and a checkpoint at one
 * of four places; hitting the checkpoint spins it, a wall that loses its hit points breaks and comes
 * back later. Characters a player is playing (by MxDt name: "Tails", "Knuckles", "Amy" or
 * "Amy Rose") run as their second model (12 to 14) instead.
 *
 * The block keeps its state in each map gobj's Ground at +E0 and up (console offsets). Natively
 * those are the named fields of the structs below, overlaid on Ground.u (+C4 on the console; the
 * block leaves +C4 to +DF unused). Floats follow the block: it never fuses a multiply-add (each
 * product is an fmuls of its own), so every function here is MU_NO_CONTRACT. */
#include "akfamily_lib.h"

#include <melee/cm/camera.h>
#include <melee/gr/granime.h>
#include <melee/gr/grlib.h>
#include <melee/gr/grmaterial.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/it/item.h>
#include <melee/it/itcoll.h>
#include <melee/it/inlines.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <melee/mp/mplib.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/object.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/tobj.h>

#include <string.h>

/* yakumono_param (disc data, big-endian): the fields the block reads. */
typedef struct GhParam {
    /* +00 */ f32 cp_spin;         /* checkpoint: spin frames once hit (236) */
    /* +04 */ f32 cp_life_min;     /* checkpoint: frames it stays, a random value in [min, max) */
    /* +08 */ f32 cp_life_max;
    /* +0C */ f32 cp_wait;         /* frames before the next checkpoint (180) */
    /* +10 */ s32 x10;             /* not read */
    /* +14 */ f32 wall_hp;         /* 125 */
    /* +18 */ s32 wall_return_min; /* frames a wall stays intact before it may break? (5400) */
    /* +1C */ s32 wall_return_max; /* (9000) */
    /* +20 */ s32 wall_rest;       /* frames a broken wall stays down (1200) */
    /* +24 */ f32 x24;             /* not read */
    /* +28 */ Vec3_BE wall_box[6]; /* the hurtbox capsule ends of walls 7, 8, 9 (two each) */
    /* +70 */ f32 char_period;     /* frames between characters (900) */
    /* +74 */ f32 path_rate;       /* the path animation's frame rate (2) */
    /* +78 */ f32 char_group;      /* chance of three characters in a row (0.1) */
    /* +7C */ f32 char_gap;        /* frames between the three (30) */
} DISC_STRUCT GhParam;

/* Map gobj 2, Stage. */
typedef struct GhStageVars {
    u8 pad_C4[0xE0 - 0xC4];
    /* +E0 */ int char_timer;
    /* +E4 */ int char_count;      /* counts down through a group of three */
    /* +E8 */ HSD_GObj* wall8;
    /* +EC */ HSD_GObj* wall9;
    /* +F0 */ HSD_GObj* wall7;
    /* +F4 */ int checkpoint_timer;
    /* +F8 */ HSD_GObj* checkpoint;
    /* +FC */ u8 spawn;            /* 0x80 Tails, 0x40 Knuckles, 0x20 Amy: set = nobody plays it */
} GhStageVars;

/* Map gobjs 3 to 5, 12 to 14, Character. */
typedef struct GhCharVars {
    u8 pad_C4[0xE0 - 0xC4];
    /* +E0 */ HSD_GObj* path;
} GhCharVars;

/* Map gobjs 7 to 9, Breakable. */
typedef struct GhWallVars {
    u8 pad_C4[0xE0 - 0xC4];
    /* +E0 */ int state;           /* 0 intact, 1 breaking, 3 down, 2 coming back */
    /* +E4 */ int timer;
    /* +E8 */ f32 hp;
    /* +EC */ Item_GObj* item;
} GhWallVars;

/* Map gobj 10, Checkpoint. */
typedef struct GhCheckpointVars {
    u8 pad_C4[0xE0 - 0xC4];
    /* +E0 */ HSD_GObj* self;
    /* +E4 */ int state;           /* 0 appear, 1 leave, 2 fall, 3 spin (hit), 4 idle */
    /* +E8 */ int life;
    /* +EC */ int spin;
    /* +F0 */ HSD_GObj* stage;
    /* +F4 */ HSD_GObj* standing;  /* the wall it stands on, NULL on the floor */
    /* +F8 */ Vec3 pos;
    /* +104 */ Item_GObj* item;
} GhCheckpointVars;

MEXGR_WP4_VARS_FIT(GhStageVars);
MEXGR_WP4_VARS_FIT(GhCharVars);
MEXGR_WP4_VARS_FIT(GhWallVars);
MEXGR_WP4_VARS_FIT(GhCheckpointVars);

#define GH_STAGE(gp) MEXGR_WP4_VARS(GhStageVars, gp)
#define GH_CHAR(gp) MEXGR_WP4_VARS(GhCharVars, gp)
#define GH_WALL(gp) MEXGR_WP4_VARS(GhWallVars, gp)
#define GH_CP(gp) MEXGR_WP4_VARS(GhCheckpointVars, gp)

/* .bss.gr_param (+D34), set at on_init. */
static GhParam* gh_param;

static void gh_checkpoint_change_state(HSD_GObj* gobj, int state);
static void gh_wall_change_state(HSD_GObj* gobj, int state);

/* OnInit (+12C) */
static void gh_init(void)
{
    gh_param = mexgr_wp4_yakumono("greenhill.c", 0x3BF);
    mu_grTSeak_80223908(0);
    mu_grTSeak_80223908(1);
    mu_grTSeak_80223908(2);
    mu_grTSeak_80223908(11);
    Ground_801C39C0();
    Ground_801C3BB4();
}

/* Stage_UpdateCollisions (+D5C): every collision joint of the floor off, then on again by which of
 * the three walls stand (a missing wall counts as broken). The order of the mpJointListAdd calls
 * is the block's. */
static void gh_update_collisions(void)
{
    Ground* gp = HSD_GObjGetUserData(Ground_GetMapGObj(2));
    GhStageVars* sv = GH_STAGE(gp);
    int broken8, broken9, broken7;
    mpLib_80057BC0(0);
    mpLib_80057BC0(1);
    mpLib_80057BC0(2);
    mpLib_80057BC0(3);
    mpLib_80057BC0(4);
    mpLib_80057BC0(5);
    mpLib_80057BC0(6);
    mpLib_80057BC0(7);
    mpLib_80057BC0(8);
    broken8 = sv->wall8 == NULL || GH_WALL((Ground*) HSD_GObjGetUserData(sv->wall8))->state != 0;
    broken9 = sv->wall9 == NULL || GH_WALL((Ground*) HSD_GObjGetUserData(sv->wall9))->state != 0;
    broken7 = sv->wall7 == NULL || GH_WALL((Ground*) HSD_GObjGetUserData(sv->wall7))->state != 0;
    if (!broken9) {
        mpJointListAdd(0);
        if (!broken8) {
            mpJointListAdd(1);
        } else {
            mpJointListAdd(3);
            mpJointListAdd(4);
        }
        if (!broken7) {
            mpJointListAdd(2);
        } else {
            mpJointListAdd(8);
            mpJointListAdd(7);
        }
    } else {
        if (!broken8) {
            mpJointListAdd(1);
            mpJointListAdd(5);
        } else {
            mpJointListAdd(3);
        }
        if (!broken7) {
            mpJointListAdd(2);
            mpJointListAdd(6);
        } else {
            mpJointListAdd(8);
        }
    }
}

/* Stage_SpawnCharacter (+FB8): character `which` (0 Tails, 1 Knuckles, 2 Amy) as map gobj 3 + which,
 * or 12 + which when a player plays that character. */
static void gh_spawn_character(HSD_GObj* gobj, int which)
{
    GhStageVars* sv = GH_STAGE((Ground*) HSD_GObjGetUserData(gobj));
    u8 bit;
    if (which == 0) {
        bit = 0x80;
    } else if ((unsigned) which == 1) {
        bit = 0x40;
    } else if ((unsigned) which == 2) {
        bit = 0x20;
    } else {
        mu_grTSeak_80223908(which + 3);
        return;
    }
    mu_grTSeak_80223908((sv->spawn & bit) ? which + 3 : which + 12);
}

/* Stage_OnCreation (+250) */
static void gh_stage_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    GhStageVars* sv = GH_STAGE(gp);
    int slot;
    grAnime_801C8138(gobj, gp->map_id, 0);
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
    sv->char_timer = (int) gh_param->char_period;
    sv->char_count = 0;
    sv->wall7 = mu_grTSeak_80223908(7);
    sv->wall9 = mu_grTSeak_80223908(9);
    sv->wall8 = mu_grTSeak_80223908(8);
    sv->checkpoint_timer = (int) gh_param->cp_wait;
    sv->checkpoint = NULL;
    gh_update_collisions();
    sv->spawn |= 0xE0;
    /* Fighter_GetMEXName (+F44): MEX_GetData(12)[Player_GetPlayerCharacter(slot)]; the native slot
     * holds a native character kind, which mu_gr_mex_fighter_name maps back to the disc's external
     * id. It is never NULL for a fighter in a match; NULL is skipped (strcmp would fault). */
    for (slot = 0; slot != 6; slot++) {
        const char* name;
        if (Player_GetPlayerSlotType(slot) == 3) {
            continue;
        }
        name = mu_gr_mex_fighter_name(Player_GetPlayerCharacter(slot));
        if (name == NULL) {
            continue;
        }
        if (strcmp("Tails", name) == 0) {
            sv->spawn &= ~0x80;
        } else if (strcmp("Knuckles", name) == 0) {
            sv->spawn &= ~0x40;
        } else if (strcmp("Amy", name) == 0 || strcmp("Amy Rose", name) == 0) {
            sv->spawn &= ~0x20;
        }
    }
}

/* Stage_OnFrame (+400) */
MU_NO_CONTRACT static void gh_stage_frame(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    GhStageVars* sv = GH_STAGE(gp);
    Ground_UpdateMapColl(gobj);
    if (sv->char_timer != 0) {
        sv->char_timer--;
    } else {
        sv->char_timer = (int) gh_param->char_period;
        /* "cror eq, gt, un": one character unless the draw is at most the group chance. */
        if (!(HSD_Randf() <= gh_param->char_group)) {
            gh_spawn_character(gobj, HSD_Randi(3));
        } else {
            float gap = gh_param->char_gap;
            sv->char_count = (int) (gap + gap);
        }
    }
    if (sv->char_count > 0) {
        float gap = gh_param->char_gap;
        if ((float) sv->char_count == gap + gap) {
            gh_spawn_character(gobj, 0);
        }
        if ((float) sv->char_count == gh_param->char_gap) {
            gh_spawn_character(gobj, 1);
        }
        if (sv->char_count == 1) {
            gh_spawn_character(gobj, 2);
        }
        sv->char_count--;
    }
    if (sv->checkpoint == NULL) {
        if (sv->checkpoint_timer > 0) {
            sv->checkpoint_timer--;
        } else {
            /* +5A8: a new checkpoint at one of four floor joints 0x4D to 0x50. 0x4E stands on wall
             * 8 and 0x4F on wall 7; when that wall is down the next place on the floor is taken
             * (0x4D, 0x50). The block reads both walls' states first, unchecked. */
            HSD_GObj* cp_gobj = mu_grTSeak_80223908(10);
            GhCheckpointVars* cv;
            int wall8_up, wall7_up, joint;
            HSD_GObj* standing = NULL;
            sv->checkpoint = cp_gobj;
            sv->checkpoint_timer = (int) gh_param->cp_wait;
            cv = GH_CP((Ground*) HSD_GObjGetUserData(cp_gobj));
            cv->stage = gobj;
            joint = HSD_Randi(4) + 0x4D;
            wall8_up = GH_WALL((Ground*) HSD_GObjGetUserData(sv->wall8))->state == 0;
            wall7_up = GH_WALL((Ground*) HSD_GObjGetUserData(sv->wall7))->state == 0;
            if (joint == 0x4E) {
                if (wall8_up) {
                    standing = sv->wall8;
                } else {
                    joint = 0x4D;
                }
            } else if (joint == 0x4F) {
                if (wall7_up) {
                    standing = sv->wall7;
                } else {
                    joint = 0x50;
                }
            }
            cv->standing = standing;
            lb_8000B1CC(Ground_801C3FA4(gobj, joint), NULL, &cv->pos);
            gh_checkpoint_change_state(sv->checkpoint, 0);
        }
    }
    lb_800115F4();
}

/* Character_OnCreation (+6BC): its path (map gobj 6) is made with it, and the character's joint
 * follows the path's joint 1. */
static void gh_char_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    GhCharVars* cv = GH_CHAR(gp);
    HSD_JObj* jobj;
    cv->path = mu_grTSeak_80223908(6);
    grAnime_801C8138(gobj, gp->map_id, 0);
    jobj = Ground_801C3FA4(gobj, 0);
    lb_8000C2F8(jobj, Ground_801C3FA4(cv->path, 1));
}

/* Character_OnFrame (+738): when the path's animation ends, both go. */
static void gh_char_frame(Ground_GObj* gobj)
{
    GhCharVars* cv = GH_CHAR((Ground*) HSD_GObjGetUserData(gobj));
    if (grAnime_801C84A4(cv->path, 0, 7) == true) {
        Ground_801C4A08(cv->path);
        Ground_801C4A08(gobj);
    }
}

/* Path_SetAnimationSpeed (+11A8), an HSD_ForeachAnim callback (AOBJ_ARG_AF: its float is not read). */
static void gh_path_rate(HSD_AObj* aobj, f32 unused)
{
    (void) unused;
    if (aobj != NULL) {
        aobj->framerate = gh_param->path_rate;
    }
}

/* Path_OnCreation (+7A0). The block calls HSD_ForeachAnim with arg type 1 (AOBJ_ARG_AF) and no float
 * argument (crclr cr1eq): the callback ignores it, so 0 is passed here. */
static void gh_path_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
    HSD_ForeachAnim(Ground_801C3FA4(gobj, 0), JOBJ_TYPE, ALL_TYPE_MASK & ~(MOBJ_MASK | TOBJ_MASK),
                    gh_path_rate, AOBJ_ARG_AF, 0.0);
}

/* Breakable_OnHurt (+11C4), the hurtbox item's damage callback. */
MU_NO_CONTRACT static void gh_wall_hurt(Item_GObj* item, Ground* gp, Vec3* pos, HSD_GObj* src,
                                        f32 damage)
{
    GhWallVars* wv = GH_WALL(gp);
    (void) pos;
    (void) src;
    wv->hp = wv->hp - damage;
    if (wv->hp <= 0.0f) {
        it_80271508(item, HurtCapsule_Intangible);
    }
}

/* Breakable_ChangeState (+120C) */
MU_NO_CONTRACT static void gh_wall_change_state(HSD_GObj* gobj, int state)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    GhWallVars* wv = GH_WALL(gp);
    HSD_JObjClearFlagsAll(GET_JOBJ(gobj), JOBJ_HIDDEN);
    grAnime_801C8138(gobj, gp->map_id, state);
    wv->state = state;
    switch (state) {
    case 2: {
        Item* ip;
        Item_80268E5C(wv->item, 1, ITEM_ANIM_UPDATE);
        ip = GET_ITEM(wv->item);
        lb_8000B1CC(Ground_801C3FA4(gobj, 2), NULL, &ip->x5D4_hitboxes[0].hit.b_offset);
        break;
    }
    case 0: {
        const s32 lo = gh_param->wall_return_min;
        const float range = (float) (gh_param->wall_return_max - lo);
        float product;
        wv->hp = gh_param->wall_hp;
        product = range * HSD_Randf();
        wv->timer = (int) ((float) lo + product);
        it_80271508(wv->item, HurtCapsule_Enabled);
        Item_80268E5C(wv->item, 0, ITEM_ANIM_UPDATE);
        gh_update_collisions();
        break;
    }
    case 1:
        Item_80268E5C(wv->item, 0, ITEM_ANIM_UPDATE);
        gh_update_collisions();
        mu_gr_mex_sfx(2);
        break;
    case 3:
        Item_80268E5C(wv->item, 0, ITEM_ANIM_UPDATE);
        wv->timer = gh_param->wall_rest;
        break;
    }
}

/* Breakable_OnCreation (+814): collision, the hurtbox item from the param capsule of this wall,
 * state 0. */
static void gh_wall_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    GhWallVars* wv = GH_WALL(gp);
    int i;
    Vec3 a, b;
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
    i = (gp->map_id - 7) * 2;
    a = BE_VEC3(gh_param->wall_box[i]);
    b = BE_VEC3(gh_param->wall_box[i + 1]);
    wv->item = mexgr_wp4_hurtbox(gp, Ground_801C3FA4(gobj, 0), gh_wall_hurt, a.x, a.y, a.z, b.x,
                                 b.y, b.z, 4.5f);
    gh_wall_change_state(gobj, 0);
}

/* Breakable_OnFrame (+8F8) */
static void gh_wall_frame(Ground_GObj* gobj)
{
    GhWallVars* wv = GH_WALL((Ground*) HSD_GObjGetUserData(gobj));
    switch (wv->state) {
    case 2:
        if (!lb_8000B09C(Ground_801C3FA4(gobj, 0))) {
            gh_wall_change_state(gobj, 0);
        }
        break;
    case 0:
        if (wv->hp <= 0.0f) {   /* "cror eq, gt, un; bne": neither above 0 nor NaN */
            gh_wall_change_state(gobj, 1);
        }
        break;
    case 1:
        if (!lb_8000B09C(Ground_801C3FA4(gobj, 0))) {
            gh_wall_change_state(gobj, 3);
        }
        break;
    case 3:
        if (wv->timer > 0) {
            wv->timer--;
        } else {
            gh_wall_change_state(gobj, 2);
        }
        break;
    }
    Ground_UpdateMapColl(gobj);
}

/* Checkpoint_PlaybackFlags (+15A0), an HSD_ForeachAnim callback: the animation plays once. */
static void gh_checkpoint_playback(HSD_AObj* aobj, f32 unused)
{
    (void) unused;
    aobj->flags = AOBJ_FIRST_PLAY;
}

/* Checkpoint_ChangeState (+102C) */
static void gh_checkpoint_change_state(HSD_GObj* gobj, int state)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    GhCheckpointVars* cv = GH_CP(gp);
    HSD_JObj* jobj;
    grAnime_801C8138(gobj, gp->map_id, state);
    if (state == 3) {
        cv->state = 3;
        jobj = Ground_801C3FA4(gobj, 0);
        jobj->translate = cv->pos;
        HSD_JObjSetMtxDirtySub(jobj);
        it_80271508(cv->item, HurtCapsule_Intangible);
        Item_80268E5C(cv->item, 0, ITEM_ANIM_UPDATE);
        GET_ITEM(cv->item)->owner = NULL;
        mu_gr_mex_sfx(0);
        Item_80268E5C(cv->item, 2, ITEM_ANIM_UPDATE);
        cv->spin = (int) gh_param->cp_spin;
        return;
    }
    /* No float argument passed for AOBJ_ARG_AF, as at Path_OnCreation. */
    HSD_ForeachAnim(Ground_801C3FA4(gobj, 0), JOBJ_TYPE, ALL_TYPE_MASK & ~(MOBJ_MASK | TOBJ_MASK),
                    gh_checkpoint_playback, AOBJ_ARG_AF, 0.0);
    cv->state = state;
    jobj = Ground_801C3FA4(gobj, 0);
    jobj->translate = cv->pos;
    HSD_JObjSetMtxDirtySub(jobj);
    it_80271508(cv->item, HurtCapsule_Intangible);
    Item_80268E5C(cv->item, 0, ITEM_ANIM_UPDATE);
    GET_ITEM(cv->item)->owner = NULL;
    if (state == 4) {
        it_80271508(cv->item, HurtCapsule_Enabled);
    }
}

/* Checkpoint_OnHurt (+13A8): the checkpoint spins; the item's owner becomes who hit it. */
static void gh_checkpoint_hurt(Item_GObj* item, Ground* gp, Vec3* pos, HSD_GObj* src, f32 damage)
{
    Item* ip = GET_ITEM(item);
    (void) pos;
    (void) src;
    (void) damage;
    gh_checkpoint_change_state(GH_CP(gp)->self, 3);
    ip->owner = ip->xCEC_fighterGObj;
    if (ip->owner == NULL) {
        ip->owner = ip->xCF0_itemGObj;
    }
}

/* Checkpoint_OnDestroy (+13FC), the gobj's x1C destroy callback. */
static void gh_checkpoint_destroy(HSD_GObj* gobj)
{
    GhCheckpointVars* cv = GH_CP((Ground*) HSD_GObjGetUserData(gobj));
    if (cv->item != NULL) {
        Item_8026A8EC(cv->item);
    }
    if (cv->stage != NULL) {
        GH_STAGE((Ground*) HSD_GObjGetUserData(cv->stage))->checkpoint = NULL;
    }
}

/* Checkpoint_OnCreation (+9EC) */
MU_NO_CONTRACT static void gh_checkpoint_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    GhCheckpointVars* cv = GH_CP(gp);
    float lo, range, product;
    cv->item = mexgr_wp4_hurtbox(gp, Ground_801C3FA4(gobj, 4), gh_checkpoint_hurt, 0.0f, 0.0f,
                                 10.0f, 0.0f, 0.0f, 10.0f, 4.0f);
    gh_checkpoint_change_state(gobj, 0);
    cv->self = gobj;
    lo = gh_param->cp_life_min;
    range = gh_param->cp_life_max - lo;
    product = range * HSD_Randf();
    cv->life = (int) (product + lo);
    gp->x1C_callback = gh_checkpoint_destroy;
}

/* JOBJ_GetAnimFrame (+14B0): the current frame of the first animation found on the joint, its
 * DObjs (their own, their MObj's, their TObjs'), then down the first child chain; -1 when none. */
static float gh_anim_frame(HSD_JObj* jobj)
{
    if (jobj == NULL) {
        return -1.0f;
    }
    do {
        HSD_DObj* dobj;
        if (jobj->aobj != NULL) {
            return jobj->aobj->curr_frame;
        }
        for (dobj = jobj->u.dobj; dobj != NULL; dobj = dobj->next) {
            HSD_TObj* tobj;
            if (dobj->aobj != NULL) {
                return dobj->aobj->curr_frame;
            }
            if (dobj->mobj->aobj != NULL) {
                return dobj->mobj->aobj->curr_frame;
            }
            for (tobj = dobj->mobj->tobj; tobj != NULL; tobj = tobj->next) {
                if (tobj->aobj != NULL) {
                    return tobj->aobj->curr_frame;
                }
            }
        }
        jobj = jobj->child;
    } while (jobj != NULL);
    return -1.0f;
}

/* Checkpoint_DecrementLife (+1560) */
static void gh_checkpoint_life(HSD_GObj* gobj)
{
    GhCheckpointVars* cv = GH_CP((Ground*) HSD_GObjGetUserData(gobj));
    if (cv->life > 0) {
        cv->life--;
    } else {
        gh_checkpoint_change_state(gobj, 1);
    }
}

/* Checkpoint_CheckBrokenFloor (+144C): the wall under the checkpoint broke, it falls. */
static void gh_checkpoint_floor(HSD_GObj* gobj)
{
    GhCheckpointVars* cv = GH_CP((Ground*) HSD_GObjGetUserData(gobj));
    if (cv->state == 2 || cv->standing == NULL) {
        return;
    }
    if (GH_WALL((Ground*) HSD_GObjGetUserData(cv->standing))->state == 0) {
        return;
    }
    gh_checkpoint_change_state(gobj, 2);
}

/* Checkpoint_OnFrame (+AE0; its jump table at +149C points into the byte for byte copy at +15AC). */
MU_NO_CONTRACT static void gh_checkpoint_frame(Ground_GObj* gobj)
{
    GhCheckpointVars* cv = GH_CP((Ground*) HSD_GObjGetUserData(gobj));
    gh_checkpoint_floor(gobj);
    switch ((unsigned) cv->state) {
    case 0:
        if (gh_anim_frame(Ground_801C3FA4(gobj, 0)) == 12.0f) {
            mu_gr_mex_sfx(3);
        }
        if (!lb_8000B09C(Ground_801C3FA4(gobj, 0))) {
            gh_checkpoint_change_state(gobj, 4);
        }
        break;
    case 1:
        if (gh_anim_frame(Ground_801C3FA4(gobj, 0)) == 25.0f) {
            mu_gr_mex_sfx(4);
        }
        if (!lb_8000B09C(Ground_801C3FA4(gobj, 0))) {
            Ground_801C4A08(gobj);
        }
        break;
    case 2:
        if (!lb_8000B09C(Ground_801C3FA4(gobj, 0))) {
            Ground_801C4A08(gobj);
        }
        break;
    case 3: {
        Vec3 pos;
        float t, base, quarter, half;
        if (cv->spin <= 0) {
            gh_checkpoint_change_state(gobj, 4);
            break;
        }
        lb_8000B1CC(Ground_801C3FA4(gobj, 4), NULL, &pos);
        t = (float) cv->spin;
        base = gh_param->cp_spin;
        quarter = base * 0.25f;
        if (t < quarter) {
            grLib_801C96F8(0x7532, 30, &pos);
        } else {
            half = base * 0.5f;
            if (!(t < half)) {
                grLib_801C96F8(0x7530, 30, &pos);
            } else {
                grLib_801C96F8(0x7531, 30, &pos);
            }
        }
        cv->spin--;
        break;
    }
    case 4:
        gh_checkpoint_life(gobj);
        break;
    }
}

/* BGColor_OnCreation (+CF4): no animation; x10 bit 2 off, x11 bits 0 to 2 = 2 (the halfword at
 * +10: clear, clear, or 0x40), the material flags 0x28000000 on its joints. */
static void gh_bgcolor_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    gp->x11_flags.b012 = 2;
    gp->x10_flags.b2 = 0;
    grMaterial_801C8858(GET_JOBJ(gobj), 0x28000000);
}

#define GH_ROW(create, frame) { create, mexgr_wp4_unk, frame, mexgr_wp4_deletion, 0 }

/* map_gobjs (+0): OnCreation, OnUnk, OnFrame, OnDeletion, flags. */
static StageCallbacks gh_map[15] = {
    GH_ROW(mexgr_wp4_anim0_create, mu_gr_gobj_nop),          /* 0 Points */
    GH_ROW(mexgr_wp4_background_create, mu_gr_gobj_nop),     /* 1 Background */
    { gh_stage_create, mexgr_wp4_unk, gh_stage_frame, mexgr_wp4_deletion, 0xC0000000 },  /* 2 */
    GH_ROW(gh_char_create, gh_char_frame),                   /* 3 Character */
    GH_ROW(gh_char_create, gh_char_frame),                   /* 4 */
    GH_ROW(gh_char_create, gh_char_frame),                   /* 5 */
    GH_ROW(gh_path_create, mu_gr_gobj_nop),                  /* 6 Path */
    GH_ROW(gh_wall_create, gh_wall_frame),                   /* 7 Breakable */
    GH_ROW(gh_wall_create, gh_wall_frame),                   /* 8 */
    GH_ROW(gh_wall_create, gh_wall_frame),                   /* 9 */
    GH_ROW(gh_checkpoint_create, gh_checkpoint_frame),       /* 10 Checkpoint */
    { gh_bgcolor_create, NULL, NULL, NULL, 0 },              /* 11 BGColor */
    GH_ROW(gh_char_create, gh_char_frame),                   /* 12 Character, second models */
    GH_ROW(gh_char_create, gh_char_frame),                   /* 13 */
    GH_ROW(gh_char_create, gh_char_frame),                   /* 14 */
};

const MuGrStage mu_gr_green_hill_zone = {
    .name = "Green Hill Zone",
    .file = "/GrGh.dat",
    .map_gobjs = gh_map,
    .on_init = gh_init,
    .on_demo_init = mu_gr_demo_nop,
    .on_load = mu_gr_event_nop,
    .on_start = mu_gr_event_nop,
    .callback4 = mu_gr_false,
    .on_touch_line = mu_gr_no_touch_line,
    .on_check_shadow_render = mu_gr_shadow_always,
    .flags = MU_GR_READY,
};
