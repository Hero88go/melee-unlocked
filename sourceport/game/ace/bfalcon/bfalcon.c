/* ACE's B. Falcon: load, per-fighter events, the state table and the CPU helper (m-ex
 * "ftFunction" of PlBF.dat). */
#include "bfalcon.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_0CD1.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcolanim.h>
#include <melee/ft/ftlipstickswing.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/ftstarrodswing.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCaptain/ftcaptain.h>
#include <melee/ft/kinds/ftCaptain/ftcaptainspecialhi.h>
#include <melee/ft/kinds/ftCaptain/ftcaptainspeciallw.h>
#include <melee/ft/kinds/ftCaptain/ftcaptainspecialn.h>
#include <melee/ft/kinds/ftCaptain/ftcaptainspecials.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>

/* ---- CPU: m-ex's "MexCPU" helper, linked into every fighter file built with symbols ---- */

/* OnLoad passes 2: while the CPU logic runs, B. Falcon is presented as Captain Falcon (the CPU
 * tables are indexed by fighter kind and have no row for him). m-ex keeps the kind in
 * ".bss.mexcpu_spoof" (+0FC0); it is only ever this value. Retail internal ids are the native
 * kinds. */
#define ftBf_CpuSpoofKind Ft_Kind_Captain

_Static_assert(ftBf_CpuSpoofKind == 2, "the kind PlBF.dat passes to MexCPU_InitSpoofData");

/* +12BC "MexCPU_ProcSpoof": Fighter_procCpu with fp->kind swapped for the step. The step is
 * +1344 "MexCPU_Process" with no custom table (".bss.mexcpu_data", +0FBC, is never written in
 * PlBF.dat: its five relocations, at +046C, +1014, +11D0, +1368 and +13D4, are all loads, so
 * +10FC "MexCPU_ProcCustom" is never installed and the custom branch at +13C0 is never taken):
 * ftCo_800B33B0, ftCo_800B2AFC, ftCo_800B2790, ftCo_800B3E04 and the counter at fp+1B04, which
 * is mu_ak_cpu_process with no custom code. */
static void ftBf_Cpu_ProcSpoof(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    FighterKind kind;

    if (fp->is_sleeping || !ftCo_IsCpuControlled(fp)) {
        return;
    }
    kind = fp->kind;
    fp->kind = ftBf_CpuSpoofKind;
    mu_ak_cpu_process(gobj, NULL);
    fp->kind = kind;
}

/* +0FC4 "MexCPU_InitProc": runs every frame; when it finds the game's CPU proc on the fighter it
 * removes it and puts the spoofing proc at the same priority. It stays installed and finds
 * nothing on later frames, as on the console. */
static void ftBf_Cpu_InitProc(HSD_GObj* gobj)
{
    HSD_GObjProc* proc;

    for (proc = gobj->proc; proc != NULL; proc = proc->child) {
        if (proc->on_invoke == Fighter_procCpu) {
            HSD_GObjProc_RemoveProc(proc);
            HSD_GObj_SetupProc(gobj, ftBf_Cpu_ProcSpoof, 2);
            return;
        }
    }
}

/* +046C "MexCPU_InitSpoofData" */
static void ftBf_Cpu_Init(HSD_GObj* gobj)
{
    HSD_GObj_SetupProc(gobj, ftBf_Cpu_InitProc, 2);
}

/* ---- ftFunction ---- */

/* +0000 "OnLoad" (slot 0). Captain Falcon's load (wall jump, and the attribute block of his own
 * file, a ftCaptain_DatAttrs), then his two words and the CPU helper. Unlike Lucina's, this one
 * does not set the state table again: Fighter_Create took it from the registry. */
static void ftBf_Init_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftBf_FighterVars* fv = ftBf_Vars(fp);

    ftCa_Init_OnLoad(gobj);
    fv->xCC = 0;
    fv->charge = 0;
    ftBf_Cpu_Init(gobj);
}

/* +005C "OnRespawn" (slot 1, the ondeath slot). Not Captain Falcon's ftCa_Init_OnDeath: his
 * clears the part override and his two side special words (fp+222C, fp+2230). This one clears
 * B. Falcon's two words and the part override, and leaves Captain Falcon's two alone. */
