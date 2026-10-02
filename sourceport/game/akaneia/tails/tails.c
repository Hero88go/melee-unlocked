/* Akaneia's Tails, native: the fighter's own callbacks (m-ex ftFunction) and its motion states.
 *
 * Rewritten by hand from PlTs.dat's ftFunction code; each function keeps the console symbol name
 * (with the ftTs_ prefix) so it can be compared with the original. Callback slots that Tails leaves
 * empty on the disc stay NULL here. */
#include "ftTs.h"
#include "ftTs_hooks.h"

#include "../mu_ak_fighter.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <melee/ft/types.h>
#include <melee/it/it_26B1.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/memory.h>

/* Every Tails structure must fit the decomp's per-fighter unions. */
_Static_assert(sizeof(ftTails_FighterVars) <= sizeof(((Fighter*) 0)->u), "Tails fighter vars");
_Static_assert(sizeof(ftTails_SpecialLwVars) <= sizeof(((Fighter*) 0)->mv), "Tails motion vars");
_Static_assert(sizeof(ftTails_DatAttrs) == 0x1A8, "Tails attribute block is 0x1A8 bytes on the disc");

/* ------------------------------------------------------------------------------------------------
 * Load, respawn, destroy
 * --------------------------------------------------------------------------------------------- */

/* code+0x0 OnLoad */
void ftTs_OnLoad(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_FighterVars* fv = ftTs_Vars(fp);
    ftTails_DatAttrs* da;
    Fighter_VoidSlot* items;

    fv->ball_angle = 0.0f;
    fv->shot_gobj = NULL;
    fv->trail_count = 0;
    fv->trail_a = HSD_MemAlloc(sizeof(ftTails_TrailPoint) * ftTs_TrailMax);
    fv->trail_b = HSD_MemAlloc(sizeof(ftTails_TrailPoint) * ftTs_TrailMax);

    /* The costume's colour table; NULL when the costume archive has none. */
    fv->particle_hook.colors = mu_ak_costume_symbol(fp, "PlyTailsColor");
    fv->particle_hook.funcs.hookCreate = ftTs_OnSpawnParticle;
    fv->particle_hook.funcs.hookDelete = NULL;
    fv->particle_hook.funcs.setUserData = NULL;
    fv->particle_hook.fp = fp;
    fv->airs_boost_ready = true;

    PUSH_ATTRS(fp, ftTails_DatAttrs);

    items = (Fighter_VoidSlot*) DP(fp->ft_data->x48_items);
    mu_ak_register_article(fp->kind, DP(items[0]), 0);

    da = ftTs_Attrs(fp);
    MTXRotRad(fv->ball_mtx, 'x', da->ball_tilt);
    fv->ball_mtx[1][3] = da->ball_offset_y;

    if (fp->x1C_actionStateList == ftData_803C52A0) {
        /* Result screen / intro model: only the victory voice line. */
        fv->trail_gobj = NULL;
        HSD_GObj_SetupProc(gobj, (HSD_GObjEvent) ftTs_CheckWinAudio, 9);
        fp->cmd_vars[0] = 0;
        fp->cmd_vars[1] = 0;
        return;
    }

    HSD_GObj_SetupProc(gobj, (HSD_GObjEvent) ftTs_ProcessTail, 15);
    HSD_GObj_SetupProc(gobj, (HSD_GObjEvent) ftTs_ProcessTrail, 15);
    HSD_GObj_SetupProc(gobj, (HSD_GObjEvent) ftTs_ProcessMouth, 15);

    /* A render-only gobj that draws the spin trail after the fighters. */
    fv->trail_gobj = GObj_Create(0x12C, 13, 0);
    GObj_SetupGXLink(fv->trail_gobj, ftTs_GXCallback, 5, 0);
    fv->trail_gobj->user_data = gobj;

    ftTs_MexCPU_InitCustomData(gobj, &ftTs_CpuData);
}

/* code+0x1D4 OnRespawn (m-ex "ondeath") */
void ftTs_OnRespawn(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftTs_Vars(fp)->ball_angle = 0.0f;
    ftTs_Vars(fp)->airs_boost_ready = true;
    ftParts_80074A4C(gobj, 0, 0);
    ftParts_80074A4C(gobj, 1, 0);
    ftParts_80074A4C(gobj, 2, 1);
    ftParts_80074A4C(gobj, 3, 0);
    ftParts_80074A4C(gobj, 4, 0);
}

