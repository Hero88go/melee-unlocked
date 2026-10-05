/* ACE's Zero: load, per-fighter events, the state table, the sword trail and the CPU helper
 * (m-ex "ftFunction" of PlZx.dat).
 *
 * Slots the file does not export are left empty here and take the m-ex default, Link's retail
 * function: specials (ftLk_SpecialS_Enter), specialhi and specialairhi, speciallw and
 * specialairlw, the six item events, the two knockback events and the common double jump.
 *
 * Link's accessory callbacks. Those retail entries install Link's callbacks for the thrown
 * boomerang (side special) and the pulled bomb (down special), which act when the animation
 * script raises the throw flag (ftLk_SpecialLw_Enter also swaps a held bomb first). Zero's
 * load does not hand Link's articles to the item code (the file never calls ftLk_Init_OnLoad nor
 * it_8026B3F8), so on the console those items would be created without article data unless a
 * Link is in the match. His own scripts decide whether the cue ever comes; they were not read.
 * Nothing is added or removed here: the retail entries run as they are. */
#include "zero.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ftattacks4combo.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/ft/kinds/ftCommon/ftCo_AirCatch.h>
#include <melee/ft/kinds/ftLink/ftlinkspecialhi.h>
#include <melee/ft/kinds/ftLink/ftlinkspeciallw.h>
#include <melee/ft/kinds/ftLink/ftlinkspecialn.h>
#include <melee/ft/kinds/ftLink/ftlinkspecials.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>

/* ---- CPU: m-ex's "MexCPU" helper, linked into every fighter file built with symbols ----------
 * +1E50 ".bss.mexcpu_data" is never written in PlZx.dat (all its relocations are reads), so
 * +2158 "MexCPU_ProcCustom" is never installed and the custom branch of +25F0 "MexCPU_Process"
 * is never taken: neither is carried over. +1E54 ".bss.mexcpu_spoof" only ever holds [OnLoad]'s
 * 0x14 (Young Link), which is ftZx_CpuSpoofKind here. */

/* +2318 "MexCPU_ProcSpoof" (proc, priority 2): Fighter_procCpu with fp->kind swapped for the
 * step, which is mu_ak_cpu_process with no custom code. */
static void ftZx_Cpu_ProcSpoof(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    FighterKind kind;

    if (fp->is_sleeping || !ftCo_IsCpuControlled(fp)) {
        return;
    }
    kind = fp->kind;
    fp->kind = ftZx_CpuSpoofKind;
    mu_ak_cpu_process(gobj, NULL);
    fp->kind = kind;
}

/* +1E58 "MexCPU_InitProc" (proc, priority 2): swap Fighter_procCpu for the posing proc. It stays
 * installed and finds nothing on later frames, as on the console. */
static void ftZx_Cpu_InitProc(HSD_GObj* gobj)
{
    HSD_GObjProc* proc;

    for (proc = gobj->proc; proc != NULL; proc = proc->child) {
        if (proc->on_invoke == Fighter_procCpu) {
            HSD_GObjProc_RemoveProc(proc);
            HSD_GObj_SetupProc(gobj, ftZx_Cpu_ProcSpoof, 2);
            return;
        }
    }
}

/* +05EC "MexCPU_InitSpoofData" */
static void ftZx_Cpu_InitSpoofData(HSD_GObj* gobj)
{
    HSD_GObj_SetupProc(gobj, ftZx_Cpu_InitProc, 2);
}

/* ---- ftFunction ---- */

/* ftData->x48_items: an array of article pointers (disc data). */
typedef DISC_PTR(void) ftZx_ArticleSlot;

/* The three model part overrides cleared (+0068, +00F8, +05A0). */
static void ftZx_Init_ClearParts(HSD_GObj* gobj)
{
    ftParts_80074A4C(gobj, 0, 0);
    ftParts_80074A4C(gobj, 1, 0);
    ftParts_80074A4C(gobj, 2, 0);
}

/* +0548 "ResetAttributes" (slot 25): his attribute block again, 0x10 bytes, into the fighter's
 * own copy (fp+2D8). Link's ftLk_Init_LoadSpecialAttrs copies a whole ftLk_DatAttrs. */
static void ftZx_Init_LoadSpecialAttrs(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftZx_DatAttrs* src = (ftZx_DatAttrs*) DP(fp->ft_data->ext_attr);

    *(ftZx_DatAttrs*) fp->dat_attrs_backup = *src;
}

/* +0000 "OnLoad" (slot 0). Not Link's load: both articles handed to the item code by index
 * (m-ex MEX_IndexFighterItem at 0x803D7058), the attribute copy, his two words, the three part
 * overrides, and the CPU helper. None of Link's five articles is registered and fp->x2226_b1 is
 * not set. */
