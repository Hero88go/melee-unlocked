/* ACE's playable Giga Bowser: CPU control. This is m-ex's "MexCPU" helper, linked into every
 * fighter file built with symbols: a CPU Giga Bowser runs the game's own CPU step while his
 * fighter kind is the retail Giga Bowser's for the length of the call.
 *
 * On the console OnLoad cannot touch the CPU proc (Fighter_Create sets the fighter's procs up
 * after OnLoad), so it installs a proc that finds Fighter_procCpu on the fighter GObj, removes it
 * and puts the posing proc at the same priority. */
#include "gigabowser.h"

#include <melee/ft/fighter.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>

_Static_assert(ftGkp_CpuSpoofKind == 0x1F, "the kind PlGkp.dat passes to MexCPU_InitSpoofData");

/* code+0x88 [.bss.mexcpu_data] is a pointer to a custom CPU table. Nothing in PlGkp.dat writes
 * it (its five relocations are all reads), so [MexCPU_ProcCustom] (code+0x130) and the custom
 * branch of [MexCPU_Process] (code+0x3F4) are never reached and are not carried over.
 * code+0x8C [.bss.mexcpu_spoof] only ever holds 0x1F: it is ftGkp_CpuSpoofKind here, so the
 * port keeps no state. */

/* code+0x2F0 [MexCPU_ProcSpoof] (proc, priority 2): Fighter_procCpu with fp->kind swapped for
 * the call. code+0x378 [MexCPU_Process] with no custom table is ftCo_800B33B0, ftCo_800B2AFC,
 * ftCo_800B2790, ftCo_800B3E04 and the counter at fp+0x1B04: the retail ftCo_800B3900 without
 * its Ice Climbers partner step, which is what mu_ak_cpu_process runs. */
static void ftGkp_Cpu_ProcSpoof(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    FighterKind kind;

    if (fp->is_sleeping || !ftCo_IsCpuControlled(fp)) {
        return;
    }
    kind = fp->kind;
    fp->kind = ftGkp_CpuSpoofKind;
    mu_ak_cpu_process(gobj, NULL);
    fp->kind = kind;
}

/* code+0x90 [MexCPU_InitProc] (proc, priority 2): swap Fighter_procCpu for the posing proc. It
 * stays installed and finds nothing to swap on later frames, as on the console. */
static void ftGkp_Cpu_InitProc(HSD_GObj* gobj)
{
    HSD_GObjProc* proc;

    for (proc = gobj->proc; proc != NULL; proc = proc->child) {
        if (proc->on_invoke == Fighter_procCpu) {
            HSD_GObjProc_RemoveProc(proc);
            HSD_GObj_SetupProc(gobj, ftGkp_Cpu_ProcSpoof, 2);
            return;
        }
    }
}

/* code+0x44 [MexCPU_InitSpoofData]. The console returns at once when mexcpu_data is set, which
 * it never is. */
void ftGkp_Cpu_InitSpoofData(HSD_GObj* gobj)
{
    HSD_GObj_SetupProc(gobj, ftGkp_Cpu_InitProc, 2);
}
