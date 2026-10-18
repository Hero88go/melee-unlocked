/* ACE's Meta Knight: load, the CPU helper, the per-fighter events and the state table (m-ex
 * "ftFunction" of PlMk.dat).
 *
 * PlMk.dat exports 14 ftFunction slots: 0 onload, 3 move_logic, 4 to 11 the special entries,
 * 24 onactionstatechange, 32 enterdoublejump, 34 onlanding, 35 onsmashf. MxDt names Mario's
 * retail function for slots 1 (ondeath), 18 (the item drop), 25 (the attribute reload) and 40
 * (the demo state table); the registry fills those because the fields are left NULL here. */
#include "metaknight.h"

#include <dolphin/os.h>
#include <string.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/ft/kinds/ftCommon/ftCo_Attack100.h>
#include <melee/ft/kinds/ftCommon/ftCo_AttackS3.h>
#include <melee/ft/kinds/ftCommon/ftCo_AttackS4.h>
#include <melee/ft/kinds/ftSeak/ftseak.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/memory.h>

/* ---- the m-ex CPU helper, linked into the block ---- */

/* +1324 (m-ex .bss mexcpu_spoof): the kind a CPU Meta Knight runs the CPU step as. The load
 * writes 0x1C (Ft_Kind_CrezyH) into it. One word for every Meta Knight, as in the file. */
static FighterKind ftMk_CpuSpoofKind;

/* +0EC4 (m-ex .bss mexcpu_data) is a pointer to a custom CPU table. It is a zero word of the
 * block that no instruction writes (five loads, no store: consts.txt), so every test of it in the
 * file takes the "no table" branch. The custom proc it would select (+2F0C, MexCPU_ProcCustom,
 * which patches the tables at Fighter_804D64FC for the call) and the custom step inside +3AE0
 * are therefore never run and are not carried over (FUNCTIONS.md). */

/* +30C4 (MexCPU_ProcSpoof): Fighter_procCpu with fp->kind spoofed for the call. The test of the
 * word at fp+221C with 0x10 is the sleeping bit (fp+221F:3). */
static void ftMk_Cpu_ProcSpoof(HSD_GObj* gobj)
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
    fp->kind = ftMk_CpuSpoofKind;
    /* +3AE0 (MexCPU_Process): ftCo_800B33B0, ftCo_800B2AFC, the custom step (only with +0EC4
     * set), ftCo_800B2790, ftCo_800B3E04, fp+1B04 (cpu.x7C) += 1. With no custom table that is
     * mu_ak_cpu_process(gobj, NULL) call for call. */
    mu_ak_cpu_process(gobj, NULL);
    fp->kind = kind;
}

/* +1288 (MexCPU_InitProc): runs every frame; finds the fighter's Fighter_procCpu proc (the
 * callback word 0x8006ABA0 at +14 of the proc), removes it and puts the spoofing proc at
 * priority 2. Once the swap is done it finds nothing and stays installed, as on the console. */
static void ftMk_Cpu_InitProc(HSD_GObj* gobj)
{
    HSD_GObjProc* proc;

    for (proc = gobj->proc; proc != NULL; proc = proc->child) {
        if (proc->on_invoke == Fighter_procCpu) {
            HSD_GObjProc_RemoveProc(proc);
            /* +12D8: +0EC4 is zero, so +30C4 and not +2F0C. */
            HSD_GObj_SetupProc(gobj, ftMk_Cpu_ProcSpoof, 2);
            return;
        }
    }
}

/* ---- ftFunction ---- */

/* m-ex 0x803D706C allocates zeroed memory (as akaneia/dedede/dedede.c). */
static void* ftMk_Calloc(size_t size)
{
    void* p = HSD_MemAlloc(size);
    if (p != NULL) {
        memset(p, 0, size);
    }
    return p;
}

