/* ACE's Chun-Li: load, the per-fighter events, the m-ex CPU helper and the move table (m-ex
 * "ftFunction" of PlCn.dat).
 *
 * PlCn.dat exports fifteen ftFunction slots: 0 onload, 3 move_logic, 4 to 11 the special
 * moves, 21 onhit, 23 onframe, 24 onactionstatechange, 25 onrespawn and 34 onlanding. Every
 * other slot MxDt sets for internal 55 is Zelda's (1, ondeath: ftZd_Init_OnDeath) or common
 * (32, the double jump; 30, the costume lookup data), which the registry fills from MxDt when
 * a field here is NULL. */
#include "chunli.h"

#include <dolphin/os.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/ft/kinds/ftSeak/ftseak.h>
#include <melee/ft/kinds/ftZelda/ftzeldaspecialhi.h>
#include <melee/ft/kinds/ftZelda/ftzeldaspeciallw.h>
#include <melee/ft/kinds/ftZelda/ftzeldaspecials.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>

/* mu_ak_fighters.c: the native kind of an m-ex internal id (also used by ace/knuckles). */
extern int mu_ak_kind_from_mex(int mex_internal);

/* ---- the m-ex CPU helper ---------------------------------------------------------------------
 * The file carries m-ex's MexCPU code without symbols: +1750 [mexcpu_data], +1754
 * [MexCPU_InitProc], +17F0 [mexcpu_spoof], +2D38 [MexCPU_ProcCustom], +2EF0 [MexCPU_ProcSpoof],
 * +3340 [MexCPU_Process]; the load inlines [MexCPU_InitSpoofData] (+00A8 to +0108).
 *
 * +1750 is a pointer to a custom CPU table. Nothing in PlCn.dat writes it (its five relocations
 * are reads) and the file holds 0 there, so the custom proc (+2D38) and the custom branch of
 * +3340 are never reached and are not carried over. +17F0 only ever holds 0x1C, written by
 * each load: it is a constant here.
 *
 * 0x1C is written to fp->kind for the CPU step. On the console fp->kind is the m-ex internal
 * id, so 0x1C is internal 28 (Diddy Kong on ACE); it is mapped to its native kind as the
 * Knuckles port does (NOTES.md, unproven points). */
#define FTCN_CPU_SPOOF_INTERNAL 0x1C

extern const struct MuAkKirbyCopy ftKbCn_Copy;

/* +2EF0 [MexCPU_ProcSpoof] (proc, priority 2): the game's CPU step with fp->kind swapped for the
 * call. +3340 [MexCPU_Process] with no custom table is ftCo_800B33B0, ftCo_800B2AFC,
 * ftCo_800B2790, ftCo_800B3E04 and the counter at fp+1B04, which is mu_ak_cpu_process. */
static void ftCn_Cpu_ProcSpoof(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    FighterKind kind;

    if (fp->is_sleeping) {
        return;
    }
    if (!ftCo_IsCpuControlled(fp)) {
        return;
    }
    kind = fp->kind;
    fp->kind = (FighterKind) mu_ak_kind_from_mex(FTCN_CPU_SPOOF_INTERNAL);
    mu_ak_cpu_process(gobj, NULL);
    fp->kind = kind;
}

/* +1754 [MexCPU_InitProc] (proc, priority 2): find Fighter_procCpu on the fighter GObj, remove
 * it and put the posing proc in at the same priority. It stays installed and finds nothing to
 * swap on later frames, as on the console. */
static void ftCn_Cpu_InitProc(HSD_GObj* gobj)
{
    HSD_GObjProc* proc;

    for (proc = gobj->proc; proc != NULL; proc = proc->child) {
        if (proc->on_invoke == Fighter_procCpu) {
            HSD_GObjProc_RemoveProc(proc);
            HSD_GObj_SetupProc(gobj, ftCn_Cpu_ProcSpoof, 2);
            return;
        }
    }
}

/* +00A8 to +0108 of the load ([MexCPU_InitSpoofData] inlined): with +1750 clear, which it
 * always is, the spoof kind and the proc above. */
void ftCn_Cpu_InitSpoofData(HSD_GObj* gobj)
{
    HSD_GObj_SetupProc(gobj, ftCn_Cpu_InitProc, 2);
}

