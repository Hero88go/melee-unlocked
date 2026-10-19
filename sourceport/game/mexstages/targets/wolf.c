/* GrTWf +A0: joint 1 returns the file's four directional hit descriptions. */
#include "targets_lib.h"
#include <melee/gr/ground.h>
#include <melee/lb/forward.h>
#include <melee/mp/mplib.h>

typedef struct WolfParams {
    DISC_PTR(lbColl_80008D30_arg1) hit[4];
} DISC_STRUCT WolfParams;

static DynamicsDesc* wolf_touch(int line)
{
    WolfParams* p = Ground_GetYakumonoParam();
    int index;
    if (line == -1 || mpJointFromLine(line) != 1) return NULL;
    switch (mpLineGetKind(line)) {
    case 1: index = 0; break;
    case 2: index = 1; break;
    case 4: index = 2; break;
    case 8: index = 3; break;
    default: return NULL;
    }
    /* The stage callback's historical DynamicsDesc return type also carries hit descriptions. */
    return (DynamicsDesc*) DP(p->hit[index]);
}

static StageCallbacks wolf_map[3] = {
    TARGETS_ROW(targets_points_create, mu_gr_gobj_nop, 0),
    TARGETS_ROW(targets_coll_create, targets_coll_frame, 0),
    TARGETS_ROW(targets_coll_create, targets_floor_frame, 0xC0000000),
};
const MuGrStage mu_gr_targets_wolf = {
    .name = "Targets!Wolf", .file = "/GrTWf.dat",
    TARGETS_EXPORTS(wolf_map, wolf_touch), .flags = MU_GR_READY,
};
