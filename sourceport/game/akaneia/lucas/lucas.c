/* Akaneia's Lucas: the fighter table, load/respawn, item and knockback hooks, double jump, tether
 * entry, forward smash entry, intro/taunt/grab snake, and the custom GX link that draws the rope.
 * Source: PlLc.dat "ftFunction" (m-ex), offsets in the comments are into that code blob. */
#include "lucas.h"

#include <string.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftdrawcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/ft_0877.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <melee/it/it_26B1.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/memory.h>

/* ---------------------------------------------------------------------------------------------
 * move_logic (console code+0x1E0, 30 entries of 0x20 bytes). The flag words are the disc's own.
 * ------------------------------------------------------------------------------------------- */
static MotionState ftLc_MotionStates[ftLc_MS_SelfCount] = {
    /* 341 AttackS4 */
    { 62, 0x00241A09, FtMoveId_AttackS4 << 24, ftLc_AttackS4_Anim, ftLc_AttackS4_IASA,
      ftLc_AttackS4_Phys, ftLc_AttackS4_Coll, ftCamera_UpdateCameraBox },
    /* 342 SpecialNStart */
    { 295, 0x00340111, FtMoveId_SpecialN << 24, ftLc_SpecialNStart_Anim, ftLc_SpecialNStart_IASA,
      ftLc_SpecialNStart_Phys, ftLc_SpecialNStart_Coll, ftCamera_UpdateCameraBox },
    /* 343 SpecialNHold */
    { 296, 0x00340111, FtMoveId_SpecialN << 24, ftLc_SpecialNHold_Anim, ftLc_SpecialNHold_IASA,
      ftLc_SpecialNHold_Phys, ftLc_SpecialNHold_Coll, ftCamera_UpdateCameraBox },
    /* 344 SpecialNEnd */
    { 298, 0x00340111, FtMoveId_SpecialN << 24, ftLc_SpecialNEnd_Anim, NULL,
      ftLc_SpecialNEnd_Phys, NULL, ftCamera_UpdateCameraBox },
    /* 345 SpecialAirNStart */
    { 299, 0x00340511, FtMoveId_SpecialN << 24, ftLc_SpecialAirNStart_Anim,
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
    { 305, 0x00340113, FtMoveId_SpecialHi << 24, ftLc_SpecialHiStart_Anim, ftLc_SpecialHi_NoIASA,
      ftLc_SpecialHiStart_Phys, ftLc_SpecialHiStart_Coll, ftCamera_UpdateCameraBox },
    /* 351 SpecialHiHold */
    { 306, 0x00340113, FtMoveId_SpecialHi << 24, ftLc_SpecialHiHold_Anim, ftLc_SpecialHi_NoIASA,
      ftLc_SpecialHiHold_Phys, ftLc_SpecialHiHold_Coll, ftCamera_UpdateCameraBox },
    /* 352 SpecialHiEnd */
    { 307, 0x00340113, FtMoveId_SpecialHi << 24, ftLc_SpecialHiEnd_Anim, ftLc_SpecialHi_NoIASA,
      ftLc_SpecialHiEnd_Phys, ftLc_SpecialHiEnd_Coll, ftCamera_UpdateCameraBox },
    /* 353 SpecialHi (PK Thunder 2, grounded) */
    { 308, 0x00340113, FtMoveId_SpecialHi << 24, ftLc_SpecialHi_Anim, ftLc_SpecialHi_NoIASA,
      ftLc_SpecialHi_Phys, ftLc_SpecialHi_Coll, ftCamera_UpdateCameraBox },
    /* 354 SpecialAirHiStart */
    { 309, 0x00340513, FtMoveId_SpecialHi << 24, ftLc_SpecialAirHiStart_Anim,
      ftLc_SpecialHi_NoIASA, ftLc_SpecialAirHiStart_Phys, ftLc_SpecialAirHiStart_Coll,
      ftCamera_UpdateCameraBox },
    /* 355 SpecialAirHiHold */
    { 310, 0x00340513, FtMoveId_SpecialHi << 24, ftLc_SpecialAirHiHold_Anim,
      ftLc_SpecialHi_NoIASA, ftLc_SpecialAirHiStart_Phys, ftLc_SpecialAirHiHold_Coll,
      ftCamera_UpdateCameraBox },
    /* 356 SpecialAirHiEnd */
    { 311, 0x00340513, FtMoveId_SpecialHi << 24, ftLc_SpecialAirHiEnd_Anim,
      ftLc_SpecialHi_NoIASA, ftLc_SpecialAirHiStart_Phys, ftLc_SpecialAirHiEnd_Coll,
      ftCamera_UpdateCameraBox },
    /* 357 SpecialAirHi (PK Thunder 2, aerial) */
    { 312, 0x00340513, FtMoveId_SpecialHi << 24, ftLc_SpecialAirHi_Anim, ftLc_SpecialHi_NoIASA,
      ftLc_SpecialAirHi_Phys, ftLc_SpecialAirHi_Coll, ftCamera_UpdateCameraBox },
    /* 358 SpecialHiBound */
    { 313, 0x00340113, FtMoveId_SpecialHi << 24, ftLc_SpecialHiBound_Anim,
      ftLc_SpecialHi_NoIASA, ftLc_SpecialHiBound_Phys, ftLc_SpecialHiBound_Coll,
      ftCamera_UpdateCameraBox },
    /* 359 SpecialLwStart */
    { 314, 0x00340014, FtMoveId_SpecialLw << 24, ftLc_SpecialLwStart_Anim,
      ftLc_SpecialLwStart_IASA, ftLc_SpecialLwStart_Phys, ftLc_SpecialLwStart_Coll,
      ftCamera_UpdateCameraBox },
    /* 360 SpecialLwHold */
    { 315, 0x003C0014, FtMoveId_SpecialLw << 24, ftLc_SpecialLwHold_Anim, ftLc_SpecialLw_NoIASA,
      ftLc_SpecialLwHold_Phys, ftLc_SpecialLwHold_Coll, ftCamera_UpdateCameraBox },
    /* 361 SpecialLwHit */
    { 316, 0x00340014, FtMoveId_SpecialLw << 24, ftLc_SpecialLwHit_Anim, ftLc_SpecialLw_NoIASA,
      ftLc_SpecialLwHold_Phys, ftLc_SpecialLwHit_Coll, ftCamera_UpdateCameraBox },
    /* 362 SpecialLwEnd */
    { 317, 0x00340014, FtMoveId_SpecialLw << 24, ftLc_SpecialLwEnd_Anim, ftLc_SpecialLw_NoIASA,
      ftLc_SpecialLwEnd_Phys, ftLc_SpecialLwEnd_Coll, ftCamera_UpdateCameraBox },
    /* 363 SpecialLwTurn */
    { 315, 0x00340014, FtMoveId_SpecialLw << 24, ftLc_SpecialLwTurn_Anim, ftLc_SpecialLw_NoIASA,
      ftLc_SpecialLwHold_Phys, ftLc_SpecialLwTurn_Coll, ftCamera_UpdateCameraBox },
    /* 364 SpecialAirLwStart */
    { 318, 0x00340414, FtMoveId_SpecialLw << 24, ftLc_SpecialLwStart_Anim,
      ftLc_SpecialLwStart_IASA, ftLc_SpecialAirLwStart_Phys, ftLc_SpecialAirLwStart_Coll,
      ftCamera_UpdateCameraBox },
    /* 365 SpecialAirLwHold */
    { 319, 0x003C0414, FtMoveId_SpecialLw << 24, ftLc_SpecialLwHold_Anim, ftLc_SpecialLw_NoIASA,
      ftLc_SpecialAirLwHold_Phys, ftLc_SpecialAirLwHold_Coll, ftCamera_UpdateCameraBox },
    /* 366 SpecialAirLwHit */
    { 320, 0x00340414, FtMoveId_SpecialLw << 24, ftLc_SpecialLwHit_Anim, ftLc_SpecialLw_NoIASA,
      ftLc_SpecialAirLwHold_Phys, ftLc_SpecialAirLwHit_Coll, ftCamera_UpdateCameraBox },
    /* 367 SpecialAirLwEnd */
    { 321, 0x00340414, FtMoveId_SpecialLw << 24, ftLc_SpecialLwEnd_Anim, ftLc_SpecialLw_NoIASA,
      ftLc_SpecialAirLwEnd_Phys, ftLc_SpecialAirLwEnd_Coll, ftCamera_UpdateCameraBox },
    /* 368 SpecialAirLwTurn */
    { 319, 0x00340414, FtMoveId_SpecialLw << 24, ftLc_SpecialLwTurn_Anim, ftLc_SpecialLw_NoIASA,
      ftLc_SpecialAirLwHold_Phys, ftLc_SpecialAirLwTurn_Coll, ftCamera_UpdateCameraBox },
    /* 369 AirCatch */
    { 322, 0x00200000, FtMoveId_Default << 24, ftLc_AirCatch_Anim, ftLc_AirCatch_IASA,
      ftLc_AirCatch_Phys, ftLc_AirCatch_Coll, ftCamera_UpdateCameraBox },
    /* 370 AirCatchHit */
    { 323, 0x00C00000, FtMoveId_Default << 24, ftLc_AirCatchHit_Anim, ftLc_AirCatchHit_IASA,
      ftLc_AirCatchHit_Phys, ftLc_AirCatchHit_Coll, ftCamera_UpdateCameraBox },
};

/* ---------------------------------------------------------------------------------------------
 * Small shared helpers.
 * ------------------------------------------------------------------------------------------- */

/* code+0x634C "fsign": 1, -1, or 0. */
float ftLc_Sign(float x)
{
    if (x > 0.0f) {
        return 1.0f;
    }
    if (x < 0.0f) {
        return -1.0f;
    }
    return 0.0f;
}

/* code+0x4360: detach an article from Lucas and destroy it. */
void ftLc_RemoveAndDestroyItem(Item_GObj* item_gobj)
{
    if (item_gobj == NULL) {
        return;
    }
    {
        Item* ip = GET_ITEM(item_gobj);
        *(HSD_GObj**) &ip->xDD4_itemVar = NULL; /* ip+0xDD4: the article's owner reference */
        ip->owner = NULL;
    }
    Item_8026A8EC(item_gobj);
}

/* code+0x5B64: the forward smash stick (or snake) held at fp+0x2248. */
static void ftLc_RemoveHeldItem(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_FighterVars* vars = ftLc_Vars(fp);
    if (vars->held_item_gobj != NULL) {
        Item_8026A8EC(vars->held_item_gobj);
        vars->held_item_gobj = NULL;
    }
}

/* code+0x3DC4: death/damage callback while an article is out. */
void ftLc_RemoveAllArticles(HSD_GObj* gobj)
{
    ftLc_RemovePKFreeze(gobj);
    ftLc_RemovePKThunder(gobj);
    ftLc_RemoveHeldItem(gobj);
}

/* ---------------------------------------------------------------------------------------------
 * ftFunction exports.
 * ------------------------------------------------------------------------------------------- */

/* m-ex onrespawn, code+0xC8C "ResetAttributes": reload the special attributes from the file. */
void ftLc_ResetAttributes(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    _Static_assert(sizeof(ftLucasAttributes) == 0x13C, "Lucas attribute block is 0x13C bytes");
    memcpy(fp->dat_attrs_backup, DP(fp->ft_data->ext_attr), sizeof(ftLucasAttributes));
}

/* code+0x1038: victory screen accessory (fp->x21EC), the stick in his win pose. */
static void ftLc_WinAccessory(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->motion_id == 2) {
        ftLc_Vars(fp)->held_item_gobj =
            ftLc_SpawnStick(gobj, &fp->cur_pos, ftLc_Part_Hand, fp->facing_dir);
    }
}

