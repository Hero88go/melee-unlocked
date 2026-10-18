/* PlKbCpCn.dat: kbFunction +0000..+05A8, article 1 +0000..+0180.
 * Listing and archive evidence: chunli/astra17/hat/. The hat exports no
 * InitCopyItems. Its projectile uses article 1 already registered by Chun-Li's
 * load, with this hat's callbacks. The fighter never creates article 1 itself.
 * MxDt kirby_function[0..7][55] are all zero before the hat's exports. */
#include "chunli.h"
#include "../../akaneia/common/mu_ak_kirby.h"
#include <melee/db/db.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftKirby/ftkirby.h>
#include <melee/it/inlines.h>
#include <melee/it/it_26B1.h>
#include <melee/it/it_2725.h>
#include <melee/it/item.h>
#include <melee/it/itgroundcoll.h>
#include <melee/lb/lb_00B0.h>

/* Article +0010. The two parameters remain disc floats. */
typedef struct ftKbCn_ShotAttrs {
    float lifetime;
    float speed;
} DISC_STRUCT ftKbCn_ShotAttrs;

static void ftKbCn_ShotSpawned(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ftKbCn_ShotAttrs* attrs = DP(ip->xC4_article_data->x4_specialAttributes);
    it_80275158(gobj, attrs->lifetime);
    Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
    ip->x40_vel.x = attrs->speed * ip->facing_dir;
    it_8026B3A8(gobj);
}

static void ftKbCn_Nothing(Item_GObj* gobj) { (void) gobj; }
static bool ftKbCn_True(Item_GObj* gobj) { return true; }
static bool ftKbCn_False(Item_GObj* gobj) { return false; }
/* Article +00C0: common reflection, with no extra model rotation. */
static bool ftKbCn_Reflected(Item_GObj* gobj)
{
    it_80273030(gobj);
    return false;
}
/* Article +00F4: subtract first, then compare the new lifetime with zero. */
static bool ftKbCn_ShotAnim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    float timer = ip->xD44_lifeTimer - 1.0f;
    ip->xD44_lifeTimer = timer;
    return timer <= 0.0f;
}
/* Article +0130: the contact flags are read before collision updates them. */
static bool ftKbCn_ShotColl(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    u32 prior = (u32) ip->x378_itemColl.env_flags;
    it_8026D9A0(gobj);
    if (prior & 0x0001EFFFu) {
        ip->xD44_lifeTimer = 0.0f;
    }
    return false;
}
static ItemStateTable ftKbCn_ShotStates[] = {
    { 0, ftKbCn_ShotAnim, ftKbCn_Nothing, ftKbCn_ShotColl },
};
ItemLogicTable ftKbCn_ShotLogic = {
    .states = ftKbCn_ShotStates,
    .spawned = ftKbCn_ShotSpawned,
    .destroyed = ftKbCn_Nothing,
    .picked_up = ftKbCn_Nothing,
    .dropped = ftKbCn_Nothing,
    .thrown = ftKbCn_Nothing,
    .dmg_dealt = ftKbCn_True,
    .dmg_received = ftKbCn_True,
    .entered_air = ftKbCn_Nothing,
    .reflected = ftKbCn_Reflected,
    .shield_bounced = ftKbCn_False,
    .hit_shield = ftKbCn_True,
};

/* Hat +04C4. The private creator preserves ground state zero as written by
 * this routine. Its unspecified hold-kind and scratch bytes are zero here. */
static void ftKbCn_Spawn(HSD_GObj* owner, Vec3* pos, ItemKind kind, float facing)
{
    SpawnItem spawn = { 0 };
    Item_GObj* item;
    ftLib_80086990(owner, &spawn.pos);
    spawn.x0_parent_gobj = owner;
    spawn.x4_parent_gobj2 = owner;
    spawn.kind = kind;
    spawn.prev_pos.x = pos->x;
    spawn.prev_pos.y = pos->y;
    spawn.facing_dir = facing;
    spawn.x44_flag.b0 = true;
    item = mu_ak_item_create(&spawn);
    it_8026B3A8(item);
    db_80225DD8(item, owner);
    it_802750F8(item);
}

/* Hat +0260: console parts +01A0 is bone 26, stride 0x10. */
static void ftKbCn_Accessory(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->throw_flags_b0) {
        Vec3 pos;
        fp->throw_flags_b0 = false;
        lb_8000B1CC(fp->parts[26].joint, NULL, &pos);
        ftKbCn_Spawn(gobj, &pos, mu_ak_article_kind(gobj, 1), fp->facing_dir);
    }
}

/* Hat +011C/+0194. */
static void ftKbCn_Enter(HSD_GObj* gobj, int state)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->throw_flags_b0 = false;
    mu_ak_kirby_state_change(gobj, state, 0, 0.0f, 1.0f, 0.0f);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftKbCn_Accessory;
}
static void ftKbCn_Ground(HSD_GObj* gobj) { ftKbCn_Enter(gobj, 0); }
static void ftKbCn_Air(HSD_GObj* gobj) { ftKbCn_Enter(gobj, 1); }
/* Hat +02E8/+03B8. */
static void ftKbCn_GroundAnim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) ft_8008A2BC(gobj);
}
static void ftKbCn_AirAnim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) ftCo_Fall_Enter(gobj);
}
/* Hat +0338: no accessory callback restored on the ground-to-air change. */
static void ftKbCn_GroundColl(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        Fighter* fp = GET_FIGHTER(gobj);
        ftCommon_8007D5D4(fp);
        mu_ak_kirby_state_change(gobj, 1, 0, fp->cur_anim_frame, 1.0f, 0.0f);
    }
}
/* Hat +0408: restore the accessory only when the new frame is below 15. */
static void ftKbCn_AirColl(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        Fighter* fp = GET_FIGHTER(gobj);
        ftCommon_8007D7FC(fp);
        mu_ak_kirby_state_change(gobj, 0, 0, fp->cur_anim_frame, 1.0f, 0.0f);
        if (fp->cur_anim_frame < 15.0f) fp->accessory4_cb = ftKbCn_Accessory;
    }
}
static const MotionState ftKbCn_States[] = {
    { 0, 0x00340211, 0x12000000, ftKbCn_GroundAnim, NULL,
      ft_80084F3C, ftKbCn_GroundColl, ftCamera_UpdateCameraBox },
    { 1, 0x00340611, 0x12000000, ftKbCn_AirAnim, NULL,
      ft_80084DB0, ftKbCn_AirColl, ftCamera_UpdateCameraBox },
};
const MuAkKirbyCopy ftKbCn_Copy = {
    .on_swallow = mu_ak_kirby_attach_hat,
    .on_lose = ftKb_SpecialN_800EFAF0,
    .special_n = ftKbCn_Ground,
    .special_air_n = ftKbCn_Air,
    .move_logic = ftKbCn_States,
    .move_logic_count = 2,
};
