/* PlLc2.dat native C. Ness layouts, local callbacks. Review status in PROGRESS.md. */
#include "lucas2_speciallw.h"

#include <Runtime/platform.h>

#include <melee/ft/forward.h>

#include "lucas2.h"
#include <melee/ft/kinds/ftNess/types.h>
#include <dolphin/mtx.h>
#include <melee/ef/efasync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/kinds/ftCommon/inlines.h>
#include <melee/ft/types.h>
#include <melee/pl/player.h>
#include <melee/pl/plbonuslib.h>

#define FTNESS_SPECIALLW_COLL_FLAG                                            \
    Ft_MF_KeepGfx | Ft_MF_SkipMatAnim | Ft_MF_SkipColAnim | Ft_MF_UpdateCmd | \
        Ft_MF_SkipItemVis | Ft_MF_Unk19 | Ft_MF_SkipModelPartVis |            \
        Ft_MF_SkipModelFlags | Ft_MF_Unk27

#define FTNESS_SPECIALLW_END_FLAG                                             \
    Ft_MF_SkipMatAnim | Ft_MF_SkipColAnim | Ft_MF_UpdateCmd |                 \
        Ft_MF_SkipItemVis | Ft_MF_Unk19 | Ft_MF_SkipModelPartVis |            \
        Ft_MF_SkipModelFlags | Ft_MF_Unk27

void ftLc2_SpecialLwStart_Enter(HSD_GObj* gobj) 
                                               
{
    ftNessAttributes* ness_attr;
    Fighter* temp_fp;

    u8 _[20];

    temp_fp = GET_FIGHTER(gobj);
    ness_attr = temp_fp->dat_attrs;
    temp_fp->mv.ns.speciallw.releaseLag =
        (s32) ness_attr->x74_PSI_MAGNET_RELEASE_LAG;
    temp_fp->mv.ns.speciallw.isRelease = 0;
    temp_fp->mv.ns.speciallw.gravityDelay =
        (s32) ftLc2_FloatBits(ness_attr->x84_PSI_MAGNET_FRAMES_BEFORE_GRAVITY);
    temp_fp->mv.ns.speciallw.x10 = 0;
    Fighter_ChangeMotionState(gobj, ftNs_MS_SpecialLwStart, 0, 0, 1, 0, NULL);
    ftAnim_8006EBA4(gobj);
}

void ftLc2_SpecialAirLwStart_Enter(
    HSD_GObj* gobj) 
{
    Fighter* temp_fp;
    ftNessAttributes* ness_attr;

    u8 _[20];

    temp_fp = GET_FIGHTER(gobj);
    ness_attr = temp_fp->dat_attrs;
    temp_fp->mv.ns.speciallw.releaseLag =
        (s32) ness_attr->x74_PSI_MAGNET_RELEASE_LAG;
    temp_fp->mv.ns.speciallw.isRelease = 0;
    temp_fp->mv.ns.speciallw.gravityDelay =
        (s32) ftLc2_FloatBits(ness_attr->x84_PSI_MAGNET_FRAMES_BEFORE_GRAVITY);
    temp_fp->mv.ns.speciallw.x10 = 0;
    temp_fp->self_vel.y = 0.0f;
    temp_fp->self_vel.x /= ness_attr->x88_PSI_MAGNET_MOMENTUM_PRESERVATION;
    Fighter_ChangeMotionState(gobj, ftNs_MS_SpecialAirLwStart, 0, 0.0f, 1.0f,
                              0.0f, NULL);
    ftAnim_8006EBA4(gobj);
}

void ftLc2_SpecialLwStart_Anim(
    HSD_GObj* gobj) 
{
    Fighter* fp;
    Fighter* fighter_data2;

    fighter_data2 = GET_FIGHTER(gobj);

    if (!(fighter_data2->input.held_buttons[0] & HSD_PAD_B)) {
        fighter_data2->mv.ns.speciallw.isRelease = 1;
    }

    if (!ftAnim_IsFramesRemaining(gobj)) {
        fp = GET_FIGHTER(gobj);

        if (!fp->x2219_b0) {
            efAsync_Spawn(gobj, &fp->x60C, 0U, 5002,
                          fp->parts[0x17].joint);
            fp->x2219_b0 = true;
        }
        Fighter_SetEffectHitlagCallbacks(fp);
        fighter_data2->mv.ns.speciallw.x10 = 0;
        if ((s32) fighter_data2->ground_or_air == GA_Ground) {
            ftLc2_SpecialLwHold_Enter(gobj);
            return;
        }
        ftLc2_SpecialAirLwHold_Enter(gobj);
    }
}

