/* ACE's Meta Knight: the neutral special (states 341 to 345), his own code.
 *
 *   341  start on the ground; 344 start in the air (same animation callback)
 *   342  the loop, 45 frames: the stick steers it (up to 0.18 a frame up, 0.09 sideways, x within
 *        +-1.1, y under 0.6); with the stick in the dead zone the x speed slows by the air
 *        friction; in the air at 0.3 of gravity and terminal speed. Then he faces the way he
 *        moves, and ends on the ground (343) or in the air (345)
 *   343  end on the ground
 *   345  end in the air, into the special fall (no landing lag change: 30 frames, mobility 1) */
#include "metaknight.h"

#include <math.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/types.h>

static void ftMk_SpecialAirN_Land(HSD_GObj* gobj);

/* +04E0 (slot 4, specialn) */
void ftMk_SpecialN_Enter(HSD_GObj* gobj)
{
    FTMK_CHANGE(gobj, ftMk_MS_SpecialNStart);
}

/* +0504 (slot 5, specialairn) */
void ftMk_SpecialAirN_Enter(HSD_GObj* gobj)
{
    FTMK_CHANGE(gobj, ftMk_MS_SpecialAirNStart);
}

/* +1328: animation of 341 and 344: at the end the loop, every jump given back, 45 frames. */
void ftMk_SpecialNStart_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    FTMK_CHANGE(gobj, ftMk_MS_SpecialNLoop);
    fp->x1968_jumpsUsed = (u8) fp->co_attrs.max_jumps;
    ftMk_MVars(fp)->x2364.i = 45;
}

/* +13B8: collision of 341 and 343: off the floor, the common fall. */
void ftMk_SpecialNStart_Coll(HSD_GObj* gobj)
{
    if (ft_80082708(gobj) == 0) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +1404: animation of 342: the frame count; at 0 he faces the way he moves (x speed above 0:
 * right, else left) and ends in 345 in the air or 343 on the ground. */
void ftMk_SpecialNLoop_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftMk_MotionVars* mv = ftMk_MVars(fp);

    mv->x2364.i = mv->x2364.i - 1;
    if (mv->x2364.i > 0) {
        return;
    }
    if (!(fp->self_vel.x <= 0.0f)) { /* ble: a NaN faces right */
        fp->facing_dir = 1.0f;
    } else {
        fp->facing_dir = -1.0f;
    }
    if (fp->ground_or_air == GA_Air) {
        FTMK_CHANGE(gobj, ftMk_MS_SpecialAirNEnd);
    } else {
        FTMK_CHANGE(gobj, ftMk_MS_SpecialNEnd);
    }
}

/* +149C: physics of 342. */
void ftMk_SpecialNLoop_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float lx;
    float ax;
    float vx;

    if (fp->ground_or_air == GA_Air) {
        /* +15F0: two double products, each rounded to single */
        const float gravity = (float) ((double) fp->co_attrs.gravity * 0.3);
        const float terminal = (float) ((double) fp->co_attrs.terminal_velocity * 0.3);
        ftCommon_Fall(fp, gravity, terminal);
    }

    /* +14C0 */
    lx = fp->input.lstick[0].x;
    ax = lx < 0.0f ? -lx : lx;
    /* the branch senses of the file (bgt, then ble): a NaN y takes the steering path */
    if (ax > 0.3 || !(fp->input.lstick[0].y <= 0.3)) {
        /* +14FC */
        const float angle = atan2f(fp->input.lstick[0].y, lx);
        const float rise = (float) ((double) sinf(angle) * 0.18);
        if (!(rise <= 0.0f)) {
            if (fp->ground_or_air == GA_Ground) {
                /* +161C */
                ftCommon_8007D5D4(fp);
            }
            /* +1538 */
            fp->self_vel.y = fp->self_vel.y + rise;
        }
        /* +1544 */
        vx = (float) __builtin_fma((double) cosf(angle), 0.09, (double) fp->self_vel.x);
        fp->self_vel.x = vx;
    } else {
        /* +15DC */
        ftCommon_CalcSelfAccel_Deaccel(fp, fp->co_attrs.aerial_friction);
        vx = fp->self_vel.x;
    }

    /* +1568 (ble / bge / ble: a NaN takes the clamp) */
    if (!(vx <= 1.1)) {
        fp->self_vel.x = 1.1f;
    } else if (!(vx >= -1.1)) {
        fp->self_vel.x = -1.1f;
    }
    if (!(fp->self_vel.y <= 0.6)) {
        fp->self_vel.y = 0.6f;
    }
}

/* +1630: collision of 342: in the air a landing puts him on the ground (ftCommon_8007D6A4);
 * on the ground a walk-off puts him in the air (ftCommon_8007D5D4). The state stays. */
void ftMk_SpecialNLoop_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->ground_or_air == GA_Air) {
        if (ft_80081D0C(gobj)) {
            ftCommon_8007D6A4(fp);
        }
    } else {
        if (ft_80082708(gobj) == 0) {
            ftCommon_8007D5D4(fp);
        }
    }
}

/* +16AC: animation of 343. */
void ftMk_SpecialNEnd_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +16F8: physics of 344 and 345: a branch to ft_80084EEC. */
void ftMk_SpecialAirN_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

/* +16FC: collision of 344: on landing, +3148. */
void ftMk_SpecialAirNStart_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftMk_SpecialAirN_Land(gobj);
    }
}

/* +1748: animation of 345: at the end the special fall (no interrupt, mobility 1, landing lag
 * 30), every jump given back. */
void ftMk_SpecialAirNEnd_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    ftCo_80096900(gobj, 0, 1, false, 1.0f, 30.0f);
    fp->x1968_jumpsUsed = (u8) fp->co_attrs.max_jumps;
}

/* +17CC: collision of 345: ft_80083A48 with the landing +3148. */
void ftMk_SpecialAirNEnd_Coll(HSD_GObj* gobj)
{
    ft_80083A48(gobj, ftMk_SpecialAirN_Land);
}

/* +3148: the landing of 344 and 345: on the ground, 344 goes on as 341 and 345 as 343 from the
 * same frame. */
static void ftMk_SpecialAirN_Land(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D7FC(fp);
    if (fp->motion_id == ftMk_MS_SpecialAirNStart) {
        FTMK_CHANGE_AT_FRAME(gobj, fp, ftMk_MS_SpecialNStart);
    } else if (fp->motion_id == ftMk_MS_SpecialAirNEnd) {
        FTMK_CHANGE_AT_FRAME(gobj, fp, ftMk_MS_SpecialNEnd);
    }
}
