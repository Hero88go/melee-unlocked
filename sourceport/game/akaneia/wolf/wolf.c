/* Akaneia's Wolf: load, per-fighter events and the move table (m-ex "ftFunction" of PlWf.dat). */
#include "wolf.h"

#include <melee/ft/forward.h>
#include <melee/it/forward.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <melee/ft/kinds/ftFox/ftfoxappeals.h>
#include <melee/ft/kinds/ftFox/ftfoxspecialn.h>
#include <melee/ft/kinds/ftFox/ftfoxspecials.h>
#include <melee/ft/types.h>
#include <melee/it/it_26B1.h>

/* Wolf's special attributes, the "param_exts" block compiled into his m-ex code. Stored in the
 * console's byte order like every attribute block (DISC_STRUCT), so it can be copied over
 * fp->dat_attrs as is and read by Fox's callbacks too. */
const ftWolf_DatAttrs ftWf_Init_Attrs = {
    /* blaster: Fox's fields, not read by Wolf's own code */
    4.0f, 1.0f, 4.0f, 1.0f, 0.0f, 1.9f, 0.0f, 54, 74,
    /* Wolf Flash */
    15.0f, 1.5f, 0.05f, 0.016666668f, 4.0f, 0.1f, 2.0f, 0.07f, 5.0f, 0.08f, 0.4f, 20.0f,
    /* Fire Wolf */
    15.0f, 1.25f, 0.02f, 0.015f, 0.5f, 30.0f, 15, 6.0f, 3.2f, 0.1f, 1.5f, 20.0f, 0.8f, 0.125f,
    0.4f, 18.0f, 20.0f,
    /* Reflector */
    18.0f, 4.0f, 2.0f, 4, 2.0f, 0.026666667f,
    /* The reflect bubble: bone 1, 50 damage cap, offset (0, 6.5, 0), radius 8.5, damage x1.5, speed
     * x1. m-ex's C declares the last field an int and sets it to 3, so the byte the game reads as
     * the behavior is 0. */
    { 1, 50, { 0.0f, 6.5f, 0.0f }, 8.5f, 1.5f, 1.0f, 0 },
};

/* [onLoad]. The same shape as Fox's: install the attributes, then hand the two articles of the
 * fighter file to the item code (m-ex MEX_IndexFighterItem; Fox calls it_8026B3F8). */
static void ftWf_Init_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ArticleSlot* items = DP(fp->ft_data->x48_items);

    *(ftWolf_DatAttrs*) fp->dat_attrs_backup = ftWf_Init_Attrs;
    fp->dat_attrs = fp->dat_attrs_backup;

    mu_ak_article_set(mu_ak_item_kind(fp->kind, ftWf_Article_Laser), DP(items[ftWf_Article_Laser]));
    mu_ak_article_set(mu_ak_item_kind(fp->kind, ftWf_Article_Blaster),
                      DP(items[ftWf_Article_Blaster]));
}

/* [onRespawn], the ondeath slot. Unlike Fox, Wolf does not clear anything of his own here: the
 * blaster is dropped by the damage callbacks (ftWf_SpecialN_DestroyGun). */
static void ftWf_Init_OnDeath(HSD_GObj* gobj)
{
    ftParts_80074A4C(gobj, 0, 0);
}

/* [OnDestroy]: empty in Akaneia. Kept as an explicit empty event so the slot is not the default. */
static void ftWf_Init_OnDestroy(HSD_GObj* gobj) {}

/* [OnItemPickup] and [OnItemCatch]: Fox's (Fighter_OnItemPickup with both part flags set). */
static void ftWf_Init_OnItemPickup(HSD_GObj* gobj, bool catch_item)
{
    Fighter_OnItemPickup(gobj, catch_item, true, true);
}

/* [OnItemRelease] and [OnUnknownItemRelated]: Fox's (Fighter_OnItemDrop). */
static void ftWf_Init_OnItemDrop(HSD_GObj* gobj, bool drop_item)
{
    Fighter_OnItemDrop(gobj, drop_item, true, true);
}

