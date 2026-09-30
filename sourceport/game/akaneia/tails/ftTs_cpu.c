/* Akaneia's Tails, native: CPU players.
 *
 * Tails ships an m-ex "MexCPU" setup: on the first frame it swaps the fighter's Fighter_procCpu
 * proc for its own, which (1) points the PlCo.dat per-kind CPU attack tables of kind 0x20 at
 * Tails's own tables and poses as kind 0x20 while the game's CPU logic runs, and (2) lets
 * CPU_Tails steer his specials (keep charging the spin dash, jump out of the roll toward a target
 * above, stay over ground during the side special, flap up during flight). Everything is restored
 * after the frame, as on the console. */
#include "ftTs.h"
#include "ftTs_hooks.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ftcmdscript.h>
#include <melee/ft/ftcpuattack.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/ft/types.h>
#include <melee/mp/mplib.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/random.h>

#define TS_CPU_KIND 0x20 /* the per-kind table slot m-ex borrows */

/* ------------------------------------------------------------------------------------------------
 * The attack tables (code+0x38A8 .. 0x3F20 and 0x6248), zero-terminated.
 * Fields: cmd, x04, x08, x0C, x10, x14, weight, x1C, min level.
 * --------------------------------------------------------------------------------------------- */
static const ftTails_CpuAttack ftTs_CpuGround[] = {
    { 27, 4, 6.0f, 50.0f, -1.0f, 8.0f, 1.0f, 30, 0 },
    { 39, 6, 6.1f, 38.9f, -1.4f, 5.6f, 3.0f, 30, 0 },
    { 5, 4, 2.0f, 16.0f, -2.0f, 12.0f, 3.0f, 3, 0 },
    { 10, 4, 2.0f, 14.0f, -2.0f, 7.0f, 2.0f, 3, 0 },
    { 6, 4, 1.0f, 8.0f, -1.0f, 15.0f, 3.0f, 3, 0 },
    { 4, 4, 2.0f, 10.0f, -1.0f, 10.0f, 5.0f, 8, 0 },
    { 12, 4, 2.0f, 16.0f, -1.0f, 11.0f, 6.0f, 8, 0 },
    { 13, 4, -7.0f, 7.0f, 5.0f, 18.0f, 5.0f, 8, 0 },
    { 4, 4, -9.0f, 9.0f, -1.0f, 5.0f, 5.0f, 8, 0 },
    { 60, 12, 0.0f, 1.0f, 0.0f, 12.5f, 3.0f, 8, 5 },
    { 0 },
};

static const ftTails_CpuAttack ftTs_CpuAir[] = {
    { 2, 4, -5.0f, 5.0f, -2.0f, 8.0f, 2.0f, 3, 0 },
    { 8, 4, 2.0f, 20.0f, -2.0f, 10.0f, 4.0f, 3, 0 },
    { 6, 4, -11.0f, 6.0f, 3.0f, 25.0f, 4.0f, 3, 0 },
    { 10, 4, 4.0f, 15.0f, -2.0f, -10.0f, 3.0f, 3, 0 },
    { 9, 4, -17.0f, 0.0f, -2.0f, 15.0f, 3.0f, 3, 0 },
    { 27, 4, 6.0f, 38.0f, -1.0f, 12.0f, 1.0f, 30, 0 },
    { 0 },
};

static const ftTails_CpuAttack ftTs_CpuRanged[] = {
    { 38, 2, 0.0f, 0.0f, 0.0f, 0.0f, 3.0f, 2, 0 },
    { 17, 2, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 7, 0 },
    { 0 },
};

static const ftTails_CpuAttack ftTs_CpuSmash[] = {
    { 40, 7, 0.1f, 10.0f, 4.2f, 10.0f, 1.0f, 1, 0 },
    { 41, 12, 8.0f, 18.9f, 4.2f, 10.0f, 1.0f, 1, 0 },
    { 0 },
};

