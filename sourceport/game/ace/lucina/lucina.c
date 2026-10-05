/* ACE's Lucina: load, per-fighter events, the state table, the sword trail and the CPU helper
 * (m-ex "ftFunction" of PlLu.dat). */
#include "lucina.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/ft/kinds/ftMars/ftmars.h>
#include <melee/ft/kinds/ftMars/ftmarsspecialhi.h>
#include <melee/ft/kinds/ftMars/ftmarsspeciallw.h>
#include <melee/ft/kinds/ftMars/ftmarsspecialn.h>
#include <melee/ft/kinds/ftMars/ftmarsspecials.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>

/* ---- CPU: m-ex's "MexCPU" helper, linked into every fighter file built with symbols ---- */

/* OnLoad passes 0x1A: while the CPU logic runs, Lucina is presented as Roy (the CPU tables are
 * indexed by fighter kind and have no row for her). m-ex keeps the kind in ".bss.mexcpu_spoof"
 * (+0AEC); it is only ever this value. */
#define ftLu_CpuSpoofKind Ft_Kind_Emblem

/* +0DCC "MexCPU_ProcSpoof": Fighter_procCpu with fp->kind swapped for the step. The step is
 * +0E54 "MexCPU_Process" with no custom table (".bss.mexcpu_data", +0AE8, is never written in
 * PlLu.dat, so +0C0C "MexCPU_ProcCustom" is never installed): ftCo_800B33B0, ftCo_800B2AFC,
 * ftCo_800B2790, ftCo_800B3E04 and the counter, which is mu_ak_cpu_process with no custom code. */
static void ftLu_Cpu_ProcSpoof(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    FighterKind kind;

    if (fp->is_sleeping || !ftCo_IsCpuControlled(fp)) {
        return;
    }
    kind = fp->kind;
    fp->kind = ftLu_CpuSpoofKind;
    mu_ak_cpu_process(gobj, NULL);
    fp->kind = kind;
}

/* +0AF0 "MexCPU_InitProc": runs every frame; when it finds the game's CPU proc on the fighter it
 * removes it and puts the spoofing proc at the same priority. It stays installed and finds
 * nothing on later frames, as on the console. */
static void ftLu_Cpu_InitProc(HSD_GObj* gobj)
{
    HSD_GObjProc* proc;

    for (proc = gobj->proc; proc != NULL; proc = proc->child) {
        if (proc->on_invoke == Fighter_procCpu) {
            HSD_GObjProc_RemoveProc(proc);
            HSD_GObj_SetupProc(gobj, ftLu_Cpu_ProcSpoof, 2);
            return;
        }
    }
}

/* +0910 "MexCPU_InitSpoofData" */
static void ftLu_Cpu_Init(HSD_GObj* gobj)
{
    HSD_GObj_SetupProc(gobj, ftLu_Cpu_InitProc, 2);
}

/* ---- ftFunction ---- */

/* +0000 "OnLoad" (slot 0). Marth's load (the attribute block of her own file, a MarsAttributes),
 * then her state table, both model part overrides cleared, her two words, and the CPU helper.
 * The table is the one Fighter_Create already took from the registry; the mod sets it again. */
static void ftLu_Init_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLu_FighterVars* fv = ftLu_Vars(fp);

    ftMs_Init_OnLoad(gobj);
    fp->x20_actionStateList = (MotionState*) ftLu_Init_MotionStateTable;
    ftParts_80074A4C(gobj, 0, 0);
    ftParts_80074A4C(gobj, 1, 0);
    fv->specialairs_lifted = 0;
    fv->specialairs_count = 0;
    ftLu_Cpu_Init(gobj);
}

/* +0088 "OnRespawn" (slot 1, the ondeath slot). Not Marth's ftMs_Init_OnDeath: his clears both
 * part overrides and his own word (fp+222C). Hers clears the first override and her two words,
 * leaves fp+222C alone, and then calls ftParts_80074A74 (a getter) for parts 0 and 1 and drops
 * the results; the two calls are kept as they are in the file. */
static void ftLu_Init_OnDeath(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLu_FighterVars* fv = ftLu_Vars(fp);

    ftParts_80074A4C(gobj, 0, 0);
    fv->specialairs_lifted = 0;
    fv->specialairs_count = 0;
    (void) ftParts_80074A74(gobj, 0);
    (void) ftParts_80074A74(gobj, 1);
}

/* +04EC "ResetAttributes" (slot 25): empty. It must stay an explicit function: an empty field
 * would take the m-ex default, Marth's ftMs_Init_LoadSpecialAttrs, which copies the attribute
 * block again. */
