#ifndef MU_TARGETS_LIB_H
#define MU_TARGETS_LIB_H
#include "../common/mu_gr_common.h"

void targets_init(void);
void targets_points_create(Ground_GObj*);
void targets_coll_create(Ground_GObj*);
void targets_coll_frame(Ground_GObj*);
void targets_floor_frame(Ground_GObj*);
bool targets_unk(Ground_GObj*);

#define TARGETS_ROW(create, frame, flags) \
    { create, targets_unk, frame, mu_gr_gobj_nop, flags }
#define TARGETS_EXPORTS(table, touch) \
    .map_gobjs = table, .on_init = targets_init, \
    .on_load = mu_gr_event_nop, .on_start = mu_gr_event_nop, .on_touch_line = touch
#endif
