/* Native, typed storage for the trail routines embedded in PlRc.dat.
 * The nine configurations use follow mode 4, lifetime mode 0, curve mode 2.
 * Unreachable item-owner and other follow modes are not exposed here. */
#ifndef MU_ACE_SKULLKID_TRAIL_H
#define MU_ACE_SKULLKID_TRAIL_H
#include "skullkid.h"
#include <dolphin/mtx.h>

typedef struct ftSd_TrailSpec {
    int motion, follow_mode, lifetime_mode, curve_mode, history_count;
    float start, end, fade_start;
    int bone;
    Vec3 offset, scale, axis, plane;
    u32 color, color2;
    int unused;
} ftSd_TrailSpec;

typedef struct ftSd_TrailMatrix {
    struct ftSd_TrailMatrix *prev, *next;
    Mtx previous, current;
} ftSd_TrailMatrix;

typedef struct ftSd_TrailController {
    HSD_GObj *fighter, *active;
    HSD_Archive* archive;
    const ftSd_TrailSpec* specs;
    int spec_count;
} ftSd_TrailController;

typedef struct ftSd_TrailData {
    HSD_GObj* fighter;
    HSD_GObj* controller;
    HSD_JObj *anchor, *fighter_root;
    const ftSd_TrailSpec* spec;
    ftSd_TrailMatrix *history, *model;
    int model_segments;
    float frame;
} ftSd_TrailData;

typedef struct ftSd_TrailSegment {
    Vec3 start, control1, end, control2;
    float length;
} ftSd_TrailSegment;

void ftSd_Trail_Controls(MtxPtr before, MtxPtr start, MtxPtr end, MtxPtr after,
                        MtxPtr offset, ftSd_TrailSegment* out);
void ftSd_Trail_Sample(ftSd_TrailSegment* segment, MtxPtr start, MtxPtr end,
                      float t, MtxPtr out);
void ftSd_Trails_OnLoad(HSD_GObj* gobj);
#endif

