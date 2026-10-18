/* PlKbCpRc.dat kbFunction +0000..+070C and article 1.
 * Evidence: raichu/verified16/hat/listing.txt. No export 4, 5 or 7. */
#include "raichu.h"
#include "../../akaneia/common/mu_ak_kirby.h"
#include <melee/db/db.h>
#include <melee/ft/fighter.h>
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
#include <melee/it/item.h>
#include <melee/it/it_2725.h>
#include <melee/it/it_3F14.h>
#include <melee/it/itgroundcoll.h>
#include <melee/it/kinds/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbvector.h>
#include <melee/mp/mpcoll.h>
#include <math.h>

static bool ftKbRc_True(Item_GObj* gobj) { return true; }
static bool ftKbRc_Reflected(Item_GObj* gobj)
{
    it_80273030(gobj);
    return false;
}
/* Hat article +50..+120. +210 is double 0.00001 (3EE4F8B588E368F1),
 * whereas retail BounceOffShield compares with the rounded float. */
static bool ftKbRc_ShieldBounce(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    lbVector_Mirror(&ip->x40_vel, &ip->xC58);
    if (fabsf(ip->x40_vel.x) >= 0.00001 || ip->facing_dir == 0.0f) {
        ip->facing_dir = ip->x40_vel.x > 0.0f ? 1.0f : -1.0f;
    }
    mpCollSetFacingDir(&ip->x378_itemColl, ip->facing_dir == -1.0f ? -1 : 1);
    return false;
}
/* +12C: same decrement and comparison as it_80273130. */
static bool ftKbRc_Anim(Item_GObj* gobj) { return it_80273130(gobj); }
static void ftKbRc_Phys(Item_GObj* gobj)
{
    it_80274658(gobj, it_804D6D28->x68_float);
}
static bool ftKbRc_Coll(Item_GObj* gobj)
{
    it_8026D9A0(gobj);
    /* +21C initializes r3 to zero, unlike the fighter's +F8. */
    return itRc_Jolt_CheckSurfacesFrom(gobj, false);
}
static ItemStateTable ftKbRc_ItemStates[] = {
    { 0, ftKbRc_Anim, ftKbRc_Phys, ftKbRc_Coll },
};
static ItemLogicTable ftKbRc_ItemLogic = {
    .states = ftKbRc_ItemStates, .dmg_dealt = ftKbRc_True,
    .reflected = ftKbRc_Reflected, .clanked = ftKbRc_True,
    .shield_bounced = ftKbRc_ShieldBounce, .hit_shield = ftKbRc_True,
};

/* +52C and +644. The hat has no InitCopyItems export: article data comes
 * from Raichu's load. Select the hat's own item callbacks on this instance,
 * so a simultaneous fighter projectile retains its own callbacks. */
static void ftKbRc_Spawn(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->throw_flags_b0) {
        Vec3 pos;
        SpawnItem spawn = { 0 };
        Item_GObj* item;
        Item* ip;
        float* attrs;
        fp->throw_flags_b0 = false;
        /* Console parts+2B0: index 43. */
        lb_8000B1CC(fp->parts[43].joint, NULL, &pos);
        ftLib_80086990(gobj, &spawn.pos);
        spawn.x0_parent_gobj = spawn.x4_parent_gobj2 = gobj;
        spawn.kind = mu_ak_article_kind(gobj, 1);
        spawn.prev_pos.x = pos.x + fp->facing_dir;
        spawn.prev_pos.y = pos.y;
        spawn.facing_dir = fp->facing_dir;
        spawn.x44_flag.b0 = true;
        item = Item_80268B18(&spawn);
        ip = GET_ITEM(item);
        ip->xB8_itemLogicTable = &ftKbRc_ItemLogic;
        ip->xBC_itemStateContainer = ftKbRc_ItemStates;
        attrs = DP(ip->xC4_article_data->x4_specialAttributes);
        float speed = attrs[0] * ip->facing_dir;
        if (fp->ground_or_air == GA_Air) {
            ip->x40_vel.x = cosf(attrs[1]) * speed;
            ip->x40_vel.y = sinf(attrs[1]) * attrs[0];
        } else {
            ip->x40_vel.x = speed;
            ip->x40_vel.y = 0.0f;
        }
        ip->x40_vel.z = 0.0f;
        it_80275158(item, attrs[2]);
        Item_80268E5C(item, 0, 2);
        db_80225DD8(item, gobj);
        it_802750F8(item);
    }
}

static void ftKbRc_Enter(HSD_GObj* gobj, int state)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->cmd_vars[0] = 0;
    fp->throw_flags_b0 = false;
    mu_ak_kirby_state_change(gobj, state, 0, 0, 1, 0);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftKbRc_Spawn;
}
static void ftKbRc_Ground(HSD_GObj* gobj) { ftKbRc_Enter(gobj, 0); }
static void ftKbRc_Air(HSD_GObj* gobj) { ftKbRc_Enter(gobj, 1); }
static void ftKbRc_GroundColl(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        Fighter* fp = GET_FIGHTER(gobj);
        ftCommon_8007D5D4(fp);
        mu_ak_kirby_state_change(gobj, 1, 0x0C4C5084, fp->cur_anim_frame, 1, 0);
    }
}
static void ftKbRc_AirAnim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) ftCo_Fall_Enter(gobj);
}
static void ftKbRc_AirPhys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ft_80084DB0(gobj);
    if (fp->cur_anim_frame == 40.0f) {
        fp->self_vel.x = (float) __builtin_fma(-(double) fp->facing_dir, 0.7,
                                             (double) fp->self_vel.x);
        fp->self_vel.y = 1.8f;
    }
}
static void ftKbRc_AirColl(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        Fighter* fp = GET_FIGHTER(gobj);
        ftCommon_8007D7FC(fp);
        mu_ak_kirby_state_change(gobj, 0, 0x0C4C5084, fp->cur_anim_frame, 1, 0);
    }
}
static const MotionState ftKbRc_States[] = {
    { 0, 0x00340211, 0x12000000, ftRc_SpecialN_Anim, NULL,
      ftRc_SpecialN_Phys, ftKbRc_GroundColl, ftCamera_UpdateCameraBox },
    { 1, 0x00340611, 0x12000000, ftKbRc_AirAnim, NULL,
      ftKbRc_AirPhys, ftKbRc_AirColl, ftCamera_UpdateCameraBox },
};
const MuAkKirbyCopy ftKbRc_Copy = {
    .on_swallow = mu_ak_kirby_attach_hat,
    .on_lose = ftKb_SpecialN_800EFAF0,
    .special_n = ftKbRc_Ground, .special_air_n = ftKbRc_Air,
    .move_logic = ftKbRc_States, .move_logic_count = 2,
};
