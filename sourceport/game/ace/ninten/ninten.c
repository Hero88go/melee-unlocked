/* ACE's Ninten: load, respawn, the item and knockback events, the double jump (m-ex
 * "ftFunction" of PlNt.dat).
 *
 * PlNt.dat exports 23 ftFunction slots: 0 onload, 1 ondeath, 2 onunknown, 3 move_logic, 4 to
 * 11 the eight specials, 13 to 18 the six item events, 21 and 22 the two knockback events,
 * 25 onrespawn, 32 enterdoublejump and 35 onsmashf. MxDt holds no default for him at all
 * (run-source/rel09-ace-native/ninten/mxdt.txt): every slot he does not export is empty.
 *
 * The state table uses the disc's 28 rows and its checked callback mappings. */
#include "ninten.h"

#include <string.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <melee/ft/types.h>
#include <melee/it/inlines.h>
#include <melee/it/it_26B1.h>
#include <sysdolphin/baselib/gobj.h>

/* code+0x3500 "Ninten_RemoveAllArticles": the death and damage callback while an article is
 * out. Lucas's removes his PK Thunder as well; he has none. The first call is Lucas's routine
 * byte for byte (code+0x4304, Lucas_RemoveSpecialItemGOBJ). */
void ftNt_RemoveAllArticles(HSD_GObj* gobj)
{
    ftLc_RemovePKFreeze(gobj);
    ftNt_RemoveBat(gobj);
}

/* code+0x9DC "ResetAttributes" (slot 25): reload the special attributes from the file. */
static void ftNt_ResetAttributes(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    memcpy(fp->dat_attrs_backup, DP(fp->ft_data->ext_attr), sizeof(ftNintenAttributes));
}

/* code+0x0 "OnLoad" (slot 0). Lucas's without the rope, the draw link and the results
 * accessory: the attributes, three articles by index (m-ex MEX_IndexFighterItem), and the CPU
 * helper posing as Ness (kind 8). The helper is Lucas's: MexCPU_InitSpoofData (+0xBFC),
 * MexCPU_InitProc (+0x353C) and MexCPU_ProcSpoof (+0x4560) do what his do, MexCPU_Process
 * (+0x4A14) is his byte for byte, and the data word the custom branch needs
 * (.bss.mexcpu_data, +0x3534) is only ever read, so MexCPU_ProcCustom (+0x43A0) is dead. */
static void ftNt_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    DISC_PTR(void)* items;
    int i;

    ftNt_ResetAttributes(gobj);
    fp->dat_attrs = fp->dat_attrs_backup;

    items = DP(fp->ft_data->x48_items);
    for (i = 0; i < ftNt_Article_Count; i++) {
        mu_ak_register_article(fp->kind, DP(items[i]), i);
    }
    ftLc_InitCpuSpoof(gobj, Ft_Kind_Ness);
}

_Static_assert(Ft_Kind_Ness == 8, "the kind PlNt.dat passes at code+0x68");

/* code+0x90 "OnRespawn" (slot 1, the respawn setup). */
static void ftNt_OnRespawn(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNinten_FighterVars* vars = ftNt_Vars(fp);

    ftParts_80074A4C(gobj, 0, 0);
    vars->x222C = 0;
    vars->pkhypnosis_gobj = NULL;
    vars->bat_gobj = NULL;
}

/* code+0xD4 "OnDestroy" (slot 2): empty. */
static void ftNt_OnDestroy(HSD_GObj* gobj)
{
    (void) gobj;
}

/* code+0x71C "OnItemPickup" (slots 13 and, through +0x8F4, 17). The same calls as Lucas's
 * (static there). */
static void ftNt_OnItemPickup(HSD_GObj* gobj, bool catch_item)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (itIsHeavy(fp->item_gobj)) {
        return;
    }
    switch (itGetHoldKind(fp->item_gobj)) {
    case 1:
        ftAnim_80070FB4(gobj, 1, 1);
        break;
    case 2:
        ftAnim_80070FB4(gobj, 1, 0);
        break;
    case 3:
        ftAnim_80070FB4(gobj, 1, 2);
        break;
    case 4:
        ftAnim_80070FB4(gobj, 1, 3);
        break;
    default:
        break;
    }
    if (catch_item) {
        ftAnim_80070C48(gobj, 1);
    }
}

