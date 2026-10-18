/* ACE's Chun-Li: the neutral special (states 341 and 342) and the B + A shot (state 367)
 * (m-ex "ftFunction" of PlCn.dat).
 *
 * Both are a shot from the bone of part 86 when the animation's script raises the first throw
 * flag (fp+2210 bit 0x80): article 2 for the neutral special, article 3 for state 367 (entered
 * from the onframe event, chunli_attacks.c). The state change on landing or on leaving the
 * ground is a plain one (flags 0): the animation keeps its frame, nothing else is kept. */
#include "chunli.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/types.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/gobj.h>

/* +046C, [specialn]: the first throw flag cleared, state 341, its script run once, the shot
 * callback. */
void ftCn_SpecialN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->throw_flags_b0 = false;
    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialN, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftCn_SpecialN_OnAccessory4;
}

/* +04E4, [specialairn]: the same with state 342. */
void ftCn_SpecialAirN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->throw_flags_b0 = false;
    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialAirN, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftCn_SpecialN_OnAccessory4;
}

/* +17F4, the animation callback of 341 (and +2828 of 367, the same instructions): wait when the
 * animation is over. */
void ftCn_SpecialN_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

/* +1848, the collision callback of 341 (and +287C of 367, the same instructions): off the
 * ground, airborne and state 342 from the same frame. */
void ftCn_SpecialN_Coll(HSD_GObj* gobj)
{
    Fighter* fp;

    if (ft_80082708(gobj)) {
        return;
    }
    fp = GET_FIGHTER(gobj);
    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialAirN, Ft_MF_None, fp->cur_anim_frame, 1.0f,
                              0.0f, NULL);
}

/* +18C4, the animation callback of 342: fall when the animation is over. */
void ftCn_SpecialAirN_Anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

/* +1918, the collision callback of 342: landed, grounded and state 341 from the same frame. The
 * shot callback is put back only before frame 15 (the state change cleared it). */
void ftCn_SpecialAirN_Coll(HSD_GObj* gobj)
{
    Fighter* fp;

    if (!ft_80081D0C(gobj)) {
        return;
    }
    fp = GET_FIGHTER(gobj);
    ftCommon_8007D7FC(fp);
    Fighter_ChangeMotionState(gobj, ftCn_MS_SpecialN, Ft_MF_None, fp->cur_anim_frame, 1.0f, 0.0f,
                              NULL);
    if (fp->cur_anim_frame < 15.0f) {
        fp->accessory4_cb = ftCn_SpecialN_OnAccessory4;
    }
}

/* +2954, the accessory callback of 341 and 342: once the script raises the first throw flag,
 * clear it and fire article 2 from the bone of part 86, facing her way. */
void ftCn_SpecialN_OnAccessory4(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ItemKind kind;
    Vec3 pos;

    if (!fp->throw_flags_b0) {
        return;
    }
    fp->throw_flags_b0 = false;
    lb_8000B1CC(fp->parts[FTCN_SHOT_PART].joint, NULL, &pos);
    kind = mu_ak_article_kind(gobj, ftCn_Article_Shot);
    itCn_Shot_Spawn(gobj, &pos, kind, fp->facing_dir);
}

/* +2CBC, the accessory callback of 367: the same with article 3. */
void ftCn_SpecialNAlt_OnAccessory4(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ItemKind kind;
    Vec3 pos;

    if (!fp->throw_flags_b0) {
        return;
    }
    fp->throw_flags_b0 = false;
    lb_8000B1CC(fp->parts[FTCN_SHOT_PART].joint, NULL, &pos);
    kind = mu_ak_article_kind(gobj, ftCn_Article_ShotAlt);
    itCn_Shot_Spawn(gobj, &pos, kind, fp->facing_dir);
}