static void ftLu_Init_LoadSpecialAttrs(HSD_GObj* gobj)
{
    (void) gobj;
}

/* +04F0 "OnLanding" (slot 34): entered a grounded state. */
static void ftLu_Init_OnLanding(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLu_FighterVars* fv = ftLu_Vars(fp);

    fv->specialairs_lifted = 0;
    fv->specialairs_count = 0;
}

/* +0AC8 ".data.desc.0": the sword trail, in place of the block at +78 of her attributes (which
 * holds Marth's values unchanged). Console byte order, like every attribute block:
 * the afterimage code reads it through the same type as Marth's.
 *   widths 0.4 and 1.0, alpha 255 to 0, inner color C8 F4 FF, outer color 68 F4 FC,
 *   bone 75, and 1.0 and 10.0 for fp+20F8 and fp+20FC. */
static const struct SwordAttrs ftLu_Init_TrailData = {
    0.4f, 1.0f, 0xFF, 0x00, 0xC8, 0xF4, 0xFF, 0x00, 0x68, 0xF4, 0xFC,
    { 0x00, 0xFF, 0xFF }, 75, 1.0f, 10.0f,
};

/* +0504 "GetTrailData" (slot 45). The m-ex code at ftCo_800C2600+18C and ftCo_800C2FD8+C0 calls
 * it with the fighter GObj and uses the result where the retail code picks the trail block by
 * fighter kind. */
static struct SwordAttrs* ftLu_Init_GetTrailData(HSD_GObj* gobj)
{
    (void) gobj;
    return (struct SwordAttrs*) &ftLu_Init_TrailData;
}

/* +00EC "move_logic" (slot 3; a second copy sits at +0510, the one OnLoad names). Marth's table
 * word for word (animation ids, flags, move ids, callbacks) but for the 12 callbacks marked with
 * their offset, all in the aerial Dancing Blade states 358 to 366. */
