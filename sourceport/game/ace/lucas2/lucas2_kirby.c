/* PlKbCpLc2.dat kbFunction +0000..+0FE8, with byte-identical articles 2/8.
 * The table has eight states, two charge-release rows per ground/air move.
 * Parameters are the hat block's constants, not Lucas TDX's attributes. */
#include "lucas2.h"
#include "../../akaneia/common/mu_ak_kirby.h"
#include <math.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftKirby/ftkirby.h>
#include <melee/it/kinds/itnesspkflash.h>
#include <melee/lb/lb_00B0.h>

/* +0000 clears the custom held object only when the hat is first attached. */
static void swallow(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->u.kb.hat.jobj != NULL) return;
    mu_ak_kirby_attach_hat(gobj);
    *ftLc2_KirbyFlashSlot(fp) = NULL;
}

/* +00C8 is the retail single-joint hat release body. */
static void lose(HSD_GObj* gobj)
{
    ftKb_SpecialN_800EFAF0(gobj);
}

/* +0124/+01B4 clear fp+22A8, the retail Ness-copy slot. The custom
 * projectile slot at fp+2270 is not cleared by either entry. */
static void enter(HSD_GObj* gobj, int air)
{
    Fighter* fp = GET_FIGHTER(gobj);
    mu_ak_kirby_state_change(gobj, air ? 4 : 0, 0, 0.0f, 1.0f, 0.0f);
    fp->cmd_vars[0] = fp->cmd_vars[1] = fp->cmd_vars[2] = fp->cmd_vars[3] = 0;
    fp->u.kb.ns_flash_gobj = NULL;
    fp->mv.ns.specialn.frames_to_loop_charge_ground = 30;
    fp->mv.ns.specialn.frames_to_loop_charge_air = 25;
    fp->mv.ns.specialn.falling_acceleration_delay = 10;
    fp->mv.ns.specialn.charge_release_delay = 30;
    if (air) fp->self_vel.y = 0.0f;
    ftAnim_8006EBA4(gobj);
}

static void special_n(HSD_GObj* gobj) { enter(gobj, 0); }
static void special_air_n(HSD_GObj* gobj) { enter(gobj, 1); }

/* +0254: hat+0C/+10 are article 2/8 pointers. Export 7 is a no-op. */
static void init_items(FighterKind kind, KirbyHatStruct* hat)
{
    mu_ak_register_article(kind, DISC_GET(Article, hat->hat_dynamics[0]), 2);
    mu_ak_register_article(kind, DISC_GET(Article, hat->hat_dynamics[1]), 8);
}

static void empty(HSD_GObj* gobj) { (void) gobj; }

/* +03E8/+094C: body differs only by next-state index. */
static void start_anim(HSD_GObj* gobj, int air)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ftAnim_IsFramesRemaining(gobj)) return;
    mu_ak_kirby_state_change(gobj, air ? 5 : 1, 0, 0.0f, 1.0f, 1.0f);
    if (*ftLc2_KirbyFlashSlot(fp) == NULL) {
        Vec3 pos;
        lb_8000B1CC(fp->parts[5].joint, NULL, &pos);
        pos.z = 0.0f;
        pos.y = __builtin_fmaf(fp->x34_scale.y, 3.0f, pos.y);
        *ftLc2_KirbyFlashSlot(fp) = it_802AA8C0(gobj, &pos,
            mu_ak_article_kind(gobj, 2), fp->facing_dir);
        if (*ftLc2_KirbyFlashSlot(fp) != NULL) {
            fp->death2_cb = ftKb_Init_800EE74C;
            fp->take_dmg_cb = ftKb_Init_800EE7B8;
        }
    }
    fp->x1968_jumpsUsed = (u8) fp->co_attrs.max_jumps;
}

static void start_ground(HSD_GObj* gobj) { start_anim(gobj, 0); }
static void start_air(HSD_GObj* gobj) { start_anim(gobj, 1); }

/* +05C0/+0B74: both release states share this animation callback. */
static void hold_anim(HSD_GObj* gobj, int air)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Item_GObj* flash;
    int release = air ? 6 : 2;
    int end = air ? 7 : 3;
    if (fp->mv.ns.specialn.frames_to_loop_charge_ground != 0)
        fp->mv.ns.specialn.frames_to_loop_charge_ground--;
    flash = *ftLc2_KirbyFlashSlot(fp);
    if (flash != NULL) {
        if (it_802AA7E4(flash) != gobj) {
            *ftLc2_KirbyFlashSlot(fp) = NULL;
        } else if (it_802AA7F0(flash) == true && fp->motion_id != 400 + release) {
            mu_ak_kirby_state_change(gobj, release, 0, fp->cur_anim_frame, 1.0f, 0.0f);
        }
        return;
    }
    if (fp->mv.ns.specialn.frames_to_loop_charge_air != 0)
        fp->mv.ns.specialn.frames_to_loop_charge_air--;
    if (fp->mv.ns.specialn.frames_to_loop_charge_ground <= 0 &&
        fp->mv.ns.specialn.frames_to_loop_charge_air <= 0) {
        mu_ak_kirby_state_change(gobj, end, 0, 0.0f, 1.0f, 0.0f);
    } else if (fp->motion_id != 400 + release) {
        mu_ak_kirby_state_change(gobj, release, 0, fp->cur_anim_frame, 1.0f, 0.0f);
    }
}