static void ftZx_Init_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftZx_FighterVars* fv = ftZx_Vars(fp);
    ftZx_ArticleSlot* items = (ftZx_ArticleSlot*) DISC_GET(void, fp->ft_data->x48_items);

    mu_ak_register_article(fp->kind, DP(items[ftZx_Article_Shot]), ftZx_Article_Shot);
    mu_ak_register_article(fp->kind, DP(items[ftZx_Article_ChargedShot]),
                           ftZx_Article_ChargedShot);

    ftZx_Init_LoadSpecialAttrs(gobj);
    fp->dat_attrs = fp->dat_attrs_backup;

    fv->specialairs_used = 0;
    fv->specialairs_count = 0;
    ftZx_Init_ClearParts(gobj);
    ftZx_Cpu_InitSpoofData(gobj);
}

/* +00C0 "OnRespawn" (slot 1, the ondeath slot). Not Link's ftLk_Init_OnDeath: the first part
 * override (twice, as the file has it), his two words, the other two overrides and Link's second
 * word (fp+2230). Link's first word (fp+222C, "used_boomerang") and his item pointers are left
 * alone. */
static void ftZx_Init_OnDeath(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftZx_FighterVars* fv = ftZx_Vars(fp);

    ftParts_80074A4C(gobj, 0, 0);
    fv->specialairs_used = 0;
    fv->specialairs_count = 0;
    ftZx_Init_ClearParts(gobj);
    fv->x2230 = 0;
}

/* +0528 "OnActionStateChange" (slot 24): catching a ledge gives the air dash back. */
static void ftZx_Init_OnActionStateChange(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftZx_FighterVars* fv = ftZx_Vars(fp);

    if (fp->motion_id == ftCo_MS_CliffCatch) {
        fv->specialairs_used = 0;
        fv->specialairs_count = 0;
    }
}

/* +057C "OnLanding" (slot 34): entered a grounded state. */
static void ftZx_Init_OnLanding(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftZx_FighterVars* fv = ftZx_Vars(fp);

    fv->specialairs_used = 0;
    fv->specialairs_count = 0;
    ftZx_Init_ClearParts(gobj);
}

/* +1E30 ".data.desc.0": the sword trail. Console byte order, read through the same type as
 * Marth's block (the layout Lucina's uses):
 *   widths 0.2 and 1.0, alpha 255 to 0, inner color 00 FF 00, outer color FF FF FF,
 *   bone 26, and 0.0 and 10.5 for fp+20F8 and fp+20FC. */
static const struct SwordAttrs ftZx_Init_TrailData = {
    0.2f, 1.0f, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    { 0x00, 0x00, 0x00 }, 26, 0.0f, 10.5f,
};

_Static_assert(sizeof(struct SwordAttrs) == 0x20, "the trail block is 32 bytes");

/* +05E0 "GetTrailData" (slot 45). Called by the afterimage code with the fighter GObj where the
 * retail code picks the trail block by fighter kind (Link's is inside his attributes, which
 * Zero does not have). */
static struct SwordAttrs* ftZx_Init_GetTrailData(HSD_GObj* gobj)
{
    (void) gobj;
    return (struct SwordAttrs*) &ftZx_Init_TrailData;
}

/* +0148 "move_logic" (slot 3): 22 rows. Rows 341 to 361 against Link's table (main.dol
 * 0x803C7E18, run-source/rel09-ace-native/zero/tables.txt): animation ids, flags and move ids
 * are Link's in every row but 360; the callbacks marked with an offset are his own. */