/* code+0x260 OnDestroy (m-ex "onunknown") */
void ftTs_OnDestroy(Fighter_GObj* gobj)
{
    ftTails_FighterVars* fv = ftTs_Vars(GET_FIGHTER(gobj));

    if (fv->trail_gobj != NULL) {
        HSD_GObjFree(fv->trail_gobj);
        fv->trail_gobj = NULL;
    }
    HSD_Free(fv->trail_a);
    fv->trail_a = NULL;
    HSD_Free(fv->trail_b);
    fv->trail_b = NULL;
}

/* code+0xAB0 ResetAttributes (m-ex "onrespawn"): fresh attributes and a full fuel tank. */
void ftTs_ResetAttributes(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_DatAttrs* da = ftTs_Attrs(fp);

    *(ftTails_DatAttrs*) fp->dat_attrs_backup =
        *(ftTails_DatAttrs*) DP(fp->ft_data->ext_attr);
    ftTs_Vars(fp)->fuel = da->hi_max_fuel;
}

/* code+0xB24 OnLanding: refuel and allow the air spin boost again. */
void ftTs_OnLanding(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftTs_Vars(fp)->fuel = ftTs_Attrs(fp)->hi_max_fuel;
    ftTs_Vars(fp)->airs_boost_ready = true;
}

/* code+0xB04 EnterDoubleJump */
void ftTs_EnterDoubleJump(Fighter_GObj* gobj)
{
    ftCo_JumpAerial_Enter_Basic(gobj);
}

/* ------------------------------------------------------------------------------------------------
 * Items held in the hand: the same behaviour as the vanilla fighters (Fighter_OnItem* inlines)
 * --------------------------------------------------------------------------------------------- */

/* code+0x7E4 */
void ftTs_OnItemPickup(Fighter_GObj* gobj, bool catch_item)
{
    Fighter_OnItemPickup(gobj, catch_item, true, true);
}

/* code+0x8CC */
void ftTs_OnItemInvisible(Fighter_GObj* gobj)
{
    Fighter_OnItemInvisible(gobj, true);
}

/* code+0x918 */
void ftTs_OnItemVisible(Fighter_GObj* gobj)
{
    Fighter_OnItemVisible(gobj, true);
}

/* code+0x968 */
void ftTs_OnItemRelease(Fighter_GObj* gobj, bool drop_item)
{
    Fighter_OnItemDrop(gobj, drop_item, true, true);
}

/* code+0x9C0 */
void ftTs_OnItemCatch(Fighter_GObj* gobj, bool catch_item)
{
    ftTs_OnItemPickup(gobj, catch_item);
}

/* code+0x9E0 */
void ftTs_OnUnknownItemRelated(Fighter_GObj* gobj, bool drop_item)
{
    ftTs_OnItemRelease(gobj, drop_item);
}

/* ------------------------------------------------------------------------------------------------
 * Eyes and the empty exports
 * --------------------------------------------------------------------------------------------- */

/* code+0xA00 EyeTextureDamaged (m-ex "onhit") */
void ftTs_EyeTextureDamaged(Fighter_GObj* gobj)
{
    ftAnim_800704F0(gobj, 0, 3.0f);
    ftAnim_800704F0(gobj, 1, 3.0f);
}

/* code+0xA54 EyeTextureNormal */
void ftTs_EyeTextureNormal(Fighter_GObj* gobj)
{
    ftAnim_800704F0(gobj, 0, 0.0f);
    ftAnim_800704F0(gobj, 1, 0.0f);
}

/* code+0xAA8, 0xAAC, 0xB00: exported but empty on the disc. */
void ftTs_OnFrame(Fighter_GObj* gobj) {}
void ftTs_OnActionStateChange(Fighter_GObj* gobj) {}
void ftTs_OnModelRender(Fighter_GObj* gobj) {}

/* ------------------------------------------------------------------------------------------------
 * move_logic (code+0x2C4): 33 states from 341
 * --------------------------------------------------------------------------------------------- */
