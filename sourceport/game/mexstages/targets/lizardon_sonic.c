/* GrTLz and GrTSn are the same block: +A0 touch callback and +2A4 hit table. */
#include "targets_lib.h"
#include <melee/lb/forward.h>
#include <melee/mp/mplib.h>
static lbColl_80008D30_arg1 target_hits[4] = {
    { 1, 22, 90, 100, 0, 80, 1, 1, 8 },
    { 1, 22, 270, 100, 0, 80, 1, 1, 8 },
    { 1, 22, 0, 100, 0, 80, 1, 1, 8 },
    { 1, 22, 180, 100, 0, 80, 1, 1, 8 },
};
static DynamicsDesc* target_touch(int line)
{
    int index;
    if (line == -1 || mpJointFromLine(line) != 1) return NULL;
    switch (mpLineGetKind(line)) {
    case 1: index = 0; break;
    case 2: index = 1; break;
    case 4: index = 2; break;
    case 8: index = 3; break;
    default: return NULL;
    }
    return (DynamicsDesc*) &target_hits[index];
}
static StageCallbacks target_map[3] = {
    TARGETS_ROW(targets_points_create, mu_gr_gobj_nop, 0),
    TARGETS_ROW(targets_coll_create, targets_coll_frame, 0),
    TARGETS_ROW(targets_coll_create, targets_floor_frame, 0xC0000000),
};
const MuGrStage mu_gr_targets_lizardon = {
    .name = "Targets!Lizardon", .file = "/GrTLz.dat",
    TARGETS_EXPORTS(target_map, target_touch), .flags = MU_GR_READY,
};
const MuGrStage mu_gr_targets_sonic = {
    .name = "Targets!Sonic", .file = "/GrTSn.dat",
    TARGETS_EXPORTS(target_map, target_touch), .flags = MU_GR_READY,
};
