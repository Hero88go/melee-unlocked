/* ACE's Toad: the meter (m-ex "ftFunction" of PlTd.dat, +075C, +17F8, +1BD0, +1C80, +1DBC and
 * the data at +08A0).
 *
 * At load the file opens Meters.dat, takes the model its public symbol "Relax_scene_models"
 * points at and makes a HUD object of it (the retail helper at 0x8019035C: object class 14, draw
 * link 11, the per-frame routine below at priority 0). The object's user data names the fighter
 * and his player slot.
 *
 * Every frame that routine (1) counts the meter down and rewrites the fighter's attributes from
 * the level, and (2) places the model next to the player's damage display, tints it by the level
 * and moves its animation toward frame timer / 12. So the gameplay part of the meter runs from
 * the HUD object: with no object the level set by the down special never drops.
 *
 * The level changes seven common attributes from their values at load (kept at +08A0):
 *   fp+138 dash max speed       base + 0.2 * level
 *   fp+128 ground friction      base + 0.00645 * level
 *   fp+12C dash initial speed   base + 0.15 * level
 *   fp+180 air friction         base * 1.05 ^ level
 *   fp+198 weight               base * 0.8975 ^ level
 *   fp+15C hop initial speed    base * 0.9 ^ level
 *   fp+17C air drift max        base * 1.075 ^ level
 * The powers are repeated double products, rounded to single at the end (+1DDC to +2018). */
#include "toad.h"

#include <dolphin/os.h>

#include <melee/ft/inlines.h>
#include <melee/ft/types.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gmtoulib.h>
#include <melee/if/ifall.h>
#include <melee/lb/lbarchive.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/pl/player.h>
#include <melee/sc/types.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/tobj.h>

#define FTTD_METER_FRAMES 1200 /* 0x4B0 */

/* +08A0: the attributes at load, in the file's order. One block for every Toad, as the file's. */
static struct {
    /* +00 */ float dash_max_velocity;      /* fp+138 */
    /* +04 */ float ground_friction;        /* fp+128 */
    /* +08 */ float dash_initial_velocity;  /* fp+12C */
    /* +0C */ float weight;                 /* fp+198 */
    /* +10 */ float hop_v_initial_velocity; /* fp+15C */
    /* +14 */ float air_drift_max;          /* fp+17C */
    /* +18 */ float aerial_friction;        /* fp+180 */
} ftTd_Base;

/* The object's user data. The console allocates 0x70 bytes through the m-ex service at
 * 0x803D706C and uses the first two words. */
typedef struct ftTd_MeterData {
    /* +0 */ HSD_GObj* fighter;
    /* +4 */ s32 player;
} ftTd_MeterData;

/* "Relax_scene_models" of Meters.dat: a list of model descriptions; the first is used. */
typedef DISC_PTR(DynamicModelDesc) ftTd_MeterModelSlot;

/* +0024 to +0070 of [onload]. */
void ftTd_Meter_SaveBase(Fighter* fp)
{
    ftTd_Base.aerial_friction = fp->co_attrs.aerial_friction;
    ftTd_Base.air_drift_max = fp->co_attrs.air_drift_max;
    ftTd_Base.dash_initial_velocity = fp->co_attrs.dash_initial_velocity;
    ftTd_Base.ground_friction = fp->co_attrs.ground_friction;
    ftTd_Base.weight = fp->co_attrs.weight;
    ftTd_Base.hop_v_initial_velocity = fp->co_attrs.hop_v_initial_velocity;
    ftTd_Base.dash_max_velocity = fp->co_attrs.dash_max_velocity;
}

/* base ^ n as the file computes it: 1.0 times base, n times, in double. */
static double ftTd_Meter_Pow(double base, int n)
{
    double result = 1.0;

    while (n-- > 0) {
        result *= base;
    }
    return result;
}

/* +1DBC: the seven attributes from the level. */
void ftTd_Meter_ApplyLevel(HSD_GObj* fighter_gobj)
{
    Fighter* fp = GET_FIGHTER(fighter_gobj);
    int level = ftTd_Meter(fp)->level;
    double n = (double) level;
    float aerial_friction = ftTd_Base.aerial_friction;
    float weight = ftTd_Base.weight;
    float hop = ftTd_Base.hop_v_initial_velocity;
    float air_drift = ftTd_Base.air_drift_max;

    if (level > 0) {
        aerial_friction =
            (float) ((double) ftTd_Base.aerial_friction * ftTd_Meter_Pow(1.05, level));
    }
    fp->co_attrs.aerial_friction = aerial_friction;

    /* three fused multiply-adds in double (fmadd), each rounded to single */
    fp->co_attrs.dash_max_velocity =
        (float) __builtin_fma(n, 0.2, (double) ftTd_Base.dash_max_velocity);
    fp->co_attrs.ground_friction =
        (float) __builtin_fma(n, 0.00645, (double) ftTd_Base.ground_friction);
    fp->co_attrs.dash_initial_velocity =
        (float) __builtin_fma(n, 0.15, (double) ftTd_Base.dash_initial_velocity);

    if (level > 0) {
        weight = (float) ((double) ftTd_Base.weight * ftTd_Meter_Pow(0.8975, level));
        hop = (float) ((double) ftTd_Base.hop_v_initial_velocity * ftTd_Meter_Pow(0.9, level));
        air_drift = (float) (ftTd_Meter_Pow(1.075, level) * (double) ftTd_Base.air_drift_max);
    }
    fp->co_attrs.weight = weight;
    fp->co_attrs.hop_v_initial_velocity = hop;
    fp->co_attrs.air_drift_max = air_drift;
}