const MotionState ftZx_Init_MotionStateTable[ftZx_MS_SelfCount] = {
    {
        // ftLk_MS_AttackS42 = 341: Link's row, unchanged
        ftLk_SM_AttackS42,
        ftLk_MF_AttackS42,
        FtMoveId_AttackS4 << 24,
        ftCo_AttackS42_Anim,
        ftCo_AttackS42_IASA,
        ftCo_AttackS42_Phys,
        ftCo_AttackS42_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLk_MS_AppealSR = 342: Link's row, unchanged (empty)
        ftCo_SM_None,
        Ft_MF_None,
        FtMoveId_Default << 24,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
    },
    {
        // ftLk_MS_AppealSL = 343: Link's row, unchanged (empty)
        ftCo_SM_None,
        Ft_MF_None,
        FtMoveId_Default << 24,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
    },
    {
        // ftZx_MS_SpecialN = 344
        ftLk_SM_SpecialNStart,
        ftLk_MF_SpecialN,
        FtMoveId_SpecialN << 24,
        ftZx_SpecialN_Anim, /* +0630 */
        ftZx_SpecialN_IASA, /* +0670 */
        ftZx_SpecialN_Phys, /* +07EC */
        ftZx_SpecialN_Coll, /* +080C */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLk_MS_SpecialNLoop = 345: Link's row, unchanged; nothing of his enters it
        ftLk_SM_SpecialNLoop,
        ftLk_MF_SpecialNFullyCharged,
        FtMoveId_SpecialN << 24,
        ftLk_SpecialNLoop_Anim,
        ftLk_SpecialNLoop_IASA,
        ftLk_SpecialNLoop_Phys,
        ftLk_SpecialNLoop_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLk_MS_SpecialNEnd = 346: Link's row, unchanged; nothing of his enters it
        ftLk_SM_SpecialNEnd,
        ftLk_MF_SpecialN,
        FtMoveId_SpecialN << 24,
        ftLk_SpecialNEnd_Anim,
        ftLk_SpecialNEnd_IASA,
        ftLk_SpecialNEnd_Phys,
        ftLk_SpecialNEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftZx_MS_SpecialAirN = 347
        ftLk_SM_SpecialAirNStart,
        ftLk_MF_SpecialAirNCharge,
        FtMoveId_SpecialN << 24,
        ftZx_SpecialAirN_Anim, /* +084C */
        ftZx_SpecialAirN_IASA, /* +088C */
        ftZx_SpecialAirN_Phys, /* +0A08 */
        ftZx_SpecialAirN_Coll, /* +0A28 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLk_MS_SpecialAirNLoop = 348: Link's row, unchanged; nothing of his enters it
        ftLk_SM_SpecialAirNLoop,
        ftLk_MF_SpecialAirNFullyCharged,
        FtMoveId_SpecialN << 24,
        ftLk_SpecialAirNLoop_Anim,
        ftLk_SpecialAirNLoop_IASA,
        ftLk_SpecialAirNLoop_Phys,
        ftLk_SpecialAirNLoop_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLk_MS_SpecialAirNEnd = 349: Link's row, unchanged; nothing of his enters it
        ftLk_SM_SpecialAirNEnd,
        ftLk_MF_SpecialAirNFire,
        FtMoveId_SpecialN << 24,
        ftLk_SpecialAirNEnd_Anim,
        ftLk_SpecialAirNEnd_IASA,
        ftLk_SpecialAirNEnd_Phys,
        ftLk_SpecialAirNEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLk_MS_SpecialS1 = 350: Link's animation callback, the rest his own
        ftLk_SM_SpecialS1,
        ftLk_MF_SpecialSThrow,
        FtMoveId_SpecialS << 24,
        ftLk_SpecialS1_Anim,
        ftZx_SpecialS_IASA, /* +0B4C */
        ftZx_SpecialS_Phys, /* +0DA4 */
        ftZx_SpecialS_Coll, /* +0E30 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLk_MS_SpecialS2 = 351: Link's animation callback, the rest his own
        ftLk_SM_SpecialS2,
        ftLk_MF_SpecialSCatch,
        FtMoveId_SpecialS << 24,
        ftLk_SpecialS2_Anim,
        ftZx_SpecialS_IASA, /* +0B4C */
        ftZx_SpecialS_Phys, /* +0DA4 */
        ftZx_SpecialS_Coll, /* +0E30 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLk_MS_SpecialS1Empty = 352: Link's animation callback, the rest his own
        ftLk_SM_SpecialS1Empty,
        ftLk_MF_SpecialSCatch,
        FtMoveId_SpecialS << 24,
        ftLk_SpecialS1Empty_Anim,
        ftZx_SpecialS_IASA, /* +0B4C */
        ftZx_SpecialS_Phys, /* +0DA4 */
        ftZx_SpecialS_Coll, /* +0E30 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftZx_MS_SpecialAirS = 353
        ftLk_SM_SpecialAirS1,
        ftLk_MF_SpecialAirSThrow,
        FtMoveId_SpecialS << 24,
        ftZx_SpecialAirS_Anim, /* +0EB0 */
        ftZx_SpecialAirS_IASA, /* +0F04 */
        ftZx_SpecialAirS_Phys, /* +1150 */
        ftZx_SpecialAirS_Coll, /* +11E8 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLk_MS_SpecialAirS2 = 354: the same four callbacks
        ftLk_SM_SpecialAirS2,
        ftLk_MF_SpecialAirSThrow,
        FtMoveId_SpecialS << 24,
        ftZx_SpecialAirS_Anim, /* +0EB0 */
        ftZx_SpecialAirS_IASA, /* +0F04 */
        ftZx_SpecialAirS_Phys, /* +1150 */
        ftZx_SpecialAirS_Coll, /* +11E8 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLk_MS_SpecialAirS1Empty = 355: the same four callbacks
        ftLk_SM_SpecialAirS1Empty,
        ftLk_MF_SpecialAirSThrowEmpty,
        FtMoveId_SpecialS << 24,
        ftZx_SpecialAirS_Anim, /* +0EB0 */
        ftZx_SpecialAirS_IASA, /* +0F04 */
        ftZx_SpecialAirS_Phys, /* +1150 */
        ftZx_SpecialAirS_Coll, /* +11E8 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLk_MS_SpecialHi = 356: Link's interrupt and collision callbacks
        ftLk_SM_SpecialHi,
        ftLk_MF_SpecialHi,
        FtMoveId_SpecialHi << 24,
        ftZx_SpecialHi_Anim, /* +1234 */
        ftLk_SpecialHi_IASA,
        ftZx_SpecialHi_Phys, /* +12B0 */
        ftLk_SpecialHi_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftZx_MS_SpecialAirHi = 357: Link's animation and collision callbacks
        ftLk_SM_SpecialAirHi,
        ftLk_MF_SpecialHi,
        FtMoveId_SpecialHi << 24,
        ftLk_SpecialAirHi_Anim,
        ftZx_SpecialAirHi_IASA, /* +12DC */
        ftZx_SpecialAirHi_Phys, /* +1308 */
        ftLk_SpecialAirHi_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLk_MS_SpecialLw = 358: Link's animation callback
        ftLk_SM_SpecialLw,
        ftLk_MF_SpecialLw,
        FtMoveId_SpecialLw << 24,
        ftLk_SpecialLw_Anim,
        NULL,
        ftZx_SpecialLw_Phys, /* +17D0 */
        ftZx_SpecialLw_Coll, /* +189C */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLk_MS_SpecialAirLw = 359: Link's animation callback
        ftLk_SM_SpecialAirLw,
        ftLk_MF_SpecialAirLw,
        FtMoveId_SpecialLw << 24,
        ftLk_SpecialAirLw_Anim,
        NULL,
        ftZx_SpecialAirLw_Phys, /* +1914 */
        ftZx_SpecialAirLw_Coll, /* +19A4 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftZx_MS_SpecialAirLwFall = 360: Link's hookshot state replaced. Same animation id
        // (312); flags 00340414 and the down special's move id in place of 00200000 and none.
        ftLk_SM_AirCatch,
        ftLk_MF_SpecialAirLw,
        FtMoveId_SpecialLw << 24,
        ftZx_SpecialAirLwFall_Anim, /* +1AEC */
        ftZx_SpecialAirLwFall_IASA, /* +1AF0 */
        ftZx_SpecialAirLwFall_Phys, /* +1AF4 */
        ftZx_SpecialAirLwFall_Coll, /* +1BBC */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLk_MS_AirCatchHit = 361: Link's row, unchanged; nothing of his enters it
        ftLk_SM_AirCatchHit,
        ftLk_MF_ZairCatch,
        FtMoveId_Default << 24,
        ftCo_AirCatchHit_Anim,
        ftCo_AirCatchHit_IASA,
        ftCo_AirCatchHit_Phys,
        ftCo_AirCatchHit_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftZx_MS_SpecialLwLand = 362: a row Link does not have; one callback
        ftZx_SM_SpecialLwLand,
        ftLk_MF_SpecialAirLw,
        FtMoveId_SpecialLw << 24,
        NULL,
        NULL,
        NULL,
        ftZx_SpecialLwLand_Coll, /* +1CF0 */
        ftCamera_UpdateCameraBox,
    },
};

/* The ten slots PlZx.dat exports. Every other slot is left empty and takes the m-ex default,
 * which MxDt gives as Link's retail function. Written and never run, so not MU_AK_READY. */
const MuAkFighter mu_ak_zero = {
    .name = "Zero",
    .file = "PlZx.dat",

    .onload = ftZx_Init_OnLoad,
    .ondeath = ftZx_Init_OnDeath,
    .specialn = ftZx_SpecialN_Enter,
    .specialairn = ftZx_SpecialAirN_Enter,
    .specialairs = ftZx_SpecialAirS_Enter,
    .onactionstatechange = ftZx_Init_OnActionStateChange,
    .onrespawn = ftZx_Init_LoadSpecialAttrs,
    .onlanding = ftZx_Init_OnLanding,

    .move_logic = ftZx_Init_MotionStateTable,
    .move_logic_count = ftZx_MS_SelfCount,

    .flags = 0,
    .articles = itZx_Articles,
    .article_count = ftZx_Article_Count,
    .gettraildata = (MuAkEvent) (void (*)(void)) ftZx_Init_GetTrailData,
};
