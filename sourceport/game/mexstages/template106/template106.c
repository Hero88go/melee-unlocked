/* ACE 2.0.0's shared stage block: Arena Ferox (GrAr.dat), Bowser's Castle (GrKpCst.dat), Cave of
 * the Past (GrCop.dat), Mementos (GrMe (1).dat) and WarioWare (GrWw.dat) carry the same 106
 * instruction grFunction block, byte for byte (no symbols; disassembly:
 * run-source/rel09-stages/out_dis_GrAr.txt, `py -3.12 grdis.py ace GrAr.dat`).
 *
 * Exports 3 to 9 only; export 1 (the map gobj table) is absent, so the MxDt row's word stands:
 * Sheik's target stage table for four of them, Yoshi's Island 64's for Mementos. The block never
 * runs a table callback: on_init creates every map gobj of the file itself (Map_Create:
 * mu_gr_map_create_plain), adds the wind gobj, then the camera range and blast zone.
 *
 * The table still matters for its flags: Ground_GetStageGObj reads flags_b2 of entry map_id (a
 * camera gobj for the map gobj) and the light and fog searches read b0 and b1. Sheik's table has 4
 * entries and Arena Ferox has 8 map gobjs, Bowser's Castle 7, WarioWare 6: the console reads past
 * the table into grTSk_StageData and a string. mexgr_sheik_flags[] holds the words it reads there
 * (from the retail main.dol, rel09-stages WORKPACKAGES.md "map table overreads"), so a native
 * gobj gets the flags the console gives it: Arena Ferox's map gobj 7 gets b1 and b2 (a camera gobj,
 * its descriptor has a camera). Mementos (6 map gobjs, the 6 entry table of Yoshi's Island 64)
 * stays in bounds and keeps its row's table. */
#include "../common/mu_gr_common.h"

#include <melee/gr/ground.h>
#include <melee/gr/types.h>

/* on_init (block +0) */
static void mexgr_template_init(void)
{
    const int count = mu_gr_map_count();
    int i;
    for (i = 0; i < count; i++) {
        mu_gr_map_create_plain(i);
    }
    mu_gr_wind_gobj();
    Ground_801C39C0();
    Ground_801C3BB4();
}

/* Sheik's target stage table (grtseak.c grTSk_StageCallbacks, 4 entries) and the 4 rows the console
 * reads after it. Only the flag words are read for these stages; the callbacks stay NULL. */
static StageCallbacks mexgr_sheik_flags[8] = {
    { NULL, NULL, NULL, NULL, 0 },
    { NULL, NULL, NULL, NULL, 0 },
    { NULL, NULL, NULL, NULL, 0xC0000000 },
    { NULL, NULL, NULL, NULL, 0 },
    { NULL, NULL, NULL, NULL, 0x803E94B8 },   /* grTSk_StageData word 1 (its callbacks) */
    { NULL, NULL, NULL, NULL, 0x802238DC },   /* grTSk_StageData word 6 (on_start) */
    { NULL, NULL, NULL, NULL, 0 },            /* grTSk_StageData word 11 (joints) */
    { NULL, NULL, NULL, NULL, 0x6E207420 },   /* "n t " of the "couldn t get gobj" string */
};

#define MEXGR_TEMPLATE(table)                                                                      \
    .map_gobjs = (table), .on_init = mexgr_template_init, .on_demo_init = mu_gr_demo_nop,             \
    .on_load = mu_gr_event_nop, .on_start = mu_gr_event_nop, .callback4 = mu_gr_false,             \
    .on_touch_line = mu_gr_no_touch_line, .on_check_shadow_render = mu_gr_shadow_always,           \
    .flags = MU_GR_READY

const MuGrStage mu_gr_arena_ferox = { .name = "Arena Ferox", .file = "GrAr.dat", MEXGR_TEMPLATE(mexgr_sheik_flags) };
const MuGrStage mu_gr_bowsers_castle = { .name = "Bowser's Castle", .file = "GrKpCst.dat", MEXGR_TEMPLATE(mexgr_sheik_flags) };
const MuGrStage mu_gr_cave_of_the_past = { .name = "Cave of the Past", .file = "GrCop.dat", MEXGR_TEMPLATE(mexgr_sheik_flags) };
const MuGrStage mu_gr_mementos = { .name = "Mementos", .file = "GrMe (1).dat", MEXGR_TEMPLATE(NULL) };
const MuGrStage mu_gr_warioware = { .name = "WarioWare", .file = "GrWw.dat", MEXGR_TEMPLATE(mexgr_sheik_flags) };