void ftLc2_SpecialAirLwStart_Anim(
    HSD_GObj* gobj) 
{
    Fighter* fighter_data2;
    Fighter* fp;

    u8 _[8];

    fighter_data2 = fp = GET_FIGHTER(gobj);

    if ((fighter_data2->input.held_buttons[0] & HSD_PAD_B) == false) {
        fighter_data2->mv.ns.speciallw.isRelease = 1;
    }

    if (!ftAnim_IsFramesRemaining(gobj)) {
        fp = GET_FIGHTER(gobj);

        if (!fp->x2219_b0) {
            efAsync_Spawn(gobj, &fp->x60C, 0U, 5002,
                          fp->parts[0x17].joint);
            fp->x2219_b0 = true;
        }
        Fighter_SetEffectHitlagCallbacks(fp);
        fighter_data2->mv.ns.speciallw.x10 = 0;
        if ((s32) fighter_data2->ground_or_air == GA_Ground) {
            ftLc2_SpecialLwHold_Enter(gobj);
            return;
        }
        ftLc2_SpecialAirLwHold_Enter(gobj);
    }
}

void ftLc2_SpecialLwStart_IASA(
    HSD_GObj* gobj) 
{
    return;
}

void ftLc2_SpecialAirLwStart_IASA(
    HSD_GObj* gobj) 
{
    return;
}

void ftLc2_SpecialLwStart_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftLc2_SpecialAirLwStart_Phys(HSD_GObj* gobj)
{
    u8 _[8];

    Fighter* fp = GET_FIGHTER(gobj);
    ftCo_DatAttrs* da = &fp->co_attrs;
    ftNessAttributes* ness_attr = fp->dat_attrs;
    int gravity_timer = fp->mv.ns.speciallw.gravityDelay;

    if (gravity_timer != 0) {
        fp->mv.ns.speciallw.gravityDelay = gravity_timer - 1;
    } else {
        ftCommon_Fall(fp, ness_attr->x8C_PSI_MAGNET_FALL_ACCEL,
                      da->terminal_velocity);
    }

    ftCommon_CalcSelfAccel_DeaccelQuickAir(fp);
}

void ftLc2_SpecialLwStart_Coll(
    HSD_GObj* gobj) 
{
    if (ft_80082708(gobj) == false) {
        ftLc2_SpecialLwStart_GroundToAir(gobj);
    }
}

void ftLc2_SpecialAirLwStart_Coll(
    HSD_GObj* gobj) 
{
    if (ft_80081D0C(gobj) != false) {
        ftLc2_SpecialAirLwStart_AirToGround(gobj);
    }
}

void ftLc2_SpecialLwStart_GroundToAir(
    HSD_GObj* gobj) 
                    
{
    Fighter* fp;

    fp = GET_FIGHTER(gobj);
    ftCommon_GroundToAirStateChange(gobj, fp, ftNs_MS_SpecialAirLwStart,
                                    FTNESS_SPECIALLW_COLL_FLAG);
}

void ftLc2_SpecialAirLwStart_AirToGround(
    HSD_GObj* gobj) 
                    
{
    Fighter* fp;

    fp = GET_FIGHTER(gobj);
    ftCommon_AirToGroundStateChange(gobj, fp, ftNs_MS_SpecialLwStart,
                                    FTNESS_SPECIALLW_COLL_FLAG);
    ftCommon_ClampAirDrift(fp);
}

void ftLc2_SpecialLwHold_Anim(
    HSD_GObj* gobj) 
{
    Fighter* temp_r31;
    Fighter* fp;

    fp = GET_FIGHTER(gobj);
    if ((fp->input.held_buttons[0] & HSD_PAD_B) == false) {
        fp->mv.ns.speciallw.isRelease = 1;
    }

    if (fp->mv.ns.speciallw.releaseLag > 0) {
        fp->mv.ns.speciallw.releaseLag--;
    }
    if ((fp->mv.ns.speciallw.releaseLag <= 0) &&
        ((s32) fp->mv.ns.speciallw.isRelease != 0))
    {
        if ((s32) fp->ground_or_air == GA_Ground) {
            ftLc2_SpecialLwEnd_Enter(gobj);
        } else {
            ftLc2_SpecialAirLwEnd_Enter(gobj);
        }
    }
    temp_r31 = GET_FIGHTER(gobj);

    temp_r31->mv.ns.speciallw.x10--;

    if (temp_r31->mv.ns.speciallw.x10 <= 0) {
        ft_80088478(temp_r31, 0x13D9, 127, 64);
        temp_r31->mv.ns.speciallw.x10 = 40;
    }
}