static void hold_ground(HSD_GObj* gobj) { hold_anim(gobj, 0); }
static void hold_air(HSD_GObj* gobj) { hold_anim(gobj, 1); }

/* +0754/+0D08. Clears the custom slot but not the item's owner. */
static void hold_iasa(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->mv.ns.specialn.charge_release_delay--;
    if (fp->mv.ns.specialn.charge_release_delay > 0) return;
    fp->mv.ns.specialn.charge_release_delay = 0;
    if (fp->input.held_buttons[0] & HSD_PAD_B) return;
    *ftLc2_KirbyFlashSlot(fp) = NULL;
    fp->death2_cb = fp->take_dmg_cb = NULL;
}

/* +082C/+0E48: refresh the four timers every frame of the end animation. */
static void end_anim(HSD_GObj* gobj, int air)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->mv.ns.specialn.frames_to_loop_charge_ground = 30;
    fp->mv.ns.specialn.frames_to_loop_charge_air = 25;
    fp->mv.ns.specialn.falling_acceleration_delay = 10;
    fp->mv.ns.specialn.charge_release_delay = 30;
    *ftLc2_KirbyFlashSlot(fp) = NULL;
    fp->death2_cb = fp->take_dmg_cb = NULL;
    ftPartSetRotX(fp, 0, 0.0f);
    if (ftAnim_IsFramesRemaining(gobj)) return;
    if (air) ftCo_80096900(gobj, 1, 0, true, 1.0f, 30.0f);
    else ft_8008A2BC(gobj);
}

static void end_ground(HSD_GObj* gobj) { end_anim(gobj, 0); }
static void end_air(HSD_GObj* gobj) { end_anim(gobj, 1); }

static void ground_start_phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->mv.ns.specialn.falling_acceleration_delay != 0)
        fp->mv.ns.specialn.falling_acceleration_delay--;
    ft_80084F3C(gobj);
}

/* +0A80/+0D54/+0EF8. */
static void air_phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->mv.ns.specialn.falling_acceleration_delay != 0)
        fp->mv.ns.specialn.falling_acceleration_delay--;
    else ftCommon_Fall(fp, 0.017000000923871994f, fp->co_attrs.terminal_velocity);
    ftCommon_CalcSelfAccel_Deaccel(fp, fp->co_attrs.aerial_friction);
}

/* Every ground/air transition uses +0C4C5080, which KirbyStateChange ignores. */
static void ground_coll(HSD_GObj* gobj, int state)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80082708(gobj)) return;
    ftCommon_8007D5D4(fp);
    mu_ak_kirby_state_change(gobj, state, 0x0C4C5080, fp->cur_anim_frame, 1.0f, 0.0f);
}

static void air_coll(HSD_GObj* gobj, int state)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!ft_80081D0C(gobj)) return;
    ftCommon_8007D7FC(fp);
    mu_ak_kirby_state_change(gobj, state, 0x0C4C5080, fp->cur_anim_frame, 1.0f, 0.0f);
}

static void ground_start_coll(HSD_GObj* gobj) { ground_coll(gobj, 4); }
static void ground_hold_coll(HSD_GObj* gobj) { ground_coll(gobj, 5); }
static void ground_end_coll(HSD_GObj* gobj) { ground_coll(gobj, 7); }
static void air_start_coll(HSD_GObj* gobj) { air_coll(gobj, 0); }
static void air_hold_coll(HSD_GObj* gobj) { air_coll(gobj, 1); }
static void air_end_coll(HSD_GObj* gobj) { air_coll(gobj, 3); }

static const MotionState states[8] = {
    { 0, 0x0034011C, 0x12000000, start_ground, empty, ground_start_phys, ground_start_coll, ftCamera_UpdateCameraBox },
    { 1, 0x0034011C, 0x12000000, hold_ground, hold_iasa, ft_80084F3C, ground_hold_coll, ftCamera_UpdateCameraBox },
    { 2, 0x0034011C, 0x12000000, hold_ground, hold_iasa, ft_80084F3C, ground_hold_coll, ftCamera_UpdateCameraBox },
    { 3, 0x0034011C, 0x12000000, end_ground, empty, ft_80084F3C, ground_end_coll, ftCamera_UpdateCameraBox },
    { 4, 0x0034051C, 0x12000000, start_air, empty, air_phys, air_start_coll, ftCamera_UpdateCameraBox },
    { 5, 0x0034051C, 0x12000000, hold_air, hold_iasa, air_phys, air_hold_coll, ftCamera_UpdateCameraBox },
    { 6, 0x0034051C, 0x12000000, hold_air, hold_iasa, air_phys, air_hold_coll, ftCamera_UpdateCameraBox },
    { 7, 0x0034051C, 0x12000000, end_air, empty, air_phys, air_end_coll, ftCamera_UpdateCameraBox },
};

const MuAkKirbyCopy ftKbLc2_Copy = {
    .on_swallow = swallow, .on_lose = lose,
    .special_n = special_n, .special_air_n = special_air_n,
    .init_items = init_items, .move_logic = states, .move_logic_count = 8,
    .on_frame = empty,
};