/* [EyeTextureDamaged], the onhit slot: Fox's OnKnockbackEnter. m-ex calls ftAnim_ApplyPartAnim
 * without its float argument, so the frame it passes is whatever f1 held; Fox passes 0. */
static void ftWf_Init_OnKnockbackEnter(HSD_GObj* gobj)
{
    Fighter_OnKnockbackEnter(gobj, 1);
    ftAnim_ApplyPartAnim(gobj, 3, 3, 0.0f);
    ftAnim_ApplyPartAnim(gobj, 4, 3, 0.0f);
}

/* [EyeTextureNormal]: Fox's OnKnockbackExit. */
static void ftWf_Init_OnKnockbackExit(HSD_GObj* gobj)
{
    Fighter_OnKnockbackExit(gobj, 1);
    ftAnim_ApplyPartAnim(gobj, 3, 2, 0.0f);
    ftAnim_ApplyPartAnim(gobj, 4, 2, 0.0f);
}

/* [OnFrame]: empty in Akaneia. */
static void ftWf_Init_OnFrame(HSD_GObj* gobj) {}

/* [ResetAttributes], the onrespawn slot: write the attribute block back over fp->dat_attrs (Fox's
 * ftFx_Init_LoadSpecialAttrs copies it from the fighter file instead). */
static void ftWf_Init_LoadSpecialAttrs(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    *(ftWolf_DatAttrs*) fp->dat_attrs = ftWf_Init_Attrs;
}

/* [EnterDoubleJump] */
static void ftWf_Init_EnterDoubleJump(HSD_GObj* gobj)
{
    ftCo_JumpAerial_Enter_Basic(gobj);
}

/* [move_logic]. Same animation ids, flags and move ids as Fox's table; the states Wolf never enters
 * keep Fox's callbacks, as they do in Akaneia. */