#define TS_STATE(anim, flags, move, prefix)                                                      \
    {                                                                                            \
        (anim), (flags), (move) << 24, prefix##_Anim, prefix##_IASA, prefix##_Phys,             \
            prefix##_Coll, ftCamera_UpdateCameraBox,                                             \
    }

const MotionState ftTs_MotionStateTable[ftTs_MS_SelfCount] = {
    /* 341 */ TS_STATE(0x127, ftTs_MF_SpecialN, FtMoveId_SpecialN, ftTs_SpecialN),
    /* 342 */ TS_STATE(0x128, ftTs_MF_SpecialN, FtMoveId_SpecialN, ftTs_SpecialAirN),
    /* 343 */ TS_STATE(0x129, ftTs_MF_SpecialHi, FtMoveId_SpecialHi, ftTs_SpecialHi_Start),
    /* 344 */ TS_STATE(0x12A, ftTs_MF_SpecialHi, FtMoveId_SpecialHi, ftTs_SpecialAirHi_Start),
    /* 345 */ TS_STATE(0x12B, ftTs_MF_SpecialHi, FtMoveId_SpecialHi, ftTs_SpecialHi_Loop),
    /* 346 */ TS_STATE(0x12C, ftTs_MF_SpecialHi, FtMoveId_SpecialHi, ftTs_SpecialHi_Exhaust),
    /* 347 */ TS_STATE(0x12D, ftTs_MF_SpecialHi, FtMoveId_SpecialHi, ftTs_SpecialHi_Cancel),
    /* 348 */ TS_STATE(0x12E, ftTs_MF_SpecialLw, FtMoveId_SpecialLw, ftTs_SpecialLwStart),
    /* 349 */ TS_STATE(0x130, ftTs_MF_SpecialLw, FtMoveId_SpecialLw, ftTs_SpecialLwEnd),
    /* 350 */ TS_STATE(0x131, ftTs_MF_SpecialLw, FtMoveId_SpecialLw, ftTs_SpecialAirLwStart),
    /* 351 */ TS_STATE(0x133, ftTs_MF_SpecialLw, FtMoveId_SpecialLw, ftTs_SpecialAirLwEnd),
    /* 352 */ TS_STATE(0x12F, ftTs_MF_SpecialLw, FtMoveId_SpecialLw, ftTs_SpecialLwCharge),
    /* 353 */ TS_STATE(0x134, ftTs_MF_SpecialLw, FtMoveId_SpecialLw, ftTs_SpecialLwRun),
    /* 354 */ TS_STATE(0x135, ftTs_MF_SpecialLw, FtMoveId_SpecialLw, ftTs_SpecialLwRunTurn),
    /* 355 */ TS_STATE(0x136, ftTs_MF_SpecialLw, FtMoveId_SpecialLw, ftTs_SpecialLwRunJump),
    /* 356 */ TS_STATE(0x137, ftTs_MF_SpecialLw, FtMoveId_SpecialLw, ftTs_SpecialLwDive),
    /* 357: the common RunBrake animation (submotion 0xE) */
    /* 357 */ TS_STATE(0x00E, ftTs_MF_SpecialLw, FtMoveId_SpecialLw, ftTs_SpecialLwRunBrake),
    /* 358 */ TS_STATE(0x138, ftTs_MF_SpecialLw, FtMoveId_SpecialLw, ftTs_SpecialLwStopWall),
    /* 359 */ TS_STATE(0x139, ftTs_MF_SpecialLw, FtMoveId_SpecialLw, ftTs_SpecialLwStopWall),
    /* 360 */ TS_STATE(0x13A, ftTs_MF_SpecialS, FtMoveId_SpecialS, ftTs_SpecialS_Start),
    /* 361 */ TS_STATE(0x13B, ftTs_MF_SpecialS, FtMoveId_SpecialS, ftTs_SpecialS_End),
    /* 362 */ TS_STATE(0x13C, ftTs_MF_SpecialS, FtMoveId_SpecialS, ftTs_SpecialS_Loop),
    /* 363 */ TS_STATE(0x13D, ftTs_MF_SpecialS, FtMoveId_SpecialS, ftTs_SpecialS_Loop),
    /* 364 */ TS_STATE(0x13E, ftTs_MF_SpecialS, FtMoveId_SpecialS, ftTs_SpecialS_Loop),
    /* 365 */ TS_STATE(0x13F, ftTs_MF_SpecialS, FtMoveId_SpecialS, ftTs_SpecialS_Loop),
    /* 366 */ TS_STATE(0x140, ftTs_MF_SpecialS, FtMoveId_SpecialS, ftTs_SpecialS_Loop),
    /* 367 */ TS_STATE(0x141, ftTs_MF_SpecialS, FtMoveId_SpecialS, ftTs_SpecialAirS_Start),
    /* 368 */ TS_STATE(0x143, ftTs_MF_SpecialS, FtMoveId_SpecialS, ftTs_SpecialAirS_End),
    /* 369 */ TS_STATE(0x144, ftTs_MF_SpecialS, FtMoveId_SpecialS, ftTs_SpecialAirS_Loop),
    /* 370 */ TS_STATE(0x145, ftTs_MF_SpecialS, FtMoveId_SpecialS, ftTs_SpecialAirS_Loop),
    /* 371 */ TS_STATE(0x146, ftTs_MF_SpecialS, FtMoveId_SpecialS, ftTs_SpecialAirS_Loop),
    /* 372 */ TS_STATE(0x147, ftTs_MF_SpecialS, FtMoveId_SpecialS, ftTs_SpecialAirS_Loop),
    /* 373 */ TS_STATE(0x148, ftTs_MF_SpecialS, FtMoveId_SpecialS, ftTs_SpecialAirS_Loop),
};

