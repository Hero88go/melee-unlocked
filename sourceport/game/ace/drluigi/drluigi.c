/* ACE's Dr. Luigi: load, the CPU setup and the move table (m-ex "ftFunction" of PlDl.dat).
 *
 * PlDl.dat exports four ftFunction slots: 0 onload, 3 move_logic, 4 specialn, 5 specialairn.
 * Every other slot is Luigi's function in MxDt (ondeath, the six other specials, the item and
 * knockback events, the attribute reload, the common double jump, the two demo hooks and the
 * demo move table), which the registry fills from Luigi's rows when a field here is NULL. */
#include "drluigi.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/ft/kinds/ftLuigi/ftluigi.h>
#include <melee/ft/kinds/ftLuigi/ftluigispecialhi.h>
#include <melee/ft/kinds/ftLuigi/ftluigispeciallw.h>
#include <melee/ft/kinds/ftLuigi/ftluigispecialn.h>
#include <melee/ft/kinds/ftLuigi/ftluigispecials.h>
#include <melee/ft/kinds/ftLuigi/types.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>

/* ---- CPU players --------------------------------------------------------------------------------
 * The file carries the m-ex "MexCPU" helper with no custom data: its data pointer (code+0x434)
 * is never written, so the branch with its own attack tables (code+0x69C) and the decision call
 * inside the step (code+0xA38) are dead code and are not ported. What runs: on the first frame
 * the fighter's Fighter_procCpu proc is swapped for one that poses as Luigi (m-ex internal 17,
 * the value onload stores at code+0x4D4) while the game's CPU logic runs, so a CPU Dr. Luigi
 * plays from Luigi's tables. The native service for the step is mu_ak_cpu_process (the game's
 * step without ftCo_800B0AF4, as the disc's copy at code+0x9BC). */
#define FTDL_CPU_SPOOF_KIND Ft_Kind_Luigi

/* code+0x854 (and its tail at +0x8A0), MexCPU_ProcSpoof (proc, priority 2). */
static void ftDl_MexCPU_ProcSpoof(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    FighterKind saved_kind;

    if (fp->is_sleeping || !ftCo_IsCpuControlled(fp)) {
        return;
    }
    saved_kind = fp->kind;
    fp->kind = FTDL_CPU_SPOOF_KIND;
    mu_ak_cpu_process(gobj, NULL);
    fp->kind = saved_kind;
}

/* code+0x438, MexCPU_InitProc (proc, priority 2): swap the fighter's Fighter_procCpu proc for
 * ours. It stays installed and finds nothing to do after the first frame, as on the console. */
static void ftDl_MexCPU_InitProc(Fighter_GObj* gobj)
{
    HSD_GObjProc* proc;

    for (proc = gobj->proc; proc != NULL; proc = proc->child) {
        if (proc->on_invoke == Fighter_procCpu) {
            HSD_GObjProc_RemoveProc(proc);
            HSD_GObj_SetupProc(gobj, ftDl_MexCPU_ProcSpoof, 2);
            return;
        }
    }
}

/* ---- load ---------------------------------------------------------------------------------------
 * ftData->x48_items: an array of article pointers (disc data). */
typedef DISC_PTR(void) ftDl_ArticleSlot;

/* code+0x0 (and its tail at +0x7C), [onload]. Same as retail ftLg_Init_OnLoad: copy Luigi's
 * attribute block (0x98 bytes, ftLuigiAttributes) from his own file and hand article 0 to the
 * item code, by index (m-ex MEX_IndexFighterItem at 0x803D7058; Luigi calls it_8026B3F8 with
 * It_Kind_Luigi_Fire). Then the CPU setup above. */
static void ftDl_Init_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftDl_ArticleSlot* items = (ftDl_ArticleSlot*) DISC_GET(void, fp->ft_data->x48_items);

    PUSH_ATTRS(fp, ftLuigiAttributes);

    mu_ak_register_article(fp->kind, DP(items[ftDl_Article_Pill]), ftDl_Article_Pill);

    HSD_GObj_SetupProc(gobj, ftDl_MexCPU_InitProc, 2);
}