static const ftTails_CpuAttack ftTs_CpuSpecial[] = {
    { 2, 2, 2.2f, 18.9f, 2.6f, 12.5f, 3.0f, 1, 0 },
    { 6, 5, -10.8f, 9.8f, -0.3f, 21.4f, 3.0f, 1, 0 },
    { 7, 5, -1.3f, 19.5f, 2.5f, 12.0f, 3.0f, 1, 0 },
    { 10, 7, 4.5f, 20.8f, -2.9f, 5.9f, 3.0f, 1, 0 },
    { 13, 7, -10.7f, 14.0f, -0.5f, 25.4f, 3.0f, 1, 0 },
    { 15, 12, 9.5f, 28.6f, -2.9f, 17.4f, 3.0f, 1, 0 },
    { 14, 6, -13.3f, 13.8f, -3.4f, 6.5f, 3.0f, 1, 0 },
    { 0 },
    { 0 },
};

static const ftTails_CpuAttack ftTs_CpuWeapon[] = {
    { 2, 5, 3.0f, 27.6f, -1.6f, 23.9f, 3.0f, 5, 0 },
    { 7, 12, -0.1f, 24.6f, -3.6f, 22.1f, 3.0f, 5, 0 },
    { 15, 21, -6.8f, 28.1f, -3.7f, 24.1f, 3.0f, 10, 0 },
    { 39, 10, 8.8f, 34.3f, 0.2f, 10.1f, 1.0f, 15, 5 },
    { 0 },
    { 0 },
};

static const ftTails_CpuAttack ftTs_CpuEdge[] = {
    { 17, 2, -5.0f, 75.0f, -45.0f, 12.0f, 1.0f, 90, 0 },
    { 8, 4, 3.0f, 21.0f, -12.0f, 5.0f, 4.0f, 9, 0 },
    { 14, 4, 2.0f, 18.0f, -12.0f, 4.0f, 6.0f, 12, 0 },
    { 46, 43, 20.0f, 75.0f, 10.0f, 65.0f, 3.0f, 30, 0 },
    { 47, 43, 15.0f, 55.0f, 10.0f, 60.0f, 3.0f, 30, 0 },
    { 5, 4, 2.0f, 30.0f, -8.5f, 16.0f, 3.0f, 3, 0 },
    { 0 },
};

/* code+0x6248 SpecialSAttacks: when to cut the side special short. */
static const ftTails_CpuAttack ftTs_CpuSpecialSAttacks[] = {
    { 38, 2, 0.0f, 20.0f, 0.0f, 10.0f, 3.0f, 1, 0 },
    { 0 },
};

/* ------------------------------------------------------------------------------------------------
 * Decisions (CPU_Tails)
 * --------------------------------------------------------------------------------------------- */

static inline void ftTs_CpuCmd(Fighter* fp, cmd_t cmd, int arg)
{
    ftCo_800B46B8(fp, cmd, (arg_t) arg);
}

