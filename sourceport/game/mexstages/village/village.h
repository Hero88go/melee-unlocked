/* Village, GrAc.dat. All offsets in these files refer to its grFunction block. */
#ifndef MEXGR_VILLAGE_H
#define MEXGR_VILLAGE_H

#include "../common/mu_gr_common.h"
#include "../common/mu_gr_mex.h"
#include <melee/gr/types.h>
#include <melee/gm/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>

typedef struct AcParam {
    s32 balloon_wait, balloon_min, balloon_max;
    s32 mood_min, mood_max, action_min, action_max;
    s32 blink_min, blink_max, blink_step, target_min, target_max;
    f32 look_limit, front_live, front_rest;
    s32 train_wait, train_first, train_last;
} DISC_STRUCT AcParam;

typedef struct AcResident {
    DISC_PTR(HSD_Joint) joint;
    DISC_PTR(HSD_MatAnimJoint) material;
    u16 flags;
    s16 chance, companion_chance, companion;
    DISC_PTR(char) file;
} DISC_STRUCT AcResident;
typedef DISC_PTR(AcResident) AcResidentSlot;

typedef struct AcMass {
    u8 fixed;
    Vec3 pos, previous, anchor;
    f32 gravity, damping;
    HSD_JObj* joint;
} AcMass;

typedef struct AcSpring {
    AcMass* a;
    AcMass* b;
    f32 length, limit;
} AcSpring;

typedef struct AcPhysics {
    int mass_count, spring_count, iterations;
    f32 stiffness;
    AcMass mass[5];
    AcSpring spring[4];
} AcPhysics;

typedef struct AcFloor {
    u8 pad[0x1C];
    HSD_GObj* balloon;
    int timer;
    HSD_GObj* resident[6];
} AcFloor;

typedef struct AcBalloon {
    u8 pad[0x1C];
    HSD_GObj* self;
    Item_GObj* hurtbox;
    Item_GObj* prize;
    Vec3 pos;
    int popped;
    AcPhysics* physics;
} AcBalloon;

typedef struct AcVillager {
    u8 pad[0x1C];
    AcResident* data;
    Vec3 pos;
    f32 rotation;
    u8 eye, mouth, following, player, source, stocks;
    u32 stamina_word;
    s32 falls;
    u32 kos[6];
    int target_timer, mood, mood_timer, action, action_timer;
    int blink, blink_timer;
} AcVillager;

typedef struct AcTimer {
    u8 pad[0x1C];
    int timer;
} AcTimer;

typedef struct AcEvent {
    u8 pad[0x1C];
    int minute, second, celebrated;
} AcEvent;

#define AC_VARS(type, gp) ((type*) (void*) &(gp)->u)
#define AC_GET(type, gobj) AC_VARS(type, ((Ground*) HSD_GObjGetUserData(gobj)))
#define AC_FIT(type) _Static_assert(sizeof(type) <= sizeof(((Ground*) 0)->u), #type " fits Ground.u")
AC_FIT(AcFloor);
AC_FIT(AcBalloon);
AC_FIT(AcVillager);
AC_FIT(AcTimer);
AC_FIT(AcEvent);

extern AcParam* ac_param;
extern datetime ac_date;
extern int ac_time, ac_season, ac_event;
extern AcResidentSlot* ac_database;

void ac_calendar_now(datetime* date);
int ac_determine_season(void);
int ac_determine_time(void);
int ac_determine_event(void);
MU_NO_CONTRACT int ac_random_range(int low, int high);
void ac_rope_create(AcPhysics* physics, HSD_JObj* root, int first, int last);
void ac_physics_update(AcPhysics* physics);
void ac_population_create(Ground_GObj* gobj);
void ac_villager_create(Ground_GObj* gobj);
void ac_villager_frame(Ground_GObj* gobj);
void ac_set_action(HSD_GObj* gobj, int action, int duration);
void ac_set_mood(HSD_GObj* gobj, int mood, int duration);
void ac_material_frame(HSD_GObj* gobj, int index, int frame);
void ac_add_animation(HSD_GObj* gobj, int index);
void ac_season_material(HSD_GObj* gobj);
f32 ac_anim_frame(HSD_JObj* joint);
void* mexgr_village_yakumono(const char* file, int line);
bool mexgr_village_unk(Ground_GObj* gobj);
void mexgr_village_deletion(Ground_GObj* gobj);
void mexgr_village_anim0_create(Ground_GObj* gobj);
void mexgr_village_background_create(Ground_GObj* gobj);

#endif