/* code+0x4090 is the rope renderer (lucas_aircatch.c); code+0xFC4 wraps the normal fighter draw
 * and adds it on the last pass while the rope is out. */
static void ftLc_GXLink(HSD_GObj* gobj, intptr_t pass)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDrawCommon_80080E18(gobj, pass);
    if (pass == 2 && fp->x21FC_flag.b6 &&
        (fp->motion_id == ftLc_MS_AirCatch || fp->motion_id == ftLc_MS_AirCatchHit))
    {
        ftLc_Rope_Render(gobj);
    }
}

/* m-ex onload, code+0x0. */
void ftLc_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    DISC_PTR(void)* items;
    int i;

    ftLc_ResetAttributes(gobj);
    fp->dat_attrs = fp->dat_attrs_backup;

    items = DP(fp->ft_data->x48_items);
    for (i = 0; i < ftLc_Art_Count; i++) {
        mu_ak_register_article(fp->kind, DP(items[i]), i);
    }

    /* Replace the fighter's GX link with one that can also draw the rope. */
    HSD_GObjGXLink_8039084C(gobj);
    GObj_SetupGXLink(gobj, ftLc_GXLink, 5, 0);

    /* m-ex calloc (0x803D706C) of 0x598 bytes: the rope simulation. */
    {
        LucasRope* rope = HSD_MemAlloc(sizeof(LucasRope));
        memset(rope, 0, sizeof(LucasRope));
        ftLc_Vars(fp)->rope = rope;
    }

    if (fp->x18 == 14) {
        /* Results screen fighter (ftdemo.c sets x18 = 14). */
        fp->x21EC = ftLc_WinAccessory;
    } else {
        /* CPU Lucas plays with Ness's AI (FTKIND_NESS = 8). */
        ftLc_InitCpuSpoof(gobj, 8);
    }
}