const MotionState ftLu_Init_MotionStateTable[ftLu_MS_SelfCount] = {
    {
        // ftMs_MS_SpecialNStart = 341
        ftMs_SM_SpecialNStart,
        ftMs_MF_SpecialN,
        FtMoveId_SpecialN << 24,
        ftMs_SpecialNStart_Anim,
        ftMs_SpecialNStart_IASA,
        ftMs_SpecialNStart_Phys,
        ftMs_SpecialNStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialNLoop = 342
        ftMs_SM_SpecialNLoop,
        ftMs_MF_SpecialN,
        FtMoveId_SpecialN << 24,
        ftMs_SpecialNLoop_Anim,
        ftMs_SpecialNLoop_IASA,
        ftMs_SpecialNLoop_Phys,
        ftMs_SpecialNLoop_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialNEnd0 = 343
        ftMs_SM_SpecialNEnd0,
        ftMs_MF_SpecialN,
        FtMoveId_SpecialN << 24,
        ftMs_SpecialNEnd_Anim,
        ftMs_SpecialNEnd_IASA,
        ftMs_SpecialNEnd_Phys,
        ftMs_SpecialNEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialNEnd1 = 344
        ftMs_SM_SpecialNEnd1,
        ftMs_MF_SpecialN,
        FtMoveId_SpecialN << 24,
        ftMs_SpecialNEnd_Anim,
        ftMs_SpecialNEnd_IASA,
        ftMs_SpecialNEnd_Phys,
        ftMs_SpecialNEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialAirNStart = 345
        ftMs_SM_SpecialAirNStart,
        ftMs_MF_SpecialAirN,
        FtMoveId_SpecialN << 24,
        ftMs_SpecialAirNStart_Anim,
        ftMs_SpecialAirNStart_IASA,
        ftMs_SpecialAirNStart_Phys,
        ftMs_SpecialAirNStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialAirNLoop = 346
        ftMs_SM_SpecialAirNLoop,
        ftMs_MF_SpecialAirN,
        FtMoveId_SpecialN << 24,
        ftMs_SpecialAirNLoop_Anim,
        ftMs_SpecialAirNLoop_IASA,
        ftMs_SpecialAirNLoop_Phys,
        ftMs_SpecialAirNLoop_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialAirNEnd0 = 347
        ftMs_SM_SpecialAirNEnd0,
        ftMs_MF_SpecialAirN,
        FtMoveId_SpecialN << 24,
        ftMs_SpecialAirNEnd_Anim,
        ftMs_SpecialAirNEnd_IASA,
        ftMs_SpecialAirNEnd_Phys,
        ftMs_SpecialAirNEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialAirNEnd1 = 348
        ftMs_SM_SpecialAirNEnd1,
        ftMs_MF_SpecialAirN,
        FtMoveId_SpecialN << 24,
        ftMs_SpecialAirNEnd_Anim,
        ftMs_SpecialAirNEnd_IASA,
        ftMs_SpecialAirNEnd_Phys,
        ftMs_SpecialAirNEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialS1 = 349
        ftMs_SM_SpecialS1,
        ftMs_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftMs_SpecialAirS1_Anim,
        ftMs_SpecialAirS1_IASA,
        ftMs_SpecialAirS1_Phys,
        ftMs_SpecialAirS1_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialS2Hi = 350
        ftMs_SM_SpecialS2Hi,
        ftMs_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftMs_SpecialS2_Anim,
        ftMs_SpecialS2_IASA,
        ftMs_SpecialS2_Phys,
        ftMs_SpecialS2_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialS2Lw = 351
        ftMs_SM_SpecialS2Lw,
        ftMs_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftMs_SpecialS2_Anim,
        ftMs_SpecialS2_IASA,
        ftMs_SpecialS2_Phys,
        ftMs_SpecialS2_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialS3Hi = 352
        ftMs_SM_SpecialS3Hi,
        ftMs_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftMs_SpecialS3_Anim,
        ftMs_SpecialS3_IASA,
        ftMs_SpecialS3_Phys,
        ftMs_SpecialS3_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialS3S = 353
        ftMs_SM_SpecialS3S,
        ftMs_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftMs_SpecialS3_Anim,
        ftMs_SpecialS3_IASA,
        ftMs_SpecialS3_Phys,
        ftMs_SpecialS3_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialS3Lw = 354
        ftMs_SM_SpecialS3Lw,
        ftMs_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftMs_SpecialS3_Anim,
        ftMs_SpecialS3_IASA,
        ftMs_SpecialS3_Phys,
        ftMs_SpecialS3_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialS4Hi = 355
        ftMs_SM_SpecialS4Hi,
        ftMs_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftMs_SpecialS4_Anim,
        ftMs_SpecialS4_IASA,
        ftMs_SpecialS4_Phys,
        ftMs_SpecialS4_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialS4S = 356
        ftMs_SM_SpecialS4S,
        ftMs_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftMs_SpecialS4_Anim,
        ftMs_SpecialS4_IASA,
        ftMs_SpecialS4_Phys,
        ftMs_SpecialS4_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialS4Lw = 357
        ftMs_SM_SpecialS4Lw,
        ftMs_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftMs_SpecialS4_Anim,
        ftMs_SpecialS4_IASA,
        ftMs_SpecialS4_Phys,
        ftMs_SpecialS4_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialAirS1 = 358
        ftMs_SM_SpecialAirS1,
        ftMs_MF_SpecialS1,
        FtMoveId_SpecialS << 24,
        ftMs_SpecialAirS1_Anim,
        ftMs_SpecialAirS1_IASA,
        ftLu_SpecialAirS1_Phys,   /* +0954 */
        ftLu_SpecialAirS_Coll,    /* +0A48 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialAirS2Hi = 359
        ftMs_SM_SpecialAirS2Hi,
        ftMs_MF_SpecialS1,
        FtMoveId_SpecialS << 24,
        ftMs_SpecialS2_Anim,
        ftMs_SpecialS2_IASA,
        ftLu_SpecialAirS2_Phys,   /* +0A70 */
        ftLu_SpecialAirS_Coll,    /* +0A48 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialAirS2Lw = 360
        ftMs_SM_SpecialAirS2Lw,
        ftMs_MF_SpecialS1,
        FtMoveId_SpecialS << 24,
        ftMs_SpecialS2_Anim,
        ftMs_SpecialS2_IASA,
        ftLu_SpecialAirS2_Phys,   /* +0A70 */
        ftLu_SpecialAirS_Coll,    /* +0A48 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialAirS3Hi = 361
        ftMs_SM_SpecialAirS3Hi,
        ftMs_MF_SpecialS1,
        FtMoveId_SpecialS << 24,
        ftMs_SpecialS3_Anim,
        ftMs_SpecialS3_IASA,
        ftMs_SpecialS3_Phys,
        ftLu_SpecialAirS_Coll,    /* +0A48 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialAirS3S = 362
        ftMs_SM_SpecialAirS3S,
        ftMs_MF_SpecialS1,
        FtMoveId_SpecialS << 24,
        ftMs_SpecialS3_Anim,
        ftMs_SpecialS3_IASA,
        ftMs_SpecialS3_Phys,
        ftLu_SpecialAirS_Coll,    /* +0A48 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialAirS3Lw = 363
        ftMs_SM_SpecialAirS3Lw,
        ftMs_MF_SpecialS1,
        FtMoveId_SpecialS << 24,
        ftMs_SpecialS3_Anim,
        ftMs_SpecialS3_IASA,
        ftMs_SpecialS3_Phys,
        ftLu_SpecialAirS_Coll,    /* +0A48 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialAirS4Hi = 364
        ftMs_SM_SpecialAirS4Hi,
        ftMs_MF_SpecialS1,
        FtMoveId_SpecialS << 24,
        ftMs_SpecialS4_Anim,
        ftMs_SpecialS4_IASA,
        ftMs_SpecialS4_Phys,
        ftLu_SpecialAirS_Coll,    /* +0A48 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialAirS4S = 365
        ftMs_SM_SpecialAirS4S,
        ftMs_MF_SpecialS1,
        FtMoveId_SpecialS << 24,
        ftMs_SpecialS4_Anim,
        ftMs_SpecialS4_IASA,
        ftMs_SpecialS4_Phys,
        ftLu_SpecialAirS_Coll,    /* +0A48 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialAirS4Lw = 366
        ftMs_SM_SpecialAirS4Lw,
        ftMs_MF_SpecialS1,
        FtMoveId_SpecialS << 24,
        ftMs_SpecialS4_Anim,
        ftMs_SpecialS4_IASA,
        ftMs_SpecialS4_Phys,
        ftLu_SpecialAirS_Coll,    /* +0A48 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialHi = 367
        ftMs_SM_SpecialHi,
        ftMs_MF_SpecialHi,
        FtMoveId_SpecialHi << 24,
        ftMs_SpecialHi_Anim,
        ftMs_SpecialHi_IASA,
        ftMs_SpecialHi_Phys,
        ftMs_SpecialHi_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialAirHi = 368
        ftMs_SM_SpecialAirHi,
        ftMs_MF_SpecialAirHi,
        FtMoveId_SpecialHi << 24,
        ftMs_SpecialAirHi_Anim,
        ftMs_SpecialAirHi_IASA,
        ftMs_SpecialAirHi_Phys,
        ftMs_SpecialAirHi_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialLw = 369
        ftMs_SM_SpecialLw,
        ftMs_MF_SpecialLw,
        FtMoveId_SpecialLw << 24,
        ftMs_SpecialLw_Anim,
        ftMs_SpecialLw_IASA,
        ftMs_SpecialLw_Phys,
        ftMs_SpecialLw_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialLwHit = 370
        ftMs_SM_SpecialLwHit,
        ftMs_MF_SpecialLw,
        FtMoveId_SpecialLw << 24,
        ftMs_SpecialLwHit_Anim,
        ftMs_SpecialLwHit_IASA,
        ftMs_SpecialLwHit_Phys,
        ftMs_SpecialLwHit_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialAirLw = 371
        ftMs_SM_SpecialAirLw,
        ftMs_MF_SpecialAirLw,
        FtMoveId_SpecialLw << 24,
        ftMs_SpecialAirLw_Anim,
        ftMs_SpecialAirLw_IASA,
        ftMs_SpecialAirLw_Phys,
        ftMs_SpecialAirLw_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMs_MS_SpecialAirLwHit = 372
        ftMs_SM_SpecialAirLwHit,
        ftMs_MF_SpecialAirLw,
        FtMoveId_SpecialLw << 24,
        ftMs_SpecialAirLwHit_Anim,
        ftMs_SpecialAirLwHit_IASA,
        ftMs_SpecialAirLwHit_Phys,
        ftMs_SpecialAirLwHit_Coll,
        ftCamera_UpdateCameraBox,
    },
};

_Static_assert(sizeof(struct SwordAttrs) == 0x20, "the trail block is 32 bytes");

/* The six slots PlLu.dat exports. Every other slot is left empty and takes the m-ex default,
 * which MxDt gives as Marth's retail function: the eight specials, the item events, the two
 * knockback events and the common double jump. Written and never run, so not MU_AK_READY. */
const MuAkFighter mu_ak_lucina = {
    .name = "Lucina",
    .file = "PlLu.dat",
    .onload = ftLu_Init_OnLoad,
    .ondeath = ftLu_Init_OnDeath,
    .onrespawn = ftLu_Init_LoadSpecialAttrs,
    .onlanding = ftLu_Init_OnLanding,
    .move_logic = ftLu_Init_MotionStateTable,
    .move_logic_count = ftLu_MS_SelfCount,
    .flags = 0,
    .gettraildata = (MuAkEvent) (void (*)(void)) ftLu_Init_GetTrailData,
};
