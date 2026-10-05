/* Akaneia's Charizard: the MuAkFighter the integration layer installs, and its move_logic.
 * See NOTES.md in this folder for what each routine is and what it needs from the integration. */
#include "ftlizardon.h"

#include "../mu_ak_fighter.h"

#include <melee/ft/ftcamera.h>

/* The overlays must fit the unions they overlay, and the attribute block is the file's 0xB0. */
_Static_assert(sizeof(ftLz_FighterVars) <= sizeof(((Fighter*) 0)->u), "fighter vars overflow");
_Static_assert(sizeof(ftLz_MotionVars) <= sizeof(((Fighter*) 0)->mv), "motion vars overflow");
_Static_assert(sizeof(ftLz_DatAttrs) == 0xB0, "special attributes are 0xB0 bytes");

/* move_logic, states 341-360. The third word is the console value: the move id in the top byte and
 * the x9 flag byte next to it (the jumps carry 0x6A there where vanilla's JumpAerialF has 0x80). */
static MotionState const ftLz_MotionStateTable[ftLz_MS_SelfCount] = {
    {
        // ftLz_MS_JumpAerialF1 = 341
        ftLz_SM_JumpAerialF1,
        ftLz_MF_JumpAerial,
        { (FtMoveId_Default << 24) | (0x6A << 16) },
        ftLz_JumpAerial_Anim,
        ftLz_JumpAerial_IASA,
        ftLz_JumpAerial_Phys,
        ftLz_JumpAerial_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLz_MS_JumpAerialF2 = 342
        ftLz_SM_JumpAerialF2,
        ftLz_MF_JumpAerial,
        { (FtMoveId_Default << 24) | (0x6A << 16) },
        ftLz_JumpAerial_Anim,
        ftLz_JumpAerial_IASA,
        ftLz_JumpAerial_Phys,
        ftLz_JumpAerial_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLz_MS_SpecialNStart = 343
        ftLz_SM_SpecialNStart,
        ftLz_MF_SpecialN,
        { FtMoveId_SpecialN << 24 },
        ftLz_SpecialNStart_Anim,
        ftLz_SpecialNStart_IASA,
        ftLz_SpecialNStart_Phys,
        ftLz_SpecialNStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLz_MS_SpecialN = 344
        ftLz_SM_SpecialN,
        ftLz_MF_SpecialNLoop,
        { FtMoveId_SpecialN << 24 },
        ftLz_SpecialN_Anim,
        ftLz_SpecialN_IASA,
        ftLz_SpecialN_Phys,
        ftLz_SpecialN_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLz_MS_SpecialNEnd = 345
        ftLz_SM_SpecialNEnd,
        ftLz_MF_SpecialN,
        { FtMoveId_SpecialN << 24 },
        ftLz_SpecialNEnd_Anim,
        ftLz_SpecialNEnd_IASA,
        ftLz_SpecialNEnd_Phys,
        ftLz_SpecialNEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLz_MS_SpecialAirNStart = 346
        ftLz_SM_SpecialAirNStart,
        ftLz_MF_SpecialAirN,
        { FtMoveId_SpecialN << 24 },
        ftLz_SpecialAirNStart_Anim,
        ftLz_SpecialAirNStart_IASA,
        ftLz_SpecialAirNStart_Phys,
        ftLz_SpecialAirNStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLz_MS_SpecialAirN = 347
        ftLz_SM_SpecialAirN,
        ftLz_MF_SpecialAirNLoop,
        { FtMoveId_SpecialN << 24 },
        ftLz_SpecialAirN_Anim,
        ftLz_SpecialAirN_IASA,
        ftLz_SpecialAirN_Phys,
        ftLz_SpecialAirN_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLz_MS_SpecialAirNEnd = 348
        ftLz_SM_SpecialAirNEnd,
        ftLz_MF_SpecialAirN,
        { FtMoveId_SpecialN << 24 },
        ftLz_SpecialAirNEnd_Anim,
        ftLz_SpecialAirNEnd_IASA,
        ftLz_SpecialAirNEnd_Phys,
        ftLz_SpecialAirNEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLz_MS_SpecialHi = 349
        ftLz_SM_SpecialHi,
        ftLz_MF_SpecialHi,
        { FtMoveId_SpecialHi << 24 },
        ftLz_SpecialHi_Anim,
        ftLz_SpecialHi_IASA,
        ftLz_SpecialHi_Phys,
        ftLz_SpecialHi_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLz_MS_SpecialAirHi = 350
        ftLz_SM_SpecialAirHi,
        ftLz_MF_SpecialHi,
        { FtMoveId_SpecialHi << 24 },
        ftLz_SpecialAirHi_Anim,
        ftLz_SpecialAirHi_IASA,
        ftLz_SpecialAirHi_Phys,
        ftLz_SpecialAirHi_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLz_MS_SpecialLw = 351
        ftLz_SM_SpecialLw,
        ftLz_MF_SpecialLw,
        { FtMoveId_SpecialLw << 24 },
        ftLz_SpecialLw_Anim,
        ftLz_SpecialLw_IASA,
        ftLz_SpecialLw_Phys,
        ftLz_SpecialLw_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLz_MS_SpecialAirLw = 352
        ftLz_SM_SpecialAirLw,
        ftLz_MF_SpecialLw,
        { FtMoveId_SpecialLw << 24 },
        ftLz_SpecialAirLw_Anim,
        ftLz_SpecialAirLw_IASA,
        ftLz_SpecialAirLw_Phys,
        ftLz_SpecialAirLw_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLz_MS_SpecialSStart = 353
        ftLz_SM_SpecialSStart,
        ftLz_MF_SpecialS,
        { FtMoveId_SpecialS << 24 },
        ftLz_SpecialSStart_Anim,
        ftLz_SpecialSStart_IASA,
        ftLz_SpecialSStart_Phys,
        ftLz_SpecialSStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLz_MS_SpecialS = 354
        ftLz_SM_SpecialS,
        ftLz_MF_SpecialS,
        { FtMoveId_SpecialS << 24 },
        ftLz_SpecialS_Anim,
        ftLz_SpecialS_IASA,
        ftLz_SpecialS_Phys,
        ftLz_SpecialS_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLz_MS_SpecialSBlown = 355
        ftLz_SM_SpecialSBlown,
        ftLz_MF_SpecialS,
        { FtMoveId_SpecialS << 24 },
        ftLz_SpecialSBlown_Anim,
        ftLz_SpecialSBlown_IASA,
        ftLz_SpecialSBlown_Phys,
        ftLz_SpecialSBlown_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLz_MS_SpecialSEnd = 356
        ftLz_SM_SpecialSEnd,
        ftLz_MF_SpecialS,
        { FtMoveId_SpecialS << 24 },
        ftLz_SpecialSEnd_Anim,
        ftLz_SpecialSEnd_IASA,
        ftLz_SpecialSEnd_Phys,
        ftLz_SpecialSEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLz_MS_SpecialAirSStart = 357
        ftLz_SM_SpecialAirSStart,
        ftLz_MF_SpecialS,
        { FtMoveId_SpecialS << 24 },
        ftLz_SpecialAirSStart_Anim,
        ftLz_SpecialAirSStart_IASA,
        ftLz_SpecialAirSStart_Phys,
        ftLz_SpecialAirSStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLz_MS_SpecialAirS = 358
        ftLz_SM_SpecialAirS,
        ftLz_MF_SpecialS,
        { FtMoveId_SpecialS << 24 },
        ftLz_SpecialAirS_Anim,
        ftLz_SpecialAirS_IASA,
        ftLz_SpecialAirS_Phys,
        ftLz_SpecialAirS_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLz_MS_SpecialAirSBlown = 359
        ftLz_SM_SpecialAirSBlown,
        ftLz_MF_SpecialS,
        { FtMoveId_SpecialS << 24 },
        ftLz_SpecialAirSBlown_Anim,
        ftLz_SpecialAirSBlown_IASA,
        ftLz_SpecialAirSBlown_Phys,
        ftLz_SpecialAirSBlown_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLz_MS_SpecialAirSEnd = 360
        ftLz_SM_SpecialAirSEnd,
        ftLz_MF_SpecialS,
        { FtMoveId_SpecialS << 24 },
        ftLz_SpecialAirSEnd_Anim,
        ftLz_SpecialAirSEnd_IASA,
        ftLz_SpecialAirSEnd_Phys,
        ftLz_SpecialAirSEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
};

/* The item callbacks take a second argument (the game's Fighter_ItemEvent, void (*)(gobj, bool));
 * MuAkFighter stores every slot as a MuAkEvent, so they are cast here and must be called through
 * Fighter_ItemEvent (see NOTES.md). */
#define FTLZ_ITEM_EVENT(fn) ((MuAkEvent) (void (*)(void)) (fn))

const MuAkFighter mu_ak_charizard = {
    .name = "Charizard",
    .file = "PlLz.dat",

    .onload = ftLz_Init_OnLoad,
    .ondeath = ftLz_Init_OnDeath,
    .onunknown = ftLz_Init_OnUserDataRemove,
    .specialn = ftLz_SpecialN_Enter,
    .specialairn = ftLz_SpecialAirN_Enter,
    .specials = ftLz_SpecialS_Enter,
    .specialairs = ftLz_SpecialAirS_Enter,
    .specialhi = ftLz_SpecialHi_Enter,
    .specialairhi = ftLz_SpecialAirHi_Enter,
    .speciallw = ftLz_SpecialLw_Enter,
    .specialairlw = ftLz_SpecialAirLw_Enter,
    .onabsorb = NULL,
    .onitempickup = FTLZ_ITEM_EVENT(ftLz_Init_OnItemPickup),
    .onmakeiteminvisible = ftLz_Init_OnItemInvisible,
    .onmakeitemvisible = ftLz_Init_OnItemVisible,
    .onitemdrop = FTLZ_ITEM_EVENT(ftLz_Init_OnItemDrop),
    .onitemcatch = FTLZ_ITEM_EVENT(ftLz_Init_OnItemCatch),
    .onunknownitemrelated = FTLZ_ITEM_EVENT(ftLz_Init_OnItemUnknown),
    .onunknowncharactermodelflags1 = NULL,
    .onunknowncharactermodelflags2 = NULL,
    .onhit = ftLz_Init_OnKnockbackEnter,
    .onunknowneyetexturerelated = ftLz_Init_OnKnockbackExit,
    .onframe = ftLz_Init_OnFrame,
    .onactionstatechange = NULL,
    .onrespawn = ftLz_Init_LoadSpecialAttrs,
    .onmodelrender = NULL,
    .onshadowrender = NULL,
    .onunknownmultijump = NULL,
    .onactionstatechangewhileeyetextureischanged = NULL,
    .ontwoentrytable = NULL,
    .enterfloat = NULL,
    .enterdoublejump = ftLz_Init_EnterDoubleJump,
    .entertether = NULL,
    .onlanding = NULL,
    .onsmashf = NULL,
    .onsmashhi = NULL,
    .onsmashlw = NULL,

    .move_logic = ftLz_MotionStateTable,
    .move_logic_count = ftLz_MS_SelfCount,

    .articles = mu_ak_charizard_item_logic,
    .article_count = ftLz_Item_TableCount,

    .kirby = &ftKbLz_Copy,
};