/* +0000 (slot 0, onload). In the file's order:
 *   1. 0x2AA0 bytes (341 states) allocated and the fighter's common state table fp+1C copied in;
 *      in the copy the animation callback of 27 and 28 (JumpAerialF, JumpAerialB) becomes
 *      +0CE4, state 53 (AttackS3S) gets +0C50 and +0C9C, state 60 (AttackS4S) gets +0AA0 and no
 *      interrupt callback;
 *   2. fp+2D8 and fp+2D4 set to ftData->ext_attr itself (the disc block, not a copy);
 *   3. fp+1C = the copy;
 *   4. Sheik's retail onload by address (0x801100EC, ftSk_Init_OnLoad: the wall jump bit, her
 *      attribute copy, which here copies the block onto itself, and her four articles);
 *   5. fp+20 = +0EC8, a second copy of his state table (equal to the exported one word for word,
 *      consts.txt);
 *   6. +0EC4 is zero: the spoof kind 0x1C and the CPU swap proc (+1288) at priority 2;
 *   7. fp+22F8 = fp+22F4 = 0, then model part 1 to 0 and part 2 to 1.
 *
 * Two native guards for a results-screen fighter (fp->x18 == 14, set by ftdemo.c before the
 * load), as Sonic BM's port does for step 1:
 *   - step 1 copies only fp->x18 rows (the list's own count) and leaves the rest of the copy
 *     zero; the console copies 0x2AA0 bytes from the 14-row demo list, past its end, and the
 *     demo never reads rows 14 and up of fp+1C (they go to fp+20);
 *   - step 5 is left out: under the demo's numbering (fp->x18 = 14) his first state's end
 *     (342) would index row 328 of his 30-row table, a read past it on the console too. The
 *     demo keeps Mario's two demo rows (the MxDt default of slot 40).
 *
 * Native addition after step 4, not in the file: the wall jump bit from MxDt. On the console the
 * m-ex code at ftWallJump_8008169C+28 reads MxDt's wall jump byte (0 for internal 42) in place of
 * the bit that Sheik's onload sets; natively the check reads the bit, so the byte is copied into
 * it (Metal Mario's and Toad's ports do the same). */
static void ftMk_Init_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftMk_FighterVars* fv = ftMk_Vars(fp);
    const bool results_screen = fp->x18 == 14;
    MotionState* table;
    void* attrs;
    int walljump;

    /* The console does not test the allocation; natively a failed one keeps fp+1C as it is. */
    table = ftMk_Calloc(sizeof(MotionState) * ftCo_MS_Count);
    if (table != NULL) {
        int rows = fp->x18;
        if (rows < 0 || rows > ftCo_MS_Count) {
            rows = ftCo_MS_Count;
        }
        memcpy(table, fp->x1C_actionStateList, sizeof(MotionState) * rows);
        table[ftCo_MS_AttackS3S].input_cb = ftMk_AttackS3S_IASA;   /* +6B0 */
        table[ftCo_MS_JumpAerialB].anim_cb = ftMk_JumpAerial_Anim; /* +38C */
        table[ftCo_MS_JumpAerialF].anim_cb = ftMk_JumpAerial_Anim; /* +36C */
        table[ftCo_MS_AttackS4S].anim_cb = ftMk_AttackS4S_Anim;    /* +78C */
        table[ftCo_MS_AttackS4S].input_cb = NULL;                  /* +790 */
        table[ftCo_MS_AttackS3S].anim_cb = ftMk_AttackS3S_Anim;    /* +6AC */
    }

    attrs = DP(fp->ft_data->ext_attr);
    fp->dat_attrs_backup = attrs;
    fp->dat_attrs = attrs;
    if (table != NULL) {
        fp->x1C_actionStateList = table;
    }

    ftSk_Init_OnLoad(gobj);

    walljump = mu_mex_fighter_walljump(mu_ak_mex_internal(fp->kind));
    if (walljump >= 0) {
        fp->can_walljump = walljump != 0;
    }

    if (!results_screen) {
        fp->x20_actionStateList = (MotionState*) ftMk_Init_MotionStateTable;
    }

    /* +00FC (MexCPU_InitSpoofData, inline) */
    ftMk_CpuSpoofKind = (FighterKind) 0x1C;
    HSD_GObj_SetupProc(gobj, ftMk_Cpu_InitProc, 2);

    fv->parts_on = 0;
    fv->prev_motion = 0;
    ftParts_80074A4C(gobj, 1, 0);
    ftParts_80074A4C(gobj, 2, 1);

    {
        /* Bring-up evidence (once). */
        static int logged;
        if (!logged) {
            logged = 1;
            OSReport("[ak] Meta Knight load: state copy %s, wall jump %d, results screen %d\n",
                     table != NULL ? "made" : "NOT made", (int) fp->can_walljump,
                     (int) results_screen);
        }
    }
}