/* code+0x5678 CPU_SpecialLw */
static bool ftTs_CPU_SpecialLw(Fighter* fp)
{
    FtMotionId msid = fp->motion_id;
    Fighter* target;
    float dist;

    if ((msid & ~2) == ftTs_MS_SpecialLwStart) {
        /* Start (ground or air): hold down to charge. */
        ftTs_CpuCmd(fp, CpuCmd_SetLstickX, 0);
        ftTs_CpuCmd(fp, CpuCmd_SetLstickY, -127);
        ftCo_800B49F4(fp);
        return true;
    }
    if (msid == ftTs_MS_SpecialLwCharge) {
        if ((double) HSD_Randf() < 0.2) {
            /* Let go: launch. */
            ftTs_CpuCmd(fp, CpuCmd_SetLstickX, 0);
            ftTs_CpuCmd(fp, CpuCmd_SetLstickY, 0);
            ftCo_800B463C(fp, CpuCmd_ReleaseAll);
            ftCo_800B49F4(fp);
        } else {
            /* Hold down and rev. */
            ftTs_CpuCmd(fp, CpuCmd_SetLstickX, 0);
            ftTs_CpuCmd(fp, CpuCmd_SetLstickY, -127);
            ftTs_CpuCmd(fp, CpuCmd_PressBFor, 1);
            ftTs_CpuCmd(fp, CpuCmd_ReleaseBFor, 5);
            ftCo_800B49F4(fp);
        }
        return true;
    }
    if ((u32) (msid - ftTs_MS_SpecialLwRun) > ftTs_MS_SpecialLwDive - ftTs_MS_SpecialLwRun) {
        return false;
    }

    /* Run, RunTurn, RunJump, Dive: a target close by and above? */
    target = fp->cpu.x44;
    if (target != NULL) {
        dist = fp->cur_pos.x - target->cur_pos.x;
        if (dist < 0.0f) {
            dist = -dist;
        }
        if (dist < 10.0f && target->cur_pos.y > fp->cur_pos.y) {
            if (fp->ground_or_air != GA_Ground) {
                ftTs_CpuCmd(fp, CpuCmd_SetLstickX, 0);
                ftTs_CpuCmd(fp, CpuCmd_SetLstickY, 0);
                ftTs_CpuCmd(fp, CpuCmd_PressAFor, 1);
                ftTs_CpuCmd(fp, CpuCmd_ReleaseAFor, 1);
                ftTs_CpuCmd(fp, CpuCmd_LstickXTowardFighter, 127);
                ftCo_800B49F4(fp);
                return true;
            }
            if (!((double) HSD_Randf() < (double) fp->cpu.level * 0.1)) {
                return false;
            }
            /* Jump out of the roll at it. */
            ftTs_CpuCmd(fp, CpuCmd_LstickXTowardDestination, 127);
            ftTs_CpuCmd(fp, CpuCmd_SetLstickY, 0);
            ftTs_CpuCmd(fp, CpuCmd_PressYFor, 1);
            ftTs_CpuCmd(fp, CpuCmd_ReleaseYFor, 1);
            dist = fp->cur_pos.y - fp->cpu.x44->cur_pos.y;
            if (dist < 0.0f) {
                dist = -dist;
            }
            if (dist > 12.0f) {
                /* Far above: double jump and attack on the way up. */
                ftTs_CpuCmd(fp, CpuCmd_PressYFor, 1);
                ftCo_800B463C(fp, CpuCmd_ReleaseY);
                ftTs_CpuCmd(fp, CpuCmd_WaitFor, HSD_Randi(5) + 10);
                ftTs_CpuCmd(fp, CpuCmd_SetLstickX, 0);
                ftTs_CpuCmd(fp, CpuCmd_SetLstickY, 0);
                ftTs_CpuCmd(fp, CpuCmd_PressAFor, 1);
                ftTs_CpuCmd(fp, CpuCmd_ReleaseAFor, 1);
                ftTs_CpuCmd(fp, CpuCmd_LstickXTowardDestination, 127);
                ftTs_CpuCmd(fp, CpuCmd_WaitFor, 10);
            }
            ftCo_800B49F4(fp);
            return true;
        }
    }

    /* Otherwise just roll toward the destination. */
    ftTs_CpuCmd(fp, CpuCmd_LstickXTowardDestination, 127);
    ftTs_CpuCmd(fp, CpuCmd_SetLstickY, 0);
    ftCo_800B49F4(fp);
    return true;
}

