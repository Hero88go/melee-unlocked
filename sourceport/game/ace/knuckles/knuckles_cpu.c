/* PlKx.dat +0BD8/+3264/+4DCC/+5384: CPU control spoofed to m-ex internal 28.
 * The custom-data pointer is BSS and is never initialized by this fighter.
 * Each fighter owns its proc; there is no shared spoof-kind variable. */
#include "knuckles.h"
#include <melee/ft/fighter.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>

extern int mu_ak_kind_from_mex(int internal);

static void ftKx_CpuSpoof(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    FighterKind kind;
    if (fp->is_sleeping || !ftCo_IsCpuControlled(fp)) {
        return;
    }
    kind = fp->kind;
    fp->kind = (FighterKind) mu_ak_kind_from_mex(28);
    mu_ak_cpu_process(gobj, NULL);
    fp->kind = kind;
}

static void ftKx_CpuInit(HSD_GObj* gobj)
{
    HSD_GObjProc* proc;
    for (proc = gobj->proc; proc != NULL; proc = proc->child) {
        if (proc->on_invoke == Fighter_procCpu) {
            HSD_GObjProc_RemoveProc(proc);
            HSD_GObj_SetupProc(gobj, ftKx_CpuSpoof, 2);
            return;
        }
    }
}

void ftKx_InitCpu(HSD_GObj* gobj)
{
    HSD_GObj_SetupProc(gobj, ftKx_CpuInit, 2);
}
