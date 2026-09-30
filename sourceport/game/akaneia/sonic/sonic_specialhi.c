/* Akaneia's Sonic: up special, the spring jump.
 *
 * A spring article appears under him (replacing any earlier one) and he is launched straight up
 * with his jumps used up, with light drift and one turn-around on the first frame. The spring
 * stays behind for anyone to bounce on (sonic_spring.c).
 * Hand-written from PlSn.dat's ftFunction code; see NOTES.md. */
#include "sonic.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/types.h>
#include <melee/it/inlines.h>
#include <melee/it/item.h>
#include <melee/it/types.h>

/* Spawn_Spring */
void ftSn_SpawnSpring(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_FighterVars* fv = ftSn_FV(fp);
    SpawnItem spawn = { 0 };
    Item_GObj* spring;

    if (fv->spring_gobj != NULL) {
        Item_8026A8EC(fv->spring_gobj);
    }
    if (mu_ak_sonic_hooks.fighter_item_kind == NULL) {
        return;
    }

    spawn.x0_parent_gobj = gobj;
    spawn.x4_parent_gobj2 = gobj;
    spawn.kind = mu_ak_sonic_hooks.fighter_item_kind(gobj, 0);
    spawn.hold_kind = 8;
    spawn.x10 = 0;
    spawn.pos = fp->cur_pos;
    spawn.prev_pos = fp->cur_pos;
    spawn.vel.x = 0.0F;
    spawn.vel.y = 0.0F;
    spawn.vel.z = 0.0F;
    spawn.facing_dir = fp->facing_dir;
    spawn.x3C_damage = 0;
    spawn.x3E = 0;
    spawn.x40 = 0;
    /* m-ex sets only this bit and leaves the rest of x44..x47 as whatever was on the stack;
     * here they start at zero. */
    spawn.x44_flag.b0 = true;
    spawn.x48_ground_or_air = fp->ground_or_air;

    /* m-ex calls the item creator (Item_8026862C, static in the decomp) directly with the fields
     * above. The public wrappers do the same after forcing x48 and recomputing hold_kind, which
     * gives 8 again for a fighter article; pick the wrapper that keeps x48. */
    if (spawn.x48_ground_or_air == GA_Ground) {
        spring = Item_80268B5C(&spawn);
    } else {
        spring = Item_80268B18(&spawn);
    }

    if (spring != NULL) {
        Item* ip = GET_ITEM(spring);
        ftSn_SpringVars(ip)->owner = gobj;
        fv->spring_gobj = spring;
        /* stand the spring on the ground line Sonic stands on */
        ip->x378_itemColl.floor.index = fp->coll_data.floor.index;
    }
}

/* SpecialHi_Enter: specialhi and specialairhi */
void ftSn_SpecialHi_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);

    ftSn_SpawnSpring(gobj);
    if (fp->ground_or_air == GA_Ground) {
        if (ftSn_FV(fp)->spring_gobj != NULL) {
            /* start on top of the spring */
            fp->cur_pos.y = (float) (fp->cur_pos.y + 3.3);
        }
        fp->self_vel.y = da->specialhi_ground_vel_y;
    }
    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, ftSn_MS_SpecialHi, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    fp->x1968_jumpsUsed = fp->co_attrs.max_jumps;
    fp->self_vel.x = 0.0F;
    fp->self_vel.y = da->specialhi_vel_y;
    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
}

void ftSn_SpecialHi_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    if (!(da->specialhi_landing_lag <= 0.0F)) {
        ftCo_80096900(gobj, 0, 1, false, da->specialhi_fall_mobility, da->specialhi_landing_lag);
    } else {
        ftCo_Fall_Enter(gobj);
    }
}

/* One chance, on the first interruptible frame, to turn around with the stick. */
void ftSn_SpecialHi_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float stick_x;

    if (fp->cmd_vars[0] != 0) {
        return;
    }
    fp->cmd_vars[0] = 1;

    stick_x = fp->input.lstick[0].x;
    if (stick_x < 0.0F) {
        stick_x = -stick_x;
    }
    if (ftSn_DA(fp)->specialhi_turn_threshold < stick_x) {
        ftCommon_UpdateFacing(fp);
        ftPartSetRotY(fp, 0, (float) (fp->facing_dir * 1.5707963267948966));
    }
}

void ftSn_SpecialHi_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSonic_DatAttrs* da = ftSn_DA(fp);

    ftCommon_CalcSelfAccel_DriftSimple(fp, 0.0F, da->specialhi_drift_accel,
                                       da->specialhi_drift_max);
    ftCommon_Fall(fp, da->specialhi_gravity, da->specialhi_terminal_vel);
}

/* cmd_vars[1] (set by the animation script) opens ledge grabbing; then the normal air
 * collision runs as well, and landing goes to special landing. */
void ftSn_SpecialHi_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[1] == 1) {
        if (!ft_CheckGroundAndLedge(gobj, (int) fp->facing_dir)) {
            ftCliffCommon_80081298(gobj);
        }
    }
    if (ft_80081D0C(gobj)) {
        ftCo_LandingFallSpecial_Enter(gobj, false, ftSn_DA(fp)->specialhi_landing_lag);
    }
}
