/* Akaneia's Diddy Kong, Special N: Peanut Popgun.
 *
 * Start pulls the popgun (cmd_vars[0] from the script spawns it), Charge and Danger count frames
 * while B is held, releasing B fires (Shoot). Holding through Danger ends in Blow: the gun
 * explodes (cmd_vars[1] from the script destroys it) and in the air Diddy takes 5%.
 * Ground and air versions swap on landing / leaving the ground, keeping the frame. */
#include "ftdiddy.h"

#include <math.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/it/itcoll.h>
#include <melee/it/item.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbspdisplay.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/jobj.h>

/* Every Special N state lets a hit or a death take the gun away. */
static inline void ftDd_SpecialN_SetCallbacks(Fighter* fp)
{
    Fighter_SetDamageCallbacks(fp, ftDd_SpecialN_GunDestroy, ftDd_SpecialN_GunDestroy);
}

/* ------------------------------------------------------------------------------------------ */
/* The popgun                                                                                 */
/* ------------------------------------------------------------------------------------------ */

/* Gun_ChangeModel: the popgun's three looks are its item states 0 (idle), 1 (danger), 2 (blow). */
void ftDd_SpecialN_GunChangeModel(Item_GObj* gun, int model)
{
    /* The console code assumes the gun was created. A failed create leaves no gun to change. */
    if (gun == NULL) {
        return;
    }
    Item_80268E5C(gun, model, ITEM_ANIM_UPDATE);
}

/* Gun_Spawn. @p article is the popgun's place in the item list the fighter's articles come from:
 * Diddy's own popgun, or the one of the ability Kirby copies from him (diddy_kirby.c), whose
 * routine is this one with another article number. */
void ftDd_SpecialN_GunSpawnArticle(HSD_GObj* gobj, int article)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Item_GObj* gun = ftDd_SpawnArticle(gobj, article, 8, &fp->cur_pos, GA_Ground);

    ftDd_MV(fp)->specialn.gun = gun;
    /* A failed create (item limit, no article data): the move runs without the gun, as a
     * retail fighter's does when its item is not created. */
    if (gun == NULL) {
        return;
    }
    Item_8026AB54(gun, gobj, ftParts_GetBoneIndex(fp, (Fighter_Part) ftDd_Part_GunHand));
    ftDd_SpecialN_GunChangeModel(gun, 0);
}

/* Gun_Spawn, Diddy's own. */
void ftDd_SpecialN_GunSpawn(HSD_GObj* gobj)
{
    ftDd_SpecialN_GunSpawnArticle(gobj, ftDd_Article_Popgun);
}

/* Gun_Destroy, also the take-damage and death callback of every Special N state. */
void ftDd_SpecialN_GunDestroy(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftDd_MV(fp)->specialn.gun != NULL) {
        Item_8026A8EC(ftDd_MV(fp)->specialn.gun);
        ftDd_MV(fp)->specialn.gun = NULL;
    }
    fp->take_dmg_cb = NULL;
    fp->death2_cb = NULL;
}

/* Gun_Shoot: a peanut from the barrel (gun joint 7). Its speed, damage and knockback scale with
 * the whole charge; its angle flattens and the recoil grows until specialn_angle_charge of it.
 *
 * @p article is the peanut's place in the item list, @p da the thirteen values of the shot and
 * @p charge_frames the length of a whole charge. Diddy reads them from his attributes and his
 * fighter variables (ftDd_SpecialN_GunShoot below); the ability Kirby copies carries its own
 * (diddy_kirby.c). The routine is the same in both files. */