/* ---- ftFunction --------------------------------------------------------------------------------
 * +0000, [onload]. Her attribute block (0x8C bytes) into fp+2D8, which becomes fp+2D4, the four
 * command variables cleared, then the retail Sheik's onload by its console address (+003C:
 * lis/ori 0x801100EC, bctrl; ftSk_Init_OnLoad): the wall jump, Sheik's attribute size (0x74
 * bytes of the same block, copied again over the same place) and the first four articles of her
 * file handed to Sheik's four retail item kinds. Then the same four articles under her own kinds
 * by index (m-ex MEX_IndexFighterItem at 0x803D7058), and the CPU helper.
 *
 * As on the console, a Sheik (or a Zelda who becomes Sheik) in the same match shares the four
 * retail kinds with her: whichever loaded last gives them its articles. */
static void ftCn_Init_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCn_DatAttrs* src = (ftCn_DatAttrs*) DP(fp->ft_data->ext_attr);
    DISC_PTR(void)* items = DP(fp->ft_data->x48_items);

    *(ftCn_DatAttrs*) fp->dat_attrs_backup = *src;
    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    fp->cmd_vars[2] = 0;
    fp->cmd_vars[3] = 0;
    fp->dat_attrs = fp->dat_attrs_backup;
    ftSk_Init_OnLoad(gobj);

    mu_ak_register_article(fp->kind, DP(items[ftCn_Article_0]), ftCn_Article_0);
    mu_ak_register_article(fp->kind, DP(items[ftCn_Article_1]), ftCn_Article_1);
    mu_ak_register_article(fp->kind, DP(items[ftCn_Article_Shot]), ftCn_Article_Shot);
    mu_ak_register_article(fp->kind, DP(items[ftCn_Article_ShotAlt]), ftCn_Article_ShotAlt);

    ftCn_Cpu_InitSpoofData(gobj);

    {
        /* Bring-up evidence (once). */
        static int logged;
        if (!logged) {
            logged = 1;
            OSReport("[ak] Chun-Li load: shot kinds %d and %d, wall jump %d, CPU poses as kind "
                     "%d\n",
                     mu_ak_item_kind(fp->kind, ftCn_Article_Shot),
                     mu_ak_item_kind(fp->kind, ftCn_Article_ShotAlt), (int) fp->can_walljump,
                     mu_ak_kind_from_mex(FTCN_CPU_SPOOF_INTERNAL));
        }
    }
}

/* +0774, [onhit]: the eye texture of part 1, then of part 0, at frame 6.0 (the retail
 * Fighter_OnKnockbackEnter uses another frame, so this is her own). */
static void ftCn_Init_OnKnockbackEnter(HSD_GObj* gobj)
{
    ftAnim_800704F0(gobj, 1, 6.0f);
    ftAnim_800704F0(gobj, 0, 6.0f);
}

/* +16FC, [onactionstatechange]: a ledge catch (252) or DeadDown (0) gives back the air side
 * and down specials. */
static void ftCn_Init_OnActionStateChange(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->motion_id == ftCo_MS_CliffCatch || fp->motion_id == ftCo_MS_DeadDown) {
        ftCn_Vars(fp)->specialairs_used = 0;
        ftCn_Vars(fp)->specialairlw_used = 0;
    }
}

/* +1724, [onrespawn]: her attribute block again, 0x8C bytes, into fp+2D8 (the copy fp+2D4
 * points to). */
static void ftCn_Init_LoadSpecialAttrs(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftCn_DatAttrs* src = (ftCn_DatAttrs*) DP(fp->ft_data->ext_attr);

    *(ftCn_DatAttrs*) fp->dat_attrs_backup = *src;
}

/* +173C, [onlanding]: the air side and down specials are back. */
static void ftCn_Init_OnLanding(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCn_Vars(fp)->specialairs_used = 0;
    ftCn_Vars(fp)->specialairlw_used = 0;
}

/* The ten interrupt callbacks that are a lone blr. */
void ftCn_Empty(HSD_GObj* gobj)
{
    (void) gobj;
}