/* code+0x804 "OnSetItemInvisible" (slot 14) and +0x850 "OnSetItemVisible" (slot 15): one body
 * on the disc, Lucas's byte for byte. */
static void ftNt_OnItemInvisible(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!itIsHeavy(fp->item_gobj)) {
        ftAnim_80070CC4(gobj, 1);
    }
}

/* code+0x89C "OnItemRelease" (slots 16 and, through +0x914, 18): Lucas's byte for byte. */
static void ftNt_OnItemDrop(HSD_GObj* gobj, bool drop_item)
{
    ftAnim_80070FB4(gobj, 1, -1);
    if (drop_item) {
        ftAnim_80070CC4(gobj, 1);
    }
}

/* code+0x934 "EyeTextureDamaged" (slot 21): Lucas's byte for byte. */
static void ftNt_OnKnockbackEnter(HSD_GObj* gobj)
{
    ftAnim_800704F0(gobj, 0, 3.0f);
    ftAnim_800704F0(gobj, 1, 3.0f);
}

/* code+0x988 "EyeTextureNormal" (slot 22): Lucas's byte for byte. */
static void ftNt_OnKnockbackExit(HSD_GObj* gobj)
{
    ftAnim_800704F0(gobj, 0, 0.0f);
    ftAnim_800704F0(gobj, 1, 0.0f);
}

/* code+0xA10 "EnterDoubleJump" (slot 32): Lucas's byte for byte, which is Ness's double jump
 * with the drift seed truncated to an int and stored as a bit pattern. */
static void ftNt_EnterDoubleJump(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftNs_JumpAerial_Enter(gobj);
    {
        s32 truncated = (s32) (fp->input.lstick[0].x * fp->co_attrs.air_jump_h_multiplier);

        memcpy(&fp->mv.co.jumpaerial.init_h_vel, &truncated, sizeof(truncated));
    }
}

