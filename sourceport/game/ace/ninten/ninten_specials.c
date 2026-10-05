/* ACE's Ninten: side special, the slingshot (states 348 and 349). Lucas's PK Fire with one
 * change: the shot does not push him back (Lucas's accessory callback sets his speed to
 * -1.5 x facing first). The six state callbacks are Lucas's functions: five byte for byte
 * (code+0x137C, +0x13BC, +0x13DC, +0x141C, +0x145C) and the aerial collision (+0x147C) the
 * same calls with other registers. */
#include "ninten.h"

#include <dolphin/os.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/inlines.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/gobj.h>

/* code+0x4238 "SpecialS_Accessory4_Callback": on the frame the script raises the throw flag,
 * make the pellet in front of his hand. The offsets and the speed are attributes +48, +4C and
 * +44, Lucas's layout. The spawn (code+0x48E4 "Spawn_Slingshot") builds the same request and
 * makes the same calls as Lucas's Spawn_PKFire (article 1 by index, the position from
 * ftLib_80086990, state 0, the lifetime of the article, the owner twice, the model mirrored
 * by the facing), so Lucas's function is called. */
static void ftNt_SpecialS_Accessory(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs;
    Item_GObj* pellet_gobj;
    Vec3 pos;
    Vec3 vel;

    if (!fp->throw_flags_b0) {
        return;
    }
    attrs = ftLc_Attrs(fp);
    fp->throw_flags_b0 = false;

    lb_8000B1CC(fp->parts[ftNt_Part_Hand].joint, NULL, &pos);
    pos.x += fp->facing_dir * attrs->x48_PKFIRE_SPAWN_X;
    pos.y += attrs->x4C_PKFIRE_SPAWN_Y;
    pos.z = 0.0f;
    vel.x = attrs->x44_PKFIRE_VEL_X * fp->facing_dir;
    vel.y = 0.0f;
    vel.z = 0.0f;
    pellet_gobj = ftLc_PKFire_Spawn(gobj, &pos, &vel, fp->facing_dir);

    {
        /* Bring-up evidence (the first ones only). */
        static int logged;
        if (logged < 4) {
            logged++;
            OSReport("[ak] Ninten pellet: item kind %d, %s\n",
                     (int) mu_ak_article_kind(gobj, ftNt_Article_Pellet),
                     pellet_gobj != NULL ? "created" : "not created");
        }
    }
}

/* code+0x3238 "SpecialS_Enter" */
static void ftNt_SpecialS_EnterShared(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->throw_flags = 0;
    fp->cmd_vars[0] = 0;
    Fighter_ChangeMotionState(gobj, msid, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftNt_SpecialS_Accessory;
}

/* code+0x52C "SpecialS" (slot 6) */
void ftNt_SpecialS_Enter(HSD_GObj* gobj)
{
    ftNt_SpecialS_EnterShared(gobj, ftLc_MS_SpecialS);
}

/* code+0x550 "SpecialAirS" (slot 7) */
void ftNt_SpecialAirS_Enter(HSD_GObj* gobj)
{
    ftNt_SpecialS_EnterShared(gobj, ftLc_MS_SpecialAirS);
}
