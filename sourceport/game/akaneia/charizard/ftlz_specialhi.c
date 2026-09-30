/* Charizard Up B, Fly (states 349, 350).
 *
 * The animation carries Charizard up; the stick adds a little drift that is kept frame to frame in
 * mv.specialhi.vel, and one turn-around is allowed (cmd_vars[0] marks it used). It ends in special
 * fall with specialhi_landing_lag, and grabs ledges on the way. */
#include "ftlizardon.h"

#include <dolphin/mtx.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/types.h>

/* PlLz SpecialHi_OnEnter. */
static void ftLz_SpecialHi_OnEnter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_MotionVars* mv = ftLz_MV(fp);

    ftAnim_8006EBA4(gobj);
    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    fp->cmd_vars[2] = 0;
    fp->cmd_vars[3] = 0;
    fp->self_vel.y = 0.0F;
    mv->specialhi.vel.x = 0.0F;
    mv->specialhi.vel.y = 0.0F;
    mv->specialhi.vel.z = 0.0F;
}

/* m-ex "specialhi". */
void ftLz_SpecialHi_Enter(HSD_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftLz_MS_SpecialHi, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    ftLz_SpecialHi_OnEnter(gobj);
}

/* m-ex "specialairhi". */
void ftLz_SpecialAirHi_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, ftLz_MS_SpecialAirHi, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    ftLz_SpecialHi_OnEnter(gobj);
    ftCommon_8007D60C(fp);
}

/* PlLz SpecialHi_OnLand, the ledge/landing callback. */
static void ftLz_SpecialHi_OnLand(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCo_LandingFallSpecial_Enter(gobj, false, ftLz_Attrs(fp)->specialhi_landing_lag);
}

/* PlLz SpecialHi_AnimCB and SpecialAirHi_AnimCB. */
void ftLz_SpecialHi_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_DatAttrs* da = ftLz_Attrs(fp);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 0, 1, false, da->specialhi_drift_mul, da->specialhi_landing_lag);
    }
}

/* PlLz SpecialHi_IASACB: one turn-around, as soon as the stick leaves the centre. */
void ftLz_SpecialHi_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float stick_x;

    if (fp->cmd_vars[0] == 1) {
        return;
    }
    stick_x = fp->input.lstick[0].x;
    if ((stick_x < 0.0F ? -stick_x : stick_x) > 0.0F) {
        ftCommon_UpdateFacing(fp);
        ftPartSetRotY(fp, 0, (float) (fp->facing_dir * M_PI_2));
        fp->cmd_vars[0] = 1;
    }
}

/* PlLz SpecialHi_PhysCB: the animation's motion plus the stick drift carried in mv. */
void ftLz_SpecialHi_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLz_DatAttrs* da = ftLz_Attrs(fp);
    ftLz_MotionVars* mv = ftLz_MV(fp);
    ftCo_DatAttrs* co = &fp->co_attrs;

    fp->self_vel = mv->specialhi.vel;
    ftCommon_CalcSelfAccel_DriftSimple_NoFriction(fp, p_ftCommonData->x258,
                                                  co->air_drift_stick_mul * da->specialhi_drift_mul,
                                                  co->air_drift_max * da->specialhi_drift_max);
    PSVECAdd(&fp->x74_self_accel, &fp->self_vel, &mv->specialhi.vel);
    ft_80085134(gobj);
    fp->x74_self_accel.x = 0.0F;
    fp->x74_self_accel.y = 0.0F;
    PSVECAdd(&fp->self_vel, &mv->specialhi.vel, &fp->self_vel);
}

/* PlLz SpecialHi_CollCB: in the air, grab ledges and land into special landing; on the ground,
 * stop at ledges and fall off. */
void ftLz_SpecialHi_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->ground_or_air == GA_Air) {
        ft_80083A48(gobj, ftLz_SpecialHi_OnLand);
    } else {
        ft_80084104(gobj);
    }
}

void ftLz_SpecialAirHi_Anim(HSD_GObj* gobj)
{
    ftLz_SpecialHi_Anim(gobj);
}

void ftLz_SpecialAirHi_IASA(HSD_GObj* gobj)
{
    ftLz_SpecialHi_IASA(gobj);
}

void ftLz_SpecialAirHi_Phys(HSD_GObj* gobj)
{
    ftLz_SpecialHi_Phys(gobj);
}

void ftLz_SpecialAirHi_Coll(HSD_GObj* gobj)
{
    ftLz_SpecialHi_Coll(gobj);
}
