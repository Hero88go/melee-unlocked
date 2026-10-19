#include "targets_lib.h"
static StageCallbacks lucas_map[3] = {
    TARGETS_ROW(targets_points_create, mu_gr_gobj_nop, 0),
    TARGETS_ROW(targets_coll_create, targets_coll_frame, 0),
    TARGETS_ROW(targets_coll_create, targets_floor_frame, 0xC0000000),
};
const MuGrStage mu_gr_targets_lucas = {
    .name = "Targets!Lucas", .file = "/GrTLc.dat",
    TARGETS_EXPORTS(lucas_map, mu_gr_no_touch_line), .flags = MU_GR_READY,
};
