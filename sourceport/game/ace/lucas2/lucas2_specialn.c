/* PlLc2.dat ftFunction +05B4/+0658 and +0E70..+1AB0. */
#include "lucas2.h"
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/it/kinds/itnesspkflash.h>
#include <melee/lb/lb_00B0.h>

static void init(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNessAttributes* a = fp->dat_attrs;
    fp->mv.ns.specialn.frames_to_loop_charge_ground = a->x0_PKFLASH_TIMER1_LOOPFRAMES;
    fp->mv.ns.specialn.frames_to_loop_charge_air = a->x4_PKFLASH_TIMER2_LOOPFRAMES;
    fp->mv.ns.specialn.falling_acceleration_delay = a->x8_PKFLASH_GRAVITY_DELAY;
    fp->mv.ns.specialn.charge_release_delay = a->xC_PKFLASH_MINCHARGEFRAMES;
    fp->u.ns.pkflash_gobj = NULL;
    fp->death2_cb = fp->take_dmg_cb = NULL;
}

static void enter(HSD_GObj* gobj, bool air)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter_ChangeMotionState(gobj, air ? 352 : 348, 0, 0.0f, 1.0f, 0.0f, NULL);
    if (air) fp->self_vel.y = 0.0f;
    fp->cmd_vars[0] = fp->cmd_vars[1] = fp->cmd_vars[2] = fp->cmd_vars[3] = 0;
    init(gobj);
    ftAnim_8006EBA4(gobj);
}

void ftLc2_SpecialNStart_Enter(HSD_GObj* gobj) { enter(gobj, false); }
void ftLc2_SpecialAirNStart_Enter(HSD_GObj* gobj) { enter(gobj, true); }

/* +0E70/+13D4: the initial hold transition passes blend 1.0, then article 2. */
static void start_anim(HSD_GObj* gobj, bool air)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ftAnim_IsFramesRemaining(gobj)) return;
    Fighter_ChangeMotionState(gobj, air ? 353 : 349, 0, 0.0f, 1.0f, 1.0f, NULL);
    if (fp->u.ns.pkflash_gobj == NULL) {
        Vec3 pos;
        lb_8000B1CC(fp->parts[24].joint, NULL, &pos);
        pos.z = 0.0f;
        pos.y = __builtin_fmaf(fp->x34_scale.y, 3.0f, pos.y);
        fp->u.ns.pkflash_gobj = it_802AA8C0(gobj, &pos,
            mu_ak_article_kind(gobj, 2), fp->facing_dir);
        if (fp->u.ns.pkflash_gobj != NULL) {
            fp->death2_cb = fp->take_dmg_cb = ftLc2_Init_OnDamage;
        }
    }
    fp->x1968_jumpsUsed = (u8) fp->co_attrs.max_jumps;
}

void ftLc2_SpecialNStart_Anim(HSD_GObj* gobj) { start_anim(gobj, false); }
void ftLc2_SpecialAirNStart_Anim(HSD_GObj* gobj) { start_anim(gobj, true); }

