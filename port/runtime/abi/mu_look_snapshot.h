/* Read-only fighter geometry for the Source Port's low-detail views. */
#pragma once

#define MU_LOOK_CAPSULES 15
#define MU_LOOK_BONES 192
#define MU_LOOK_STAGE_LINES 1536
typedef struct MuLookCapsule { float ax, ay, bx, by, radius; } MuLookCapsule;
typedef struct MuLookBone { float ax, ay, bx, by; } MuLookBone;
typedef struct MuLookStageLine { float x0, y0, x1, y1; int floor, platform; } MuLookStageLine;
typedef struct MuLookFighter {
    int present, kind, external, costume, team, stocks;
    int action;
    float x, y, facing, percent, shield, animation;
    int capsule_count, bone_count;
    MuLookCapsule capsules[MU_LOOK_CAPSULES];
    MuLookBone bones[MU_LOOK_BONES];
} MuLookFighter;
typedef struct MuLookSnapshot {
    int major, minor, frame, stage, teams;
    int stage_line_count;
    MuLookStageLine stage_lines[MU_LOOK_STAGE_LINES];
    MuLookFighter fighters[4][2];
} MuLookSnapshot;