static void ftBf_Init_OnDeath(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftBf_FighterVars* fv = ftBf_Vars(fp);

    fv->xCC = 0;
    fv->charge = 0;
    ftParts_80074A4C(gobj, 0, 0);
}

/* +0434 "OnFrame" (slot 23; Captain Falcon has none). While a charge over 1 is kept, the charged
 * color overlay is asked for every frame. */
static void ftBf_Init_OnFrame(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftBf_Vars(fp)->charge > 1) {
        ftCo_800BFFD0(fp, ftBf_ColAnim_Charged, false);
    }
}

/* +0094 "move_logic" (slot 3). Rows 0 to 22 are Captain Falcon's table word for word (animation
 * ids, flags, move ids, callbacks; compared with main.dol in tables.txt) but for the seven
 * callbacks marked with their offset. Rows 23 to 28 are his own: the charge states of the
 * neutral special, with Captain Falcon's neutral special physics. */
const MotionState ftBf_Init_MotionStateTable[ftBf_MS_SelfCount] = {
    {
        // ftCa_MS_SwordSwing4 = 341
        ftCa_SM_SwordSwing4,
        ftCo_MF_SwordSwing4,
        FtMoveId_SwordSwing4 << 24,
        ftCo_SwordSwing_Anim,
        ftCo_SwordSwing_IASA,
        ftCo_SwordSwing_Phys,
        ftCo_SwordSwing_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCa_MS_BatSwing4 = 342
        ftCa_SM_BatSwing4,
        ftCo_MF_BatSwing4,
        FtMoveId_BatSwing4 << 24,
        ftCo_BatSwing_Anim,
        ftCo_BatSwing_IASA,
        ftCo_BatSwing_Phys,
        ftCo_BatSwing_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCa_MS_ParasolSwing4 = 343
        ftCa_SM_ParasolSwing4,
        ftCo_MF_ParasolSwing4,
        FtMoveId_ParasolSwing4 << 24,
        ftCo_ParasolSwing_Anim,
        ftCo_ParasolSwing_IASA,
        ftCo_ParasolSwing_Phys,
        ftCo_ParasolSwing_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCa_MS_HarisenSwing4 = 344
        ftCa_SM_HarisenSwing4,
        ftCo_MF_HarisenSwing4,
        FtMoveId_HarisenSwing4 << 24,
        ftCo_HarisenSwing_Anim,
        ftCo_HarisenSwing_IASA,
        ftCo_HarisenSwing_Phys,
        ftCo_HarisenSwing_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCa_MS_StarRodSwing4 = 345
        ftCa_SM_StarRodSwing4,
        ftCo_MF_StarRodSwing4,
        FtMoveId_StarRodSwing4 << 24,
        ftCo_StarRodSwing_Anim,
        ftCo_StarRodSwing_IASA,
        ftCo_StarRodSwing_Phys,
        ftCo_StarRodSwing_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCa_MS_LipstickSwing4 = 346
        ftCa_SM_LipstickSwing4,
        ftCo_MF_LipstickSwing4,
        FtMoveId_LipstickSwing4 << 24,
        ftCo_LipstickSwing_Anim,
        ftCo_LipstickSwing_IASA,
        ftCo_LipstickSwing_Phys,
        ftCo_LipstickSwing_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCa_MS_SpecialN = 347
        ftCa_SM_SpecialN,
        ftCa_MF_SpecialN,
        FtMoveId_SpecialN << 24,
        ftCa_SpecialN_Anim,
        ftBf_SpecialN_IASA,           /* +04B0 */
        ftCa_SpecialN_Phys,
        ftCa_SpecialN_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCa_MS_SpecialAirN = 348
        ftCa_SM_SpecialAirN,
        ftCa_MF_SpecialAirN,
        FtMoveId_SpecialN << 24,
        ftCa_SpecialAirN_Anim,
        ftBf_SpecialAirN_IASA,        /* +088C */
        ftCa_SpecialAirN_Phys,
        ftCa_SpecialAirN_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCa_MS_SpecialSStart = 349
        ftCa_SM_SpecialSStart,
        ftCa_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftCa_SpecialSStart_Anim,
        ftCa_SpecialSStart_IASA,
        ftCa_SpecialSStart_Phys,
        ftCa_SpecialSStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCa_MS_SpecialS = 350
        ftCa_SM_SpecialS,
        ftCa_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftCa_SpecialS_Anim,
        ftCa_SpecialS_IASA,
        ftCa_SpecialS_Phys,
        ftCa_SpecialS_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCa_MS_SpecialAirSStart = 351
        ftCa_SM_SpecialAirSStart,
        ftCa_MF_SpecialAirSStart,
        FtMoveId_SpecialS << 24,
        ftCa_SpecialAirSStart_Anim,
        ftBf_SpecialAirSStart_IASA,   /* +0C68 */
        ftCa_SpecialAirSStart_Phys,
        ftCa_SpecialAirSStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCa_MS_SpecialAirS = 352
        ftCa_SM_SpecialAirS,
        ftCa_MF_SpecialAirS,
        FtMoveId_SpecialS << 24,
        ftCa_SpecialAirS_Anim,
        ftBf_SpecialAirS_IASA,        /* +0CD0 */
        ftCa_SpecialAirS_Phys,
        ftCa_SpecialAirS_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCa_MS_SpecialHi = 353
        ftCa_SM_SpecialHi,
        ftCa_MF_SpecialHi,
        FtMoveId_SpecialHi << 24,
        ftCa_SpecialHi_Anim,
        ftCa_SpecialHi_IASA,
        ftCa_SpecialHi_Phys,
        ftCa_SpecialHi_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCa_MS_SpecialAirHi = 354
        ftCa_SM_SpecialAirHi,
        ftCa_MF_SpecialAirHi,
        FtMoveId_SpecialHi << 24,
        ftCa_SpecialAirHi_Anim,
        ftCa_SpecialAirHi_IASA,
        ftCa_SpecialAirHi_Phys,
        ftCa_SpecialAirHi_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCa_MS_SpecialHiCatch = 355
        ftCa_SM_SpecialHiCatch,
        ftCa_MF_SpecialHi,
        FtMoveId_SpecialHi << 24,
        ftCa_SpecialHiCatch_Anim,
        ftCa_SpecialHiCatch_IASA,
        ftCa_SpecialHiCatch_Phys,
        ftCa_SpecialHiCatch_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCa_MS_SpecialHiThrow = 356
        ftCa_SM_SpecialHiThrow0,
        ftCa_MF_SpecialHi,
        FtMoveId_SpecialHi << 24,
        ftCa_SpecialHiThrow0_Anim,
        ftCa_SpecialHiThrow0_IASA,
        ftCa_SpecialHiThrow0_Phys,
        ftCa_SpecialHiThrow0_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCa_MS_SpecialLw = 357
        ftCa_SM_SpecialLw,
        ftCa_MF_SpecialLw,
        FtMoveId_SpecialLw << 24,
        ftCa_SpecialLw_Anim,
        ftBf_SpecialLw_IASA,          /* +0D38 */
        ftCa_SpecialLw_Phys,
        ftCa_SpecialLw_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCa_MS_SpecialLwEnd = 358
        ftCa_SM_SpecialLwEnd,
        ftCa_MF_SpecialLw,
        FtMoveId_SpecialLw << 24,
        ftCa_SpecialLwEnd_Anim,
        NULL,
        ftCa_SpecialLwEnd_Phys,
        ftCa_SpecialLwEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCa_MS_SpecialAirLw = 359
        ftCa_SM_SpecialAirLw,
        ftCa_MF_SpecialLwRebound,
        FtMoveId_SpecialLw << 24,
        ftCa_SpecialAirLw_Anim,
        ftBf_SpecialLw_IASA,          /* +0D38 */
        ftCa_SpecialAirLw_Phys,
        ftCa_SpecialAirLw_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCa_MS_SpecialAirLwEnd = 360
        ftCa_SM_SpecialAirLwEnd,
        ftCa_MF_SpecialLwRebound,
        FtMoveId_SpecialLw << 24,
        ftCa_SpecialAirLwEnd_Anim,
        NULL,
        ftCa_SpecialAirLwEnd_Phys,
        ftCa_SpecialAirLwEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCa_MS_SpecialAirLwEndAir = 361
        ftCa_SM_SpecialAirLwEndAir,
        ftCa_MF_SpecialLwRebound,
        FtMoveId_SpecialLw << 24,
        ftCa_SpecialAirLwEndAir_Anim,
        NULL,
        ftCa_SpecialAirLwEndAir_Phys,
        ftCa_SpecialAirLwEndAir_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCa_MS_SpecialLwEndAir = 362
        ftCa_SM_SpecialLwEndAir,
        ftCa_MF_SpecialLw,
        FtMoveId_SpecialLw << 24,
        ftCa_SpecialLwEndAir_Anim,
        NULL,
        ftCa_SpecialLwEndAir_Phys,
        ftCa_SpecialLwEndAir_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCa_MS_SpecialHiThrow1 = 363
        ftCa_SM_SpecialHiThrow1,
        ftCa_MF_SpecialLwRebound,
        FtMoveId_SpecialLw << 24,
        ftCa_SpecialHiThrow1_Anim,
        NULL,
        ftCa_SpecialHiThrow1_Phys,
        ftBf_SpecialHiThrow1_Coll,    /* +0D4C */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftBf_MS_SpecialNCharge2 = 364
        ftBf_SM_SpecialNCharge2,
        ftCa_MF_SpecialN,
        FtMoveId_SpecialN << 24,
        ftBf_SpecialNCharge_Anim,     /* +0DC0 */
        NULL,
        ftCa_SpecialN_Phys,
        ftBf_SpecialNCharge_Coll,     /* +0E18 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftBf_MS_SpecialAirNCharge2 = 365
        ftBf_SM_SpecialAirNCharge2,
        ftCa_MF_SpecialAirN,
        FtMoveId_SpecialN << 24,
        ftBf_SpecialAirNCharge_Anim,  /* +0E58 */
        NULL,
        ftCa_SpecialAirN_Phys,
        ftBf_SpecialAirNCharge2_Coll, /* +0ECC */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftBf_MS_SpecialNCharge0 = 366
        ftBf_SM_SpecialNCharge0,
        ftCa_MF_SpecialN,
        FtMoveId_SpecialN << 24,
        ftBf_SpecialNCharge_Anim,     /* +0DC0 */
        NULL,
        ftCa_SpecialN_Phys,
        ftBf_SpecialNCharge_Coll,     /* +0E18 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftBf_MS_SpecialAirNCharge0 = 367
        ftBf_SM_SpecialAirNCharge0,
        ftCa_MF_SpecialAirN,
        FtMoveId_SpecialN << 24,
        ftBf_SpecialAirNCharge_Anim,  /* +0E58 */
        NULL,
        ftCa_SpecialAirN_Phys,
        ftBf_SpecialAirNCharge0_Coll, /* +0F44 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftBf_MS_SpecialNCharge1 = 368
        ftBf_SM_SpecialNCharge1,
        ftCa_MF_SpecialN,
        FtMoveId_SpecialN << 24,
        ftBf_SpecialNCharge_Anim,     /* +0DC0 */
        NULL,
        ftCa_SpecialN_Phys,
        ftBf_SpecialNCharge_Coll,     /* +0E18 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftBf_MS_SpecialAirNCharge1 = 369
        ftBf_SM_SpecialAirNCharge1,
        ftCa_MF_SpecialAirN,
        FtMoveId_SpecialN << 24,
        ftBf_SpecialAirNCharge_Anim,  /* +0E58 */
        NULL,
        ftCa_SpecialAirN_Phys,
        ftBf_SpecialAirNCharge0_Coll, /* +0F44 */
        ftCamera_UpdateCameraBox,
    },
};

/* The four slots PlBF.dat exports. Every other slot is left empty and takes the m-ex default,
 * which MxDt gives as Captain Falcon's retail function (internal 2; Ganondorf, internal 25,
 * shares only the eight special entries): the eight specials, the item events, the attribute
 * reload and the common double jump. Written and never run, so not MU_AK_READY. */
const MuAkFighter mu_ak_bfalcon = {
    .name = "B. Falcon",
    .file = "PlBF.dat",
    .onload = ftBf_Init_OnLoad,
    .ondeath = ftBf_Init_OnDeath,
    .onframe = ftBf_Init_OnFrame,
    .move_logic = ftBf_Init_MotionStateTable,
    .move_logic_count = ftBf_MS_SelfCount,
    .flags = 0,
};
