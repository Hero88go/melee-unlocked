/* GrWiiSword exports 1, 3, 5, 6, 8. Its two outer segments lower when fewer players
 * remain alive. A stage-local debug menu supplies the three parameters in the block. */
#include "swordplay.h"
#include <melee/ft/types.h>
#include <melee/gr/granime.h>
#include <melee/gr/grdatfiles.h>
#include <melee/gr/ground.h>
#include <melee/gr/stage.h>
#include <melee/lb/lb_00B0.h>
#include <melee/mp/mplib.h>
#include <melee/mp/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/memory.h>

typedef struct SwordVars { int alive; float bottom; } SwordVars;
static SwordVars* sword_vars(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj); return (SwordVars*) &gp->u;
}
static bool sword_false(Ground_GObj* gobj) { (void) gobj; return false; }
static void sword_plain(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
    grAnime_801C8138(gobj, gp->map_id, 0);
}
static void sword_plain_frame(Ground_GObj* gobj) { Ground_UpdateMapColl(gobj); }
static int sword_alive(void)
{
    int i, count = 0;
    for (i = 0; i < 6; i++) if (Player_GetPlayerSlotType(i) != 3 && Player_GetStocks(i) > 0) count++;
    return count;
}
static void sword_max_y(struct UnkStageDat_x8_t* data, HSD_JObj* joint, float* max)
{
    CollJoint* coll = mpGetGroundCollJoint();
    GrJoint* bindings = DP(data->unk20);
    int i;
    for (i = 0; i < data->unk24; i++) {
        CollJoint* row = &coll[bindings[i].x];
        if (row->x20 == joint && *max < row->bounding_max.y) *max = row->bounding_max.y;
    }
    if (joint->child != NULL) sword_max_y(data, joint->child, max);
    if (joint->next != NULL) sword_max_y(data, joint->next, max);
}
static void sword_segment(Ground_GObj* gobj, HSD_JObj* segment, bool up, bool animate)
{
    if (up) {
        float y = animate ? segment->translate.y + sword_speed : 0.0F;
        segment->translate.y = y > 0.0F ? 0.0F : y;
    } else {
        float bottom = sword_vars(gobj)->bottom;
        float y = animate ? segment->translate.y - sword_speed : bottom;
        segment->translate.y = y < bottom ? bottom : y;
    }
    HSD_JObjSetMtxDirtySub(segment);
}
static void sword_floor_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    SwordVars* v = sword_vars(gobj);
    struct UnkStageDat_x8_t* data;
    HSD_JObj* root;
    Vec3 world;
    float max = -50000.0F, size, scale;
    int i;
    sword_plain(gobj);
    data = &DP(grDatFiles_801C6330(gp->map_id)->unk4->unk8)[gp->map_id];
    root = Ground_801C3FA4(gobj, 0);
    sword_max_y(data, root->child, &max);
    max -= 32.5F;
    lb_8000B1CC(root, NULL, &world);
    size = max - world.y;
    v->bottom = (Stage_GetBlastZoneBottomOffset() - 1.0F) - size;
    v->alive = sword_spoof ? sword_players : sword_alive();
    sword_segment(gobj, Ground_801C3FA4(gobj, 2), v->alive > 3, false);
    sword_segment(gobj, Ground_801C3FA4(gobj, 11), v->alive > 2, false);
    scale = v->alive > 3 ? 1.0F : v->alive == 3 ? 0.8F : 0.5F;
    for (i = 0; i < 4; i++) {
        HSD_JObj* spawn = stage_info.x280[i];
        spawn->translate.x *= scale;
        HSD_JObjSetMtxDirtySub(spawn);
    }
}
static void sword_floor_frame(Ground_GObj* gobj)
{
    SwordVars* v = sword_vars(gobj);
    if (sword_spoof) v->alive = sword_players;
    else { int count = sword_alive(); if (v->alive > count) v->alive = count; }
    sword_segment(gobj, Ground_801C3FA4(gobj, 2), v->alive > 3, true);
    sword_segment(gobj, Ground_801C3FA4(gobj, 11), v->alive > 2, true);
    Ground_UpdateMapColl(gobj);
}
/* +F04: same transition detector as GrBeat +1020. Previous motion is unspecified on console
 * allocation; use -1 initially, as recorded in jev_questions.json and jev_answer.json. */
MU_NO_CONTRACT static void sword_no_walljump(HSD_GObj* gobj)
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
static void sword_load(void)
{
    HSD_GObj* gobj = GObj_Create(17, 13, 0);
    int (*previous)[2] = HSD_MemAlloc(sizeof(int) * 12);
    int i, j;
    for (i = 0; i < 6; i++) for (j = 0; j < 2; j++) previous[i][j] = -1;
    GObj_InitUserData(gobj, 4, HSD_Free, previous);
    HSD_GObj_SetupProc(gobj, sword_no_walljump, 6);
}
#define SWORD_ROW(create, frame) { create, sword_false, frame, mu_gr_gobj_nop, 0 }
static StageCallbacks sword_map[3] = {
    SWORD_ROW(sword_plain, sword_plain_frame), SWORD_ROW(sword_plain, sword_plain_frame),
    SWORD_ROW(sword_floor_create, sword_floor_frame),
};
static void sword_init(void)
{
    mu_grTSeak_80223908(0); mu_grTSeak_80223908(1);
    Ground_801C39C0(); Ground_801C3BB4();
    mu_grTSeak_80223908(2); mpLib_80058560(); mu_gr_wind_gobj();
    sword_menu_init();
}
const MuGrStage mu_gr_swordplay_arena = {
    .name = "Swordplay Arena", .file = "GrWiiSword.dat", .map_gobjs = sword_map,
    .on_init = sword_init, .on_load = sword_load, .on_start = mu_gr_event_nop,
    .on_touch_line = mu_gr_no_touch_line, .flags = MU_GR_READY,
};