/* +0684 (slot 24, onactionstatechange). +390C is a second copy of the same routine (one
 * instruction differs only in its branch encoding, consts.txt); both jump tables (+2D98 and
 * +2E48) point into the copy, so the copy is reached only as these switch targets.
 *   - leaving Run (fp+22F4 == 21): ft_80088A50
 *   - the new motion id is kept in fp+22F4
 *   - Dash, JumpAerialF, JumpAerialB, 347, 352: fp+22F8 = 1 and the two parts are set
 *   - Run: sound 5043 (100, 67)
 *   - 360: fp+22F8 = 1, the parts are set, and set again (fp+22F8 is 1)
 *   - 356 to 358 and 361 to 363: the down special effect (+2E8C)
 *   - AttackS4S, AttackHi4, CliffCatch: fp+22F8 = 0 and return
 *   - otherwise, with fp+22F8 == 1 the parts are set
 *   - ThrowB turns him around (after the parts). */
static void ftMk_Init_OnActionStateChange(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftMk_FighterVars* fv = ftMk_Vars(fp);
    s32 msid;

    if (fv->prev_motion == ftCo_MS_Run) {
        ft_80088A50(fp);
    }

    msid = fp->motion_id;
    fv->prev_motion = msid;

    if (msid > 0x16B) {
        goto check;
    }
    if (msid > 0x15A) {
        switch (msid) {
        case ftMk_MS_SpecialSDrill: /* 0x15B */
        case ftMk_MS_SpecialHiRise: /* 0x160 */
            goto parts_on;
        case ftMk_MS_SpecialLwN:
        case ftMk_MS_SpecialLwF:
        case ftMk_MS_SpecialLwB:
        case ftMk_MS_SpecialAirLwN:
        case ftMk_MS_SpecialAirLwF:
        case ftMk_MS_SpecialAirLwB:
            ftMk_SpecialLw_Effect(gobj);
            goto check;
        case ftMk_MS_SpecialAirLwStart: /* +3A40 (+07B8) */
            fv->parts_on = 1;
            ftParts_80074B0C(gobj, 1, 1);
            ftParts_80074B0C(gobj, 2, 0);
            if (fv->parts_on == 1) {
                goto parts;
            }
            goto flip;
        default:
            goto check;
        }
    }
    if (msid > 0x3F) {
        if (msid != ftCo_MS_CliffCatch) {
            goto check;
        }
        fv->parts_on = 0;
        return;
    }
    if (msid <= 0x13) {
        goto check;
    }
    switch (msid) {
    case ftCo_MS_Dash:
    case ftCo_MS_JumpAerialF:
    case ftCo_MS_JumpAerialB:
        goto parts_on;
    case ftCo_MS_Run: /* +3AA4 (+081C) */
        ft_800881D8(fp, 5043, 100, 67);
        if (fv->parts_on == 1) {
            goto parts;
        }
        goto flip;
    case ftCo_MS_AttackS4S:
    case ftCo_MS_AttackHi4: /* +3A98 (+0810) */
        fv->parts_on = 0;
        return;
    default:
        goto check;
    }

check: /* +0704 */
    if (fv->parts_on == 1) {
        goto parts;
    }
flip: /* +0710 */
    if (fp->motion_id == ftCo_MS_ThrowB) {
        fp->facing_dir = -fp->facing_dir;
    }
    return;

parts_on: /* +0768 */
    fv->parts_on = 1;
parts: /* +0770: the word at +2F08 holds 0x80074B0C (ftParts_80074B0C); nothing writes it */
    ftParts_80074B0C(gobj, 1, 1);
    ftParts_80074B0C(gobj, 2, 0);
    goto flip;
}

