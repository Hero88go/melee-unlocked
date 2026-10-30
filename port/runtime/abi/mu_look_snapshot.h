/* Read-only fighter geometry for the Source Port's low-detail views. */
#pragma once
#include <math.h>

#define MU_LOOK_CAPSULES 15
#define MU_LOOK_BONES 192
#define MU_LOOK_STAGE_LINES 1536

/* Upper scene bits preserve the v1 struct layout; the low byte remains the major scene. */
#define MU_LOOK_SCENE_PAUSED 0x100
#define MU_LOOK_SCENE_ENDED 0x200

/* Hurtbox radii are bone-local, while their endpoints are already in world space.
 * Read the cached matrix only: setting up a joint here would mutate simulation state. */
static inline float mu_look_world_radius(float radius, const float matrix[3][4])
{
    const float x = matrix[0][0] * matrix[0][0] + matrix[0][1] * matrix[0][1] + matrix[0][2] * matrix[0][2];
    const float y = matrix[1][0] * matrix[1][0] + matrix[1][1] * matrix[1][1] + matrix[1][2] * matrix[1][2];
    return radius * sqrtf(x > y ? x : y);
}
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

/* Separate optional export: the original geometry snapshot ABI keeps its size. */
#define MU_LOOK_ITEMS 128
typedef struct MuLookItem {
    int type, state, owner, missile_type, charge_level;
    float facing, vx, vy, x, y;
} MuLookItem;
typedef struct MuLookItems {
    int count;
    MuLookItem items[MU_LOOK_ITEMS];
} MuLookItems;