/* +1BD0: the countdown, then the attributes. Nothing counts while the game is paused. */
static void ftTd_Meter_Tick(HSD_GObj* fighter_gobj)
{
    Fighter* fp = GET_FIGHTER(fighter_gobj);
    ftTd_MeterVars* mv = ftTd_Meter(fp);

    if (mv->timer < 0) {
        mv->timer = 0;
    }
    /* The console compares the flag routine's result with 2, the value of pause flag 1. */
    if (!gm_GetDbPauseFlag(1) && mv->level != 0) {
        if (mv->timer != 0) {
            mv->timer--;
        } else {
            mv->level--;
            if (mv->level > 0) {
                mv->timer = FTTD_METER_FRAMES;
            }
        }
    }
    ftTd_Meter_ApplyLevel(fighter_gobj);
}

/* +1C80: the current frame of the first animation found in a joint tree (down the first child
 * of each joint: the joint's own, then each of its display objects', their materials', their
 * textures'), -1.0 (+15F0) when there is none. */
static float ftTd_Meter_AnimFrame(HSD_JObj* jobj)
{
    for (; jobj != NULL; jobj = jobj->child) {
        HSD_DObj* dobj;

        if (jobj->aobj != NULL) {
            return jobj->aobj->curr_frame;
        }
        for (dobj = jobj->u.dobj; dobj != NULL; dobj = dobj->next) {
            HSD_MObj* mobj;
            HSD_TObj* tobj;

            if (dobj->aobj != NULL) {
                return dobj->aobj->curr_frame;
            }
            mobj = dobj->mobj;
            if (mobj->aobj != NULL) {
                return mobj->aobj->curr_frame;
            }
            for (tobj = mobj->tobj; tobj != NULL; tobj = tobj->next) {
                if (tobj->aobj != NULL) {
                    return tobj->aobj->curr_frame;
                }
            }
        }
    }
    return -1.0f;
}

/* +17F8: the HUD object's per-frame routine. */
static void ftTd_Meter_Proc(HSD_GObj* gobj)
{
    HSD_JObj* jobj = gobj->hsd_obj;
    ftTd_MeterData* data = gobj->user_data;
    HSD_JObj* bar = NULL;
    Fighter* fp;
    Vec3* hud;
    HSD_DObj* dobj;
    HSD_Material* mat;
    GXColor color;
    float target;
    float cur;

    lb_80011E24(jobj, &bar, 0, -1);
    ftTd_Meter_Tick(data->fighter);

    if (data->fighter == NULL || gm_GetDbPauseFlag(1) ||
        Player_GetStocks(data->player) <= 0)
    {
        HSD_JObjSetFlagsAll(jobj, JOBJ_HIDDEN);
        return;
    }

    fp = GET_FIGHTER(data->fighter);
    hud = ifAll_GetPlayerHUDPosition(fp->player_idx);
    /* +18C8 to +1908: the three words copied, then x - 2.6 in double and y + 2.375 in single */
    jobj->translate.x = (float) ((double) hud->x - 2.6);
    jobj->translate.y = hud->y + 2.375f;
    jobj->translate.z = hud->z;

    /* +1914 to +1968: ambient, diffuse and specular of the bar's first material */
    dobj = HSD_JObjGetDObj(bar);
    mat = dobj->mobj->mat;
    color.r = (u8) (ftTd_Meter(fp)->level * 0x55);
    color.g = (u8) (ftTd_Meter(fp)->level * 7);
    color.b = (u8) ~(ftTd_Meter(fp)->level * 0x55);
    color.a = 0xAF;
    mat->ambient = color;
    mat->diffuse = color;
    mat->specular = color;

    /* +18DC to +1A14: the animation follows frame timer / 12 (a signed division) */
    target = (float) (ftTd_Meter(fp)->timer / 12);
    cur = ftTd_Meter_AnimFrame(bar);
    if (cur != target) {
        if (cur < target) {
            HSD_JObjReqAnimAll(bar, target);
        }
        cur = ftTd_Meter_AnimFrame(bar);
        if (target < cur) {
            HSD_JObjReqAnimAll(bar, cur - 1.0f);
        }
        HSD_JObjAnimAll(bar);
    }
    HSD_JObjClearFlagsAll(jobj, JOBJ_HIDDEN);
    HSD_JObjSetMtxDirtySub(jobj);
}

/* +075C: the HUD object. The console does not test the file or the symbol (it only prints
 * "Report: ..." lines around them); here either one missing gives no object and one log line. */
HSD_GObj* ftTd_Meter_Create(HSD_GObj* fighter_gobj)
{
    Fighter* fp = GET_FIGHTER(fighter_gobj);
    HSD_Archive* archive;
    ftTd_MeterModelSlot* models;
    DynamicModelDesc* model;
    HSD_GObj* meter;
    ftTd_MeterData* data;

    archive = lbArchive_LoadArchive("Meters.dat");
    models = archive != NULL ? (ftTd_MeterModelSlot*) HSD_ArchiveGetPublicAddress(
                                   archive, "Relax_scene_models")
                             : NULL;
    model = models != NULL ? DP(models[0]) : NULL;
    if (model == NULL) {
        OSReport("[ak] Toad meter: %s; no meter object (the level will not count down)\n",
                 archive == NULL ? "Meters.dat was not loaded"
                                 : "Relax_scene_models was not found");
        return NULL;
    }

    meter = fn_8019035C(false, model, 0, 0, 11, true, ftTd_Meter_Proc, 0.0f);
    data = HSD_MemAlloc(sizeof(ftTd_MeterData));
    GObj_InitUserData(meter, 4, HSD_Free, data);
    data->fighter = fighter_gobj;
    data->player = fp->player_idx;
    return meter;
}