/* +0840 (slot 32, enterdoublejump; also the tail of +0CE4). Still rising in JumpAerialF, 365 or
 * 366: nothing. No jump left: nothing. Else the speed (x times the air jump multiplier, y the
 * jump speed times 1 - 0.075 * jumps used, one fused double), the state (the last jump 366, the
 * first JumpAerialF, the others 365), then the turn: facing right with the stick left of -0.1
 * (1), facing left with the stick right of 0.3 (2), else 0, into fp+2368; fp+236C = 15 frames,
 * the word fp+2228 = 0, one more jump used. */
void ftMk_Init_EnterDoubleJump(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftMk_MotionVars* mv = ftMk_MVars(fp);
    const s32 msid = fp->motion_id;
    u8 used;
    s32 left;
    s32 turn;
    double rise;

    if (msid == ftCo_MS_JumpAerialF || (u32) (msid - ftMk_MS_JumpAerial2) <= 1) {
        if (fp->self_vel.y > 0.0f) {
            return;
        }
    }
    used = fp->x1968_jumpsUsed;
    left = fp->co_attrs.max_jumps - used;
    if (left == 0) {
        return;
    }

    fp->self_vel.x = fp->self_vel.x * fp->co_attrs.air_jump_h_multiplier;
    rise = __builtin_fma(-(double) used, 0.075, 1.0);
    fp->self_vel.y = (float) (rise * (double) fp->co_attrs.jump_v_initial_velocity);

    if (left == 1) {
        FTMK_CHANGE(gobj, ftMk_MS_JumpAerialLast);
    } else if (used == 1) {
        FTMK_CHANGE(gobj, ftCo_MS_JumpAerialF);
    } else {
        FTMK_CHANGE(gobj, ftMk_MS_JumpAerial2);
    }

    /* +0924 */
    if (!(fp->facing_dir <= 0.0f)) {
        turn = fp->input.lstick[0].x < -0.1 ? 1 : 0;
    } else if (fp->facing_dir >= 0.0f) {
        turn = 0;
    } else {
        turn = fp->input.lstick[0].x > 0.3 ? 2 : 0;
    }
    fp->x1968_jumpsUsed = fp->x1968_jumpsUsed + 1;
    mv->x2368.i = turn;
    ftMk_Word2228_Set(fp, 0);
    mv->x236C = 15;
}

/* +0A00 (slot 34, onlanding): fp+22F8 = 0 unless the motion id is TurnRun, Dash, Run,
 * RunBrake or 352. */
static void ftMk_Init_OnLanding(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const s32 msid = fp->motion_id;

    if (msid == ftCo_MS_RunBrake) {
        return;
    }
    if (msid > ftCo_MS_RunBrake) {
        if (msid == ftMk_MS_SpecialHiRise) {
            return;
        }
    } else if ((u32) (msid - ftCo_MS_TurnRun) <= 2) {
        return;
    }
    ftMk_Vars(fp)->parts_on = 0;
}

/* +0A40 (slot 35, onsmashf): AttackS4S of his rewritten common table (blend 1.0), the four
 * hitbox marks cleared, fp+236C = 1, the held frame count fp+2368 = 0. None of the common
 * forward smash entry (charge, turn) is made. */
static void ftMk_Init_OnSmashF(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftMk_MotionVars* mv;

    Fighter_ChangeMotionState(gobj, ftCo_MS_AttackS4S, Ft_MF_None, 0.0f, 1.0f, 1.0f, NULL);
    mv = ftMk_MVars(fp);
    mv->x2370[0] = 0;
    mv->x2370[1] = 0;
    mv->x2370[2] = 0;
    mv->x2370[3] = 0;
    mv->x236C = 1;
    mv->x2368.i = 0;
}

