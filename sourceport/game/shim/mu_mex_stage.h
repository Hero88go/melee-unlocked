/* m-ex stages, native: the descriptor a stage with its own code registers (phase B of
 * run-source/rel09-stages/LAYER.md).
 *
 * An m-ex stage file may carry a "grFunction" code block whose exports replace words of its MxDt
 * descriptor row (MexTK grFunction.txt): 1 the map gobj table, 3 on_init, 4 on_demo_init, 5 on_load,
 * 6 on_start, 7 callback4, 8 on_touch_line, 9 on_check_shadow_render. The Source Port never runs
 * that PowerPC: a stage's code is C under sourceport/game/stages/<stage>/, described by one
 * MuGrStage. The layer (shim/mu_mex_stage.c) finds it by the file name MxDt gives the row, exactly
 * as the disc spells it ("GrMe (1).dat" keeps its space and parentheses), and puts each non-NULL
 * field into the native row. A NULL field keeps what the MxDt row names: a retail callback, found
 * by matching the row against the retail rows, the way a stage file without that export keeps it.
 * A row word of 0 or -1 that no descriptor fills keeps the stage locked.
 *
 * Write the descriptor line exactly like this, at the start of a line, so the build can list it:
 *
 *     const MuGrStage mu_gr_<name> = { ... };
 *
 * A stage is offered to a match only with .flags = MU_GR_READY. Stage items (m-ex item kinds 237
 * to 252) are not supported yet: a row that names any stays locked whatever its descriptor says. */
#ifndef MU_MEX_STAGE_H
#define MU_MEX_STAGE_H

#include <Runtime/platform.h>
#include <melee/gr/forward.h>
#include <melee/gr/types.h>

#define MU_GR_READY 1u

typedef struct MuGrStage {
    const char* name;                    /* "Arena Ferox" */
    const char* file;                    /* the stage file MxDt names, "GrAr.dat" */
    StageCallbacks* map_gobjs;           /* export 1: the map gobj table */
    Event on_init;                       /* 3 */
    void (*on_demo_init)(int);           /* 4 */
    Event on_load;                       /* 5 */
    Event on_start;                      /* 6 */
    Predicate callback4;                 /* 7 */
    GrTouchLineCallback on_touch_line;   /* 8 */
    GrCheckShadowRenderCallback on_check_shadow_render;   /* 9 */
    unsigned int flags;                  /* MU_GR_READY */
} MuGrStage;

#endif
