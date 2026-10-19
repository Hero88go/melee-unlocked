/* GrBeat exports 1 and 3 to 9. Four map gobjs; background and floor use the same six-state
 * animation machine, and only the floor updates collision and plays fallback sound cues. */
#include "beatblock.h"
#include "../common/mu_gr_mex.h"
#include <melee/ft/types.h>
#include <melee/gr/granime.h>
#include <melee/gr/ground.h>
#include <melee/lb/lb_00B0.h>
#include <melee/mp/mplib.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/memory.h>

typedef struct BeatVars { u8 state; } BeatVars;
static BeatVars* beat_vars(HSD_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj); return (BeatVars*) &gp->u;
}
static bool beat_false(Ground_GObj* gobj) { (void) gobj; return false; }
static void beat_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
    grAnime_801C8138(gobj, gp->map_id, 0);
}
static void beat_coll(Ground_GObj* gobj) { Ground_UpdateMapColl(gobj); }
static void beat_change(Ground_GObj* gobj, int state, bool floor)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, state); beat_vars(gobj)->state = state;
    if (floor && beat_fallback) {
        if (state == 1 || state == 4) mu_gr_mex_sfx(0);
        else if (state == 2 || state == 5) mu_gr_mex_sfx(1);
    }
}
static void beat_animate(Ground_GObj* gobj, bool floor)
{
    int state = beat_vars(gobj)->state, next = -1;
    int small = beat_pulses[0].active | beat_pulses[1].active | beat_pulses[2].active;
    int fourth = beat_pulses[3].active;
    switch (state) {
    case 0: if (small) next = 1; else if (fourth) next = 2; break;
    case 1: if (grAnime_801C83D0(gobj, false, 7)) next = 0; else if (fourth) next = 2; break;
    case 2: if (grAnime_801C83D0(gobj, false, 7)) next = 3; break;
    case 3: if (small) next = 4; else if (fourth) next = 5; break;
    case 4: if (grAnime_801C83D0(gobj, false, 7)) next = 3; else if (fourth) next = 5; break;
    case 5: if (grAnime_801C83D0(gobj, false, 7)) next = 0; break;
    }
    if (next >= 0) beat_change(gobj, next, floor);
    if (floor) Ground_UpdateMapColl(gobj);
}
static void beat_background(Ground_GObj* gobj) { beat_animate(gobj, false); }
static void beat_floor(Ground_GObj* gobj) { beat_animate(gobj, true); }
/* +1020: previous-motion initialization follows the recorded review in swordplay.
 * The defined wall-jump suppression and wall-side correction match both blocks. */
MU_NO_CONTRACT static void beat_no_walljump(HSD_GObj* gobj)
{
    int (*previous)[2] = HSD_GObjGetUserData(gobj);
    int i, j;
    for (i = 0; i < 6; i++) for (j = 0; j < 2; j++) {
        Fighter_GObj* fighter = Player_GetEntityAtIndex(i, j);
        if (fighter != NULL) {
            Fighter* fp = HSD_GObjGetUserData(fighter);
            int motion = fp->motion_id;
            if (motion == 0xFC) fp->wall_jump_input_timer = 254;
            else if (motion == 0x1D && previous[i][j] == 0xFD && (fp->coll_data.env_flags & 0xFFF)) {
                double step = (fp->coll_data.env_flags & 0x3F) ? -0.1 : 0.1;
                fp->cur_pos.x = (float) ((double) fp->cur_pos.x + step);
            }
            previous[i][j] = motion;
        }
    }
}
static void beat_load(void)
{
    HSD_GObj* gobj;
    int (*previous)[2];
    int i, j;
    beat_sync_create();
    gobj = GObj_Create(17, 13, 0); previous = HSD_MemAlloc(12 * sizeof(int));
    for (i = 0; i < 6; i++) for (j = 0; j < 2; j++) previous[i][j] = -1;
    GObj_InitUserData(gobj, 4, HSD_Free, previous);
    HSD_GObj_SetupProc(gobj, beat_no_walljump, 6);
}
#define BEAT_ROW(frame) { beat_create, beat_false, frame, mu_gr_gobj_nop, 0 }
static StageCallbacks beat_map[4] = {
    BEAT_ROW(beat_coll), BEAT_ROW(beat_coll), BEAT_ROW(beat_background), BEAT_ROW(beat_floor),
};
static void beat_init(void)
{
    int i;
    for (i = 0; i < 4; i++) mu_grTSeak_80223908(i);
    mpLib_80058560(); Ground_801C39C0(); Ground_801C3BB4(); mu_gr_wind_gobj();
}
static bool beat_shadow(Vec3* pos, int arg, HSD_JObj* jobj)
{
    Vec3 world; (void) arg;
    lb_8000B1CC(jobj, NULL, &world);
    return world.y < pos->y + 2.0F;
}
const MuGrStage mu_gr_beat_block_galaxy = {
    .name = "Beat Block Galaxy", .file = "GrBeat.dat", .map_gobjs = beat_map,
    .on_init = beat_init, .on_demo_init = mu_gr_demo_nop, .on_load = beat_load,
    .on_start = mu_gr_event_nop, .callback4 = mu_gr_false,
    .on_touch_line = mu_gr_no_touch_line, .on_check_shadow_render = beat_shadow, .flags = MU_GR_READY,
};