/* PlNt.dat move_logic at +00D8: first 28 Lucas rows, disc callback mappings. */
static MotionState ftNt_MotionStates[ftNt_MS_SelfCount] = {
    /* 341 AttackS4 */
    { 62, 0x00241A09, FtMoveId_AttackS4 << 24, ftNt_AttackS4_Anim, ftNt_AttackS4_IASA,
      ftLc_AttackS4_Phys, ftLc_AttackS4_Coll, ftCamera_UpdateCameraBox },
    /* 342 SpecialNStart */
    { 295, 0x00340111, FtMoveId_SpecialN << 24, ftNt_SpecialNStart_Anim, ftLc_SpecialNStart_IASA,
      ftLc_SpecialNStart_Phys, ftLc_SpecialNStart_Coll, ftCamera_UpdateCameraBox },
    /* 343 SpecialNHold */
    { 296, 0x00340111, FtMoveId_SpecialN << 24, ftLc_SpecialNHold_Anim, ftLc_SpecialNHold_IASA,
      ftLc_SpecialNHold_Phys, ftLc_SpecialNHold_Coll, ftCamera_UpdateCameraBox },
    /* 344 SpecialNEnd */
    { 298, 0x00340111, FtMoveId_SpecialN << 24, ftLc_SpecialNEnd_Anim, NULL,
      ftLc_SpecialNEnd_Phys, NULL, ftCamera_UpdateCameraBox },
    /* 345 SpecialAirNStart */
    { 299, 0x00340511, FtMoveId_SpecialN << 24, ftNt_SpecialAirNStart_Anim,
      ftLc_SpecialAirNStart_IASA, ftLc_SpecialAirN_Phys, ftLc_SpecialAirNStart_Coll,
      ftCamera_UpdateCameraBox },
    /* 346 SpecialAirNHold */
    { 300, 0x00340511, FtMoveId_SpecialN << 24, ftLc_SpecialAirNHold_Anim,
      ftLc_SpecialNHold_IASA, ftLc_SpecialAirN_Phys, ftLc_SpecialAirNHold_Coll,
      ftCamera_UpdateCameraBox },
    /* 347 SpecialAirNEnd */
    { 302, 0x00340511, FtMoveId_SpecialN << 24, ftLc_SpecialAirNEnd_Anim, NULL,
      ftLc_SpecialAirN_Phys, ftLc_SpecialAirNEnd_Coll, ftCamera_UpdateCameraBox },
    /* 348 SpecialS */
    { 303, 0x00340112, FtMoveId_SpecialS << 24, ftLc_SpecialS_Anim, NULL, ftLc_SpecialS_Phys,
      ftLc_SpecialS_Coll, ftCamera_UpdateCameraBox },
    /* 349 SpecialAirS */
    { 304, 0x00340512, FtMoveId_SpecialS << 24, ftLc_SpecialAirS_Anim, NULL,
      ftLc_SpecialAirS_Phys, ftLc_SpecialAirS_Coll, ftCamera_UpdateCameraBox },
    /* 350 SpecialHiStart */
    { 305, 0x00340113, FtMoveId_SpecialHi << 24, ftNt_SpecialHiStart_Anim, ftNt_NoCallback,
      ftLc_SpecialHiStart_Phys, ftNt_SpecialHiStart_Coll, ftCamera_UpdateCameraBox },
    /* 351 SpecialHiHold */
    { 306, 0x00340113, FtMoveId_SpecialHi << 24, ftNt_SpecialHiHold_Anim, ftNt_SpecialHiHold_IASA,
      ftNt_NoCallback, ftNt_SpecialHiHold_Coll, ftCamera_UpdateCameraBox },
    /* 352 SpecialHiEnd */
    { 307, 0x00340113, FtMoveId_SpecialHi << 24, ftLc_SpecialHiEnd_Anim, ftNt_NoCallback,
      ftNt_SpecialHiEnd_Phys, ftNt_SpecialHiEnd_Coll, ftCamera_UpdateCameraBox },
    /* 353 SpecialHi (PK Thunder 2, grounded) */
    { 308, 0x00340113, FtMoveId_SpecialHi << 24, ftNt_SpecialHi_Anim, ftNt_NoCallback,
      ftNt_SpecialHi_Phys, ftNt_SpecialHi_Coll, ftCamera_UpdateCameraBox },
    /* 354 SpecialAirHiStart */
    { 309, 0x00340513, FtMoveId_SpecialHi << 24, ftNt_SpecialAirHiStart_Anim,
      ftNt_NoCallback, ftLc_SpecialAirHiStart_Phys, ftNt_SpecialAirHiStart_Coll,
      ftCamera_UpdateCameraBox },
    /* 355 SpecialAirHiHold */
    { 310, 0x00340513, FtMoveId_SpecialHi << 24, ftNt_SpecialAirHiHold_Anim,
      ftNt_SpecialAirHiHold_IASA, ftLc_SpecialAirHiStart_Phys, ftNt_SpecialAirHiHold_Coll,
      ftCamera_UpdateCameraBox },
    /* 356 SpecialAirHiEnd */
    { 311, 0x00340513, FtMoveId_SpecialHi << 24, ftNt_SpecialAirHiEnd_Anim,
      ftNt_NoCallback, ftNt_SpecialAirHiEnd_Phys, ftNt_SpecialAirHiEnd_Coll,
      ftCamera_UpdateCameraBox },
    /* 357 SpecialAirHi (PK Thunder 2, aerial) */
    { 312, 0x00340513, FtMoveId_SpecialHi << 24, ftNt_SpecialAirHi_Anim, ftNt_NoCallback,
      ftNt_SpecialAirHi_Phys, ftNt_SpecialAirHi_Coll, ftCamera_UpdateCameraBox },
    /* 358 SpecialHiBound */
    { 313, 0x00340113, FtMoveId_SpecialHi << 24, ftNt_SpecialHiBound_Anim,
      ftNt_NoCallback, ftLc_SpecialHiBound_Phys, ftNt_SpecialHiBound_Coll,
      ftCamera_UpdateCameraBox },
    /* 359 SpecialLwStart */
    { 314, 0x00340014, FtMoveId_SpecialLw << 24, ftNt_SpecialLwStart_Anim,
      ftNt_SpecialLwStart_IASA, ftLc_SpecialLwStart_Phys, ftNt_SpecialLwStart_Coll,
      ftCamera_UpdateCameraBox },
    /* 360 SpecialLwHold */
    { 315, 0x003C0014, FtMoveId_SpecialLw << 24, ftNt_SpecialLwHold_Anim, ftNt_NoCallback,
      ftNt_SpecialLwHold_Phys, ftNt_SpecialLwHold_Coll, ftCamera_UpdateCameraBox },
    /* 361 SpecialLwHit */
    { 316, 0x00340014, FtMoveId_SpecialLw << 24, ftNt_SpecialLwHit_Anim, ftNt_NoCallback,
      ftNt_SpecialLwHold_Phys, ftNt_SpecialLwHit_Coll, ftCamera_UpdateCameraBox },
    /* 362 SpecialLwEnd */
    { 317, 0x00340014, FtMoveId_SpecialLw << 24, ftLc_SpecialLwEnd_Anim, ftNt_NoCallback,
      ftLc_SpecialLwEnd_Phys, ftNt_SpecialLwEnd_Coll, ftCamera_UpdateCameraBox },
    /* 363 SpecialLwTurn */
    { 315, 0x00340014, FtMoveId_SpecialLw << 24, ftNt_SpecialLwTurn_Anim, ftNt_NoCallback,
      ftNt_SpecialLwHold_Phys, ftNt_SpecialLwTurn_Coll, ftCamera_UpdateCameraBox },
    /* 364 SpecialAirLwStart */
    { 318, 0x00340414, FtMoveId_SpecialLw << 24, ftNt_SpecialLwStart_Anim,
      ftNt_SpecialLwStart_IASA, ftNt_SpecialAirLwStart_Phys, ftNt_SpecialAirLwStart_Coll,
      ftCamera_UpdateCameraBox },
    /* 365 SpecialAirLwHold */
    { 319, 0x003C0414, FtMoveId_SpecialLw << 24, ftNt_SpecialLwHold_Anim, ftNt_NoCallback,
      ftNt_SpecialAirLwHold_Phys, ftNt_SpecialAirLwHold_Coll, ftCamera_UpdateCameraBox },
    /* 366 SpecialAirLwHit */
    { 320, 0x00340414, FtMoveId_SpecialLw << 24, ftNt_SpecialLwHit_Anim, ftNt_NoCallback,
      ftNt_SpecialAirLwHold_Phys, ftNt_SpecialAirLwHit_Coll, ftCamera_UpdateCameraBox },
    /* 367 SpecialAirLwEnd */
    { 321, 0x00340414, FtMoveId_SpecialLw << 24, ftLc_SpecialLwEnd_Anim, ftNt_NoCallback,
      ftNt_SpecialAirLwStart_Phys, ftNt_SpecialAirLwEnd_Coll, ftCamera_UpdateCameraBox },
    /* 368 SpecialAirLwTurn */
    { 319, 0x00340414, FtMoveId_SpecialLw << 24, ftNt_SpecialLwTurn_Anim, ftNt_NoCallback,
      ftNt_SpecialAirLwHold_Phys, ftNt_SpecialAirLwTurn_Coll, ftCamera_UpdateCameraBox },
};