void ftDd_SpecialN_GunShootWith(HSD_GObj* gobj, int article, const ftDd_PeanutParams* da,
                                float charge_frames)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_JObj* gun_jobj;
    HSD_JObj* barrel;
    Vec3 muzzle;
    Item_GObj* nut;
    Item* ip;
    float charge, full, early, speed, angle, recoil;

    /* Without a gun there is no barrel to fire from; without a peanut there is nothing to
     * launch. The console code checks neither. */
    if (ftDd_MV(fp)->specialn.gun == NULL) {
        return;
    }
    gun_jobj = GET_JOBJ(ftDd_MV(fp)->specialn.gun);
    HSD_JObjSetMtxDirtySub(gun_jobj);
    lb_80011E24(gun_jobj, &barrel, 7, -1);
    lb_8000B1CC(barrel, NULL, &muzzle);

    nut = ftDd_SpawnArticle(gobj, article, 8, &muzzle, GA_Air);
    if (nut == NULL) {
        return;
    }
    ip = GET_ITEM(nut);

    charge = ftDd_MV(fp)->specialn.charge;
    full = charge / charge_frames;
    early = charge / (charge_frames * da->specialn_angle_charge);
    if (early > 1.0F) {
        early = 1.0F;
    }

    speed = da->specialn_speed_min + (da->specialn_speed_max - da->specialn_speed_min) * full;
    angle = da->specialn_angle_min_charge +
            (da->specialn_angle_max_charge - da->specialn_angle_min_charge) * early;
    ip->x40_vel.x = cosf(angle) * speed * fp->facing_dir;
    ip->x40_vel.y = sinf(angle) * speed;
    ip->x40_vel.z = 0.0F;

    {
        HitCapsule* hit = &ip->x5D4_hitboxes[0].hit;
        float dmg = da->specialn_dmg_min + (da->specialn_dmg_max - da->specialn_dmg_min) * full;
        float bkb = da->specialn_bkb_min + (da->specialn_bkb_max - da->specialn_bkb_min) * full;
        float kbg = da->specialn_kbg_min + (da->specialn_kbg_max - da->specialn_kbg_min) * full;

        it_80272460(hit, (int) dmg, nut);
        hit->x2C = (int) bkb; /* base knockback */
        hit->x24 = (int) kbg; /* knockback growth */
    }

    recoil = da->specialn_recoil_min + (da->specialn_recoil_max - da->specialn_recoil_min) * early;
    recoil *= -fp->facing_dir;
    if (fp->ground_or_air == GA_Ground) {
        fp->gr_vel = recoil;
    } else {
        fp->self_vel.x += recoil;
    }
}

/* Gun_Shoot, Diddy's own: the values of his attribute block (disc data, read into host order)
 * and the charge length his onload measured. */
void ftDd_SpecialN_GunShoot(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDd_DatAttrs* attrs = ftDd_Attrs(fp);
    ftDd_PeanutParams da;

    da.specialn_angle_charge = attrs->specialn_angle_charge;
    da.specialn_speed_min = attrs->specialn_speed_min;
    da.specialn_speed_max = attrs->specialn_speed_max;
    da.specialn_angle_min_charge = attrs->specialn_angle_min_charge;
    da.specialn_angle_max_charge = attrs->specialn_angle_max_charge;
    da.specialn_dmg_min = attrs->specialn_dmg_min;
    da.specialn_dmg_max = attrs->specialn_dmg_max;
    da.specialn_bkb_min = attrs->specialn_bkb_min;
    da.specialn_bkb_max = attrs->specialn_bkb_max;
    da.specialn_kbg_min = attrs->specialn_kbg_min;
    da.specialn_kbg_max = attrs->specialn_kbg_max;
    da.specialn_recoil_min = attrs->specialn_recoil_min;
    da.specialn_recoil_max = attrs->specialn_recoil_max;
    ftDd_SpecialN_GunShootWith(gobj, ftDd_Article_Peanut, &da,
                               ftDd_FV(fp)->peanut_charge_frames);
}

/* ------------------------------------------------------------------------------------------ */
/* Entering the states                                                                        */
/* ------------------------------------------------------------------------------------------ */

static void ftDd_SpecialN_StartInit(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    ftAnim_8006EBA4(gobj);
    ftDd_MV(fp)->specialn.gun = NULL;
    ftDd_MV(fp)->specialn.charge = 0;
    fp->cmd_vars[0] = 0;
    ftDd_SpecialN_SetCallbacks(fp);
}

/* SpecialNStart_Enter */
void ftDd_SpecialN_Enter(HSD_GObj* gobj)
{
    ftDd_SpecialN_StartInit(gobj, ftDd_MS_SpecialNStart);
}

/* SpecialAirNStart_Enter */
void ftDd_SpecialAirN_Enter(HSD_GObj* gobj)
{
    ftDd_SpecialN_StartInit(gobj, ftDd_MS_SpecialAirNStart);
}

static void ftDd_SpecialNCharge_Enter(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    ftDd_SpecialN_SetCallbacks(fp);
}