void ftLc2_SpecialAirLwHold_Anim(
    HSD_GObj* gobj) 
{
    Fighter* temp_r31;
    Fighter* fp;

    u8 _[8];

    fp = GET_FIGHTER(gobj);
    if ((fp->input.held_buttons[0] & HSD_PAD_B) == false) {
        fp->mv.ns.speciallw.isRelease = 1;
    }

    if (fp->mv.ns.speciallw.releaseLag > 0) {
        fp->mv.ns.speciallw.releaseLag =
            (s32) (fp->mv.ns.speciallw.releaseLag - 1);
    }
    if ((fp->mv.ns.speciallw.releaseLag <= 0) &&
        ((s32) fp->mv.ns.speciallw.isRelease != 0))
    {
        if ((s32) fp->ground_or_air == GA_Ground) {
            ftLc2_SpecialLwEnd_Enter(gobj);
        } else {
            ftLc2_SpecialAirLwEnd_Enter(gobj);
        }
    }
    temp_r31 = GET_FIGHTER(gobj);

    temp_r31->mv.ns.speciallw.x10 = (s32) (temp_r31->mv.ns.speciallw.x10 - 1);

    if (temp_r31->mv.ns.speciallw.x10 <= 0) {
        ft_80088478(temp_r31, 0x13D9, 127, 64);
        temp_r31->mv.ns.speciallw.x10 = 40;
    }
}

void ftLc2_SpecialLwHold_IASA(
    HSD_GObj* gobj) 
{
    return;
}

void ftLc2_SpecialAirLwHold_IASA(
    HSD_GObj* gobj) 
{
    return;
}

void ftLc2_SpecialLwHold_Phys(
    HSD_GObj* gobj) 
{
    ft_80084F3C(gobj);
    ftColl_8007AF10(gobj);
}

void ftLc2_SpecialAirLwHold_Phys(HSD_GObj* gobj)
{
    u8 _[24];

    Fighter* fp = GET_FIGHTER(gobj);
    ftNessAttributes* ness_attr = fp->dat_attrs;
    ftCo_DatAttrs* da = &fp->co_attrs;

    {
        int gravity_timer = fp->mv.ns.speciallw.gravityDelay;

        if (gravity_timer != 0) {
            fp->mv.ns.speciallw.gravityDelay = gravity_timer - 1;
        } else {
            ftCommon_Fall(fp, ness_attr->x8C_PSI_MAGNET_FALL_ACCEL,
                          da->terminal_velocity);
        }

        ftCommon_CalcSelfAccel_DeaccelQuickAir(fp);
        ftColl_8007AF10(gobj);
    }
}

void ftLc2_SpecialLwHold_Coll(
    HSD_GObj* gobj) 
{
    if (ft_80082708(gobj) == false) {
        ftLc2_SpecialLwHold_GroundToAir(gobj);
    }
}

void ftLc2_SpecialAirLwHold_Coll(
    HSD_GObj* gobj) 
{
    if (ft_80081D0C(gobj) != false) {
        ftLc2_SpecialAirLwHold_AirToGround(gobj);
    }
}

void ftLc2_SpecialLwHold_GroundToAir(
    HSD_GObj* gobj) 
                    
{
    Fighter* fp;
    ftNessAttributes* ness_attr;

    u8 _[16];

    fp = GET_FIGHTER(gobj);
    ftCommon_GroundToAirStateChange(gobj, fp, ftNs_MS_SpecialAirLwHold,
                                    FTNESS_SPECIALLW_COLL_FLAG);
    fp = GET_FIGHTER(gobj);
    ness_attr = fp->dat_attrs;
    ftColl_CreateAbsorbHit(gobj, DISC_ABSORB(&ness_attr->x98_PSI_MAGNET_ABSORPTION));
}