/* code+0xB4, [move_logic]: 18 rows. Compared word for word with retail
 * ftLg_Init_MotionStateTable (main.dol 0x803D0628): the animation ids, flags, move ids and
 * callbacks are Luigi's in every row, except the collision callback of rows 0 and 1. The two
 * rows that follow on the disc (code+0x2F4) are Luigi's demo table (ftLg_Init_UnkMotionStates0),
 * not exported: MxDt names Luigi's own for that slot. */
const MotionState ftDl_Init_MotionStateTable[ftDl_MS_SelfCount] = {
    {
        // ftLg_MS_SpecialN = 341: Luigi's row with his own collision callback (code+0x4D8)
        ftLg_SM_SpecialN,
        ftLg_MF_SpecialN,
        FtMoveId_SpecialN << 24,
        ftLg_SpecialN_Anim,
        ftLg_SpecialN_IASA,
        ftLg_SpecialN_Phys,
        ftDl_SpecialN_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLg_MS_SpecialAirN = 342: Luigi's row with his own collision callback (code+0x568)
        ftLg_SM_SpecialAirN,
        ftLg_MF_SpecialAirN,
        FtMoveId_SpecialN << 24,
        ftLg_SpecialAirN_Anim,
        ftLg_SpecialAirN_IASA,
        ftLg_SpecialAirN_Phys,
        ftDl_SpecialAirN_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLg_MS_SpecialSStart = 343: Luigi's row, unchanged
        ftLg_SM_SpecialSStart,
        ftLg_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftLg_SpecialSStart_Anim,
        ftLg_SpecialSStart_IASA,
        ftLg_SpecialSStart_Phys,
        ftLg_SpecialSStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLg_MS_SpecialSHold = 344: Luigi's row, unchanged
        ftLg_SM_SpecialSHold,
        ftLg_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftLg_SpecialSHold_Anim,
        ftLg_SpecialSHold_IASA,
        ftLg_SpecialSHold_Phys,
        ftLg_SpecialSHold_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLg_MS_SpecialS2 = 345: Luigi's row, unchanged
        ftLg_SM_SpecialS2,
        ftLg_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftLg_SpecialS2_Anim,
        ftLg_SpecialS2_IASA,
        ftLg_SpecialS2_Phys,
        ftLg_SpecialS2_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLg_MS_SpecialSEnd = 346: Luigi's row, unchanged
        ftLg_SM_SpecialSEnd,
        ftLg_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftLg_SpecialSEnd_Anim,
        ftLg_SpecialSEnd_IASA,
        ftLg_SpecialSEnd_Phys,
        ftLg_SpecialSEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLg_MS_SpecialS = 347: Luigi's row, unchanged
        ftLg_SM_SpecialS,
        ftLg_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftLg_SpecialS_Anim,
        ftLg_SpecialS_IASA,
        ftLg_SpecialS_Phys,
        ftLg_SpecialS_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLg_MS_SpecialSMisfire = 348: Luigi's row, unchanged
        ftLg_SM_SpecialSMisfire,
        ftLg_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftLg_SpecialSMisfire_Anim,
        ftLg_SpecialSMisfire_IASA,
        ftLg_SpecialSMisfire_Phys,
        ftLg_SpecialSMisfire_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLg_MS_SpecialAirSStart = 349: Luigi's row, unchanged
        ftLg_SM_SpecialAirSStart,
        ftLg_MF_SpecialAirS,
        FtMoveId_SpecialS << 24,
        ftLg_SpecialAirSStart_Anim,
        ftLg_SpecialAirSStart_IASA,
        ftLg_SpecialAirSStart_Phys,
        ftLg_SpecialAirSStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLg_MS_SpecialAirSHold = 350: Luigi's row, unchanged
        ftLg_SM_SpecialAirSHold,
        ftLg_MF_SpecialAirS,
        FtMoveId_SpecialS << 24,
        ftLg_SpecialAirSHold_Anim,
        ftLg_SpecialAirSHold_IASA,
        ftLg_SpecialAirSHold_Phys,
        ftLg_SpecialAirSHold_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLg_MS_SpecialAirS2 = 351: Luigi's row, unchanged
        ftLg_SM_SpecialS2,
        ftLg_MF_SpecialAirS,
        FtMoveId_SpecialS << 24,
        ftLg_SpecialAirS2_Anim,
        ftLg_SpecialAirS2_IASA,
        ftLg_SpecialAirS2_Phys,
        ftLg_SpecialAirS2_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLg_MS_SpecialAirSEnd = 352: Luigi's row, unchanged
        ftLg_SM_SpecialAirSEnd,
        ftLg_MF_SpecialAirS,
        FtMoveId_SpecialS << 24,
        ftLg_SpecialAirSEnd_Anim,
        ftLg_SpecialAirSEnd_IASA,
        ftLg_SpecialAirSEnd_Phys,
        ftLg_SpecialAirSEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLg_MS_SpecialAirS = 353: Luigi's row, unchanged
        ftLg_SM_SpecialAirS,
        ftLg_MF_SpecialAirS,
        FtMoveId_SpecialS << 24,
        ftLg_SpecialAirS_Anim,
        ftLg_SpecialAirS_IASA,
        ftLg_SpecialAirS_Phys,
        ftLg_SpecialAirS_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLg_MS_SpecialAirSMisfire = 354: Luigi's row, unchanged
        ftLg_SM_SpecialAirSMisfire,
        ftLg_MF_SpecialAirS,
        FtMoveId_SpecialS << 24,
        ftLg_SpecialAirSMisfire_Anim,
        ftLg_SpecialAirSMisfire_IASA,
        ftLg_SpecialAirSMisfire_Phys,
        ftLg_SpecialAirSMisfire_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLg_MS_SpecialHi = 355: Luigi's row, unchanged
        ftLg_SM_SpecialHi,
        ftLg_MF_SpecialHi,
        FtMoveId_SpecialHi << 24,
        ftLg_SpecialHi_Anim,
        ftLg_SpecialHi_IASA,
        ftLg_SpecialHi_Phys,
        ftLg_SpecialHi_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLg_MS_SpecialAirHi = 356: Luigi's row, unchanged
        ftLg_SM_SpecialAirHi,
        ftLg_MF_SpecialAirHi,
        FtMoveId_SpecialHi << 24,
        ftLg_SpecialAirHi_Anim,
        ftLg_SpecialAirHi_IASA,
        ftLg_SpecialAirHi_Phys,
        ftLg_SpecialAirHi_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLg_MS_SpecialLw = 357: Luigi's row, unchanged
        ftLg_SM_SpecialLw,
        ftLg_MF_SpecialLw,
        FtMoveId_SpecialLw << 24,
        ftLg_SpecialLw_Anim,
        ftLg_SpecialLw_IASA,
        ftLg_SpecialLw_Phys,
        ftLg_SpecialLw_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftLg_MS_SpecialAirLw = 358: Luigi's row, unchanged
        ftLg_SM_SpecialAirLw,
        ftLg_MF_SpecialAirLw,
        FtMoveId_SpecialLw << 24,
        ftLg_SpecialAirLw_Anim,
        ftLg_SpecialAirLw_IASA,
        ftLg_SpecialAirLw_Phys,
        ftLg_SpecialAirLw_Coll,
        ftCamera_UpdateCameraBox,
    },
};

/* Every slot not named here is NULL: the registry then takes MxDt's default, which is Luigi's
 * function for all of them (run-source/rel09-ace-native/drluigi/mxdt.txt). Nothing has run yet,
 * so he is not marked MU_AK_READY. */
const MuAkFighter mu_ak_drluigi = {
    .name = "Dr. Luigi",
    .file = "PlDl.dat",
    .onload = ftDl_Init_OnLoad,
    .specialn = ftDl_SpecialN_Enter,
    .specialairn = ftDl_SpecialAirN_Enter,
    .move_logic = ftDl_Init_MotionStateTable,
    .move_logic_count = ftDl_MS_SelfCount,
    .flags = 0,
    .articles = itDl_Articles,
    .article_count = ftDl_Article_Count,
};