/* ---- the state table ----------------------------------------------------------------------------
 * +010C, [move_logic]: 27 rows, compared word for word with retail ftZd_Init_MotionStateTable
 * (main.dol 0x803CFA58, tables.txt). The animation ids, flag words and move ids are the words
 * of the file. Rows whose four callbacks are retail addresses name the retail function (the
 * words are those addresses, every one checked against the decomp's symbols); the others are
 * her code, each marked with its offset. A callback of hers that is only a jump to a retail
 * function names that function. */
const MotionState ftCn_Init_MotionStateTable[ftCn_MS_SelfCount] = {
    {
        // ftCn_MS_SpecialN = 341
        295,
        0x00341011,
        FtMoveId_SpecialN << 24,
        ftCn_SpecialN_Anim, /* +17F4 */
        ftCn_Empty,         /* +1840 */
        ft_80084F3C,        /* +1844: a jump to 0x80084F3C */
        ftCn_SpecialN_Coll, /* +1848 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_SpecialAirN = 342
        296,
        0x00341411,
        FtMoveId_SpecialN << 24,
        ftCn_SpecialAirN_Anim, /* +18C4 */
        ftCn_Empty,            /* +1910 */
        ft_80084DB0,           /* +1914: a jump to 0x80084DB0 */
        ftCn_SpecialAirN_Coll, /* +1918 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_ZdSpecialSStart = 343: Zelda's row, animation -1
        -1,
        0x00340112,
        FtMoveId_SpecialS << 24,
        ftZd_SpecialSStart_Anim,
        ftZd_SpecialSStart_IASA,
        ftZd_SpecialSStart_Phys,
        ftZd_SpecialSStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_ZdSpecialSLoop = 344: Zelda's row, animation -1
        -1,
        0x00340112,
        FtMoveId_SpecialS << 24,
        ftZd_SpecialSLoop_Anim,
        ftZd_SpecialSLoop_IASA,
        ftZd_SpecialSLoop_Phys,
        ftZd_SpecialSLoop_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_SpecialSStart = 345
        297,
        0x00340112,
        FtMoveId_SpecialS << 24,
        ftCn_SpecialSStart_Anim, /* +19D8 */
        ftCn_Empty,              /* +1A28 */
        ftCn_SpecialSStart_Phys, /* +1A2C */
        ftCn_SpecialSStart_Coll, /* +1A5C */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_ZdSpecialAirSStart = 346: Zelda's row, animation -1
        -1,
        0x00340512,
        FtMoveId_SpecialS << 24,
        ftZd_SpecialAirSStart_Anim,
        ftZd_SpecialAirSStart_IASA,
        ftZd_SpecialAirSStart_Phys,
        ftZd_SpecialAirSStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_ZdSpecialAirSLoop = 347: Zelda's row, animation -1
        -1,
        0x00340512,
        FtMoveId_SpecialS << 24,
        ftZd_SpecialAirSLoop_Anim,
        ftZd_SpecialAirSLoop_IASA,
        ftZd_SpecialAirSLoop_Phys,
        ftZd_SpecialAirSLoop_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_SpecialAirSStart = 348
        298,
        0x00340512,
        FtMoveId_SpecialS << 24,
        ftCn_SpecialSStart_Anim,    /* +1AE8: the instructions of +19D8 */
        ftCn_Empty,                 /* +1B38 */
        ftCn_SpecialSStart_Phys,    /* +1B3C: the instructions of +1A2C */
        ftCn_SpecialAirSStart_Coll, /* +1B6C */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_SpecialHi = 349
        302,
        0x00340013,
        FtMoveId_SpecialHi << 24,
        ftCn_SpecialHi_Anim, /* +1BF8 */
        ftCn_SpecialHi_IASA, /* +1C70 */
        ftCn_SpecialHi_Phys, /* +1D90 */
        ftCn_SpecialHi_Coll, /* +1E0C */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_ZdSpecialHiStart_1 = 350: Zelda's row, animation -1
        -1,
        0x00340013,
        FtMoveId_SpecialHi << 24,
        ftZd_SpecialHiStart_1_Anim,
        ftZd_SpecialHiStart_1_IASA,
        ftZd_SpecialHiStart_1_Phys,
        ftZd_SpecialHiStart_1_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_ZdSpecialHi = 351: Zelda's row, animation -1
        -1,
        0x00340013,
        FtMoveId_SpecialHi << 24,
        ftZd_SpecialHi_Anim,
        ftZd_SpecialHi_IASA,
        ftZd_SpecialHi_Phys,
        ftZd_SpecialHi_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_SpecialAirHi = 352
        303,
        0x00340413,
        FtMoveId_SpecialHi << 24,
        ftCn_SpecialHi_Anim,    /* +1E54: the instructions of +1BF8 */
        ftCn_SpecialHi_IASA,    /* +1ECC: a jump to +1C70 */
        ftCn_SpecialHi_Phys,    /* +1ED0: the instructions of +1D90 */
        ftCn_SpecialAirHi_Coll, /* +1F4C */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_ZdSpecialAirHiStart_1 = 353: Zelda's row, animation -1
        -1,
        0x00340413,
        FtMoveId_SpecialHi << 24,
        ftZd_SpecialAirHiStart_1_Anim,
        ftZd_SpecialAirHiStart_1_IASA,
        ftZd_SpecialAirHiStart_1_Phys,
        ftZd_SpecialAirHiStart_1_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_ZdSpecialAirHi = 354: Zelda's row, animation -1
        -1,
        0x00340413,
        FtMoveId_SpecialHi << 24,
        ftZd_SpecialAirHi_Anim,
        ftZd_SpecialAirHi_IASA,
        ftZd_SpecialAirHi_Phys,
        ftZd_SpecialAirHi_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_SpecialLw = 355
        304,
        0x00340014,
        FtMoveId_SpecialLw << 24,
        ftCn_SpecialN_Anim, /* +1F94: the instructions of +17F4 */
        ftCn_Empty,         /* +1FE0 */
        ft_80085154,        /* +1FE4: a jump to 0x80085154 */
        ftCn_SpecialLw_Coll, /* +1FE8 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_ZdSpecialLw2 = 356: Zelda's row, animation -1
        -1,
        0x00340014,
        FtMoveId_SpecialLw << 24,
        ftZd_SpecialLw2_Anim,
        ftZd_SpecialLw2_IASA,
        ftZd_SpecialLw2_Phys,
        ftZd_SpecialLw2_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_SpecialAirLw = 357
        305,
        0x00340414,
        FtMoveId_SpecialLw << 24,
        ftCn_SpecialAirN_Anim,  /* +2070: the instructions of +18C4 */
        ftCn_SpecialHi_IASA,    /* +20BC: a jump to +1C70 */
        ftCn_SpecialAirLw_Phys, /* +20C0 */
        ftCn_SpecialAirLw_Coll, /* +2150 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_ZdSpecialAirLw2 = 358: Zelda's row, animation -1
        -1,
        0x00340414,
        FtMoveId_SpecialLw << 24,
        ftZd_SpecialAirLw2_Anim,
        ftZd_SpecialAirLw2_IASA,
        ftZd_SpecialAirLw2_Phys,
        ftZd_SpecialAirLw2_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_SpecialSLoop = 359
        299,
        0x00340112,
        FtMoveId_SpecialS << 24,
        ftCn_SpecialSLoop_Anim, /* +21C4 */
        ftCn_Empty,             /* +2230 */
        ftCn_SpecialSLoop_Phys, /* +2234 */
        ftCn_SpecialSLoop_Coll, /* +225C */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_SpecialAirSLoop = 360: the move id word is SpecialLw's (0x15), as in the file
        299,
        0x00340512,
        FtMoveId_SpecialLw << 24,
        ftCn_SpecialAirSLoop_Anim, /* +22E4 */
        ftCn_Empty,                /* +2350 */
        ftCn_SpecialSLoop_Phys,    /* +2354: the instructions of +2234 */
        ftCn_SpecialAirSLoop_Coll, /* +237C */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_SpecialSEnd = 361
        300,
        0x00340112,
        FtMoveId_SpecialS << 24,
        ftCn_SpecialN_Anim,    /* +2418: the instructions of +17F4 */
        ftCn_Empty,            /* +2464 */
        ftCn_SpecialSEnd_Phys, /* +2468 */
        ftCn_SpecialSEnd_Coll, /* +2498 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_SpecialAirSEnd = 362
        301,
        0x00340512,
        FtMoveId_SpecialS << 24,
        ftCn_SpecialAirN_Anim,    /* +2518: the instructions of +18C4 */
        ftCn_Empty,               /* +2564 */
        ftCn_SpecialAirSEnd_Phys, /* +2568 */
        ftCn_SpecialAirSEnd_Coll, /* +25BC */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_SpecialSEnd2 = 363: 361's four callbacks, animation -1, move id 0x15
        -1,
        0x00340112,
        FtMoveId_SpecialLw << 24,
        ftCn_SpecialN_Anim,    /* +2418 */
        ftCn_Empty,            /* +2464 */
        ftCn_SpecialSEnd_Phys, /* +2468 */
        ftCn_SpecialSEnd_Coll, /* +2498 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_AttackLw3Light = 364
        306,
        0x00340512,
        FtMoveId_SpecialLw << 24,
        ftCn_AttackLw3Light_Anim, /* +263C */
        ftCn_AttackLw3Light_IASA, /* +26A8 */
        ft_80084F3C,              /* +2714: a jump to 0x80084F3C */
        ftCn_AttackLw3Light_Coll, /* +2718 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_ZdSpecialLw2b = 365: Zelda's SpecialLw2 callbacks, animation -1
        -1,
        0x00340014,
        FtMoveId_SpecialLw << 24,
        ftZd_SpecialLw2_Anim,
        ftZd_SpecialLw2_IASA,
        ftZd_SpecialLw2_Phys,
        ftZd_SpecialLw2_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_AttackHi3Light = 366
        307,
        0x00340512,
        FtMoveId_SpecialLw << 24,
        ftCn_SpecialN_Anim,       /* +2764: the instructions of +17F4 */
        ftCn_AttackHi3Light_IASA, /* +27B0 */
        ft_80084F3C,              /* +281C: a jump to 0x80084F3C */
        ftCn_AttackHi3Light_Coll, /* +2820 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftCn_MS_SpecialNAlt = 367: the move id word is SpecialN's (0x12)
        310,
        0x00340014,
        FtMoveId_SpecialN << 24,
        ftCn_SpecialN_Anim, /* +2828: the instructions of +17F4 */
        ftCn_Empty,         /* +2874 */
        ft_80084F3C,        /* +2878: a jump to 0x80084F3C */
        ftCn_SpecialN_Coll, /* +287C: the instructions of +1848 */
        ftCamera_UpdateCameraBox,
    },
};