static void ftDd_SpecialNDanger_Enter(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    ftDd_SpecialN_GunChangeModel(ftDd_MV(fp)->specialn.gun, 1);
    ftDd_SpecialN_SetCallbacks(fp);
}

static void ftDd_SpecialNShoot_Enter(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    fp->cmd_vars[1] = 0;
    ftDd_SpecialN_GunShoot(gobj);
    ftDd_SpecialN_SetCallbacks(fp);
}

static void ftDd_SpecialNBlow_Enter(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    fp->cmd_vars[1] = 0;
    ftDd_SpecialN_GunChangeModel(ftDd_MV(fp)->specialn.gun, 2);
    ftDd_SpecialN_SetCallbacks(fp);
}

/* SpecialAirNBlow_Enter: the air explosion also hurts Diddy. */
static void ftDd_SpecialAirNBlow_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftDd_MS_SpecialAirNBlow, Ft_MF_None, 0.0F, 1.0F, 0.0F,
                              NULL);
    fp->cmd_vars[1] = 0;
    ftDd_SpecialN_GunChangeModel(ftDd_MV(fp)->specialn.gun, 2);
    Fighter_TakeDamage_8006CC7C(fp, 5.0F);
    ftDd_SpecialN_SetCallbacks(fp);
}

/* The *_Trans routines: same state on the other ground, same frame, blend and speed. */
static void ftDd_SpecialN_GroundToAir(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, msid, ftDd_MF_SwapN, fp->cur_anim_frame,
                              fp->frame_speed_mul, fp->x8A4_animBlendFrames, NULL);
    ftCommon_8007D5D4(fp);
    ftDd_SpecialN_SetCallbacks(fp);
}

static void ftDd_SpecialN_AirToGround(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, msid, ftDd_MF_SwapN, fp->cur_anim_frame,
                              fp->frame_speed_mul, fp->x8A4_animBlendFrames, NULL);
    ftCommon_8007D6A4(fp);
    ftDd_SpecialN_SetCallbacks(fp);
}

/* ------------------------------------------------------------------------------------------ */
/* Shared state bodies                                                                        */
/* ------------------------------------------------------------------------------------------ */

/* Start: pull the gun when the script says so, then charge. */
static void ftDd_SpecialN_StartAnim(HSD_GObj* gobj, FtMotionId charge)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftDd_MV(fp)->specialn.gun == NULL && fp->cmd_vars[0] != 0) {
        ftDd_SpecialN_GunSpawn(gobj);
        fp->cmd_vars[0] = 0;
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftDd_SpecialNCharge_Enter(gobj, charge);
    }
}

/* Charge and Danger: count while B is held, fire on release. */
static void ftDd_SpecialN_ChargeInput(HSD_GObj* gobj, FtMotionId shoot)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->input.held_buttons[0] & HSD_PAD_B) {
        ftDd_MV(fp)->specialn.charge++;
    } else {
        ftDd_SpecialNShoot_Enter(gobj, shoot);
    }
}

/* Blow and Shoot: the script's cmd_vars[1] (== 1) puts the gun away; the end of the animation
 * returns to Wait on the ground or Fall in the air. */
static bool ftDd_SpecialN_EndAnim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[1] == 1) {
        fp->cmd_vars[1] = 0;
        ftDd_SpecialN_GunDestroy(gobj);
    }
    return !ftAnim_IsFramesRemaining(gobj);
}

/* ------------------------------------------------------------------------------------------ */
/* Ground states                                                                              */
/* ------------------------------------------------------------------------------------------ */

void ftDd_SpecialNStart_Anim(HSD_GObj* gobj)
{
    ftDd_SpecialN_StartAnim(gobj, ftDd_MS_SpecialNCharge);
}

void ftDd_SpecialNStart_IASA(HSD_GObj* gobj) {}

void ftDd_SpecialNStart_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDd_SpecialNStart_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftDd_SpecialN_GroundToAir(gobj, ftDd_MS_SpecialAirNStart);
    }
}

void ftDd_SpecialNCharge_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftDd_SpecialNDanger_Enter(gobj, ftDd_MS_SpecialNDanger);
    }
}

void ftDd_SpecialNCharge_IASA(HSD_GObj* gobj)
{
    ftDd_SpecialN_ChargeInput(gobj, ftDd_MS_SpecialNShoot);
}