void ftLc2_SpecialAirLwHold_AirToGround(
    HSD_GObj* gobj) 
                    
{
    Fighter* fp;
    ftNessAttributes* ness_attr;

    u8 _[16];

    fp = GET_FIGHTER(gobj);
    ftCommon_AirToGroundStateChange(gobj, fp, ftNs_MS_SpecialLwHold,
                                    FTNESS_SPECIALLW_COLL_FLAG);
    ftCommon_ClampAirDrift(fp);
    fp = GET_FIGHTER(gobj);
    ness_attr = fp->dat_attrs;
    ftColl_CreateAbsorbHit(gobj, DISC_ABSORB(&ness_attr->x98_PSI_MAGNET_ABSORPTION));
}

void ftLc2_SpecialLwHold_Enter(
    HSD_GObj* gobj) 
{
    Fighter* fp;
    ftNessAttributes* ness_attr;

    u8 _[8];

    Fighter_ChangeMotionState(gobj, ftNs_MS_SpecialLwHold, Ft_MF_KeepGfx, 0.0f,
                              1.0f, 0.0f, NULL);
    fp = GET_FIGHTER(gobj);
    ness_attr = fp->dat_attrs;
    ftColl_CreateAbsorbHit(gobj, DISC_ABSORB(&ness_attr->x98_PSI_MAGNET_ABSORPTION));
}

void ftLc2_SpecialAirLwHold_Enter(
    HSD_GObj* gobj) 
{
    Fighter* fp;
    ftNessAttributes* ness_attr;

    u8 _[8];

    Fighter_ChangeMotionState(gobj, ftNs_MS_SpecialAirLwHold, Ft_MF_KeepGfx,
                              0.0f, 1.0f, 0.0f, NULL);
    fp = GET_FIGHTER(gobj);
    ness_attr = fp->dat_attrs;
    ftColl_CreateAbsorbHit(gobj, DISC_ABSORB(&ness_attr->x98_PSI_MAGNET_ABSORPTION));
}

static inline float returnStateVar(s32 stateVar)
{
    return (float) stateVar;
}

static inline void getAttrStuff(HSD_GObj* arg0)
{
    Fighter* temp_r30 = arg0->user_data;
    ftNessAttributes* temp_r31 = temp_r30->dat_attrs;
    temp_r30->mv.ns.speciallw.turnFrames += -1;

    if ((temp_r30->cmd_vars[0] == 0U) &&
        ((returnStateVar(temp_r30->mv.ns.speciallw.turnFrames)) <=
         temp_r31->x78_PSI_MAGNET_UNK1))
    {
        temp_r30->cmd_vars[0] = 1;
        temp_r30->facing_dir = -temp_r30->facing_dir;
    }
    ftPartSetRotY(temp_r30, 0,
                  -(MTXDegToRad(180.0F / temp_r31->x78_PSI_MAGNET_UNK1) -
                    ftPartGetRotZ(temp_r30, 0)));
}

void ftLc2_SpecialLwTurn_Anim(HSD_GObj* arg0)
{
    u8 _[4];

    Fighter* fp0 = arg0->user_data;

    {
        Fighter* fp1 = fp0;

        if (!(fp1->input.held_buttons[0] & HSD_PAD_B)) {
            fp1->mv.ns.speciallw.isRelease = true;
        }

        if (fp1->mv.ns.speciallw.releaseLag > 0) {
            fp1->mv.ns.speciallw.releaseLag =
                fp1->mv.ns.speciallw.releaseLag - 1;
        }

        getAttrStuff(arg0);

        if ((signed) fp1->mv.ns.speciallw.turnFrames <= 0) {
            ftLc2_SpecialLwHold_GroundOrAir(arg0);
        }
    }
}

void ftLc2_SpecialAirLwTurn_Anim(HSD_GObj* arg0)
{
    u8 _[8];

    Fighter* fp0 = arg0->user_data;

    {
        Fighter* fp1 = fp0;

        if (!(fp1->input.held_buttons[0] & HSD_PAD_B)) {
            fp1->mv.ns.speciallw.isRelease = true;
        }

        if (fp1->mv.ns.speciallw.releaseLag > 0) {
            fp1->mv.ns.speciallw.releaseLag =
                fp1->mv.ns.speciallw.releaseLag - 1;
        }

        getAttrStuff(arg0);

        if ((signed) fp1->mv.ns.speciallw.turnFrames <= 0) {
            ftLc2_SpecialLwHold_GroundOrAir(arg0);
        }
    }
}