_Static_assert(FtMoveId_SpecialN == 0x12 && FtMoveId_SpecialS == 0x13 &&
                   FtMoveId_SpecialHi == 0x14 && FtMoveId_SpecialLw == 0x15,
               "the move id bytes of the table words (0x12000000 to 0x15000000)");

/* The fifteen slots PlCn.dat exports. Every slot not named here is NULL: the registry then takes
 * MxDt's default for internal 55 (run-source/rel09-ace-native/chunli/mxdt.txt): Zelda's ondeath
 * (ftZd_Init_OnDeath), the common double jump, the costume lookup word. Articles 2 and 3 have
 * code (chunli_items.c). Written and never run, so not MU_AK_READY. */
const MuAkFighter mu_ak_chunli = {
    .name = "Chun-Li",
    .file = "PlCn.dat",

    .onload = ftCn_Init_OnLoad,
    .specialn = ftCn_SpecialN_Enter,
    .specialairn = ftCn_SpecialAirN_Enter,
    .specials = ftCn_SpecialS_Enter, /* +055C: a jump to +29D0 */
    .specialairs = ftCn_SpecialAirS_Enter,
    .specialhi = ftCn_SpecialHi_Enter,
    .specialairhi = ftCn_SpecialAirHi_Enter,
    .speciallw = ftCn_SpecialLw_Enter,
    .specialairlw = ftCn_SpecialAirLw_Enter,
    .onhit = ftCn_Init_OnKnockbackEnter,
    .onframe = ftCn_Init_OnFrame,
    .onactionstatechange = ftCn_Init_OnActionStateChange,
    .onrespawn = ftCn_Init_LoadSpecialAttrs,
    .onlanding = ftCn_Init_OnLanding,

    .move_logic = ftCn_Init_MotionStateTable,
    .move_logic_count = ftCn_MS_SelfCount,

    .flags = 0,
    .article_tables = itCn_ArticleTables,
    .article_count = ftCn_Article_Count,
    .kirby = &ftKbCn_Copy,
};