void ftDd_SpecialNCharge_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDd_SpecialNCharge_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftDd_SpecialN_GroundToAir(gobj, ftDd_MS_SpecialAirNCharge);
    }
}

void ftDd_SpecialNDanger_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftDd_SpecialNBlow_Enter(gobj, ftDd_MS_SpecialNBlow);
    }
}

void ftDd_SpecialNDanger_IASA(HSD_GObj* gobj)
{
    ftDd_SpecialN_ChargeInput(gobj, ftDd_MS_SpecialNShoot);
}

void ftDd_SpecialNDanger_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDd_SpecialNDanger_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftDd_SpecialN_GroundToAir(gobj, ftDd_MS_SpecialAirNDanger);
    }
}

void ftDd_SpecialNBlow_Anim(HSD_GObj* gobj)
{
    if (ftDd_SpecialN_EndAnim(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftDd_SpecialNBlow_IASA(HSD_GObj* gobj) {}

void ftDd_SpecialNBlow_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

/* Blow does not swap to its air version: walking off the ledge drops the gun and falls. */
void ftDd_SpecialNBlow_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftDd_SpecialN_GunDestroy(gobj);
        ftCo_Fall_Enter(gobj);
    }
}

/* PlDd.dat also calls m-ex's bp() here, an empty debugger hook. */
void ftDd_SpecialNShoot_Anim(HSD_GObj* gobj)
{
    if (ftDd_SpecialN_EndAnim(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftDd_SpecialNShoot_IASA(HSD_GObj* gobj) {}

void ftDd_SpecialNShoot_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDd_SpecialNShoot_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftDd_SpecialN_GroundToAir(gobj, ftDd_MS_SpecialAirNShoot);
    }
}

/* ------------------------------------------------------------------------------------------ */
/* Air states                                                                                 */
/* ------------------------------------------------------------------------------------------ */

void ftDd_SpecialAirNStart_Anim(HSD_GObj* gobj)
{
    ftDd_SpecialN_StartAnim(gobj, ftDd_MS_SpecialAirNCharge);
}

void ftDd_SpecialAirNStart_IASA(HSD_GObj* gobj) {}

void ftDd_SpecialAirNStart_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

void ftDd_SpecialAirNStart_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftDd_SpecialN_AirToGround(gobj, ftDd_MS_SpecialNStart);
    }
}

void ftDd_SpecialAirNCharge_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftDd_SpecialNDanger_Enter(gobj, ftDd_MS_SpecialAirNDanger);
    }
}

void ftDd_SpecialAirNCharge_IASA(HSD_GObj* gobj)
{
    ftDd_SpecialN_ChargeInput(gobj, ftDd_MS_SpecialAirNShoot);
}

void ftDd_SpecialAirNCharge_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

void ftDd_SpecialAirNCharge_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftDd_SpecialN_AirToGround(gobj, ftDd_MS_SpecialNCharge);
    }
}

void ftDd_SpecialAirNDanger_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftDd_SpecialAirNBlow_Enter(gobj);
    }
}

void ftDd_SpecialAirNDanger_IASA(HSD_GObj* gobj)
{
    ftDd_SpecialN_ChargeInput(gobj, ftDd_MS_SpecialAirNShoot);
}

void ftDd_SpecialAirNDanger_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

void ftDd_SpecialAirNDanger_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftDd_SpecialN_AirToGround(gobj, ftDd_MS_SpecialNDanger);
    }
}

void ftDd_SpecialAirNBlow_Anim(HSD_GObj* gobj)
{
    if (ftDd_SpecialN_EndAnim(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

void ftDd_SpecialAirNBlow_IASA(HSD_GObj* gobj) {}

void ftDd_SpecialAirNBlow_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

void ftDd_SpecialAirNBlow_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftDd_SpecialN_AirToGround(gobj, ftDd_MS_SpecialNBlow);
    }
}

void ftDd_SpecialAirNShoot_Anim(HSD_GObj* gobj)
{
    if (ftDd_SpecialN_EndAnim(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

void ftDd_SpecialAirNShoot_IASA(HSD_GObj* gobj) {}

void ftDd_SpecialAirNShoot_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

void ftDd_SpecialAirNShoot_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftDd_SpecialN_AirToGround(gobj, ftDd_MS_SpecialNShoot);
    }
}