void ftLc2_SpecialLwTurn_IASA(HSD_GObj* gobj) 
                                             
{
    return;
}

void ftLc2_SpecialAirLwTurn_IASA(
    HSD_GObj* gobj) 
                    
{
    return;
}

void ftLc2_SpecialLwTurn_Phys(HSD_GObj* gobj) 
                                             
{
    ft_80084F3C(gobj);
    ftColl_8007AF10(gobj);
}

void ftLc2_SpecialAirLwTurn_Phys(
    HSD_GObj* gobj) 
                    
{
    Fighter* fp = GET_FIGHTER(gobj);

    u8 _[24];

    s32 magnetTimer;
    ftNessAttributes* ness_attr = fp->dat_attrs;
    ftCo_DatAttrs* da = &fp->co_attrs;

    magnetTimer = fp->mv.ns.speciallw.gravityDelay;

    if (magnetTimer != 0) {
        fp->mv.ns.speciallw.gravityDelay = magnetTimer - 1;
    } else {
        ftCommon_Fall(fp, ness_attr->x8C_PSI_MAGNET_FALL_ACCEL,
                      da->terminal_velocity);
    }

    ftCommon_CalcSelfAccel_DeaccelQuickAir(fp);
    ftColl_8007AF10(gobj);
}

void ftLc2_SpecialLwTurn_Coll(
    HSD_GObj* gobj) 
                    
{
    if (ft_80082708(gobj) == false) {
        ftLc2_SpecialLwTurn_GroundToAir(gobj);
    }
}

void ftLc2_SpecialAirLwTurn_Coll(
    HSD_GObj* gobj) 
                    
{
    if (ft_80081D0C(gobj) != false) {
        ftLc2_SpecialAirLwTurn_AirToGround(gobj);
    }
}

void ftLc2_SpecialLwTurn_GroundToAir(
    HSD_GObj* gobj) 
                    
{
    Fighter* fp;

    fp = GET_FIGHTER(gobj);
    ftCommon_GroundToAirStateChange(gobj, fp, ftNs_MS_SpecialAirLwTurn,
                                    FTNESS_SPECIALLW_COLL_FLAG);
}

void ftLc2_SpecialAirLwTurn_AirToGround(
    HSD_GObj* gobj) 
                    
{
    Fighter* fp;

    fp = GET_FIGHTER(gobj);
    ftCommon_AirToGroundStateChange(gobj, fp, ftNs_MS_SpecialLwTurn,
                                    FTNESS_SPECIALLW_COLL_FLAG);
    ftCommon_ClampAirDrift(fp);
}

bool ftLc2_SpecialLwHold_GroundOrAir(
    HSD_GObj* arg0) 
{
    u8 _[24];

    Fighter* fp = GET_FIGHTER(arg0);

    if (fp->mv.ns.speciallw.releaseLag <= 0 && fp->mv.ns.speciallw.isRelease) {
        if (fp->ground_or_air == GA_Ground) {
            ftLc2_SpecialLwEnd_Enter(arg0);
        } else {
            ftLc2_SpecialAirLwEnd_Enter(arg0);
        }

        return false;
    }
    if ((s32) fp->ground_or_air == GA_Ground) {
        Fighter_ChangeMotionState(arg0, ftNs_MS_SpecialLwHold, Ft_MF_KeepGfx,
                                  0.0f, 1.0f, 0.0f, NULL);

        {
            Fighter* fp = GET_FIGHTER(arg0);
            ftNessAttributes* sa = fp->dat_attrs;
            ftColl_CreateAbsorbHit(arg0, DISC_ABSORB(&sa->x98_PSI_MAGNET_ABSORPTION));
        }
    } else {
        Fighter_ChangeMotionState(arg0, ftNs_MS_SpecialAirLwHold,
                                  Ft_MF_KeepGfx, 0.0f, 1.0f, 0.0f, NULL);

        {
            Fighter* fp = GET_FIGHTER(arg0);
            ftNessAttributes* sa = fp->dat_attrs;
            ftColl_CreateAbsorbHit(arg0, DISC_ABSORB(&sa->x98_PSI_MAGNET_ABSORPTION));
        }
    }

    return true;
}