/* code+0x5A2C CPU_SpecialS */
static bool ftTs_CPU_SpecialS(Fighter* fp)
{
    Fighter* target;
    double ahead;
    float x;
    float dir;
    Vec3 floor_pos;
    Vec3 floor_normal;
    int floor_line;
    u32 floor_flags;

    if ((u32) (fp->motion_id - ftTs_MS_SpecialSStart) >
        ftTs_MS_SpecialAirSLoop4 - ftTs_MS_SpecialSStart)
    {
        return false;
    }
    target = fp->cpu.x44;
    if (target == NULL) {
        return false;
    }
    if (ftCo_800B4AB0(fp, target, (void*) ftTs_CpuSpecialSAttacks)) {
        /* In range: let go to finish the spin. */
        ftTs_CpuCmd(fp, CpuCmd_SetLstickX, 0);
        ftTs_CpuCmd(fp, CpuCmd_SetLstickY, 0);
        ftCo_800B463C(fp, CpuCmd_ReleaseAll);
        ftCo_800B49F4(fp);
        return true;
    }

    /* Ground 1.5 frames of movement ahead? Keep spinning toward the destination. */
    ahead = fp->pos_delta.x * 1.5;
    x = (float) (ahead + fp->cur_pos.x);
    if (mpCheckFloor(x, fp->cur_pos.y + 6.0f, x, fp->cur_pos.y - 1.0f, 0.0f, &floor_pos,
                     &floor_line, &floor_flags, &floor_normal, -1, -1, -1, NULL, NULL))
    {
        ftTs_CpuCmd(fp, CpuCmd_LstickXTowardDestination, 127);
        ftTs_CpuCmd(fp, CpuCmd_SetLstickY, 0);
        ftTs_CpuCmd(fp, CpuCmd_PressBFor, 1);
        ftCo_800B49F4(fp);
        return true;
    }

    /* No ground ahead: steer back and let go. */
    dir = fp->pos_delta.x > 0.0f ? 1.0f : -1.0f;
    ftTs_CpuCmd(fp, CpuCmd_SetLstickX, (s8) (int) (dir * -127.0f));
    ftTs_CpuCmd(fp, CpuCmd_SetLstickY, 0);
    ftCo_800B463C(fp, CpuCmd_ReleaseAll);
    ftCo_800B49F4(fp);
    return true;
}

/* code+0x5C10 CPU_SpecialHi: while below cpu.x54.y (console fp+0x1AE0, the recovery height),
 * keep flapping. */
static bool ftTs_CPU_SpecialHi(Fighter* fp)
{
    if ((u32) (fp->motion_id - ftTs_MS_SpecialHiStart) >
        ftTs_MS_SpecialHiCancel - ftTs_MS_SpecialHiStart)
    {
        return false;
    }
    ftTs_CpuCmd(fp, CpuCmd_LstickXTowardDestination, 127);
    ftTs_CpuCmd(fp, CpuCmd_SetLstickY, 0);
    if (fp->cpu.x54.y > fp->cur_pos.y) {
        ftTs_CpuCmd(fp, CpuCmd_PressBFor, 1);
        ftTs_CpuCmd(fp, CpuCmd_ReleaseBFor, 1);
    } else {
        ftTs_CpuCmd(fp, CpuCmd_ReleaseBFor, 1);
    }
    ftCo_800B49F4(fp);
    return true;
}

/* code+0x3858 CPU_Tails */
static void ftTs_CPU_Tails(Fighter* fp)
{
    if (ftTs_CPU_SpecialLw(fp)) {
        return;
    }
    if (ftTs_CPU_SpecialS(fp)) {
        return;
    }
    ftTs_CPU_SpecialHi(fp);
}

/* code+0x11D4 cpu_data */
const ftTails_CpuData ftTs_CpuData = {
    ftTs_CPU_Tails, 90.0f,
    ftTs_CpuGround, ftTs_CpuAir, ftTs_CpuRanged, ftTs_CpuSmash,
    ftTs_CpuSpecial, ftTs_CpuWeapon, ftTs_CpuEdge,
};

/* ------------------------------------------------------------------------------------------------
 * The MexCPU plumbing
 * --------------------------------------------------------------------------------------------- */

/* code+0x3F20 .bss mexcpu_data, code+0x633C .bss mexcpu_spoof */
static const ftTails_CpuData* ftTs_mexcpu_data;
static int ftTs_mexcpu_spoof;

/* The custom step of code+0x6290 MexCPU_Process, run between ftCo_800B2AFC and ftCo_800B2790:
 * only when no command script is playing. */
static void ftTs_MexCPU_Custom(Fighter* fp)
{
    if (ftTs_mexcpu_data == NULL || ftTs_mexcpu_data->decide == NULL) {
        return;
    }
    if (fp->cpu.csP != NULL || fp->cpu.command_duration != 0) {
        return;
    }
    ftCo_800B462C(fp);
    ftTs_mexcpu_data->decide(fp);
}

