/* Helpers for native m-ex stage code (sourceport/game/mexstages/<stage>/).
 *
 * m-ex stage blocks are small C files compiled with the m-ex SDK and share a handful of shapes:
 * empty exports, "create every map gobj of the file with plain collision and animation", a wind
 * gobj. Each helper below is one such shape, written once from the disassembly (rel09-stages
 * grdis.py output named at each one), so a stage file only spells out what is its own. */
#ifndef MU_GR_COMMON_H
#define MU_GR_COMMON_H

#include "../../shim/mu_mex_stage.h"

#include <dolphin/mtx.h>
#include <melee/gr/forward.h>
#include <sysdolphin/baselib/forward.h>

/* The empty exports most blocks carry (a lone blr, "li r3, 0; blr", "li r3, 1; blr"). */
void mu_gr_event_nop(void);                        /* on_load, on_start */
void mu_gr_demo_nop(int arg);                      /* on_demo_init */
bool mu_gr_false(void);                            /* callback4 */
DynamicsDesc* mu_gr_no_touch_line(int index);      /* on_touch_line: no dynamics */
bool mu_gr_shadow_always(Vec3* pos, int arg, HSD_JObj* jobj);   /* on_check_shadow_render: 1 */
void mu_gr_gobj_nop(HSD_GObj* gobj);               /* a map gobj callback that is a lone blr */

/* The map gobj count of the loaded stage file: grDatFiles_801C6330(0)->unk4->unkC. */
int mu_gr_map_count(void);

/* "Map_Create" (GrNRc.dat Map_Create, ACE's 106 instruction template sub_A8, and the helper set
 * of the PLAN section 2 "one author" stages): Ground_GetStageGObj(map_id), then x8 callback NULL,
 * xC callback an empty function, the stage display link (grDisplay_801C5DB0, link 3), collision
 * joints bound and animation set 0 started (Map_OnCreate), and a per-frame proc at priority 4 that
 * re-syncs the collision (Map_OnFrame: Ground_UpdateMapColl). Returns the gobj, NULL when
 * Ground_GetStageGObj gave none (nothing logged, as on the console). */
HSD_GObj* mu_gr_map_create_plain(int map_id);

/* The wind gobj many on_init exports make: GObj_Create(3, 5, 0) with lb_800115F4 as a priority 4
 * proc. Returns the gobj (unchecked, as on the console) so a stage can add more procs. */
HSD_GObj* mu_gr_wind_gobj(void);

/* ---- static decomp functions the blocks call (P4), exported under MU_NATIVE ----
 * grLast_8021B2D8 (gr/grlast.c, Final Destination's map gobj 9 callback1): "li r3, 0; blr" in the
 * retail main.dol, false. Smashketball (+05A8) and Midnight Marquee (+045C) call it directly; both
 * overwrite r3 right after, so the call does nothing there either. */
bool mu_grLast_8021B2D8(Ground_GObj* gobj);

#endif