/* m-ex ondeath, code+0x148 (named OnRespawn in the disc's symbols). */
void ftLc_OnRespawn(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_FighterVars* vars = ftLc_Vars(fp);
    ftParts_80074A4C(gobj, 0, 0);
    vars->x222C = 0;
    vars->pkfreeze_gobj = NULL;
    vars->pkthunder_gobj = NULL;
    vars->held_item_gobj = NULL;
    vars->pkthunder_gfx = 0;
    fp->used_tether = false;
}

/* m-ex onunknown (user data removal), code+0x1A0: free the rope. */
void ftLc_OnDestroy(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_FighterVars* vars = ftLc_Vars(fp);
    if (vars->rope != NULL) {
        HSD_Free(vars->rope);
        vars->rope = NULL;
    }
}

/* m-ex onitempickup / onitemcatch, code+0x9D0 (Fighter_OnItemPickup(gobj, flag, 1, 1)). */
static void ftLc_OnItemPickup(HSD_GObj* gobj, bool catch_item)
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

/* m-ex onmakeiteminvisible, code+0xAB4. */
static void ftLc_OnItemInvisible(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!itIsHeavy(fp->item_gobj)) {
        ftAnim_80070CC4(gobj, 1);
    }
}

/* m-ex onmakeitemvisible, code+0xB00. The disc calls ftAnim_80070CC4 here too (the vanilla
 * fighters call ftAnim_80070C48); kept as shipped. */
