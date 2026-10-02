/* Akaneia's Wolf: neutral special, the blaster.
 *
 * Unlike Fox's three-part blaster (start, loop, end), Wolf's is one animation per ground state: the
 * gun is created when the move starts, the animation script sets cmd_vars[1] on the frame the shot
 * fires, and the gun is removed when the animation ends. States 342, 343, 345 and 346 keep Fox's
 * callbacks in the move table but Wolf never enters them. */
#include "wolf.h"

#include <melee/ft/forward.h>

#include <melee/db/db.h>
#include <melee/ef/efsync.h>
#include <melee/ef/types.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/types.h>
#include <melee/it/inlines.h>
#include <melee/it/it_26B1.h>
#include <melee/it/item.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbspdisplay.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>

/* Wolf's skeleton joint that holds the blaster: a raw fp->parts index (like FtPart_109), which
 * Item_8026AB54 stores and ftLib_80086630 reads back without remapping. */
#define FTWF_GUN_HAND_PART ((Fighter_Part) 67)

/* The gun model's joint the laser leaves from (lb_80011E24 index, counted from the gun's root). */
#define FTWF_GUN_MUZZLE_JOINT 5

#define FTWF_LASER_SPEED 2.3f

/* m-ex code creates its articles with the retail spawner itself (Item_8026862C), passing the hold
 * kind of character items (8). That function is static in it/item.c. Until the integration layer
 * exposes it (NOTES.md: mu_ak_item_create, MU_AK_HAVE_ITEM_CREATE), the public grounded spawner is
 * the stand-in: it overwrites the hold kind from the item kind, so an added kind counts as a stage
 * projectile (hold kind 5, which has a spawn limit) instead of a character item. */
#ifdef MU_AK_HAVE_ITEM_CREATE
Item_GObj* mu_ak_item_create(SpawnItem* spawn);
#endif

Item_GObj* ftWf_CreateItem(SpawnItem* spawn)
{
#ifdef MU_AK_HAVE_ITEM_CREATE
    return mu_ak_item_create(spawn);
#else
    return Item_80268B5C(spawn);
#endif
}

/* [Gun_Destroy]: the damage callbacks while the gun is out, and the end of the move. */
void ftWf_SpecialN_DestroyGun(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWolf_SpecialNVars* sn = &ftWf_MV(fp)->SpecialN;

    /* m-ex calls Item_8026A8EC unconditionally; a gun that failed to spawn would crash it. */
    if (sn->gun_gobj != NULL) {
        Item_8026A8EC(sn->gun_gobj);
    }
    sn->gun_gobj = NULL;
    Fighter_SetDamageCallback(gobj, NULL);
}

/* [Gun_Spawn]: the blaster appears in Wolf's hand. */
static void ftWf_SpecialN_SpawnGun(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    SpawnItem spawn = { 0 };
    Item_GObj* gun;

    spawn.x0_parent_gobj = NULL;
    spawn.x4_parent_gobj2 = NULL;
    spawn.kind = mu_ak_item_kind(fp->kind, ftWf_Article_Blaster);
    if ((int) spawn.kind < 0) {
        /* Not an m-ex case: the registry has no item kind for the article (retail view). */
        OSReport("[ak] Wolf: no item kind for the blaster article (fighter kind %d)\n",
                 (int) fp->kind);
        ftWf_MV(fp)->SpecialN.gun_gobj = NULL;
        fp->cmd_vars[1] = 0;
        Fighter_SetDamageCallback(gobj, ftWf_SpecialN_DestroyGun);
        return;
    }
    spawn.hold_kind = ITEM_HOLD_8;
    spawn.x10 = 0;
    spawn.pos = fp->cur_pos;
    spawn.prev_pos = fp->cur_pos;
    spawn.vel.x = spawn.vel.y = spawn.vel.z = 0.0f;
    spawn.facing_dir = fp->facing_dir;
    spawn.x3C_damage = 0;
    spawn.x3E = 0;
    spawn.x40 = 0;
    spawn.x44_flag.b0 = false;
    spawn.x48_ground_or_air = GA_Ground;

    gun = ftWf_CreateItem(&spawn);
    ftWf_MV(fp)->SpecialN.gun_gobj = gun;
    /* m-ex attaches without checking the spawn either. */
    if (gun != NULL) {
        Item_8026AB54(gun, gobj, FTWF_GUN_HAND_PART);
    }

    fp->cmd_vars[1] = 0;
    Fighter_SetDamageCallback(gobj, ftWf_SpecialN_DestroyGun);
}

/* [Laser_Spawn]: fire one shot from the gun's muzzle. Called by the start states on the frame the
 * animation script sets cmd_vars[1]. */
