/* ACE's Wario: load, per-fighter events and the state table (m-ex "ftFunction" of PlWr.dat).
 *
 * PlWr.dat exports 18 ftFunction slots. MxDt holds nothing else for internal 35 but the common
 * double jump (slot 32, resolved by the registry). The file carries no CPU helper: a CPU Wario
 * runs the game's CPU step with his own kind, as on the console. */
#include "wario.h"

#include <dolphin/os.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_AppealS.h>
#include <melee/it/it_26B1.h>
#include <melee/lb/lbdvd.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/gobj.h>

/* ---- ftFunction ---- */

/* +0844 (slot 25, onrespawn): his attribute block again, 0x80 bytes, into the fighter's copy. */
static void ftWr_Init_LoadSpecialAttrs(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWr_DatAttrs* src = (ftWr_DatAttrs*) DP(fp->ft_data->ext_attr);

    *(ftWr_DatAttrs*) fp->dat_attrs = *src;
}

/* +0000 (slot 0, onload). The attribute copy into fp+2D8, which becomes fp+2D4; the wall jump
 * bit (fp+2224 |= 0x100 as a halfword: bit 0x01 of the byte at fp+2224); the four command
 * variables and fp+222C cleared; and "ftColAnim" of PlWr.dat (+0878, +0884: the two strings)
 * looked up in the loaded fighter file and kept at fp+2230.
 *
 * The console does not test the archive. Here a missing archive or symbol leaves the pointer
 * NULL, which the side special already tests before it plays the color animation. */
static void ftWr_Init_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftWr_FighterVars* fv = ftWr_Vars(fp);
    ftWr_DatAttrs* src = (ftWr_DatAttrs*) DP(fp->ft_data->ext_attr);
    HSD_Archive* archive;

    *(ftWr_DatAttrs*) fp->dat_attrs_backup = *src;
    fp->dat_attrs = fp->dat_attrs_backup;
    fp->can_walljump = true;
    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    fp->cmd_vars[2] = 0;
    fp->cmd_vars[3] = 0;
    fv->x222C = 0;

    archive = lbDvd_8001819C("PlWr.dat");
    fv->colanim = archive != NULL
                      ? (ftWr_ColAnimSlot*) HSD_ArchiveGetPublicAddress(archive, "ftColAnim")
                      : NULL;
    OSReport("[ak] Wario load: ftColAnim %s, wall jump %d\n",
             fv->colanim != NULL ? "found" : "NOT found (no color animation on the dash)",
             (int) fp->can_walljump);
}

/* +0088 (slot 1, ondeath): the first model part override cleared. */
static void ftWr_Init_OnDeath(HSD_GObj* gobj)
{
    ftParts_80074A4C(gobj, 0, 0);
}

/* +05AC (slot 13, onitempickup; slot 17, onitemcatch at +0728, is a branch to it): the common
 * hold-kind hand poses, the retail Fighter_OnItemPickup(gobj, flag, true, true) call for call. */
static void ftWr_Init_OnItemPickup(HSD_GObj* gobj, bool catch_item)
{
    Fighter_OnItemPickup(gobj, catch_item, true, true);
}

/* +06B0 (slot 16, onitemdrop) and +072C (slot 18, the same code again). Not the retail inline:
 * the hand pose is reset for both parts (0 and 1), and the release call takes 0. */
static void ftWr_Init_OnItemDrop(HSD_GObj* gobj, bool drop_item)
{
    ftAnim_80070FB4(gobj, 0, -1);
    ftAnim_80070FB4(gobj, 1, -1);
    if (drop_item) {
        ftAnim_80070CC4(gobj, 0);
    }
}

/* +07A4 (slot 21, onhit): the retail Fighter_OnKnockbackEnter(gobj, 1) (texture 1, then 0, at
 * frame 3.0, +1F9C). */
static void ftWr_Init_OnKnockbackEnter(HSD_GObj* gobj)
{
    Fighter_OnKnockbackEnter(gobj, 1);
}

/* +07F4 (slot 22): the retail Fighter_OnKnockbackExit(gobj, 1) (frame 0.0, +1F8C). */
static void ftWr_Init_OnKnockbackExit(HSD_GObj* gobj)
{
    Fighter_OnKnockbackExit(gobj, 1);
}

/* +0094 (slot 3, move_logic): fourteen states. The animation ids (295 to 307, then 239), flag
 * words and move ids are the words of the file. Row 13 names the common taunt's four callbacks
 * by address (0x800DECF4, 0x800DED30, 0x800DEE44, 0x800DEE64); nothing in his code enters it. */