/* code+0x5CBC MexCPU_ProcCustom (proc, priority 2) */
static void ftTs_MexCPU_ProcCustom(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const ftTails_CpuData* data = ftTs_mexcpu_data;
    Fighter_VoidSlot* tables[7];
    u32 saved_tables[7];
    be_f32* distance;
    float saved_distance;
    const ftTails_CpuAttack* mine[7];
    FighterKind saved_kind;
    int i;

    if (fp->is_sleeping || !ftCo_IsCpuControlled(fp)) {
        return;
    }

    tables[0] = DP(Fighter_804D64FC->x4);
    tables[1] = DP(Fighter_804D64FC->x8);
    tables[2] = DP(Fighter_804D64FC->xC);
    tables[3] = DP(Fighter_804D64FC->x10);
    tables[4] = DP(Fighter_804D64FC->x14);
    tables[5] = DP(Fighter_804D64FC->x18);
    tables[6] = DP(Fighter_804D64FC->x1C);
    mine[0] = data->ground;
    mine[1] = data->air;
    mine[2] = data->ranged;
    mine[3] = data->smash;
    mine[4] = data->special;
    mine[5] = data->weapon;
    mine[6] = data->edge;
    distance = DP(Fighter_804D64FC->x20);

    /* Borrow kind 0x20's slots for the frame. */
    saved_kind = fp->kind;
    saved_distance = BEV(distance[TS_CPU_KIND]);
    for (i = 0; i < 7; i++) {
        saved_tables[i] = tables[i][TS_CPU_KIND].raw;
    }
    fp->kind = TS_CPU_KIND;
    BEV(distance[TS_CPU_KIND]) = data->distance;
    for (i = 0; i < 7; i++) {
        DISC_SET(tables[i][TS_CPU_KIND], (const void*) mine[i]);
    }

    mu_ak_cpu_process(gobj, ftTs_MexCPU_Custom);

    fp->kind = saved_kind;
    BEV(distance[TS_CPU_KIND]) = saved_distance;
    for (i = 0; i < 7; i++) {
        tables[i][TS_CPU_KIND].raw = saved_tables[i];
    }
}

/* code+0x5E7C MexCPU_ProcSpoof (proc, priority 2): without custom data, run the game's CPU as if
 * this were kind mexcpu_spoof (0). */
static void ftTs_MexCPU_ProcSpoof(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    FighterKind saved_kind;

    if (fp->is_sleeping || !ftCo_IsCpuControlled(fp)) {
        return;
    }
    saved_kind = fp->kind;
    fp->kind = ftTs_mexcpu_spoof;
    mu_ak_cpu_process(gobj, ftTs_MexCPU_Custom);
    fp->kind = saved_kind;
}

/* code+0x3F24 MexCPU_InitProc (proc, priority 2): swap the fighter's Fighter_procCpu proc for
 * ours. Stays installed but finds nothing to do after the first frame. */
static void ftTs_MexCPU_InitProc(Fighter_GObj* gobj)
{
    HSD_GObjProc* proc;

    for (proc = gobj->proc; proc != NULL; proc = proc->child) {
        if (proc->on_invoke == Fighter_procCpu) {
            HSD_GObjProc_RemoveProc(proc);
            if (ftTs_mexcpu_data != NULL) {
                HSD_GObj_SetupProc(gobj, ftTs_MexCPU_ProcCustom, 2);
            } else {
                HSD_GObj_SetupProc(gobj, ftTs_MexCPU_ProcSpoof, 2);
            }
            return;
        }
    }
}

/* code+0x11F8 MexCPU_InitCustomData (from OnLoad) */
void ftTs_MexCPU_InitCustomData(Fighter_GObj* gobj, const ftTails_CpuData* data)
{
    ftTs_mexcpu_data = data;
    HSD_GObj_SetupProc(gobj, ftTs_MexCPU_InitProc, 2);
}
