/* ACE's Metal Mario: load, the CPU setup and the move table (m-ex "ftFunction" of PlMM.dat).
 *
 * PlMM.dat exports six ftFunction slots: 0 onload, 3 move_logic, 4 specialn, 5 specialairn,
 * 8 specialhi, 9 specialairhi. Every other slot is Mario's function in MxDt (ondeath, the side
 * and down specials, the item and knockback events, the attribute reload, the common double
 * jump, the two demo hooks and the demo move table), which the registry fills from Mario's rows
 * when a field here is NULL. */
#include "metalmario.h"

#include <dolphin/os.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/ft/kinds/ftMario/ftmario.h>
#include <melee/ft/kinds/ftMario/ftmariospecialhi.h>
#include <melee/ft/kinds/ftMario/ftmariospeciallw.h>
#include <melee/ft/kinds/ftMario/ftmariospecialn.h>
#include <melee/ft/kinds/ftMario/ftmariospecials.h>
#include <melee/ft/kinds/ftMario/types.h>
#include <melee/ft/types.h>
#include <melee/it/it_26B1.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>

/* ---- CPU players --------------------------------------------------------------------------------
 * The file carries the m-ex "MexCPU" helper with no custom data: its data pointer (code+0x40C)
 * is never written (its relocations, at code+0x64, +0x458, +0xC1C, +0xF2C and +0xF98, are all
 * loads: run-source/rel09-ace-native/metalmario/data.txt), so the branch with its own attack
 * tables (code+0xBCC) and the decision call inside the step (code+0xF84) are dead code and are
 * not ported. What runs: on the first frame the fighter's Fighter_procCpu proc is swapped for
 * one that poses as Mario (m-ex internal 0, the value onload stores at code+0x4AC) while the
 * game's CPU logic runs, so a CPU Metal Mario plays from Mario's tables. The native service for
 * the step is mu_ak_cpu_process (the game's step without ftCo_800B0AF4, as the disc's copy at
 * code+0xF08). */
#define FTMM_CPU_SPOOF_KIND Ft_Kind_Mario

_Static_assert(FTMM_CPU_SPOOF_KIND == 0, "the kind PlMM.dat stores at code+0x4AC");

/* code+0xD84 (and its tail at +0xDD0), MexCPU_ProcSpoof (proc, priority 2). */
static void ftMM_MexCPU_ProcSpoof(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    FighterKind saved_kind;

    if (fp->is_sleeping || !ftCo_IsCpuControlled(fp)) {
        return;
    }
    saved_kind = fp->kind;
    fp->kind = FTMM_CPU_SPOOF_KIND;
    mu_ak_cpu_process(gobj, NULL);
    fp->kind = saved_kind;
}

/* code+0x410, MexCPU_InitProc (proc, priority 2): swap the fighter's Fighter_procCpu proc for
 * ours. It stays installed and finds nothing to do after the first frame, as on the console. */
static void ftMM_MexCPU_InitProc(Fighter_GObj* gobj)
{
    HSD_GObjProc* proc;

    for (proc = gobj->proc; proc != NULL; proc = proc->child) {
        if (proc->on_invoke == Fighter_procCpu) {
            HSD_GObjProc_RemoveProc(proc);
            HSD_GObj_SetupProc(gobj, ftMM_MexCPU_ProcSpoof, 2);
            return;
        }
    }
}

/* ---- load ---------------------------------------------------------------------------------------
 * code+0x0 (and its tail at +0x90), [onload]. Retail ftMr_Init_OnLoad with these differences:
 * the two articles are handed to the item code by index (m-ex MEX_IndexFighterItem at
 * 0x803D7058; Mario calls it_8026B3F8 with It_Kind_Mario_Fire and his cape kind), the wall jump
 * bit is not set, and the CPU setup above follows. The file copies 0x8C bytes of attributes
 * where Mario's block is 0x84: the two extra words are never read. */
static void ftMM_Init_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    DISC_PTR(void)* items = DP(fp->ft_data->x48_items);

    PUSH_ATTRS(fp, ftMario_DatAttrs);

    mu_ak_register_article(fp->kind, DP(items[ftMM_Article_Fire]), ftMM_Article_Fire);
    mu_ak_register_article(fp->kind, DP(items[ftMM_Article_Cape]), ftMM_Article_Cape);

    /* Two native additions, neither in the file. See NOTES.md, "Native additions".
     *
     * 1. His side special is Mario's retail code, which creates the item kind his attribute
     *    block names: 0x53, the retail cape (the same value as in PlMr.dat), not his article
     *    kind 327. That kind's article is only ever registered by Mario's own load. In a match
     *    with no Mario the slot is empty natively and the cape would be a null read, so his
     *    article 2 is registered for it here, exactly as ftMr_Init_OnLoad does. */
    {
        ftMario_DatAttrs* sa = fp->dat_attrs;

        it_8026B3F8(DP(items[ftMM_Article_Cape]), sa->specials.cape_kind);
    }
    /* 2. On the console the wall jump is decided by MxDt's byte for the fighter (the m-ex code
     *    at ftWallJump_8008169C+28 reads it in place of fp->can_walljump). The native check
     *    still reads the bit, so the byte is copied into it here. MxDt gives him 1. */
    if (mu_mex_fighter_walljump(mu_ak_mex_internal(fp->kind)) > 0) {
        fp->can_walljump = true;
    }

    HSD_GObj_SetupProc(gobj, ftMM_MexCPU_InitProc, 2);

    {
        /* Bring-up evidence (once). */
        static int logged;
        if (!logged) {
            ftMario_DatAttrs* sa = fp->dat_attrs;

            logged = 1;
            OSReport("[ak] Metal Mario load: fire article kind %d, cape article kind %d, retail "
                     "cape kind %d, wall jump %d\n",
                     mu_ak_item_kind(fp->kind, ftMM_Article_Fire),
                     mu_ak_item_kind(fp->kind, ftMM_Article_Cape),
                     (int) sa->specials.cape_kind, (int) fp->can_walljump);
        }
    }
}

