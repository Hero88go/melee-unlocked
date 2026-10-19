/* ACE PlLc2.dat ftFunction. Source offsets are in listing.txt. */
#include "lucas2.h"
#include <math.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcolanim.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_AttackHi4.h>
#include <melee/ft/kinds/ftCommon/ftCo_AttackLw4.h>
#include <melee/ft/kinds/ftCommon/ftCo_AttackS4.h>
#include <melee/ft/kinds/ftNess/ftness.h>
#include <melee/ft/kinds/ftNess/ftnessattackhi4.h>
#include <melee/ft/kinds/ftNess/ftnessattacklw4.h>
#include <melee/ft/kinds/ftNess/ftnessattacks4.h>
#include <melee/ft/kinds/ftNess/ftnessspecialn.h>
#include <melee/ft/kinds/ftNess/ftnessspecialhi.h>
#include <melee/it/it_26B1.h>
#include <sysdolphin/baselib/gobj.h>

/* +0000: custom articles 0..3 and 8, retail trail kinds 70..73. */
static void ftLc2_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    DISC_PTR(void)* articles = DP(fp->ft_data->x48_items);
    memcpy(fp->dat_attrs_backup, DP(fp->ft_data->ext_attr), 0xDC);
    fp->dat_attrs = fp->dat_attrs_backup;
    for (int i = 0; i < 4; i++) {
        mu_ak_register_article(fp->kind, DP(articles[i]), i);
    }
    for (int i = 0; i < 4; i++) {
        it_8026B3F8(DP(articles[i + 4]), (ItemKind) (0x46 + i));
    }
    mu_ak_register_article(fp->kind, DP(articles[8]), 8);
}

/* +00DC. The hitbox position and +223C words are deliberately untouched. */
static void ftLc2_OnDeath(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftParts_80074A4C(gobj, 0, 0);
    fp->u.ns.yoyo_gobj = NULL;
    fp->u.ns.pkflash_gobj = NULL;
    fp->u.ns.pkthunder_gobj = NULL;
    fp->u.ns.bat_gobj = NULL;
    fp->u.ns.pkthunder_gfx = 0;
    ftLc2_Vars(fp)->energy = 0.0f;
}

static void ftLc2_ReloadAttrs(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    memcpy(fp->dat_attrs, DP(fp->ft_data->ext_attr), 0xDC);
}

/* +0B88, +0C8C/+0CF8, +0D60/+0DB0. Slots 14/15 are absent. */
static void ftLc2_OnItemPickup(HSD_GObj* gobj, bool caught)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (itIsHeavy(fp->item_gobj)) return;
    switch (itGetHoldKind(fp->item_gobj)) {
    case 1: ftAnim_80070FB4(gobj, 1, 1); break;
    case 2: ftAnim_80070FB4(gobj, 1, 0); break;
    case 3: ftAnim_80070FB4(gobj, 1, 2); break;
    case 4: ftAnim_80070FB4(gobj, 1, 3); break;
    }
    if (caught) ftAnim_80070C48(gobj, 1);
}

static void ftLc2_OnItemDrop(HSD_GObj* gobj, bool dropped)
{
    ftAnim_80070FB4(gobj, 1, -1);
    if (dropped) ftAnim_80070CC4(gobj, 1);
}

static void ftLc2_OnKnockbackEnter(HSD_GObj* gobj)
{
    ftAnim_800704F0(gobj, 1, 3.0f);
    ftAnim_800704F0(gobj, 0, 3.0f);
}

static void ftLc2_OnKnockbackExit(HSD_GObj* gobj)
{
    ftAnim_800704F0(gobj, 1, 0.0f);
    ftAnim_800704F0(gobj, 0, 0.0f);
}

/* +451C: four explicit retail call targets, not local Lucas routines. */
void ftLc2_Init_OnDamage(HSD_GObj* gobj)
{
    ftNs_AttackHi4_YoyoItemDespawn(gobj);
    ftNs_SpecialN_ItemPKFlushSetNULL(gobj);
    ftNs_SpecialHi_TakeDamage(gobj);
    ftNs_AttackS4_ItemNessBatRemove(gobj);
}

/* +4384: +134 of each HitCapsule is reused as the value 200, once per hitbox. */
void ftLc2_DamageScale(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const HSD_GObj* marker = (HSD_GObj*) (uintptr_t) 200;
    for (int i = 0; i < 4; i++) {
        HitCapsule* hit = &fp->x914[i];
        if (hit->state == HitCapsule_Disabled) {
            hit->owner = NULL;
        } else if (hit->owner != marker) {
            double base = (double) (s32) hit->unk_count;
            double ratio = (double) ftLc2_Vars(fp)->energy / 100.0;
            s32 damage = (s32) __builtin_fma(ratio * base, 0.3, base);
            ftColl_8007ABD0(hit, (u32) damage, gobj);
            hit->owner = (HSD_GObj*) marker;
        }
    }
}

/* +0E00 sets fp+21D0, the hitlag callback, to +4384. */
static void ftLc2_OnFrame(HSD_GObj* gobj)
{
    GET_FIGHTER(gobj)->hitlag_cb = ftLc2_DamageScale;
}

/* +4464: the two thresholds select color animations; lower energy removes both. */
void ftLc2_ColorScale(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float energy = ftLc2_Vars(fp)->energy;
    if (energy >= 100.0f) {
        ftCo_800BFFD0(fp, 45, false);
    } else if (energy >= 50.0f) {
        ftCo_800BFFD0(fp, 51, false);
    } else {
        ftCo_800C0200(fp, 45);
        ftCo_800C0200(fp, 51);
    }
}

static const MotionState ftLc2_States[36] = {
#include "lucas2_states.inc"
};

extern const struct MuAkKirbyCopy ftKbLc2_Copy;

/* Compiled only in an explicit lucas2 WIP build. Validation is pending. */
const MuAkFighter mu_ak_lucas2 = {
    .name = "Lucas TDX", .file = "PlLc2.dat",
    .onload = ftLc2_OnLoad, .ondeath = ftLc2_OnDeath,
    .specialn = ftLc2_SpecialNStart_Enter,
    .specialairn = ftLc2_SpecialAirNStart_Enter,
    .specials = ftLc2_SpecialS_Enter, .specialairs = ftLc2_SpecialAirS_Enter,
    .specialhi = ftLc2_SpecialHiStart_Enter,
    .specialairhi = ftLc2_SpecialAirHiStart_Enter,
    .speciallw = ftLc2_SpecialLwStart_Enter,
    .specialairlw = ftLc2_SpecialAirLwStart_Enter,
    .onabsorb = ftLc2_AbsorbThink_DecideAction,
    .onitempickup = (MuAkEvent) ftLc2_OnItemPickup,
    .onitemcatch = (MuAkEvent) ftLc2_OnItemPickup,
    .onitemdrop = (MuAkEvent) ftLc2_OnItemDrop,
    .onunknownitemrelated = (MuAkEvent) ftLc2_OnItemDrop,
    .onhit = ftLc2_OnKnockbackEnter,
    .onunknowneyetexturerelated = ftLc2_OnKnockbackExit,
    .onframe = ftLc2_OnFrame, .onactionstatechange = ftLc2_ColorScale,
    .onrespawn = ftLc2_ReloadAttrs,
    .move_logic = ftLc2_States, .move_logic_count = 36,
    .articles = ftLc2_ArticleLogic, .article_count = 9,
    .kirby = &ftKbLc2_Copy,
    .flags = MU_AK_READY,
};