/* ------------------------------------------------------------------------------------------------
 * The fighter description the integration layer installs.
 *
 * The item callbacks take a second argument on console (ftData_OnItemPickupExt and friends are
 * Fighter_ItemEvent, (gobj, bool)); they are stored through MuAkEvent and must be cast back to
 * Fighter_ItemEvent before they are called. See NOTES.md.
 * --------------------------------------------------------------------------------------------- */
const MuAkFighter mu_ak_tails = {
    .name = "Tails",
    .file = "PlTs.dat",

    .onload = ftTs_OnLoad,
    .ondeath = ftTs_OnRespawn,
    .onunknown = ftTs_OnDestroy,
    .specialn = ftTs_SpecialN_Enter,
    .specialairn = ftTs_SpecialAirN_Enter,
    .specials = ftTs_SpecialS_EnterAirOrGround,
    .specialairs = ftTs_SpecialS_EnterAirOrGround,
    .specialhi = ftTs_SpecialHi_EnterAirOrGround,
    .specialairhi = ftTs_SpecialHi_EnterAirOrGround,
    .speciallw = ftTs_SpecialLw_EnterAirOrGround,
    .specialairlw = ftTs_SpecialLw_EnterAirOrGround,
    .onabsorb = NULL,
    .onitempickup = (MuAkEvent) ftTs_OnItemPickup,
    .onmakeiteminvisible = ftTs_OnItemInvisible,
    .onmakeitemvisible = ftTs_OnItemVisible,
    .onitemdrop = (MuAkEvent) ftTs_OnItemRelease,
    .onitemcatch = (MuAkEvent) ftTs_OnItemCatch,
    .onunknownitemrelated = (MuAkEvent) ftTs_OnUnknownItemRelated,
    .onunknowncharactermodelflags1 = NULL,
    .onunknowncharactermodelflags2 = NULL,
    .onhit = ftTs_EyeTextureDamaged,
    .onunknowneyetexturerelated = ftTs_EyeTextureNormal,
    .onframe = ftTs_OnFrame,
    .onactionstatechange = ftTs_OnActionStateChange,
    .onrespawn = ftTs_ResetAttributes,
    .onmodelrender = ftTs_OnModelRender,
    .onshadowrender = NULL,
    .onunknownmultijump = NULL,
    .onactionstatechangewhileeyetextureischanged = NULL,
    .ontwoentrytable = NULL,
    .enterfloat = NULL,
    .enterdoublejump = ftTs_EnterDoubleJump,
    .entertether = NULL,
    .onlanding = ftTs_OnLanding,
    .onsmashf = NULL,
    .onsmashhi = NULL,
    .onsmashlw = NULL,

    .move_logic = ftTs_MotionStateTable,
    .move_logic_count = ftTs_MS_SelfCount,

    .articles = &itTs_Shot_Logic,
    .article_count = 1,
};