const MotionState ftWf_Init_MotionStateTable[ftWf_MS_SelfCount] = {
    {
        // ftWf_MS_SpecialNStart = 341
        ftFx_SM_SpecialNStart,
        ftFx_MF_SpecialN,
        FtMoveId_SpecialN << 24,
        ftWf_SpecialNStart_Anim,
        ftWf_SpecialNStart_IASA,
        ftWf_SpecialNStart_Phys,
        ftWf_SpecialNStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialNLoop = 342
        ftFx_SM_SpecialNLoop,
        ftFx_MF_SpecialNLoop,
        FtMoveId_SpecialN << 24,
        ftFx_SpecialNLoop_Anim,
        ftFx_SpecialNLoop_IASA,
        ftFx_SpecialNLoop_Phys,
        ftFx_SpecialNLoop_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialNEnd = 343
        ftFx_SM_SpecialNEnd,
        ftFx_MF_SpecialN,
        FtMoveId_SpecialN << 24,
        ftFx_SpecialNEnd_Anim,
        ftFx_SpecialNEnd_IASA,
        ftFx_SpecialNEnd_Phys,
        ftFx_SpecialNEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialAirNStart = 344
        ftFx_SM_SpecialAirNStart,
        ftFx_MF_SpecialAirN,
        FtMoveId_SpecialN << 24,
        ftWf_SpecialAirNStart_Anim,
        ftWf_SpecialAirNStart_IASA,
        ftWf_SpecialAirNStart_Phys,
        ftWf_SpecialAirNStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialAirNLoop = 345
        ftFx_SM_SpecialAirNLoop,
        ftFx_MF_SpecialAirNLoop,
        FtMoveId_SpecialN << 24,
        ftFx_SpecialAirNLoop_Anim,
        ftFx_SpecialAirNLoop_IASA,
        ftFx_SpecialAirNLoop_Phys,
        ftFx_SpecialAirNLoop_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialAirNEnd = 346
        ftFx_SM_SpecialAirNEnd,
        ftFx_MF_SpecialAirN,
        FtMoveId_SpecialN << 24,
        ftFx_SpecialAirNEnd_Anim,
        ftFx_SpecialAirNEnd_IASA,
        ftFx_SpecialAirNEnd_Phys,
        ftFx_SpecialAirNEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialSStart = 347
        ftFx_SM_SpecialSStart,
        ftFx_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftWf_SpecialSStart_Anim,
        ftWf_SpecialSStart_IASA,
        ftWf_SpecialSStart_Phys,
        ftWf_SpecialSStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialS = 348
        ftFx_SM_SpecialS,
        ftFx_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftFx_SpecialS_Anim,
        ftFx_SpecialS_IASA,
        ftFx_SpecialS_Phys,
        ftFx_SpecialS_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialSEnd = 349
        ftFx_SM_SpecialSEnd,
        ftFx_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftWf_SpecialSEnd_Anim,
        ftWf_SpecialSEnd_IASA,
        ftWf_SpecialSEnd_Phys,
        ftWf_SpecialSEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialAirSStart = 350
        ftFx_SM_SpecialAirSStart,
        ftFx_MF_SpecialAirS,
        FtMoveId_SpecialS << 24,
        ftWf_SpecialAirSStart_Anim,
        ftWf_SpecialAirSStart_IASA,
        ftWf_SpecialAirSStart_Phys,
        ftWf_SpecialAirSStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialAirS = 351
        ftFx_SM_SpecialAirS,
        ftFx_MF_SpecialAirS,
        FtMoveId_SpecialS << 24,
        ftWf_SpecialAirS_Anim,
        ftWf_SpecialAirS_IASA,
        ftWf_SpecialAirS_Phys,
        ftWf_SpecialAirS_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialAirSEnd = 352
        ftFx_SM_SpecialAirSEnd,
        ftFx_MF_SpecialAirS,
        FtMoveId_SpecialS << 24,
        ftWf_SpecialAirSEnd_Anim,
        ftWf_SpecialAirSEnd_IASA,
        ftWf_SpecialAirSEnd_Phys,
        ftWf_SpecialAirSEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialHiHold = 353
        ftFx_SM_SpecialHiHold,
        ftFx_MF_SpecialHi,
        FtMoveId_SpecialHi << 24,
        ftWf_SpecialHiHold_Anim,
        ftWf_SpecialHiHold_IASA,
        ftWf_SpecialHiHold_Phys,
        ftWf_SpecialHiHold_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialHiHoldAir = 354
        ftFx_SM_SpecialHiHoldAir,
        ftFx_MF_SpecialAirHiHold,
        FtMoveId_SpecialHi << 24,
        ftWf_SpecialHiHoldAir_Anim,
        ftWf_SpecialHiHoldAir_IASA,
        ftWf_SpecialHiHoldAir_Phys,
        ftWf_SpecialHiHoldAir_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialHi = 355
        ftFx_SM_SpecialHi,
        ftFx_MF_SpecialHi,
        FtMoveId_SpecialHi << 24,
        ftWf_SpecialHi_Anim,
        ftWf_SpecialHi_IASA,
        ftWf_SpecialHi_Phys,
        ftWf_SpecialHi_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialAirHi = 356
        ftFx_SM_SpecialHi,
        ftFx_MF_SpecialAirHiHold,
        FtMoveId_SpecialHi << 24,
        ftWf_SpecialAirHi_Anim,
        ftWf_SpecialAirHi_IASA,
        ftWf_SpecialAirHi_Phys,
        ftWf_SpecialAirHi_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialHiLanding = 357
        ftFx_SM_SpecialHiLanding,
        ftFx_MF_SpecialHi,
        FtMoveId_SpecialHi << 24,
        ftWf_SpecialHiLanding_Anim,
        ftWf_SpecialHiLanding_IASA,
        ftWf_SpecialHiLanding_Phys,
        ftWf_SpecialHiLanding_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialHiFall = 358
        ftFx_SM_SpecialHiFall,
        ftFx_MF_SpecialAirHiHold,
        FtMoveId_SpecialHi << 24,
        ftWf_SpecialHiFall_Anim,
        ftWf_SpecialHiFall_IASA,
        ftWf_SpecialHiFall_Phys,
        ftWf_SpecialHiFall_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialHiBound = 359
        ftFx_SM_SpecialHiBound,
        ftFx_MF_SpecialAirHiHold,
        FtMoveId_SpecialHi << 24,
        ftWf_SpecialHiBound_Anim,
        ftWf_SpecialHiBound_IASA,
        ftWf_SpecialHiBound_Phys,
        ftWf_SpecialHiBound_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialLwStart = 360
        ftFx_SM_SpecialLwStart,
        ftFx_MF_SpecialLw,
        FtMoveId_SpecialLw << 24,
        ftWf_SpecialLwStart_Anim,
        ftWf_SpecialLwStart_IASA,
        ftWf_SpecialLwStart_Phys,
        ftWf_SpecialLwStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialLwLoop = 361
        ftFx_SM_SpecialLwLoop,
        ftFx_MF_SpecialLwLoop,
        FtMoveId_SpecialLw << 24,
        ftWf_SpecialLwLoop_Anim,
        ftWf_SpecialLwLoop_IASA,
        ftWf_SpecialLwLoop_Phys,
        ftWf_SpecialLwLoop_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialLwHit = 362
        ftFx_SM_SpecialLwHit,
        ftFx_MF_SpecialLw,
        FtMoveId_SpecialLw << 24,
        ftWf_SpecialLwHit_Anim,
        ftWf_SpecialLwHit_IASA,
        ftWf_SpecialLwHit_Phys,
        ftWf_SpecialLwHit_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialLwEnd = 363
        ftFx_SM_SpecialLwEnd,
        ftFx_MF_SpecialLw,
        FtMoveId_SpecialLw << 24,
        ftWf_SpecialLwEnd_Anim,
        ftWf_SpecialLwEnd_IASA,
        ftWf_SpecialLwEnd_Phys,
        ftWf_SpecialLwEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialLwTurn = 364
        ftFx_SM_SpecialLwLoop,
        ftFx_MF_SpecialLw,
        FtMoveId_SpecialLw << 24,
        ftWf_SpecialLwTurn_Anim,
        ftWf_SpecialLwTurn_IASA,
        ftWf_SpecialLwTurn_Phys,
        ftWf_SpecialLwTurn_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialAirLwStart = 365
        ftFx_SM_SpecialAirLwStart,
        ftFx_MF_SpecialAirLw,
        FtMoveId_SpecialLw << 24,
        ftWf_SpecialAirLwStart_Anim,
        ftWf_SpecialAirLwStart_IASA,
        ftWf_SpecialAirLwStart_Phys,
        ftWf_SpecialAirLwStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialAirLwLoop = 366
        ftFx_SM_SpecialAirLwLoop,
        ftFx_MF_SpecialAirLwLoop,
        FtMoveId_SpecialLw << 24,
        ftWf_SpecialAirLwLoop_Anim,
        ftWf_SpecialAirLwLoop_IASA,
        ftWf_SpecialAirLwLoop_Phys,
        ftWf_SpecialAirLwLoop_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialAirLwHit = 367
        ftFx_SM_SpecialAirLwHit,
        ftFx_MF_SpecialAirLw,
        FtMoveId_SpecialLw << 24,
        ftWf_SpecialAirLwHit_Anim,
        ftWf_SpecialAirLwHit_IASA,
        ftWf_SpecialAirLwHit_Phys,
        ftWf_SpecialAirLwHit_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialAirLwEnd = 368
        ftFx_SM_SpecialAirLwEnd,
        ftFx_MF_SpecialAirLw,
        FtMoveId_SpecialLw << 24,
        ftWf_SpecialAirLwEnd_Anim,
        ftWf_SpecialAirLwEnd_IASA,
        ftWf_SpecialAirLwEnd_Phys,
        ftWf_SpecialAirLwEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_SpecialAirLwTurn = 369
        ftFx_SM_SpecialAirLwLoop,
        ftFx_MF_SpecialAirLw,
        FtMoveId_SpecialLw << 24,
        ftWf_SpecialAirLwTurn_Anim,
        ftWf_SpecialAirLwTurn_IASA,
        ftWf_SpecialAirLwTurn_Phys,
        ftWf_SpecialAirLwTurn_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_AppealSStartR = 370
        ftFx_SM_AppealSStartR,
        ftFx_MF_Appeal,
        FtMoveId_Default << 24,
        ftFx_AppealS_Anim,
        ftFx_AppealS_IASA,
        ftFx_AppealS_Phys,
        ftFx_AppealS_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_AppealSStartL = 371
        ftFx_SM_AppealSStartL,
        ftFx_MF_Appeal,
        FtMoveId_Default << 24,
        ftFx_AppealS_Anim,
        ftFx_AppealS_IASA,
        ftFx_AppealS_Phys,
        ftFx_AppealS_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_AppealSR = 372
        ftFx_SM_AppealSR,
        ftFx_MF_Appeal,
        FtMoveId_Default << 24,
        ftFx_AppealS_Anim,
        ftFx_AppealS_IASA,
        ftFx_AppealS_Phys,
        ftFx_AppealS_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_AppealSL = 373
        ftFx_SM_AppealSL,
        ftFx_MF_Appeal,
        FtMoveId_Default << 24,
        ftFx_AppealS_Anim,
        ftFx_AppealS_IASA,
        ftFx_AppealS_Phys,
        ftFx_AppealS_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_AppealSEndR = 374
        ftFx_SM_AppealSEndR,
        ftFx_MF_Appeal,
        FtMoveId_Default << 24,
        ftFx_AppealS_Anim,
        ftFx_AppealS_IASA,
        ftFx_AppealS_Phys,
        ftFx_AppealS_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWf_MS_AppealSEndL = 375
        ftFx_SM_AppealSEndL,
        ftFx_MF_Appeal,
        FtMoveId_Default << 24,
        ftFx_AppealS_Anim,
        ftFx_AppealS_IASA,
        ftFx_AppealS_Phys,
        ftFx_AppealS_Coll,
        ftCamera_UpdateCameraBox,
    },
};