static void ftLc_OnItemVisible(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!itIsHeavy(fp->item_gobj)) {
        ftAnim_80070CC4(gobj, 1);
    }
}

/* m-ex onitemdrop / onunknownitemrelated, code+0xB4C (Fighter_OnItemDrop(gobj, flag, 1, 1)). */
static void ftLc_OnItemDrop(HSD_GObj* gobj, bool drop_item)
{
    ftAnim_80070FB4(gobj, 1, -1);
    if (drop_item) {
        ftAnim_80070CC4(gobj, 1);
    }
}

/* m-ex onhit, code+0xBE4 "EyeTextureDamaged" (knockback enter). */
static void ftLc_OnKnockbackEnter(HSD_GObj* gobj)
{
    ftAnim_800704F0(gobj, 0, 3.0f);
    ftAnim_800704F0(gobj, 1, 3.0f);
}

/* m-ex onunknowneyetexturerelated, code+0xC38 "EyeTextureNormal" (knockback exit). */
static void ftLc_OnKnockbackExit(HSD_GObj* gobj)
{
    ftAnim_800704F0(gobj, 0, 0.0f);
    ftAnim_800704F0(gobj, 1, 0.0f);
}

/* m-ex enterdoublejump, code+0xCC0: Ness's double jump (ftNs_JumpAerial_Enter with
 * ftCo_800CBAC4 inlined), with one difference kept from the disc: the horizontal drift seed is
 * truncated to an int and its bit pattern stored in the float at fp+0x2344 (the m-ex struct
 * declares that slot as an int). */
