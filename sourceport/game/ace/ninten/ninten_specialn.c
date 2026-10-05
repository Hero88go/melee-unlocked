/* ACE's Ninten: neutral special, PK Hypnosis (states 342 to 347). The code is Lucas's PK
 * Freeze: every routine of the six states is Lucas's byte for byte or the same calls with
 * other registers (lucas_identity.txt, lucas_diff.txt), and so is the article (item block 0).
 * What is his own is one address: the death and damage callback installed when the
 * projectile is made removes his articles (code+0x3500), where Lucas's removes Lucas's. So
 * the two start callbacks are ported here and every other callback of the states is the
 * function of akaneia/lucas/lucas_specialn.c. */
#include "ninten.h"

#include <dolphin/os.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/inlines.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/gobj.h>

/* The bone above which the projectile appears (code+0x36DC: fp->parts + 0x180), Lucas's. */
#define ftNt_Part_Head ftLc_Part_Head

/* code+0x3644 "SpecialN_Start_AnimCB_Shared": at the end of the start animation, go to the
 * hold state and make the projectile above his head (3.0 up, scaled). The spawn (code+0x45E8
 * "ItemSpawn_PKHypnosis", with code+0x4AC0 "Init_PKHypnosis") is Lucas's ItemSpawn_PKFreeze
 * byte for byte. */
static void ftNt_SpecialNStart_AnimShared(HSD_GObj* gobj, FtMotionId next)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNinten_FighterVars* vars = ftNt_Vars(fp);

    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    Fighter_ChangeMotionState(gobj, next, 0, 0.0f, 1.0f, 0.0f, NULL);
    if (vars->pkhypnosis_gobj == NULL) {
        Vec3 pos;

        lb_8000B1CC(fp->parts[ftNt_Part_Head].joint, NULL, &pos);
        pos.y += fp->x34_scale.y * 3.0f;
        pos.z = 0.0f;
        vars->pkhypnosis_gobj = ftLc_PKFreeze_Spawn(
            gobj, &pos, mu_ak_article_kind(gobj, ftNt_Article_PKHypnosis), fp->facing_dir);
        if (vars->pkhypnosis_gobj != NULL) {
            fp->death2_cb = ftNt_RemoveAllArticles;
            fp->take_dmg_cb = ftNt_RemoveAllArticles;
        }

        {
            /* Bring-up evidence (the first ones only). */
            static int logged;
            if (logged < 4) {
                logged++;
                OSReport("[ak] Ninten PK Hypnosis: item kind %d, %s\n",
                         (int) mu_ak_article_kind(gobj, ftNt_Article_PKHypnosis),
                         vars->pkhypnosis_gobj != NULL ? "created" : "not created");
            }
        }
    }
    /* No double jump after the move. */
    fp->x1968_jumpsUsed = fp->co_attrs.max_jumps;
}

/* code+0xDA0 "SpecialN_Start_AnimCB" */
void ftNt_SpecialNStart_Anim(HSD_GObj* gobj)
{
    ftNt_SpecialNStart_AnimShared(gobj, ftLc_MS_SpecialNHold);
}

/* code+0x1008 "SpecialAirN_Start_AnimCB" */
void ftNt_SpecialAirNStart_Anim(HSD_GObj* gobj)
{
    ftNt_SpecialNStart_AnimShared(gobj, ftLc_MS_SpecialAirNHold);
}