const MotionState ftWr_Init_MotionStateTable[ftWr_MS_SelfCount] = {
    {
        // ftWr_MS_SpecialN = 341
        295,
        0x00340211,
        FtMoveId_SpecialN << 24,
        ftWr_SpecialN_Anim, /* +0890 */
        NULL,
        ftWr_SpecialN_Phys, /* +09A8 */
        ftWr_SpecialN_Coll, /* +0AB4 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWr_MS_SpecialAirN = 342
        296,
        0x00340611,
        FtMoveId_SpecialN << 24,
        ftWr_SpecialAirN_Anim, /* +0B44 */
        NULL,
        ftWr_SpecialAirN_Phys, /* +0C5C */
        ftWr_SpecialAirN_Coll, /* +0D78 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWr_MS_SpecialS = 343
        297,
        0x00340211,
        FtMoveId_SpecialS << 24,
        ftWr_SpecialS_Anim, /* +0E28 */
        ftWr_SpecialS_IASA, /* +0FFC */
        ftWr_SpecialS_Phys, /* +10CC */
        ftWr_SpecialS_Coll, /* +11B0 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWr_MS_SpecialSHit = 344
        298,
        0x00340211,
        FtMoveId_SpecialS << 24,
        ftWr_SpecialSHit_Anim, /* +128C */
        NULL,
        ftWr_SpecialSHit_Phys, /* +12E4 */
        ftWr_SpecialSHit_Coll, /* +12E8 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWr_MS_SpecialAirS = 345
        299,
        0x00340211,
        FtMoveId_SpecialS << 24,
        ftWr_SpecialAirS_Anim, /* +1334 */
        NULL,
        ftWr_SpecialAirS_Phys, /* +1438 */
        ftWr_SpecialAirS_Coll, /* +14D0 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWr_MS_SpecialSBound = 346
        300,
        0x00000000,
        FtMoveId_SpecialS << 24,
        ftWr_SpecialSBound_Anim, /* +1638 */
        NULL,
        ftWr_SpecialSBound_Phys, /* +16D8 */
        ftWr_SpecialSBound_Coll, /* +173C */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWr_MS_SpecialSJump = 347
        301,
        0x00340211,
        FtMoveId_SpecialS << 24,
        ftWr_SpecialSJump_Anim, /* +1788 */
        ftWr_SpecialSJump_IASA, /* +1868 */
        ftWr_SpecialSJump_Phys, /* +18D0 */
        ftWr_SpecialSJump_Coll, /* +18EC */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWr_MS_SpecialHi = 348
        302,
        0x00340013,
        FtMoveId_SpecialHi << 24,
        ftWr_SpecialHi_Anim, /* +19B0 */
        ftWr_SpecialHi_IASA, /* +1B10 */
        ftWr_SpecialHi_Phys, /* +1B14 */
        ftWr_SpecialHi_Coll, /* +1B30 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWr_MS_SpecialAirHi = 349
        303,
        0x00340413,
        FtMoveId_SpecialHi << 24,
        ftWr_SpecialAirHi_Anim, /* +1B78 */
        ftWr_SpecialAirHi_IASA, /* +1B7C */
        ftWr_SpecialAirHi_Phys, /* +1B80 */
        ftWr_SpecialAirHi_Coll, /* +1B9C */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWr_MS_SpecialLw = 350
        304,
        0x00340214,
        FtMoveId_SpecialLw << 24,
        ftWr_SpecialLw_Anim, /* +1BE4 */
        NULL,
        ftWr_SpecialLw_Phys, /* +1C50 */
        ftWr_SpecialLw_Coll, /* +1C94 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWr_MS_SpecialAirLw = 351
        305,
        0x00340214,
        FtMoveId_SpecialLw << 24,
        ftWr_SpecialAirLw_Anim, /* +1D40 */
        NULL,
        ftWr_SpecialAirLw_Phys, /* +1DAC */
        ftWr_SpecialAirLw_Coll, /* +1DCC */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWr_MS_SpecialLwFall = 352
        306,
        0x00000000,
        FtMoveId_SpecialLw << 24,
        ftWr_SpecialLwFall_Anim, /* +1E50 */
        NULL,
        ftWr_SpecialLwFall_Phys, /* +1E80 */
        ftWr_SpecialLwFall_Coll, /* +1E94 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWr_MS_SpecialLwLanding = 353: no collision callback in the file; the floor test
        // sits in the physics slot
        307,
        0x00000000,
        FtMoveId_SpecialLw << 24,
        ftWr_SpecialLwLanding_Anim, /* +1EA4 */
        NULL,
        ftWr_SpecialLwLanding_Phys, /* +1EF0 */
        NULL,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftWr_MS_AppealS = 354
        239,
        0x00000071,
        FtMoveId_Default << 24,
        ftCo_AppealS_Anim, /* 0x800DECF4 */
        ftCo_AppealS_IASA, /* 0x800DED30 */
        ftCo_AppealS_Phys, /* 0x800DEE44 */
        ftCo_AppealS_Coll, /* 0x800DEE64 */
        ftCamera_UpdateCameraBox,
    },
};

#define FTWR_ITEM_EVENT(fn) ((MuAkEvent) (void (*)(void)) (fn))

/* The 18 slots PlWr.dat exports. Everything else stays empty: the registry resolves the double
 * jump (slot 32) from MxDt. He has one item kind in MxDt's lookup (286) and no article in the
 * file, so no article is listed. Written and never run, so not MU_AK_READY. */
const MuAkFighter mu_ak_wario = {
    .name = "Wario",
    .file = "PlWr.dat",

    .onload = ftWr_Init_OnLoad,
    .ondeath = ftWr_Init_OnDeath,
    .specialn = ftWr_SpecialN_Enter,
    .specialairn = ftWr_SpecialAirN_Enter,
    .specials = ftWr_SpecialS_Enter,
    .specialairs = ftWr_SpecialAirS_Enter,
    .specialhi = ftWr_SpecialHi_Enter,
    .specialairhi = ftWr_SpecialAirHi_Enter,
    .speciallw = ftWr_SpecialLw_Enter,
    .specialairlw = ftWr_SpecialAirLw_Enter,
    .onitempickup = FTWR_ITEM_EVENT(ftWr_Init_OnItemPickup),
    .onitemdrop = FTWR_ITEM_EVENT(ftWr_Init_OnItemDrop),
    .onitemcatch = FTWR_ITEM_EVENT(ftWr_Init_OnItemPickup),
    .onunknownitemrelated = FTWR_ITEM_EVENT(ftWr_Init_OnItemDrop),
    .onhit = ftWr_Init_OnKnockbackEnter,
    .onunknowneyetexturerelated = ftWr_Init_OnKnockbackExit,
    .onrespawn = ftWr_Init_LoadSpecialAttrs,

    .move_logic = ftWr_Init_MotionStateTable,
    .move_logic_count = ftWr_MS_SelfCount,

    .flags = 0,
};