const MuAkFighter mu_ak_ninten = {
    .name = "Ninten",
    .file = "PlNt.dat",
    .onload = ftNt_OnLoad,
    .ondeath = ftNt_OnRespawn,
    .onunknown = ftNt_OnDestroy,
    .specialn = ftLc_SpecialN_Enter,
    .specialairn = ftLc_SpecialAirN_Enter,
    .specials = ftNt_SpecialS_Enter,
    .specialairs = ftNt_SpecialAirS_Enter,
    .specialhi = ftNt_SpecialHi_Enter,
    .specialairhi = ftNt_SpecialAirHi_Enter,
    .speciallw = ftNt_SpecialLw_Enter,
    .specialairlw = ftNt_SpecialAirLw_Enter,
    .onitempickup = (MuAkEvent) ftNt_OnItemPickup,
    .onmakeiteminvisible = ftNt_OnItemInvisible,
    .onmakeitemvisible = ftNt_OnItemInvisible,
    .onitemdrop = (MuAkEvent) ftNt_OnItemDrop,
    .onitemcatch = (MuAkEvent) ftNt_OnItemPickup,
    .onunknownitemrelated = (MuAkEvent) ftNt_OnItemDrop,
    .onhit = ftNt_OnKnockbackEnter,
    .onunknowneyetexturerelated = ftNt_OnKnockbackExit,
    .onrespawn = ftNt_ResetAttributes,
    .enterdoublejump = ftNt_EnterDoubleJump,
    .onsmashf = ftNt_AttackS4_Enter,
    .move_logic = ftNt_MotionStates,
    .move_logic_count = ftNt_MS_SelfCount,
    .article_tables = itNt_ArticleTables,
    .article_count = ftNt_Article_Count,
};
