/* GrFc.dat exports 1 and 3..9. Weather, scenery, antennae, aircraft and alerts.
 * Codec and fighter-taunt callbacks are in fichina_codec.c. */
#include "fichina_codec.h"
#include "../common/mu_gr_mex.h"
#include <melee/gr/granime.h>
#include <melee/gr/grdatfiles.h>
#include <melee/gr/ground.h>
#include <melee/gr/stage.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/axdriver.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mtx.h>
#include <sysdolphin/baselib/object.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/sislib.h>

typedef struct FcAircraft {
    s32 joint, unused;
    DISC_PTR(struct FcFrame) sound_frames;
} DISC_STRUCT FcAircraft;
typedef struct FcFrame { u16 value; } DISC_STRUCT FcFrame;
typedef struct FcParam {
    f32 lava;
    s32 antenna_first, antenna_last, blizzard_frames, calm_min, calm_max;
    s32 warning_frame, animation_count;
    f32 calm_wind, calm_radius;
    s32 storm_duration;
    f32 storm_wind, storm_radius;
    DISC_PTR(FcAircraft) aircraft;
} DISC_STRUCT FcParam;
typedef struct FcBackground { HSD_GObj* blizzard; int blizzard_timer, timer, animation, sound; } FcBackground;
typedef struct FcFloor { FcCodecPlayer codec; HSD_GObj* alert[2]; int warning; } FcFloor;
typedef struct FcAttached { HSD_JObj* parent; HSD_JObj* aircraft[8]; int frame; } FcAttached;
typedef struct FcAlert { int state, timer; } FcAlert;
#define FC(type, gobj) ((type*) (void*) &((Ground*) HSD_GObjGetUserData(gobj))->u)
_Static_assert(sizeof(FcAttached) <= sizeof(((Ground*) 0)->u), "aircraft fits");
_Static_assert(sizeof(FcFloor) <= sizeof(((Ground*) 0)->u), "floor fits");
static FcParam* fc_param;
static Ground* fc_ground(HSD_GObj* gobj) { return HSD_GObjGetUserData(gobj); }
FcCodecPlayer* fc_codec_player(void) { return &FC(FcFloor, Ground_GetMapGObj(2))->codec; }
int fc_bank(void) { return mu_gr_mex_stage_bank(Stage_8022519C(Stage_80225194())) * 10000; }
int fc_sound(int id, int volume, int pan) { return HSD_AudioSFXStartParam(fc_bank() + id, (u8) volume, (u8) pan, 0, 6); }
static bool fc_unk(Ground_GObj* gobj) { return gobj != NULL; }
static void fc_anim(Ground_GObj* gobj) { grAnime_801C8138(gobj, fc_ground(gobj)->map_id, 0); }
static void fc_bg_anim(Ground_GObj* gobj) { fc_anim(gobj); fc_ground(gobj)->x10_flags.b2 = 0; }
MU_NO_CONTRACT static int fc_wait(void)
{
    f32 product = (f32) (fc_param->calm_max - fc_param->calm_min) * HSD_Randf();
    return (int) ((f32) fc_param->calm_min + product);
}
static void fc_wind(bool storm)
{
    Vec3 direction = { 1.0f, 0.0f, 0.0f };
    lb_80011A50(&direction, storm ? fc_param->storm_duration : -1,
                storm ? fc_param->storm_wind : fc_param->calm_wind, 0.0f,
                storm ? fc_param->storm_radius : fc_param->calm_radius,
                -100000.0f, 100000.0f, 100000.0f, -100000.0f);
}
/* +162C. Alert timer 0 decrements to -1 and remains there, as on console. */
static void fc_alert_state(HSD_GObj* gobj, int state, int timer)
{
    FcAlert* av = FC(FcAlert, gobj);
    av->state = state; av->timer = timer;
    grAnime_801C8138(gobj, fc_ground(gobj)->map_id, state);
    if (state == 1) fc_sound(26, 255, 127);
    else if (state == 2) fc_sound(25, 170, 127);
    else if (state == 3) fc_sound(27, 255, 127);
}
static void fc_alert_create(Ground_GObj* gobj)
{
    FcAlert* av = FC(FcAlert, gobj);
    fc_anim(gobj); av->state = 0; av->timer = -1;
}
static void fc_alert_frame(Ground_GObj* gobj)
{
    FcAlert* av = FC(FcAlert, gobj);
    if (av->state != 0 && av->timer >= 0 && --av->timer == 0) {
        if (av->state == 1) fc_alert_state(gobj, 2, 140);
        else if (av->state == 2) fc_alert_state(gobj, 3, 30);
        else if (av->state == 3) fc_alert_state(gobj, 0, -1);
    }
}
static HSD_GObj* fc_alert(HSD_GObj* floor, int joint)
{
    HSD_GObj* gobj = mu_grTSeak_80223908(10);
    lb_8000C2F8(Ground_801C3FA4(gobj, 0), Ground_801C3FA4(floor, joint));
    return gobj;
}
static void fc_warning(void)
{
    HSD_GObj* floor = Ground_GetMapGObj(2);
    FcFloor* fv = FC(FcFloor, floor);
    if (fv->alert[0] != NULL) fc_alert_state(fv->alert[0], 1, 30);
    if (fv->alert[1] != NULL) fc_alert_state(fv->alert[1], 1, 30);
    grAnime_801C8138(floor, fc_ground(floor)->map_id, 1);
    fv->warning = 180;
}
static void fc_floor_create(Ground_GObj* gobj)
{
    FcFloor* fv = FC(FcFloor, gobj);
    fc_ground(gobj)->x10_flags.b5 = 1;
    fc_anim(gobj);
    fv->codec.used = 0; fv->codec.sequence = NULL; fv->codec.index = -1;
    fv->alert[0] = fc_alert(gobj, 7); fv->alert[1] = fc_alert(gobj, 14);
    fv->warning = 0;
    Ground_InitMapColl(GET_JOBJ(gobj), fc_ground(gobj)->map_id);
}
static void fc_floor_frame(Ground_GObj* gobj)
{
    FcFloor* fv = FC(FcFloor, gobj);
    Ground_UpdateMapColl(gobj);
    fc_codec_check_spawn(&fv->codec);
    if (fv->warning > 0 && --fv->warning == 0) fc_anim(gobj);
}
static void fc_attached_scale(HSD_GObj* gobj)
{
    FcAttached* av = FC(FcAttached, gobj);
    HSD_JObj* joint = Ground_801C3FA4(gobj, 0);
    HSD_MtxGetScale(av->parent->mtx, &joint->scale);
    joint->scale.x /= 80.0f; joint->scale.y /= 80.0f; joint->scale.z /= 80.0f;
    HSD_JObjSetMtxDirtySub(joint);
}
/* +110C. The original passes AOBJ_ARG_AOV and an extra integer to a float
 * callback. Jev antenna_rate: explicit_rate, confidence 1.0. Question:
 * preserve the mismatched native callback or explicitly pass the computed
 * rate? Answer: use the float signature. Console volatile f1 survival is
 * unproven; the random expression and callback identify the rate. */
