/* Akaneia's Wolf: the ability Kirby copies from him (m-ex "kbFunction" of PlKbCpWf.dat).
 *
 * Kirby's Wolf ability is Wolf's blaster with three things changed: the state change goes through
 * KirbyStateChange (the animation and script come from the hat file), the two articles are Wolf's
 * articles 2 and 3 (item kinds 255 and 256 on Akaneia, whose data is in the hat file), and the gun
 * sits on Kirby's part 0x2C where Wolf holds it on part 67. Every other instruction is the same as
 * in Wolf's own file (run-source/rel09-b1-wolf/kirby/wolf_code/PlKbCpWf.functions.md has the
 * function by function comparison), so the notes in wolf_specialn.c apply here unchanged.
 *
 * Both of Wolf's descriptors point here (wolf.c): ACE 2.0.0 names the same hat file for PlWfU.dat.
 * Nothing below keeps a kind: the articles are registered under the kind Kirby copied
 * (ftKbWf_InitCopyItems) and looked up through it (mu_ak_article_kind).
 *
 * Each function names the routine it was written from, as Akaneia's build calls it, and that
 * routine's offset in the hat file's code block (PlKbCpWf.listing.txt shows it at 0x81800000 plus
 * the offset). The code has no fixed console address: m-ex loads it with the hat file. */
#include "wolf.h"

#include <dolphin/os.h>

#include <melee/ft/forward.h>

#include <melee/db/db.h>
#include <melee/ef/efsync.h>
#include <melee/ef/types.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftKirby/ftkirby.h>
#include <melee/ft/types.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/it/inlines.h>
#include <melee/it/it_26B1.h>
#include <melee/it/item.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>

#include "../common/mu_ak_kirby.h"

/* Kirby's states of this ability (KirbyStateChange state numbers; motion ids 400 and 401). */
enum {
    ftKbWf_State_SpecialNStart,    /* 0: the blaster on the ground, the whole move */
    ftKbWf_State_SpecialAirNStart, /* 1: the blaster in the air */
    ftKbWf_State_Count
};

/* Wolf's articles that are Kirby's (indexes into Wolf's MxDt item list). */
enum {
    ftKbWf_Article_Laser = 2,   /* item kind 255 on Akaneia */
    ftKbWf_Article_Blaster = 3, /* item kind 256 on Akaneia */
};

/* Kirby's joint that holds the blaster: a raw fp->parts index, as Wolf's 67 is. */
#define FTKBWF_GUN_HAND_PART ((Fighter_Part) 0x2C)

/* The gun model's joint the laser leaves from (the same model joint as Wolf's gun). */
#define FTKBWF_GUN_MUZZLE_JOINT 5

#define FTKBWF_LASER_SPEED 2.3f

/* [OnKirbySwallow] +0x000 (48 words): put the hat on. The retail hat attach without the x2225_b2
 * write; the same for all seven hats' first step. */
static void ftKbWf_OnSwallow(HSD_GObj* gobj)
{
    mu_ak_kirby_attach_hat(gobj);
}

/* [OnKirbyLoseAbility] +0x0C0 (23 words): take the hat off. Instruction for instruction the retail
 * ftKb_SpecialN_800EFAF0 (remove the hat joint, free its display list array). */
static void ftKbWf_OnLose(HSD_GObj* gobj)
{
    ftKb_SpecialN_800EFAF0(gobj);
}

/* [OnKirbyHurt] +0x124 (1 word): empty. */
static void ftKbWf_OnHurt(HSD_GObj* gobj) {}

/* [InitCopyItems] +0x128 (19 words): the hat file's two articles become Wolf's articles 2 and 3.
 * The hat data is a KirbyHatStruct whose first two extra words (+0xC, +0x10) are the laser's and
 * the blaster's article data. */
static void ftKbWf_InitCopyItems(FighterKind kind, KirbyHatStruct* hat)
{
    mu_ak_register_article(kind, DISC_GET(Article, hat->hat_dynamics[0]), ftKbWf_Article_Laser);
    mu_ak_register_article(kind, DISC_GET(Article, hat->hat_dynamics[1]), ftKbWf_Article_Blaster);
}

/* [Gun_Spawn] +0x448 (57 words): the blaster appears in Kirby's hand. Wolf's routine with article
 * 3 and part 0x2C. */