/* The item events take a second argument (Fighter_ItemEvent); MuAkEvent is one-argument, so these
 * slots are stored cast and must be called back as Fighter_ItemEvent (see NOTES.md). */
#define WF_ITEM_EVENT(fn) ((MuAkEvent) (void (*)(void)) (Fighter_ItemEvent) (fn))

const MuAkFighter mu_ak_wolf = {
    .name = "Wolf",
    .file = "PlWf.dat",

    .onload = ftWf_Init_OnLoad,
    .ondeath = ftWf_Init_OnDeath,
    .onunknown = ftWf_Init_OnDestroy,
    .specialn = ftWf_SpecialN_Enter,
    .specialairn = ftWf_SpecialAirN_Enter,
    .specials = ftWf_SpecialS_Enter,
    .specialairs = ftWf_SpecialAirS_Enter,
    .specialhi = ftWf_SpecialHi_Enter,
    .specialairhi = ftWf_SpecialAirHi_Enter,
    .speciallw = ftWf_SpecialLw_Enter,
    .specialairlw = ftWf_SpecialAirLw_Enter,
    .onitempickup = WF_ITEM_EVENT(ftWf_Init_OnItemPickup),
    .onitemdrop = WF_ITEM_EVENT(ftWf_Init_OnItemDrop),
    .onitemcatch = WF_ITEM_EVENT(ftWf_Init_OnItemPickup),
    .onunknownitemrelated = WF_ITEM_EVENT(ftWf_Init_OnItemDrop),
    .onhit = ftWf_Init_OnKnockbackEnter,
    .onunknowneyetexturerelated = ftWf_Init_OnKnockbackExit,
    .onframe = ftWf_Init_OnFrame,
    .onrespawn = ftWf_Init_LoadSpecialAttrs,
    .enterdoublejump = ftWf_Init_EnterDoubleJump,

    .move_logic = ftWf_Init_MotionStateTable,
    .move_logic_count = ftWf_MS_SelfCount,

    /* Every routine is written, but nothing has run yet and two services are missing (the item
     * create hook and the m-ex effect ids, NOTES.md), so Wolf is not marked MU_AK_READY. */
    .flags = 0,
    .articles = itWf_Articles,
    .article_count = 2,
};