/* +1040/+15F4: retail owner/state accessors are explicit fixed call targets. */
static void hold_anim(HSD_GObj* gobj, bool air)
{
    Fighter* fp = GET_FIGHTER(gobj);
    int release = air ? 354 : 350;
    int end = air ? 355 : 351;
    if (fp->mv.ns.specialn.frames_to_loop_charge_ground != 0)
        fp->mv.ns.specialn.frames_to_loop_charge_ground--;
    if (fp->u.ns.pkflash_gobj != NULL) {
        if (it_802AA7E4(fp->u.ns.pkflash_gobj) != gobj) {
            fp->u.ns.pkflash_gobj = NULL;
        } else if (it_802AA7F0(fp->u.ns.pkflash_gobj) == true && fp->motion_id != release) {
            Fighter_ChangeMotionState(gobj, release, 0, fp->cur_anim_frame, 1.0f, 0.0f, NULL);
        }
        return;
    }
    if (fp->mv.ns.specialn.frames_to_loop_charge_air != 0)
        fp->mv.ns.specialn.frames_to_loop_charge_air--;
    if (fp->mv.ns.specialn.frames_to_loop_charge_ground <= 0 &&
        fp->mv.ns.specialn.frames_to_loop_charge_air <= 0) {
        Fighter_ChangeMotionState(gobj, end, 0, 0.0f, 1.0f, 0.0f, NULL);
    } else if (fp->motion_id != release) {
        Fighter_ChangeMotionState(gobj, release, 0, fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    }
}

void ftLc2_SpecialNRelease_Anim(HSD_GObj* gobj) { hold_anim(gobj, false); }
void ftLc2_SpecialAirNRelease_Anim(HSD_GObj* gobj) { hold_anim(gobj, true); }

/* +11D4/+1788: at zero release delay, dropping B clears only the held slot. */
static void hold_iasa(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->mv.ns.specialn.charge_release_delay--;
    if (fp->mv.ns.specialn.charge_release_delay > 0) return;
    fp->mv.ns.specialn.charge_release_delay = 0;
    if (fp->input.held_buttons[0] & HSD_PAD_B) return;
    fp->u.ns.pkflash_gobj = NULL;
    fp->death2_cb = fp->take_dmg_cb = NULL;
}

void ftLc2_SpecialNRelease_IASA(HSD_GObj* gobj) { hold_iasa(gobj); }
void ftLc2_SpecialAirNRelease_IASA(HSD_GObj* gobj) { hold_iasa(gobj); }
void ftLc2_SpecialNStart_IASA(HSD_GObj* gobj) { (void) gobj; }
void ftLc2_SpecialAirNStart_IASA(HSD_GObj* gobj) { (void) gobj; }
void ftLc2_SpecialNEnd_IASA(HSD_GObj* gobj) { (void) gobj; }
void ftLc2_SpecialAirNEnd_IASA(HSD_GObj* gobj) { (void) gobj; }

/* +12AC/+18C8 refresh the timer words during the end pose. */
static void end_anim(HSD_GObj* gobj, bool air)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNessAttributes* a = fp->dat_attrs;
    init(gobj);
    ftPartSetRotX(fp, 0, 0.0f);
    if (ftAnim_IsFramesRemaining(gobj)) return;
    if (!air) ft_8008A2BC(gobj);
    else if (a->x1C_PKFLASH_LANDING_LAG == 0.0f) ftCo_Fall_Enter(gobj);
    else ftCo_80096900(gobj, 1, 0, true, 1.0f, a->x1C_PKFLASH_LANDING_LAG);
}

void ftLc2_SpecialNEnd_Anim(HSD_GObj* gobj) { end_anim(gobj, false); }
void ftLc2_SpecialAirNEnd_Anim(HSD_GObj* gobj) { end_anim(gobj, true); }

void ftLc2_SpecialNStart_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->mv.ns.specialn.falling_acceleration_delay != 0)
        fp->mv.ns.specialn.falling_acceleration_delay--;
    ft_80084F3C(gobj);
}

void ftLc2_SpecialNRelease_Phys(HSD_GObj* gobj) { ft_80084F3C(gobj); }
void ftLc2_SpecialNEnd_Phys(HSD_GObj* gobj) { ft_80084F3C(gobj); }

static void air_phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNessAttributes* a = fp->dat_attrs;
    if (fp->mv.ns.specialn.falling_acceleration_delay != 0)
        fp->mv.ns.specialn.falling_acceleration_delay--;
    else ftCommon_Fall(fp, a->x14_PKFLASH_FALL_ACCEL, fp->co_attrs.terminal_velocity);
    ftCommon_CalcSelfAccel_Deaccel(fp, fp->co_attrs.aerial_friction);
}

void ftLc2_SpecialAirNStart_Phys(HSD_GObj* gobj) { air_phys(gobj); }
void ftLc2_SpecialAirNRelease_Phys(HSD_GObj* gobj) { air_phys(gobj); }
void ftLc2_SpecialAirNEnd_Phys(HSD_GObj* gobj) { air_phys(gobj); }

static void ground_coll(HSD_GObj* gobj, int next)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80082708(gobj)) return;
    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, next, 0x0C4C5080, fp->cur_anim_frame, 1.0f, 0.0f, NULL);
}

static void air_coll(HSD_GObj* gobj, int next)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ft_80081D0C(gobj)) return;
    ftCommon_8007D7FC(fp);
    Fighter_ChangeMotionState(gobj, next, 0x0C4C5080, fp->cur_anim_frame, 1.0f, 0.0f, NULL);
}

void ftLc2_SpecialNStart_Coll(HSD_GObj* gobj) { ground_coll(gobj, 352); }
void ftLc2_SpecialNRelease_Coll(HSD_GObj* gobj) { ground_coll(gobj, 353); }
void ftLc2_SpecialNEnd_Coll(HSD_GObj* gobj) { ground_coll(gobj, 355); }
void ftLc2_SpecialAirNStart_Coll(HSD_GObj* gobj) { air_coll(gobj, 348); }
void ftLc2_SpecialAirNRelease_Coll(HSD_GObj* gobj) { air_coll(gobj, 349); }
void ftLc2_SpecialAirNEnd_Coll(HSD_GObj* gobj) { air_coll(gobj, 351); }
