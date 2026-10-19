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
 * to 252): a stage whose MxDt row names any lists their code in .items (its itFunction blocks as
 * ItemLogicTables, in the order of the row's item list, .item_count of them); the layer registers
 * them at stage load, and stage code gets the ItemKind of its item `index` from mu_gr_item_kind,
 * the console's 803D708C. Until the layer does that registration, a row that names stage items
 * stays locked whatever its descriptor says.
 *
 * The sources live under sourceport/game/mexstages/<stage>/ (CMake globs mexstages/<dir>/*.c and
 * builds the registry from the descriptor lines; option MU_MEX_STAGES). Shared helpers for the
 * patterns m-ex stage code repeats are in mexstages/common/mu_gr_common.h. Field meanings, the
 * console traps and the work packages: run-source/rel09-stages/WORKPACKAGES.md. */
#ifndef MU_MEX_STAGE_H
#define MU_MEX_STAGE_H

#include <Runtime/platform.h>
#include <melee/gr/forward.h>
#include <melee/gr/types.h>
#include <melee/it/forward.h>
#include <melee/it/kinds/forward.h>
#include <sysdolphin/baselib/forward.h>

#define MU_GR_READY 1u

/* grTSeak_80223908 as m-ex patched it (grTSeak_80223908+28): create map gobj `map_id` from the
 * current row's map gobj table (on_init, callback3, gobj_proc of that entry). Every m-ex stage block
 * calls it; gr/grtseak.c exports it under this name (MU_NATIVE). */
HSD_GObj* mu_grTSeak_80223908(int map_id);

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
    const ItemLogicTable* items;         /* the stage items' code (itFunction 0, 1, ...), or NULL */
    int item_count;
    unsigned int flags;                  /* MU_GR_READY */
} MuGrStage;

/* The m-ex stage item kinds. */
#define MU_GR_ITEM_FIRST 237
#define MU_GR_ITEM_LAST 252

/* Stage item data (shim/mu_mex_stage.c): it_8026B40C stores the articles of kinds past retail
 * here; Item_80267978 takes an article and logic table for them from mu_gr_stage_item. */
void mu_gr_stage_item_store(int kind, void* article);
int mu_gr_stage_item(int kind, void** article, void** logic);

/* 803D708C: the ItemKind the current stage's item `index` (its place in the row's item list) was
 * registered as, -1 when it has none. The stage twin of the fighters' mu_ak_article_kind. */
ItemKind mu_gr_item_kind(int index);

#endif