/* +0120 (slot 3, move_logic): thirty rows. The animation ids (295 to 324) and move ids are the
 * words of the file; every flag word is 0. The table at +0EC8 that the load puts in fp+20 is
 * the same thirty rows. Common callbacks are named by the address in the row. */
const MotionState ftMk_Init_MotionStateTable[ftMk_MS_SelfCount] = {
    {
        // ftMk_MS_SpecialNStart = 341
        295,
        0x00000000,
        FtMoveId_SpecialN << 24,
        ftMk_SpecialNStart_Anim, /* +1328 */
        NULL,
        ftCo_AttackS4_Phys,      /* 0x8008C7F0 */
        ftMk_SpecialNStart_Coll, /* +13B8 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_SpecialNLoop = 342
        296,
        0x00000000,
        FtMoveId_SpecialN << 24,
        ftMk_SpecialNLoop_Anim, /* +1404 */
        NULL,
        ftMk_SpecialNLoop_Phys, /* +149C */
        ftMk_SpecialNLoop_Coll, /* +1630 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_SpecialNEnd = 343
        297,
        0x00000000,
        FtMoveId_SpecialN << 24,
        ftMk_SpecialNEnd_Anim,   /* +16AC */
        NULL,
        ftCo_AttackS4_Phys,      /* 0x8008C7F0 */
        ftMk_SpecialNStart_Coll, /* +13B8 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_SpecialAirNStart = 344
        298,
        0x00000000,
        FtMoveId_SpecialN << 24,
        ftMk_SpecialNStart_Anim,    /* +1328 */
        NULL,
        ftMk_SpecialAirN_Phys,      /* +16F8 */
        ftMk_SpecialAirNStart_Coll, /* +16FC */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_SpecialAirNEnd = 345
        299,
        0x00000000,
        FtMoveId_SpecialN << 24,
        ftMk_SpecialAirNEnd_Anim, /* +1748 */
        NULL,
        ftMk_SpecialAirN_Phys,    /* +16F8 */
        ftMk_SpecialAirNEnd_Coll, /* +17CC */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_SpecialSStart = 346
        300,
        0x00000000,
        FtMoveId_SpecialS << 24,
        ftMk_SpecialSStart_Anim,  /* +17D8 */
        NULL,
        ftCo_AttackS4_Phys,       /* 0x8008C7F0 */
        ftMk_SpecialS_GroundColl, /* +189C */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_SpecialSDrill = 347
        301,
        0x00000000,
        FtMoveId_SpecialS << 24,
        ftMk_SpecialSDrill_Anim, /* +18E8 */
        NULL,
        ftMk_SpecialSDrill_Phys, /* +1978 */
        ftMk_SpecialSDrill_Coll, /* +1AEC */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_SpecialSEnd = 348
        302,
        0x00000000,
        FtMoveId_SpecialS << 24,
        ftMk_SpecialSEnd_Anim,    /* +1B6C */
        NULL,
        ftCo_AttackS4_Phys,       /* 0x8008C7F0 */
        ftMk_SpecialS_GroundColl, /* +189C */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_SpecialAirSStart = 349
        303,
        0x00000000,
        FtMoveId_SpecialS << 24,
        ftMk_SpecialSStart_Anim,    /* +17D8 */
        NULL,
        ftMk_SpecialAirSStart_Phys, /* +1BB8 */
        ftMk_SpecialAirSStart_Coll, /* +1BD0 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_SpecialAirSEnd = 350
        304,
        0x00000000,
        FtMoveId_SpecialS << 24,
        ftMk_SpecialAirSEnd_Anim, /* +1C1C */
        NULL,
        ftMk_SpecialAirSEnd_Phys, /* +1CA0 */
        ftMk_SpecialAirSEnd_Coll, /* +1CA4 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_SpecialHiStart = 351: no physics, no collision callback in the file
        305,
        0x00000000,
        FtMoveId_SpecialHi << 24,
        ftMk_SpecialHiStart_Anim, /* +1D10 */
        NULL,
        NULL,
        NULL,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_SpecialHiRise = 352
        306,
        0x00000000,
        FtMoveId_SpecialHi << 24,
        ftMk_SpecialHiRise_Anim, /* +1DE4 */
        NULL,
        ftMk_SpecialHiRise_Phys, /* +1E50 */
        ftMk_SpecialHi_AirColl,  /* +1F10 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_SpecialAirHiStart = 353: no physics, no collision callback in the file
        307,
        0x00000000,
        FtMoveId_SpecialHi << 24,
        ftMk_SpecialAirHiStart_Anim, /* +1F68: a branch to +1D10 */
        NULL,
        NULL,
        NULL,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_AttackS3S2 = 354
        308,
        0x00000000,
        FtMoveId_None << 24,
        ftMk_AttackS3S_Anim,  /* +0C50 */
        ftMk_AttackS3S2_IASA, /* +1F6C */
        ftCo_AttackS3_Phys,   /* 0x8008B940 */
        ftCo_AttackS3_Coll,   /* 0x8008B960 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_SpecialLwStart = 355
        309,
        0x00000000,
        FtMoveId_SpecialLw << 24,
        ftMk_SpecialLwStart_Anim, /* +1FB4 */
        NULL,
        ftMk_SpecialLwStart_Phys, /* +2444 */
        ftMk_SpecialLw_Coll,      /* +2448 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_SpecialLwN = 356
        310,
        0x00000000,
        FtMoveId_SpecialLw << 24,
        ftMk_SpecialLwDash_Anim, /* +2494 */
        NULL,
        ftMk_SpecialLwDash_Phys, /* +24E8 */
        ftMk_SpecialLw_Coll,     /* +2448 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_SpecialLwF = 357
        311,
        0x00000000,
        FtMoveId_SpecialLw << 24,
        ftMk_SpecialLwDash_Anim, /* +2494 */
        NULL,
        ftMk_SpecialLwDash_Phys, /* +24E8 */
        ftMk_SpecialLw_Coll,     /* +2448 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_SpecialLwB = 358
        312,
        0x00000000,
        FtMoveId_SpecialLw << 24,
        ftMk_SpecialLwDash_Anim, /* +2494 */
        NULL,
        ftMk_SpecialLwDash_Phys, /* +24E8 */
        ftMk_SpecialLw_Coll,     /* +2448 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_SpecialLwEnd = 359
        313,
        0x00000000,
        FtMoveId_SpecialLw << 24,
        ftMk_SpecialLwDash_Anim, /* +2494 */
        NULL,
        ftMk_SpecialLwDash_Phys, /* +24E8 */
        ftMk_SpecialLw_Coll,     /* +2448 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_SpecialAirLwStart = 360
        314,
        0x00000000,
        FtMoveId_SpecialLw << 24,
        ftMk_SpecialAirLwStart_Anim, /* +24EC */
        NULL,
        ftMk_SpecialAirLwStart_Phys, /* +2990 */
        ftMk_SpecialAirLw_Coll,      /* +29A8 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_SpecialAirLwN = 361
        315,
        0x00000000,
        FtMoveId_SpecialLw << 24,
        ftMk_SpecialAirLwDash_Anim, /* +2A30 */
        NULL,
        ftMk_SpecialAirLwDash_Phys, /* +2ABC */
        ftMk_SpecialAirLw_Coll,     /* +29A8 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_SpecialAirLwF = 362
        316,
        0x00000000,
        FtMoveId_SpecialLw << 24,
        ftMk_SpecialAirLwDash_Anim, /* +2A30 */
        NULL,
        ftMk_SpecialAirLwDash_Phys, /* +2ABC */
        ftMk_SpecialAirLw_Coll,     /* +29A8 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_SpecialAirLwB = 363
        317,
        0x00000000,
        FtMoveId_SpecialLw << 24,
        ftMk_SpecialAirLwDash_Anim, /* +2A30 */
        NULL,
        ftMk_SpecialAirLwDash_Phys, /* +2ABC */
        ftMk_SpecialAirLw_Coll,     /* +29A8 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_SpecialAirLwEnd = 364
        318,
        0x00000000,
        FtMoveId_SpecialLw << 24,
        ftMk_SpecialAirLwDash_Anim, /* +2A30 */
        NULL,
        ftMk_SpecialAirLwDash_Phys, /* +2ABC */
        ftMk_SpecialAirLw_Coll,     /* +29A8 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_JumpAerial2 = 365
        319,
        0x00000000,
        FtMoveId_Default << 24,
        ftMk_JumpAerial_Anim,   /* +0CE4 */
        ftMk_JumpAerial_IASA,   /* +2B10 */
        ftMk_JumpAerial_Phys,   /* +2B14 */
        ftCo_JumpAerialF1_Coll, /* 0x800D767C */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_JumpAerialLast = 366
        320,
        0x00000000,
        FtMoveId_Default << 24,
        ftMk_JumpAerial_Anim,   /* +0CE4 */
        ftMk_JumpAerial_IASA,   /* +2B10 */
        ftMk_JumpAerial_Phys,   /* +2B14 */
        ftCo_JumpAerialF1_Coll, /* 0x800D767C */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_AttackS4Hold = 367
        321,
        0x00000000,
        FtMoveId_AttackS4 << 24,
        ftMk_AttackS4Hold_Anim, /* +2B48 */
        NULL,
        ftCo_AttackS4_Phys,     /* 0x8008C7F0 */
        ftCo_AttackS4_Coll,     /* 0x8008C810 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_Empty = 368: the camera callback only
        322,
        0x00000000,
        FtMoveId_None << 24,
        NULL,
        NULL,
        NULL,
        NULL,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_AttackS3S3 = 369
        323,
        0x00000000,
        FtMoveId_None << 24,
        ftMk_AttackS3S_Anim, /* +0C50 */
        NULL,
        ftCo_AttackS3_Phys,  /* 0x8008B940 */
        ftCo_AttackS3_Coll,  /* 0x8008B960 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMk_MS_SpecialHiEnd = 370
        324,
        0x00000000,
        FtMoveId_None << 24,
        ftMk_SpecialHiEnd_Anim, /* +2BD8 */
        NULL,
        ftMk_SpecialHiEnd_Phys, /* +2C58 */
        ftMk_SpecialHi_AirColl, /* +1F10 */
        ftCamera_UpdateCameraBox,
    },
};