MU_NO_CONTRACT static void fc_antenna(HSD_JObj* parent)
{
    HSD_GObj* gobj = mu_grTSeak_80223908(7);
    HSD_JObj* joint = Ground_801C3FA4(gobj, 0);
    f64 rate;
    lb_8000C2F8(joint, parent);
    FC(FcAttached, gobj)->parent = parent;
    HSD_JObjReqAnimAllByFlags(joint, 0x7FF, (f32) HSD_Randi((int) lbGetJObjEndFrame(joint)));
    HSD_JObjAnim(joint);
    rate = (f64) HSD_Randf() * 0.6;
    rate = rate + 0.4;
    HSD_ForeachAnim(joint, JOBJ_TYPE, 0xFFFF, HSD_AObjSetRate, AOBJ_ARG_AF, rate);
}
static void fc_aircraft_create(Ground_GObj* gobj)
{
    FcAttached* av = FC(FcAttached, gobj);
    FcAircraft* aircraft = DP(fc_param->aircraft);
    int i;
    fc_anim(gobj);
    for (i = 0; i < 8; i++) av->aircraft[i] = Ground_801C3FA4(gobj, aircraft[i].joint);
    av->frame = 0;
}
MU_NO_CONTRACT static void fc_aircraft_frame(Ground_GObj* gobj)
{
    FcAttached* av = FC(FcAttached, gobj);
    FcAircraft* aircraft = DP(fc_param->aircraft);
    int i;
    for (i = 0; i < 8; i++) {
        FcFrame* frames = DP(aircraft[i].sound_frames);
        for (; frames->value != 0xFFFF; frames++) if (frames->value == av->frame) {
            Vec3 pos;
            f32 pan, volume;
            lb_8000B1CC(av->aircraft[i], NULL, &pos);
            pan = pos.x / 2000.0f;
            volume = 800.0f / pos.z;
            if (volume < 0.0f) volume = -volume;
            if (volume > 1.0f) volume = 1.0f;
            if (pan < 0.0f) pan = 0.0f;
            if (pan > 1.0f) pan = 1.0f;
            fc_sound(28, (int) (volume * 128.0f), (int) (pan * 80.0f + 127.0f));
        }
    }
    av->frame++;
    if (av->frame > 1600) av->frame = 0;
    fc_attached_scale(gobj);
}
static void fc_background_create(Ground_GObj* gobj)
{
    FcBackground* bv = FC(FcBackground, gobj);
    HSD_GObj* aircraft;
    HSD_JObj* parent;
    int i;
    fc_anim(gobj);
    for (i = fc_param->antenna_first; i <= fc_param->antenna_last; i++) {
        HSD_JObj* joint = Ground_801C3FA4(gobj, i);
        if (joint != NULL) fc_antenna(joint);
    }
    bv->blizzard = mu_grTSeak_80223908(6);
    bv->timer = fc_wait(); bv->blizzard_timer = -1; bv->animation = 0;
    bv->sound = fc_sound(0, 255, 127);
    fc_wind(false);
    aircraft = mu_grTSeak_80223908(8);
    parent = Ground_801C3FA4(gobj, 2);
    lb_8000C2F8(Ground_801C3FA4(aircraft, 0), parent);
    FC(FcAttached, aircraft)->parent = parent;
}
static void fc_background_frame(Ground_GObj* gobj)
{
    FcBackground* bv = FC(FcBackground, gobj);
    if (bv->timer > 0) {
        bv->timer--;
        if (bv->timer == fc_param->warning_frame) fc_warning();
    } else {
        fc_wind(true);
        grAnime_801C8138(bv->blizzard, fc_ground(bv->blizzard)->map_id, 1);
        fc_sound(1, 255, 127);
        bv->blizzard_timer = fc_param->blizzard_frames;
        bv->timer = fc_wait();
    }
    if (bv->blizzard_timer >= 0) {
        if (bv->blizzard_timer == 0) {
            HSD_JObj* joint = Ground_801C3FA4(gobj, 0);
            HSD_GObj* sky;
            f32 frame;
            bv->animation++;
            if (bv->animation >= fc_param->animation_count) bv->animation = 0;
            frame = lbGetJObjCurrFrame(joint);
            grAnime_801C8138(gobj, fc_ground(gobj)->map_id, bv->animation);
            HSD_JObjReqAnimAllByFlags(joint, 0x7FF, frame);
            HSD_JObjAnim(joint);
            sky = Ground_GetMapGObj(3);
            if (sky != NULL) grAnime_801C8138(sky, 3, bv->animation);
        }
        bv->blizzard_timer--;
    }
    lb_800115F4();
}
static bool fc_shadow(Vec3* fighter, int arg, HSD_JObj* joint)
{
    Vec3 pos; (void) arg;
    lb_8000B1CC(joint, NULL, &pos);
    return fighter->y > pos.y;
}
static void fc_init(void)
{
    static const int order[] = { 0, 3, 4, 1, 5, 2 };
    HSD_SisSlot* sis;
    int i;
    fc_param = Ground_GetYakumonoParam();
    if (fc_param == NULL) __assert("fichina.c", 0x2E9, "yakumono node was not found in dat");
    sis = HSD_ArchiveGetPublicAddress(grDatFiles_GetArchive()->unk0, "SIS_GrFichinaData");
    if (sis == NULL) __assert("fichina.c", 0x2EF, "could not find SIS_GrFichinaData");
    HSD_SisLib_804D1124[1] = sis;
    for (i = 0; i < 6; i++) mu_grTSeak_80223908(order[i]);
    Ground_801C39C0(); Ground_801C3BB4();
    Ground_801C438C(fc_param->lava * Ground_801C0498());
}
#define FC_ROW(create, frame, flags) { create, fc_unk, frame, mu_gr_gobj_nop, flags }
static StageCallbacks fc_map[] = {
    FC_ROW(fc_anim, mu_gr_gobj_nop, 0), FC_ROW(fc_background_create, fc_background_frame, 0xC0000000),
    FC_ROW(fc_floor_create, fc_floor_frame, 0), FC_ROW(fc_bg_anim, mu_gr_gobj_nop, 0),
    FC_ROW(fc_anim, mu_gr_gobj_nop, 0), FC_ROW(fc_bg_anim, mu_gr_gobj_nop, 0),
    FC_ROW(fc_bg_anim, mu_gr_gobj_nop, 0), FC_ROW(fc_anim, fc_attached_scale, 0),
    FC_ROW(fc_aircraft_create, fc_aircraft_frame, 0), FC_ROW(fc_codec_create, fc_codec_frame, 0x20000000),
    FC_ROW(fc_alert_create, fc_alert_frame, 0),
};
const MuGrStage mu_gr_fichina = {
    .name = "Fichina", .file = "/GrFc.dat", .map_gobjs = fc_map,
    .on_init = fc_init, .on_demo_init = mu_gr_demo_nop, .on_load = mu_gr_event_nop,
    .on_start = mu_gr_event_nop, .callback4 = mu_gr_false, .on_touch_line = mu_gr_no_touch_line,
    .on_check_shadow_render = fc_shadow, .flags = MU_GR_READY,
};
