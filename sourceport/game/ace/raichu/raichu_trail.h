/* Native, typed storage for the trail routines embedded in PlRc.dat.
 * The nine configurations use follow mode 4, lifetime mode 0, curve mode 2.
 * Unreachable item-owner and other follow modes are not exposed here. */
#ifndef MU_ACE_RAICHU_TRAIL_H
#define MU_ACE_RAICHU_TRAIL_H
#include "raichu.h"
#include <dolphin/mtx.h>

typedef struct ftRc_TrailSpec {
    int motion, follow_mode, lifetime_mode, curve_mode, history_count;
    float start, end, fade_start;
    int bone;
    Vec3 offset, scale, axis, plane;
    u32 color, color2;
    int unused;
} ftRc_TrailSpec;

typedef struct ftRc_TrailMatrix {
    struct ftRc_TrailMatrix *prev, *next;
    Mtx previous, current;
} ftRc_TrailMatrix;

typedef struct ftRc_TrailController {
    HSD_GObj *fighter, *active;
    HSD_Archive* archive;
} ftRc_TrailController;

typedef struct ftRc_TrailData {
    HSD_GObj* fighter;
    HSD_GObj* controller;
    HSD_JObj *anchor, *fighter_root;
    const ftRc_TrailSpec* spec;
    ftRc_TrailMatrix *history, *model;
    int model_segments;
    float frame;
} ftRc_TrailData;

typedef struct ftRc_TrailSegment {
    Vec3 start, control1, end, control2;
    float length;
} ftRc_TrailSegment;

void ftRc_Trail_Controls(MtxPtr before, MtxPtr start, MtxPtr end, MtxPtr after,
                        MtxPtr offset, ftRc_TrailSegment* out);
void ftRc_Trail_Sample(ftRc_TrailSegment* segment, MtxPtr start, MtxPtr end,
                      float t, MtxPtr out);
void ftRc_Trail_OnLoad(HSD_GObj* gobj);
#endif