/* The 14 slots PlMk.dat exports. Every other field is NULL: the registry takes MxDt's default,
 * Mario's function, for slots 1, 18, 25 and 40 (run-source/rel09-ace-native/metaknight/
 * mxdt.txt). He has no article in his MxDt item lookup and no itFunction block. Written and
 * never run, so not MU_AK_READY. */
const MuAkFighter mu_ak_metaknight = {
    .name = "Meta Knight",
    .file = "PlMk.dat",

    .onload = ftMk_Init_OnLoad,
    .specialn = ftMk_SpecialN_Enter,
    .specialairn = ftMk_SpecialAirN_Enter,
    .specials = ftMk_SpecialS_Enter,
    .specialairs = ftMk_SpecialAirS_Enter,
    .specialhi = ftMk_SpecialHi_Enter,
    .specialairhi = ftMk_SpecialAirHi_Enter,
    .speciallw = ftMk_SpecialLw_Enter,
    .specialairlw = ftMk_SpecialAirLw_Enter,
    .onactionstatechange = ftMk_Init_OnActionStateChange,
    .enterdoublejump = ftMk_Init_EnterDoubleJump,
    .onlanding = ftMk_Init_OnLanding,
    .onsmashf = ftMk_Init_OnSmashF,

    .move_logic = ftMk_Init_MotionStateTable,
    .move_logic_count = ftMk_MS_SelfCount,

    .flags = 0,
};