static inline void MagnetStateVarCalc(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->mv.ns.speciallw.x10 = fp->mv.ns.speciallw.x10 - 1;
    if (fp->mv.ns.speciallw.x10 <= 0) {
        ft_80088478(fp, 0x13D9, 127, 64);
        fp->mv.ns.speciallw.x10 = 40;
    }
}

void ftLc2_SpecialLwHit_Anim(
    HSD_GObj* arg0) 
{
    Fighter* temp_r30;
    Fighter* temp_r3_2;
    Fighter* temp_r4;

    u8 _[44];

    s32 phi_r0;

    Fighter* temp_e1;
    Fighter* temp_e2;
    ftNessAttributes* da;

    temp_r4 = arg0->user_data;
    if (!(temp_r4->input.held_buttons[0] & HSD_PAD_B)) {
        temp_r4->mv.ns.speciallw.isRelease = 1;
    }

    if (temp_r4->mv.ns.speciallw.releaseLag > 0) {
        temp_r4->mv.ns.speciallw.releaseLag =
            (s32) (temp_r4->mv.ns.speciallw.releaseLag - 1);
    }
    if (!ftAnim_IsFramesRemaining(arg0)) {
        temp_r3_2 = arg0->user_data;
        if ((temp_r3_2->mv.ns.speciallw.releaseLag <= 0) &&
            ((s32) temp_r3_2->mv.ns.speciallw.isRelease != 0))
        {
            if ((s32) temp_r3_2->ground_or_air == GA_Ground) {
                ftLc2_SpecialLwEnd_Enter(arg0);
            } else {
                ftLc2_SpecialAirLwEnd_Enter(arg0);
            }
            phi_r0 = 0;
        } else {
            if ((s32) temp_r3_2->ground_or_air == GA_Ground) {
                Fighter_ChangeMotionState(arg0, ftNs_MS_SpecialLwHold,
                                          Ft_MF_KeepGfx, 0.0f, 1.0f, 0.0f,
                                          NULL);

                temp_e1 = arg0->user_data;
                da = temp_e1->dat_attrs;
                ftColl_CreateAbsorbHit(arg0, DISC_ABSORB(&da->x98_PSI_MAGNET_ABSORPTION));
            } else {
                Fighter_ChangeMotionState(arg0, ftNs_MS_SpecialAirLwHold,
                                          Ft_MF_KeepGfx, 0.0f, 1.0f, 0.0f,
                                          NULL);
                temp_e2 = arg0->user_data;
                da = temp_e2->dat_attrs;
                ftColl_CreateAbsorbHit(arg0, DISC_ABSORB(&da->x98_PSI_MAGNET_ABSORPTION));
            }
            phi_r0 = 1;
        }
        if (phi_r0 != 0) {
            ftCommon_8007DB24(arg0);
            temp_r30 = arg0->user_data;
            if (!temp_r30->x2219_b0) {
                efAsync_Spawn(arg0, &temp_r30->x60C, 0U, 5002,
                              temp_r30->parts[0x17].joint);
                temp_r30->x2219_b0 = true;
            }
            Fighter_SetEffectHitlagCallbacks(temp_r30);
        }
    }

    MagnetStateVarCalc(arg0);
}

void ftLc2_SpecialAirLwHit_Anim(
    HSD_GObj* arg0) 
{
    u8 _[16];

    Fighter* fp = GET_FIGHTER(arg0);

    if (!(fp->input.held_buttons[0] & HSD_PAD_B)) {
        fp->mv.ns.speciallw.isRelease = true;
    }

    if (fp->mv.ns.speciallw.releaseLag > 0) {
        fp->mv.ns.speciallw.releaseLag = fp->mv.ns.speciallw.releaseLag - 1;
    }

    if (!ftAnim_IsFramesRemaining(arg0) &&
        ftLc2_SpecialLwHold_GroundOrAir(arg0))
    {
        ftCommon_8007DB24(arg0);
        {
            Fighter* fp = GET_FIGHTER(arg0);
            if (!fp->x2219_b0) {
                efAsync_Spawn(arg0, &fp->x60C, 0U, 5002,
                              fp->parts[0x17].joint);
                fp->x2219_b0 = true;
            }
            Fighter_SetEffectHitlagCallbacks(fp);
        }
    }

    {
        Fighter* fp = GET_FIGHTER(arg0);
        fp->mv.ns.speciallw.x10 = fp->mv.ns.speciallw.x10 - 1;
        if ((signed) fp->mv.ns.speciallw.x10 <= 0) {
            ft_80088478(fp, 0x13D9, 127, 64);
            fp->mv.ns.speciallw.x10 = 40;
        }
    }
}