/* code+0xC4, [move_logic]: 11 rows. Rows 0 to 9 compared word for word with retail
 * ftMr_Init_MotionStateTable (main.dol 0x803C7120, tables.txt): the animation ids, flags and
 * move ids are Mario's in every row; the callbacks are Mario's but for the ones marked with
 * their offset. The animation, IASA and physics callbacks of 343 and 344 and the physics
 * callbacks of 347 and 348 are own code in the file that makes the same calls as Mario's, so
 * Mario's functions are named. Row 10 is his own state. */
const MotionState ftMM_Init_MotionStateTable[ftMM_MS_SelfCount] = {
    {
        // ftMr_MS_AppealSR = 341
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
        // ftMr_MS_AppealSL = 342
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
        // ftMr_MS_SpecialN = 343
        ftMr_SM_SpecialN,
        ftMr_MF_SpecialN,
        FtMoveId_SpecialN << 24,
        ftMr_SpecialN_Anim,
        ftMr_SpecialN_IASA,
        ftMr_SpecialN_Phys,
        ftMM_SpecialN_Coll,      /* code+0x514 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMr_MS_SpecialAirN = 344
        ftMr_SM_SpecialAirN,
        ftMr_MF_SpecialAirN,
        FtMoveId_SpecialN << 24,
        ftMr_SpecialAirN_Anim,
        ftMr_SpecialAirN_IASA,
        ftMr_SpecialAirN_Phys,
        ftMM_SpecialAirN_Coll,   /* code+0x608 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMr_MS_SpecialS = 345
        ftMr_SM_SpecialS,
        ftMr_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftMr_SpecialS_Anim,
        ftMr_SpecialS_IASA,
        ftMr_SpecialS_Phys,
        ftMr_SpecialS_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMr_MS_SpecialAirS = 346
        ftMr_SM_SpecialAirS,
        ftMr_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftMr_SpecialAirS_Anim,
        ftMr_SpecialAirS_IASA,
        ftMr_SpecialAirS_Phys,
        ftMr_SpecialAirS_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMr_MS_SpecialHi = 347
        ftMr_SM_SpecialHi,
        ftMr_MF_SpecialHi,
        FtMoveId_SpecialHi << 24,
        ftMM_SpecialHi_Anim,     /* code+0x698 */
        ftMM_SpecialHi_IASA,     /* code+0x758 */
        ftMr_SpecialHi_Phys,
        ftMM_SpecialHi_Coll,     /* code+0x890 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMr_MS_SpecialAirHi = 348
        ftMr_SM_SpecialAirHi,
        ftMr_MF_SpecialAirHi,
        FtMoveId_SpecialHi << 24,
        ftMM_SpecialHi_Anim,     /* code+0x8E8, the same body as +0x698 */
        ftMM_SpecialHi_IASA,     /* code+0x9A8, a jump to +0x758 */
        ftMr_SpecialAirHi_Phys,
        ftMM_SpecialHi_Coll,     /* code+0xA40, the same body as +0x890 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMr_MS_SpecialLw = 349
        ftMr_SM_SpecialLw,
        ftMr_MF_SpecialLw,
        FtMoveId_SpecialLw << 24,
        ftMr_SpecialLw_Anim,
        ftMr_SpecialLw_IASA,
        ftMr_SpecialLw_Phys,
        ftMr_SpecialLw_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMr_MS_SpecialAirLw = 350
        ftMr_SM_SpecialAirLw,
        ftMr_MF_SpecialAirLw,
        FtMoveId_SpecialLw << 24,
        ftMr_SpecialAirLw_Anim,
        ftMr_SpecialAirLw_IASA,
        ftMr_SpecialAirLw_Phys,
        ftMr_SpecialAirLw_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMM_MS_SpecialHiDive = 351: his own
        ftMM_SM_SpecialHiDive,
        ftMr_MF_SpecialAirHi,
        FtMoveId_SpecialHi << 24,
        ftMM_SpecialHi_Anim,     /* code+0x698 */
        ftMM_SpecialHi_IASA,     /* code+0x758 */
        ftMM_SpecialHiDive_Phys, /* code+0xA98 */
        ftMM_SpecialHi_Coll,     /* code+0x890 */
        ftCamera_UpdateCameraBox,
    },
};

/* Every slot not named here is NULL: the registry then takes MxDt's default, which is Mario's
 * function for all of them (run-source/rel09-ace-native/metalmario/mxdt.txt). Nothing has run
 * yet, so he is not marked MU_AK_READY. */
const MuAkFighter mu_ak_metalmario = {
    .name = "Metal Mario",
    .file = "PlMM.dat",
    .onload = ftMM_Init_OnLoad,
    .specialn = ftMM_SpecialN_Enter,
    .specialairn = ftMM_SpecialAirN_Enter,
    .specialhi = ftMM_SpecialHi_Enter,
    .specialairhi = ftMM_SpecialAirHi_Enter,
    .move_logic = ftMM_Init_MotionStateTable,
    .move_logic_count = ftMM_MS_SelfCount,
    .flags = 0,
    .articles = itMM_Articles,
    .article_count = ftMM_Article_Count,
};
