/* Akaneia's Diddy Kong, Special Lw: Banana Peel.
 *
 * The script raises cmd_vars[0] to pull a peel into Diddy's hand and cmd_vars[1] to toss it
 * back over his head. Only one peel may be on stage (ftDd_FighterVars.banana, cleared by the
 * peel when it is destroyed) and none is pulled while Diddy holds an item. Being hit or dying
 * before the toss destroys the peel in hand. The peel's own behavior is in itdiddy.c. */
#include "ftdiddy.h"

#include <math.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftpickupitem.h>
#include <melee/it/it_2725.h>
#include <melee/it/item.h>
#include <melee/lb/lb_00B0.h>
#include <melee/pl/plbonuslib.h>

/* Diddy's "pull a peel" sound. */
#define ftDd_SFX_BananaPull 0x13A1

/* Banana_OnHit: take-damage / death callback while the peel is in hand. */
static void ftDd_SpecialLw_BananaOnHit(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftDd_MV(fp)->speciallw.banana != NULL) {
        Item_8026A8EC(ftDd_MV(fp)->speciallw.banana);
        ftDd_MV(fp)->speciallw.banana = NULL;
    }
    fp->take_dmg_cb = NULL;
    fp->death2_cb = NULL;
}

/* Banana_Spawn */
static void ftDd_SpecialLw_BananaSpawn(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Item_GObj* banana;

    if (ftDd_FV(fp)->banana != NULL || fp->item_gobj != NULL) {
        return;
    }
    banana = ftDd_SpawnArticle(gobj, ftDd_Article_Banana, 7, &fp->cur_pos, GA_Ground);
    if (banana == NULL) {
        return;
    }
    ftDd_BananaVarsOf(GET_ITEM(banana))->thrower = gobj;
    ftDd_MV(fp)->speciallw.banana = banana;
    ftpickupitem_800948A8(gobj, banana);
    ft_800881D8(fp, ftDd_SFX_BananaPull, 0x7F, 0x40);
    Fighter_SetDamageCallbacks(fp, ftDd_SpecialLw_BananaOnHit, ftDd_SpecialLw_BananaOnHit);
}

/* Banana_Release: toss the held peel (only if what Diddy holds is his banana article). */
static void ftDd_SpecialLw_BananaRelease(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDd_DatAttrs* da = fp->dat_attrs_backup;
    Item_GObj* banana = fp->item_gobj;
    Vec3 pos;
    Vec3 vel;

    if (banana == NULL) {
        return;
    }
    if (GET_ITEM(banana)->kind != mu_ak_article_kind(gobj, ftDd_Article_Banana)) {
        return;
    }
    /* The console calls fn_8003E998 directly (pl_8003E978 without the wrapper). */
    fn_8003E998(fp->player_idx, fp->is_sub_fighter);
    lb_8000B1CC(it_80272C90(banana), NULL, &pos);
    vel.x = cosf(da->speciallw_throw_angle) * da->speciallw_throw_speed * fp->facing_dir;
    vel.y = da->speciallw_throw_speed * sinf(da->speciallw_throw_angle);
    vel.z = 0.0F;
    Item_8026AC74(banana, &pos, &vel, 0.5F);
    itDdBanana_SpawnThrown_Enter(banana);
    if (ftDd_MV(fp)->speciallw.banana != NULL) {
        ftDd_MV(fp)->speciallw.banana = NULL;
        ftDd_FV(fp)->banana = banana;
    }
}

static void ftDd_SpecialLw_Init(HSD_GObj* gobj, FtMotionId msid)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, msid, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    ftAnim_8006EBA4(gobj);
    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    ftDd_MV(fp)->speciallw.banana = NULL;
}

/* SpecialLw_Enter */
void ftDd_SpecialLw_Enter(HSD_GObj* gobj)
{
    ftDd_SpecialLw_Init(gobj, ftDd_MS_SpecialLw);
}

/* SpecialAirLw_Enter */
void ftDd_SpecialAirLw_Enter(HSD_GObj* gobj)
{
    ftDd_SpecialLw_Init(gobj, ftDd_MS_SpecialAirLw);
}

/* The script cues, shared by both versions. */
static void ftDd_SpecialLw_Script(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[0] == 1) {
        fp->cmd_vars[0] = 0;
        ftDd_SpecialLw_BananaSpawn(gobj);
    }
    if (fp->cmd_vars[1] == 1) {
        fp->cmd_vars[1] = 0;
        ftDd_SpecialLw_BananaRelease(gobj);
    }
}

void ftDd_SpecialLw_Anim(HSD_GObj* gobj)
{
    ftDd_SpecialLw_Script(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftDd_SpecialLw_IASA(HSD_GObj* gobj) {}

void ftDd_SpecialLw_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

void ftDd_SpecialLw_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!ft_80082708(gobj)) {
        Fighter_ChangeMotionState(gobj, ftDd_MS_SpecialAirLw, ftDd_MF_Swap, fp->cur_anim_frame,
                                  fp->frame_speed_mul, fp->x8A4_animBlendFrames, NULL);
        ftCommon_8007D5D4(fp);
    }
}

void ftDd_SpecialAirLw_Anim(HSD_GObj* gobj)
{
    ftDd_SpecialLw_Script(gobj);
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Fall_Enter(gobj);
    }
}

void ftDd_SpecialAirLw_IASA(HSD_GObj* gobj) {}

void ftDd_SpecialAirLw_Phys(HSD_GObj* gobj)
{
    ft_80084EEC(gobj);
}

void ftDd_SpecialAirLw_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_80081D0C(gobj)) {
        Fighter_ChangeMotionState(gobj, ftDd_MS_SpecialLw, ftDd_MF_Swap, fp->cur_anim_frame,
                                  fp->frame_speed_mul, fp->x8A4_animBlendFrames, NULL);
        ftCommon_8007D6A4(fp);
    }
}
