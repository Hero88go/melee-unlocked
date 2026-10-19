/* Saturn Valley (GrSv.dat grFunction), shared declarations of the native port.
 * Offsets in comments are GrSv.dat block offsets; "console +XX" is a byte offset of the
 * console's Ground user data (the stage keeps its variables from +E0 on). */
#ifndef MU_SATURNVALLEY_H
#define MU_SATURNVALLEY_H

#include "../common/mu_gr_common.h"
#include "../common/mu_gr_mex.h"

#include <melee/ft/types.h>
#include <melee/gr/types.h>
#include <melee/lb/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/sislib.h>

/* The coffee break's taunt table row (yakumono +0 points to a NULL ended list of these). */
typedef struct SvTaunt {
    DISC_PTR(char) name; /* +0: the m-ex display name, or NULL */
    s16 kind;            /* +4: compared with the native FighterKind */
    u16 right, left;     /* +6, +8: ftcmd rows for facing right, left */
} DISC_STRUCT SvTaunt;
typedef DISC_PTR(SvTaunt) SvTauntSlot;

/* The runner (map gobj 6) state table, 0x18 bytes per state (yakumono +4). */
typedef struct SvRunnerState {
    s32 anim, timer_min, timer_max, next, sfx;
    u8 collide, mode, camera, pad;
} DISC_STRUCT SvRunnerState;

/* The yakumono block. Field names keep their byte positions. */
typedef struct SvParam {
    DISC_PTR(SvTauntSlot) x00;     /* taunt rows */
    DISC_PTR(SvRunnerState) x04;   /* runner states */
    s32 x08, x0C;                  /* statue hit points, statue respawn frames */
    f32 x10, x14;                  /* Mr. Saturn spawn chance, the same with Nana in the match */
    f32 x18, x1C;                  /* turn range, turn step */
    s32 x20, x24;                  /* turn timer */
    f32 x28;                       /* retained walker: look turn step */
    s32 x2C, x30;                  /* retained walker: wait timer */
    f32 x34, x38;                  /* retained walker: look chance, look range */
    s32 x3C, x40;                  /* not read */
    f32 x44, x48;                  /* retained walker: walk step, walk on chance */
    u8 x4C[4], x50[4];             /* overlay colors A and B (RGBA); x4C[3] is the overlay alpha */
    f32 x54;                       /* overlay color step */
    s32 x58;                       /* coffee song */
    f32 x5C, x60, x64, x68;        /* music fade step, text size, text scroll step, text scale */
    s32 x6C;                       /* coffee message count */
} DISC_STRUCT SvParam;

/* Map gobj 3 (Floor): the coffee gate, two warp areas, both statues, the warped fighters. */
typedef struct SvWarpArea { Vec3 min, max, dest; } SvWarpArea;
typedef struct SvCoffeeGate {
    u8 started;                     /* console +E0 */
    Fighter_WaitAnimData* ftcmd;    /* console +E4: the file's "ftcmd" table */
} SvCoffeeGate;
typedef struct SvFloorVars {
    SvCoffeeGate gate;              /* console +E0 */
    SvWarpArea area[2];             /* console +E8, +10C */
    HSD_GObj* statue[2];            /* console +130, +134 */
    HSD_GObj* warped[6];            /* console +138 .. +14C */
} SvFloorVars;

/* The custom stage shadow list (stage_shadows). */
typedef struct SvShadow {
    struct SvShadow* next;
    LbShadow lb;
    HSD_JObj* joint;
    int center_depth;
    float radius;
} SvShadow;

extern SvParam* sv_param;

#define SV_VARS(T, g) ((T*) (void*) &((Ground*) HSD_GObjGetUserData(g))->u)
#define SV_FITS(T) _Static_assert(sizeof(T) <= sizeof(((Ground*) 0)->u), #T " fits Ground.u")

/* saturnvalley.c */
int sv_sound(int id, int volume);
float sv_step(float value, float target, float step);
float sv_approach(float value, float target, float step);
void sv_anim0(HSD_GObj* gobj);
void sv_statue_state(HSD_GObj* gobj, int state);
void sv_runner_state(HSD_GObj* gobj, int state);
bool sv_floor_warp(HSD_GObj* floor, HSD_GObj* fighter);

/* saturnvalley_coffee.c */
void sv_coffee_check(SvCoffeeGate* gate);
void sv_coffee_create(Ground_GObj* gobj);
void sv_coffee_frame(Ground_GObj* gobj);
void sv_taunt_enter(HSD_GObj* fighter, int index, SvCoffeeGate* gate);
void sv_taunt_take_damage(HSD_GObj* fighter);
void sv_overlay_destroy(HSD_GObj* overlay);

/* saturnvalley_shadow.c */
void sv_shadow_install(void);
SvShadow* sv_shadow_create(HSD_JObj* joint, int center_depth, int size, float radius);
bool sv_shadow_check(Vec3* pos, int arg, HSD_JObj* joint);
void sv_shadow_debugger(void);

/* saturnvalley_unused.c: routines nothing in the block calls */
struct SvMexFunction;
void sv_lerp(Vec3* out, const Vec3* a, const Vec3* b, float t);
void sv_saturn_decide(HSD_GObj* gobj);
HSD_GObj* sv_saturn_generate(HSD_JObj* node);
void sv_saturn_frame(HSD_GObj* gobj);
void sv_runner_vanish(HSD_GObj* gobj);
bool sv_area_has_point(const SvWarpArea* area, const Vec3* point);
void sv_each_fighter(void* arg, void (*callback)(void* arg, HSD_GObj* fighter));
void* sv_mex_symbol(struct SvMexFunction* function, const char* symbol);
void sv_floor_debug_draw(HSD_GObj* gobj, intptr_t pass);

#endif