static void ftLc_EnterDoubleJump(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    /* The disc inlines ftNs_JumpAerial_Enter step for step (ftCommon_8007D5D4, cmd_vars[0] = 1,
     * JumpAerialF/B from the stick, ftCo_800CBAC4 with a zero velocity, the bunny hood sound for a
     * non-Ness kind, phys_cb = ftNs_JumpAerial_Phys_Cb at 0x800CC654), so call it. */
    ftNs_JumpAerial_Enter(gobj);
    {
        s32 truncated = (s32) (fp->input.lstick[0].x * fp->co_attrs.air_jump_h_multiplier);
        memcpy(&fp->mv.co.jumpaerial.init_h_vel, &truncated, sizeof(truncated));
    }
}

/* m-ex entertether, code+0xE00. */
static void ftLc_EnterTether(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ftLc_Attrs(fp)->xEC_TETHER_ENABLED == 1) {
        ftLc_AirCatch_Enter(gobj);
    }
}

/* m-ex slot 41, code+0xEDC "OnIntroL": the snake comes out for the entrance. */
void mu_ak_lucas_on_intro_l(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->item_gobj = ftLc_SpawnSnake(gobj, &fp->cur_pos, 1, 6, fp->facing_dir);
}

/* m-ex slot 43, code+0xF1C "OnAppeal": taunt with the snake. */
void mu_ak_lucas_on_appeal(HSD_GObj* gobj)
{
    ftLc_CommonSpawnSnake(gobj, 2);
}

/* m-ex slot 44, code+0xF40 "OnCatch": the grab is the snake. */
void mu_ak_lucas_on_catch(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->motion_id == ftCo_MS_Catch) {
        ftLc_CommonSpawnSnake(gobj, 0);
    }
    if (fp->motion_id == ftCo_MS_CatchDash) {
        ftLc_CommonSpawnSnake(gobj, 1);
    }
    fp->accessory4_cb = ftLc_Catch_Accessory;
}

/* ---------------------------------------------------------------------------------------------
 * The fighter description. Callbacks the disc leaves empty are NULL (game default).
 * Item pickup/drop take (gobj, bool) like the decomp's Fighter_ItemEvent tables; the integration
 * layer must call them through that type.
 * ------------------------------------------------------------------------------------------- */
const MuAkFighter mu_ak_lucas = {
    .name = "Lucas",
    .file = "PlLc.dat",
    .onload = ftLc_OnLoad,
    .ondeath = ftLc_OnRespawn,
    .onunknown = ftLc_OnDestroy,
    .specialn = ftLc_SpecialN_Enter,
    .specialairn = ftLc_SpecialAirN_Enter,
    .specials = ftLc_SpecialS_Enter,
    .specialairs = ftLc_SpecialAirS_Enter,
    .specialhi = ftLc_SpecialHi_Enter,
    .specialairhi = ftLc_SpecialAirHi_Enter,
    .speciallw = ftLc_SpecialLw_Enter,
    .specialairlw = ftLc_SpecialAirLw_Enter,
    .onabsorb = ftLc_SpecialLw_OnAbsorb,
    .onitempickup = (MuAkEvent) ftLc_OnItemPickup,
    .onmakeiteminvisible = ftLc_OnItemInvisible,
    .onmakeitemvisible = ftLc_OnItemVisible,
    .onitemdrop = (MuAkEvent) ftLc_OnItemDrop,
    .onitemcatch = (MuAkEvent) ftLc_OnItemPickup,
    .onunknownitemrelated = (MuAkEvent) ftLc_OnItemDrop,
    .onhit = ftLc_OnKnockbackEnter,
    .onunknowneyetexturerelated = ftLc_OnKnockbackExit,
    .onrespawn = ftLc_ResetAttributes,
    .enterdoublejump = ftLc_EnterDoubleJump,
    .entertether = ftLc_EnterTether,
    .onsmashf = ftLc_AttackS4_Enter,
    .move_logic = ftLc_MotionStates,
    .move_logic_count = ftLc_MS_SelfCount,

    .articles = ftLc_ArticleLogic,
    .article_count = ftLc_Art_TableCount,

    .kirby = &ftKbLc_Copy,
};
