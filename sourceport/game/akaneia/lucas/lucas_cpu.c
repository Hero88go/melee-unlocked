/* Akaneia's Lucas: CPU control. m-ex's "MexCPU" helper, linked into PlLc.dat: CPU Lucas runs the
 * vanilla CPU logic while his fighter kind is temporarily spoofed to Ness.
 *
 * On console OnLoad cannot touch the CPU proc (the fighter's procs are set up after OnLoad), so it
 * installs a one-shot proc that finds Fighter_procCpu on the fighter GObj, removes it, and puts
 * the spoofing proc at the same priority. */
#include "lucas.h"

#include <melee/ft/fighter.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>

/* .bss.mexcpu_spoof (code+0x428C): the kind to impersonate. */
static FighterKind ftLc_CpuSpoofKind;

/* .bss.mexcpu_data (code+0x4288) is a pointer to a custom CPU table (m-ex "MexCPU_ProcCustom",
 * code+0x5C68, which patches the Fighter_804D64FC tables for kind 32 and calls a custom handler).
 * Nothing in PlLc.dat ever sets it, so Lucas always takes the spoof path below; the custom path is
 * not carried over (see NOTES.md). */

/* code+0x5E28 "MexCPU_ProcSpoof": Fighter_procCpu with fp->kind swapped for the frame. */
static void ftLc_CpuProcSpoof(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    FighterKind kind;
    if (fp->is_sleeping || !ftCo_IsCpuControlled(fp)) {
        return;
    }
    kind = fp->kind;
    fp->kind = ftLc_CpuSpoofKind;
    /* code+0x6C54 "MexCPU_Process" with no custom table is ftCo_800B3900 without its Ice Climbers
     * partner step (ftCo_800B0AF4), which returns at once for anyone but Popo. */
    ftCo_800B3900(gobj);
    fp->kind = kind;
}

/* code+0x4290 "MexCPU_InitProc": swap Fighter_procCpu for the spoofing proc. It stays installed
 * and finds nothing to swap on later frames, as on console. */
static void ftLc_CpuInitProc(HSD_GObj* gobj)
{
    HSD_GObjProc* proc;
    for (proc = gobj->proc; proc != NULL; proc = proc->child) {
        if (proc->on_invoke == Fighter_procCpu) {
            HSD_GObjProc_RemoveProc(proc);
            HSD_GObj_SetupProc(gobj, ftLc_CpuProcSpoof, 2);
            return;
        }
    }
}

/* code+0x1084 "MexCPU_InitSpoofData". */
void ftLc_InitCpuSpoof(HSD_GObj* gobj, int spoof_kind)
{
    ftLc_CpuSpoofKind = spoof_kind;
    HSD_GObj_SetupProc(gobj, ftLc_CpuInitProc, 2);
}