static void ftKbWf_SpawnGun(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    SpawnItem spawn = { 0 };
    Item_GObj* gun;

    spawn.x0_parent_gobj = NULL;
    spawn.x4_parent_gobj2 = NULL;
    spawn.kind = mu_ak_article_kind(gobj, ftKbWf_Article_Blaster);
    if ((int) spawn.kind < 0) {
        /* Not an m-ex case: no item kind for the article (m-ex stops the game there). */
        OSReport("[ak] Kirby (Wolf): no item kind for the blaster article (copied kind %d)\n",
                 (int) fp->u.kb.hat.kind);
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
    /* m-ex attaches without checking the spawn. */
    if (gun != NULL) {
        Item_8026AB54(gun, gobj, FTKBWF_GUN_HAND_PART);
    }

    fp->cmd_vars[1] = 0;
    /* [Gun_Destroy] +0x744 is the same code as Wolf's own: one function serves both. */
    Fighter_SetDamageCallback(gobj, ftWf_SpecialN_DestroyGun);
}

/* [Laser_Spawn] +0x52C (114 words): fire one shot from the gun's muzzle. Wolf's routine with
 * article 2. The effect id (5005) is read from the file of the fighter Kirby copied, Wolf's
 * (LAYER.md, effects). */
static void ftKbWf_FireLaser(HSD_GObj* gobj)
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
    lb_80011E24(gun_jobj, &muzzle_jobj, FTKBWF_GUN_MUZZLE_JOINT, -1);
    lb_8000B1CC(muzzle_jobj, NULL, &muzzle_pos);
    it_8026BB68(gobj, &center); /* the middle of Kirby's ECB */

    spawn.x0_parent_gobj = gobj;
    spawn.x4_parent_gobj2 = gobj;
    spawn.kind = mu_ak_article_kind(gobj, ftKbWf_Article_Laser);
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
        lv->speed = FTKBWF_LASER_SPEED;
        /* The first wall test runs from Kirby's middle to where the laser is. */
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

/* [SpecialN] +0x11C is a branch to [SpecialNStart_Enter] +0x1B4 (23 words): the special_n slot.
 * KirbyStateChange(gobj, 0, 0, NULL, 0.0, 1.0, 0.0). */
static void ftKbWf_SpecialN_Enter(HSD_GObj* gobj)
{
    mu_ak_kirby_state_change(gobj, ftKbWf_State_SpecialNStart, Ft_MF_None, 0.0f, 1.0f, 0.0f);
    ftAnim_8006EBA4(gobj);
    ftKbWf_SpawnGun(gobj);
}

/* [SpecialAirN] +0x120 is a branch to [SpecialAirNStart_Enter] +0x210 (23 words). The flag the
 * code passes (2, Ft_MF_KeepGfx, as Wolf's own air entry does) is not read by KirbyStateChange. */
static void ftKbWf_SpecialAirN_Enter(HSD_GObj* gobj)
{
    mu_ak_kirby_state_change(gobj, ftKbWf_State_SpecialAirNStart, Ft_MF_KeepGfx, 0.0f, 1.0f, 0.0f);
    ftAnim_8006EBA4(gobj);
    ftKbWf_SpawnGun(gobj);
}

/* [.text.SpecialNStart_Coll.part.0] +0x6F4 (20 words): put the gun away and fall. */
static void ftKbWf_EndToFall(HSD_GObj* gobj)
{
    ftWf_SpecialN_DestroyGun(gobj);
    ftCo_Fall_Enter(gobj);
}

/* [SpecialNStart_Anim] +0x26C (38 words) */
static void ftKbWf_SpecialNStart_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[1] == 1) {
        ftKbWf_FireLaser(gobj);
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftWf_SpecialN_DestroyGun(gobj);
        ft_8008A2BC(gobj);
    }
}

/* [SpecialNStart_Coll] +0x30C (19 words) */
static void ftKbWf_SpecialNStart_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftKbWf_EndToFall(gobj);
    }
}

/* [SpecialAirNStart_Anim] +0x358 (27 words) */
static void ftKbWf_SpecialAirNStart_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[1] == 1) {
        ftKbWf_FireLaser(gobj);
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftKbWf_EndToFall(gobj);
    }
}

/* [move_logic] +0x174 (two entries of 0x20 bytes). Words +4 and +8 are Wolf's own (Fox's blaster
 * flags and move id 0x12); KirbyStateChange does not read them. The callbacks that are the same
 * instructions as Wolf's own are Wolf's functions:
 *   [SpecialNStart_IASA] +0x304, [SpecialNStart_Phys] +0x308,
 *   [SpecialAirNStart_IASA] +0x3C4, [SpecialAirNStart_Phys] +0x3C8, [SpecialAirNStart_Coll] +0x3CC. */
static const MotionState ftKbWf_MotionStateTable[ftKbWf_State_Count] = {
    {
        /* state 0, ftcmd 0 "WfSpecialN" */
        0,
        0x00340111,
        0x12 << 24,
        ftKbWf_SpecialNStart_Anim,
        ftWf_SpecialNStart_IASA,
        ftWf_SpecialNStart_Phys,
        ftKbWf_SpecialNStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 1, ftcmd 1 "WfSpecialNAir" */
        1,
        0x00340511,
        0x12 << 24,
        ftKbWf_SpecialAirNStart_Anim,
        ftWf_SpecialAirNStart_IASA,
        ftWf_SpecialAirNStart_Phys,
        ftWf_SpecialAirNStart_Coll,
        ftCamera_UpdateCameraBox,
    },
};

/* The kbFunction exports of PlKbCpWf.dat (0 to 6; it has no export 7). */
const MuAkKirbyCopy ftKbWf_Copy = {
    .on_swallow = ftKbWf_OnSwallow,
    .on_lose = ftKbWf_OnLose,
    .special_n = ftKbWf_SpecialN_Enter,
    .special_air_n = ftKbWf_SpecialAirN_Enter,
    .on_hurt = ftKbWf_OnHurt,
    .init_items = ftKbWf_InitCopyItems,
    .move_logic = ftKbWf_MotionStateTable,
    .move_logic_count = ftKbWf_State_Count,
    .on_frame = NULL,
};