static void ftWf_SpecialN_FireLaser(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Item_GObj* gun = ftWf_MV(fp)->SpecialN.gun_gobj;
    HSD_JObj* gun_jobj;
    HSD_JObj* muzzle_jobj;
    Vec3 muzzle_pos;
    Vec3 center;
    SpawnItem spawn = { 0 };
    Item_GObj* laser;

    fp->cmd_vars[1] = 0;

    /* m-ex reads the gun's joint unchecked; without a gun there is no muzzle to fire from. */
    if (gun == NULL) {
        return;
    }
    gun_jobj = GET_JOBJ(gun);

    HSD_JObjSetMtxDirtySub(gun_jobj);
    lb_80011E24(gun_jobj, &muzzle_jobj, FTWF_GUN_MUZZLE_JOINT, -1);
    lb_8000B1CC(muzzle_jobj, NULL, &muzzle_pos);
    it_8026BB68(gobj, &center); /* the middle of Wolf's ECB */

    spawn.x0_parent_gobj = gobj;
    spawn.x4_parent_gobj2 = gobj;
    spawn.kind = mu_ak_item_kind(fp->kind, ftWf_Article_Laser);
    if ((int) spawn.kind < 0) {
        return;
    }
    spawn.hold_kind = ITEM_HOLD_8;
    spawn.x10 = 0;
    spawn.pos = center;
    spawn.prev_pos = muzzle_pos;
    spawn.vel.x = spawn.vel.y = spawn.vel.z = 0.0f;
    spawn.facing_dir = fp->facing_dir;
    spawn.x3C_damage = 0;
    spawn.x3E = 0;
    spawn.x40 = 0;
    spawn.x44_flag.b0 = false;
    spawn.x48_ground_or_air = GA_Ground;

    laser = ftWf_CreateItem(&spawn);
    {
        /* Bring-up evidence (the first shots only): where the shot leaves from. On the console
         * recording the first shot of a Wolf standing at x 41.35 appears at (53.32, 5.36). */
        static int logged;
        if (logged < 6) {
            Item* gp = GET_ITEM(gun);
            logged++;
            OSReport("[ak] Wolf laser: wolf (%.2f, %.2f) facing %.0f, gun item (%.2f, %.2f, %.2f), "
                     "muzzle (%.2f, %.2f, %.2f), middle (%.2f, %.2f), created %d\n",
                     fp->cur_pos.x, fp->cur_pos.y, fp->facing_dir, gp->pos.x, gp->pos.y,
                     gp->pos.z, muzzle_pos.x, muzzle_pos.y, muzzle_pos.z, center.x, center.y,
                     laser != NULL);
        }
    }
    if (laser == NULL) {
        return;
    }

    {
        Item* ip = GET_ITEM(laser);
        itWolfLaser_ItemVars* lv = itWfLaser_Vars(ip);
        float angle = ip->facing_dir > 0.0f ? 0.0f : 3.1415927f;
        float effect_angle;
        EF_Effect* effect;

        lv->xDD4 = 0.0f;
        lv->speed = FTWF_LASER_SPEED;
        /* The first wall test runs from Wolf's middle to where the laser is. */
        lv->prev_pos = center;
        lv->angle = angle;
        db_80225DD8(laser, gobj);

        effect_angle = lv->angle;
        effect = efSync_Spawn(ftWf_Ef_BlasterMuzzle, gobj, &muzzle_pos, &effect_angle);
        if (effect != NULL) {
            /* Written straight into the joint, without marking its matrix dirty, as m-ex does. */
            HSD_JObj* effect_jobj = GET_JOBJ(effect->gobj);
            effect_jobj->rotate.y = 1.5707964f;
        }
    }
}

/* [SpecialNStart_Enter]: the specialn slot. */
void ftWf_SpecialN_Enter(HSD_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialNStart, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    ftWf_SpecialN_SpawnGun(gobj);
}

/* [SpecialAirNStart_Enter]: the specialairn slot. */
void ftWf_SpecialAirN_Enter(HSD_GObj* gobj)
{
    Fighter_ChangeMotionState(gobj, ftWf_MS_SpecialAirNStart, Ft_MF_KeepGfx, 0.0f, 1.0f, 0.0f,
                              NULL);
    ftAnim_8006EBA4(gobj);
    ftWf_SpecialN_SpawnGun(gobj);
}

/* [.text.SpecialNStart_Coll.part.0]: put the gun away and fall. */
static void ftWf_SpecialN_EndToFall(HSD_GObj* gobj)
{
    ftWf_SpecialN_DestroyGun(gobj);
    ftCo_Fall_Enter(gobj);
}

void ftWf_SpecialNStart_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[1] == 1) {
        ftWf_SpecialN_FireLaser(gobj);
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftWf_SpecialN_DestroyGun(gobj);
        ft_8008A2BC(gobj);
    }
}

void ftWf_SpecialNStart_IASA(HSD_GObj* gobj) {}

void ftWf_SpecialNStart_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftWf_SpecialNStart_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftWf_SpecialN_EndToFall(gobj);
    }
}

void ftWf_SpecialAirNStart_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[1] == 1) {
        ftWf_SpecialN_FireLaser(gobj);
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftWf_SpecialN_EndToFall(gobj);
    }
}

void ftWf_SpecialAirNStart_IASA(HSD_GObj* gobj) {}

void ftWf_SpecialAirNStart_Phys(HSD_GObj* gobj)
{
    ft_80084DB0(gobj);
}

/* Landing puts the gun away and lands (ft_80082B1C); the shot is not carried to the ground. */
void ftWf_SpecialAirNStart_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj) == 1) {
        ftWf_SpecialN_DestroyGun(gobj);
        ft_80082B1C(gobj);
    }
}