void ftLc2_SpecialLwHit_IASA(
    HSD_GObj* gobj) 
{
}

void ftLc2_SpecialAirLwHit_IASA(
    HSD_GObj* gobj) 
{
}

void ftLc2_SpecialLwHit_Phys(
    HSD_GObj* gobj) 
{
    ft_80084F3C(gobj);
    ftColl_8007AF10(gobj);
}

void ftLc2_SpecialAirLwHit_Phys(
    HSD_GObj* arg0) 
{
    u8 _[24];

    s32 temp_r3;

    Fighter* fp = GET_FIGHTER(arg0);
    ftNessAttributes* attrs = fp->dat_attrs;
    ftCo_DatAttrs* attributes = &fp->co_attrs;

    temp_r3 = fp->mv.ns.speciallw.gravityDelay;
    if (temp_r3 != 0) {
        fp->mv.ns.speciallw.gravityDelay = temp_r3 - 1;
    } else {
        ftCommon_Fall(fp, attrs->x8C_PSI_MAGNET_FALL_ACCEL,
                      attributes->terminal_velocity);
    }

    ftCommon_CalcSelfAccel_DeaccelQuickAir(fp);
    ftColl_8007AF10(arg0);
}

void ftLc2_SpecialLwHit_Coll(
    HSD_GObj* gobj) 
{
    if (ft_80082708(gobj) == false) {
        ftLc2_SpecialLwHit_GroundToAir(gobj);
    }
}

void ftLc2_SpecialAirLwHit_Coll(
    HSD_GObj* gobj) 
{
    if (ft_80081D0C(gobj) != false) {
        ftLc2_SpecialAirLwHit_AirToGround(gobj);
    }
}

void ftLc2_SpecialLwHit_GroundToAir(
    HSD_GObj* gobj) 
                    
{
    Fighter* fp;
    ftNessAttributes* ness_attr;

    u8 _[16];

    fp = GET_FIGHTER(gobj);
    ftCommon_GroundToAirStateChange(gobj, fp, ftNs_MS_SpecialAirLwHit,
                                    FTNESS_SPECIALLW_COLL_FLAG);
    fp = GET_FIGHTER(gobj);
    ness_attr = fp->dat_attrs;
    ftColl_CreateAbsorbHit(gobj, DISC_ABSORB(&ness_attr->x98_PSI_MAGNET_ABSORPTION));
}

void ftLc2_SpecialAirLwHit_AirToGround(
    HSD_GObj* gobj) 
                    
{
    Fighter* fp;
    ftNessAttributes* ness_attr;

    u8 _[16];

    fp = GET_FIGHTER(gobj);
    ftCommon_AirToGroundStateChange(gobj, fp, ftNs_MS_SpecialLwHit,
                                    FTNESS_SPECIALLW_COLL_FLAG);
    ftCommon_ClampAirDrift(fp);
    fp = GET_FIGHTER(gobj);
    ness_attr = fp->dat_attrs;
    ftColl_CreateAbsorbHit(gobj, DISC_ABSORB(&ness_attr->x98_PSI_MAGNET_ABSORPTION));
}

