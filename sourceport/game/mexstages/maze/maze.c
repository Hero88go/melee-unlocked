/* GrMaze +C8: choose one random floor variant, hide the others, sync collision and raise
 * point 4 to the floor under x=0. Map_Create +2EC equals mu_gr_map_create_plain. */
#include "../common/mu_gr_common.h"
#include <melee/gr/grdatfiles.h>
#include <melee/gr/ground.h>
#include <melee/gr/stage.h>
#include <melee/lb/lb_00B0.h>
#include <melee/mp/mplib.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/random.h>

static StageCallbacks maze_map[10];
static void maze_init(void)
{
    struct UnkStageDat_x8_t* maps = DP(grDatFiles_801C6330(0)->unk4->unk8);
    struct UnkStageDat_x8_t* floor = &maps[2];
    GrJoint* variants = DP(floor->unk20);
    int i, chosen;
    Vec3 hit;
    float top, bottom;
    for (i = 0; i < mu_gr_map_count(); i++) mu_gr_map_create_plain(i);
    chosen = HSD_Randi(floor->unk24);
    for (i = 0; i < floor->unk24; i++) {
        if (i != chosen) HSD_JObjSetFlagsAll(Ground_801C3FA4(Ground_GetMapGObj(2), variants[i].z), 0x10);
    }
    Ground_UpdateMapColl(Ground_GetMapGObj(2));
    mpLib_80058560();
    mu_gr_wind_gobj();
    Ground_801C39C0();
    Ground_801C3BB4();
    top = Stage_GetBlastZoneTopOffset();
    bottom = Stage_GetBlastZoneBottomOffset();
    if (mpCheckFloor(0, top, 0, bottom, 0, &hit, NULL, NULL, NULL, -1, -1, -1, NULL, NULL) &&
        stage_info.x280[4] != NULL && stage_info.x280[4]->translate.y < hit.y) {
        stage_info.x280[4]->translate.x = hit.x;
        stage_info.x280[4]->translate.y = hit.y + 10.0F;
        stage_info.x280[4]->translate.z = 0;
    }
}
static bool maze_shadow(Vec3* pos, int arg, HSD_JObj* jobj)
{
    Vec3 world;
    (void) arg;
    lb_8000B1CC(jobj, NULL, &world);
    return world.y < pos->y + 2.0F;
}
const MuGrStage mu_gr_underground_maze = {
    .name = "Underground Maze", .file = "GrMaze.dat", .map_gobjs = maze_map,
    .on_init = maze_init, .on_demo_init = mu_gr_demo_nop,
    .on_load = mu_gr_event_nop, .on_start = mu_gr_event_nop, .callback4 = mu_gr_false,
    .on_touch_line = mu_gr_no_touch_line, .on_check_shadow_render = maze_shadow, .flags = MU_GR_READY,
};
