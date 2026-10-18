/* Native, typed storage for the trail routines embedded in PlRc.dat.
 * The eight configurations use follow mode 4, lifetime mode 0, curve mode 2.
 * Unreachable item-owner and other follow modes are not exposed here. */
#ifndef MU_ACE_SMEWTWO_TRAIL_H
#define MU_ACE_SMEWTWO_TRAIL_H
#include "smewtwo.h"
#include <dolphin/mtx.h>

typedef struct ftSm_TrailSpec {
    int motion, follow_mode, lifetime_mode, curve_mode, history_count;
    float start, end, fade_start;
    int bone;
    Vec3 offset, scale, axis, plane;
    u32 color, color2;
    int unused;
} ftSm_TrailSpec;

typedef struct ftSm_TrailMatrix {
    struct ftSm_TrailMatrix *prev, *next;
    Mtx previous, current;
} ftSm_TrailMatrix;

typedef struct ftSm_TrailController {
    HSD_GObj *fighter, *active;
    HSD_Archive* archive;
    const ftSm_TrailSpec* specs;
    int spec_count;
} ftSm_TrailController;

typedef struct ftSm_TrailData {
    HSD_GObj* fighter;
    HSD_GObj* controller;
    HSD_JObj *anchor, *fighter_root;
    const ftSm_TrailSpec* spec;
    ftSm_TrailMatrix *history, *model;
    int model_segments;
    float frame;
} ftSm_TrailData;

typedef struct ftSm_TrailSegment {
    Vec3 start, control1, end, control2;
    float length;
} ftSm_TrailSegment;

void ftSm_Trail_Controls(MtxPtr before, MtxPtr start, MtxPtr end, MtxPtr after,
                        MtxPtr offset, ftSm_TrailSegment* out);
void ftSm_Trail_Sample(ftSm_TrailSegment* segment, MtxPtr start, MtxPtr end,
                      float t, MtxPtr out);
void ftSm_Trails_OnLoad(HSD_GObj* gobj);
#endif