void ftLc2_AbsorbThink_DecideAction(
    HSD_GObj* gobj) 
{
    u8 _[8];

    enum_t msid;
    Fighter* fp = GET_FIGHTER(gobj);
    ftNessAttributes* sa = fp->dat_attrs;

    /* +41E8: stored absorption adds energy. It does not subtract percent. */
    {
        float amount = (float) (s32) ((float) fp->AbsorbAttr.x1A44_damageTaken * 4.0f);
        if (fp->dmg.x1830_percent < 0.0f) {
            amount += fp->dmg.x1830_percent;
            fp->dmg.x1830_percent = 0.0f;
        }
        ftLc2_Vars(fp)->energy += amount;
        if (ftLc2_Vars(fp)->energy > 100.0f) ftLc2_Vars(fp)->energy = 100.0f;
        pl_80040B8C(fp->player_idx, fp->is_sub_fighter, (s32) amount);
    }

    fp->facing_dir = fp->AbsorbAttr.x1A40_absorbHitDirection;
    msid = fp->motion_id;

    if ((msid != ftNs_MS_SpecialLwHit && msid != ftNs_MS_SpecialAirLwHit) ||
        !(fp->cur_anim_frame <= sa->x7C_PSI_MAGNET_UNK2))
    {
        enum_t msid;
        if (fp->ground_or_air == GA_Ground) {
            msid = ftNs_MS_SpecialLwHit;
        } else {
            msid = ftNs_MS_SpecialAirLwHit;
        }

        Fighter_ChangeMotionState(gobj, msid, 2, 0, 1, 0, NULL);
        ftColl_CreateAbsorbHit(gobj, DISC_ABSORB(&sa->x98_PSI_MAGNET_ABSORPTION));
    }
}

void ftLc2_SpecialLwEnd_Anim(
    HSD_GObj* gobj) 
{
    u8 _[8];

    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCommon_8007DB24(gobj);
        ftCommon_8007D92C(gobj);
    }
}

void ftLc2_SpecialAirLwEnd_Anim(
    HSD_GObj* gobj) 
{
    u8 _[8];

    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCommon_8007DB24(gobj);
        ftCommon_8007D92C(gobj);
    }
}

void ftLc2_SpecialLwEnd_IASA(
    HSD_GObj* gobj) 
{
    return;
}

void ftLc2_SpecialAirLwEnd_IASA(
    HSD_GObj* gobj) 
{
    return;
}

void ftLc2_SpecialLwEnd_Phys(
    HSD_GObj* gobj) 
{
    ft_80084F3C(gobj);
}

void ftLc2_SpecialAirLwEnd_Phys(
    HSD_GObj* arg0) 
{
    u8 _[24];

    Fighter* fp = GET_FIGHTER(arg0);
    ftNessAttributes* attrs = fp->dat_attrs;
    ftCo_DatAttrs* attributes = &fp->co_attrs;
    int gravity_timer = fp->mv.ns.speciallw.gravityDelay;

    if (gravity_timer != 0) {
        fp->mv.ns.speciallw.gravityDelay = gravity_timer - 1;
    } else {
        ftCommon_Fall(fp, attrs->x8C_PSI_MAGNET_FALL_ACCEL,
                      attributes->terminal_velocity);
    }

    ftCommon_CalcSelfAccel_DeaccelQuickAir(fp);
}

void ftLc2_SpecialLwEnd_Coll(
    HSD_GObj* gobj) 
{
    if (!ft_80082708(gobj)) {
        ftLc2_SpecialLwEnd_GroundToAir(gobj);
    }
}

void ftLc2_SpecialAirLwEnd_Coll(
    HSD_GObj* gobj) 
{
    if (ft_80081D0C(gobj) != false) {
        ftLc2_SpecialAirLwEnd_AirToGround(gobj);
    }
}

void ftLc2_SpecialLwEnd_GroundToAir(
    HSD_GObj* gobj) 
                    
{
    Fighter* fp;

    fp = GET_FIGHTER(gobj);
    ftCommon_GroundToAirStateChange(gobj, fp, ftNs_MS_SpecialAirLwEnd,
                                    FTNESS_SPECIALLW_END_FLAG);
}

void ftLc2_SpecialAirLwEnd_AirToGround(
    HSD_GObj* gobj) 
                    
{
    Fighter* temp_r31;

    temp_r31 = GET_FIGHTER(gobj);
    ftCommon_AirToGroundStateChange(gobj, temp_r31, ftNs_MS_SpecialLwEnd,
                                    FTNESS_SPECIALLW_END_FLAG);
    ftCommon_ClampAirDrift(temp_r31);
}

void ftLc2_SpecialLwEnd_Enter(
    HSD_GObj* gobj) 
{
    Fighter_ChangeMotionState(gobj, ftNs_MS_SpecialLwEnd, 0, 0.0f, 1.0f, 0.0f,
                              NULL);
}

void ftLc2_SpecialAirLwEnd_Enter(
    HSD_GObj* gobj) 
{
    Fighter_ChangeMotionState(gobj, ftNs_MS_SpecialAirLwEnd, 0, 0.0f, 1.0f,
                              0.0f, NULL);
}
