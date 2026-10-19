/* GrDp.dat, all exports and 21 map callbacks. Native stage and disc layouts
 * are distinct: private state occupies Ground.u, parameters use DISC_STRUCT. */
#include "../common/mu_gr_common.h"
#include "../common/mu_gr_mex.h"
#include <melee/cm/camera.h>
#include <melee/cm/types.h>
#include <melee/ft/ftdevice.h>
#include <melee/ft/types.h>
#include <melee/gr/granime.h>
#include <melee/gr/grdatfiles.h>
#include <melee/gr/grdisplay.h>
#include <melee/gr/grlib.h>
#include <melee/gr/ground.h>
#include <melee/gr/stage.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <melee/lb/types.h>
#include <melee/mn/mnname.h>
#include <melee/mp/mplib.h>
#include <melee/mp/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/axdriver.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/object.h>
#include <sysdolphin/baselib/psstructs.h>
#include <sysdolphin/baselib/random.h>
#include <string.h>

typedef struct DpPlace { Vec3_BE pos; s32 facing; } DISC_STRUCT DpPlace;
typedef struct DpParam {
    Vec3_BE wind_direction;
    f32 wind[7];
    s32 racer_joint[2];
    s32 water_min, water_max;
    u8 water_volume, water_pan, water_sound, water_count;
    f32 water_height;
    s32 gull_min, gull_max;
    u8 gull_volume, gull_pan, gull_sound, gull_count;
    s32 place_count;
    DISC_PTR(DpPlace) places;
    s32 sleep, appear_grace, buoy_joint, assist_joint, detect_joint;
    f32 detect_width, appear_splash, dolphin_splash, dolphin_start, dolphin_scale;
    s32 splash_frame[2], voice_frame, unused88;
    u8 unused8C, dolphin_volume, dolphin_joint[5];
} DISC_STRUCT DpParam;
typedef struct DpBackground { int water, gull, sound; HSD_AObj* aobj; } DpBackground;
typedef struct DpDolphin { int facing, frame, unused, vertex; HSD_JObj* joint[5]; } DpDolphin;
typedef struct DpManager { HSD_GObj* dolphin; HSD_GObj* buoy; u8 state; int timer, facing; } DpManager;
#define DPV(type, gobj) ((type*) (void*) &((Ground*) HSD_GObjGetUserData(gobj))->u)
_Static_assert(sizeof(DpDolphin) <= sizeof(((Ground*) 0)->u), "dolphin state fits");
_Static_assert(sizeof(DynamicsDesc) <= sizeof(((Ground*) 0)->u), "buoy dynamics fit");
_Static_assert(__builtin_offsetof(DpParam, dolphin_joint) == 0x8E, "parameter layout");
static DpParam* dp_param;
extern Camera game_camera;
static Ground* dp_ground(HSD_GObj* gobj) { return HSD_GObjGetUserData(gobj); }
static bool dp_unk(Ground_GObj* gobj) { return gobj != NULL; }
static void dp_anim(Ground_GObj* gobj) { grAnime_801C8138(gobj, dp_ground(gobj)->map_id, 0); }
static int dp_sound(int id, int volume, int pan)
{
    int bank = mu_gr_mex_stage_bank(Stage_8022519C(Stage_80225194()));
    return HSD_AudioSFXStartParam(bank * 10000 + id, (u8) volume, (u8) pan, 0, 6);
}
MU_NO_CONTRACT static int dp_wait(int low, int high)
{
    f32 product = (f32) (high - low) * HSD_Randf();
    return (int) ((f32) low + product);
}
MU_NO_CONTRACT static void dp_splash(Vec3* pos, int volume, int pan, f32 scale)
{
    HSD_Generator* generator = grLib_801C96F8(0x7530, 30, pos);
    if (generator != NULL) {
        generator->appsrt->scale.x *= scale;
        generator->appsrt->scale.y *= scale;
        generator->appsrt->scale.z *= scale;
    }
    dp_sound(5, volume, pan);
}
static bool dp_fighter_splash(Ground_GObj* stage, Fighter_GObj* fighter, Vec3* out)
{
    Fighter* fp = HSD_GObjGetUserData(fighter);
    f32 height = dp_param->water_height;
    (void) stage; (void) out;
    if (!fp->x221D_b7 && fp->cur_pos.y < height && height < fp->prev_pos.y) {
        Vec3 pos = { fp->cur_pos.x, height, fp->cur_pos.z };
        dp_splash(&pos, 180, 127, fp->x890_cameraBox->target_ext.v.z / 10.0f);
    }
    return false;
}
MU_NO_CONTRACT static void dp_jetski_volume(void* sound, enum_t type, HSD_ObjData* value)
{
    if (type == 4) HSD_AudioSFXSetVolumeEx((int) (mu_uptr) sound, (u8) (int) (value->fv * 255.0f));
}
static void dp_background_create(Ground_GObj* gobj)
{
    DpBackground* bv = DPV(DpBackground, gobj);
    UnkArchiveStruct* archive;
    HSD_AnimJoint* animation;
    Vec3 direction = BE_VEC3(dp_param->wind_direction);
    dp_anim(gobj);
    bv->water = dp_wait(dp_param->water_min, dp_param->water_max);
    bv->gull = dp_wait(dp_param->gull_min, dp_param->gull_max);
    archive = grDatFiles_801C6330(0);
    animation = HSD_ArchiveGetPublicAddress(archive->unk0, "jetskisfx_animjoint");
    if (animation != NULL) {
        bv->sound = dp_sound(6, 0, 127);
        bv->aobj = HSD_AObjLoadDesc(DP(animation->aobjdesc));
    }
    ftCo_800C07F8(gobj, 6, dp_fighter_splash);
    lb_80011A50(&direction, -1, dp_param->wind[0], dp_param->wind[1], dp_param->wind[2],
                dp_param->wind[3], dp_param->wind[4], dp_param->wind[5], dp_param->wind[6]);
}
static void dp_background_frame(Ground_GObj* gobj)
{
    DpBackground* bv = DPV(DpBackground, gobj);
    if (bv->water > 0) bv->water--;
    else {
        dp_sound(dp_param->water_sound + HSD_Randi(dp_param->water_count), dp_param->water_volume, dp_param->water_pan);
        bv->water = dp_wait(dp_param->water_min, dp_param->water_max);
    }
    if (bv->gull > 0) bv->gull--;
    else {
        dp_sound(dp_param->gull_sound + HSD_Randi(dp_param->gull_count), dp_param->gull_volume, dp_param->gull_pan);
        bv->gull = dp_wait(dp_param->gull_min, dp_param->gull_max);
    }
    if (bv->aobj != NULL) {
        HSD_AObjReqAnim(bv->aobj, lbGetJObjCurrFrame(Ground_801C3FA4(Ground_GetMapGObj(5), 0)));
        HSD_AObjInterpretAnim(bv->aobj, (void*) (mu_uptr) bv->sound, dp_jetski_volume);
    }
    lb_800115F4();
}
MU_NO_CONTRACT static void dp_reflection(HSD_GObj* gobj, intptr_t pass)
{
    f32 height = dp_param->water_height * Ground_801C0498();
    /* +1060 reads camera transform.position.y (console camera +30). */
    if (game_camera.transform.position.y >= height) grDisplay_801C5DB0(gobj, pass);
}
static void dp_reflection_create(Ground_GObj* gobj)
{
    dp_anim(gobj);
    HSD_GObjGXLink_8039084C(gobj);
    GObj_SetupGXLink(gobj, dp_reflection, 3, 0);
}
static void dp_racer_create(Ground_GObj* gobj)
{
    grLib_801C9808(0x7536, 30, Ground_801C3FA4(gobj, dp_param->racer_joint[0]));
    grLib_801C9808(0x7536, 30, Ground_801C3FA4(gobj, dp_param->racer_joint[1]));
}
static void dp_dolphin_hide(HSD_GObj* gobj) { HSD_JObjSetFlagsAll(GET_JOBJ(gobj), JOBJ_HIDDEN); }
static void dp_dolphin_create(Ground_GObj* gobj)
{
    DpDolphin* dv = DPV(DpDolphin, gobj);
    /* +0838..0854 reads collision array element 1, not the head's inner. */
    MapJoint* joint = mpGetGroundCollJoint()[1].inner;
    int i;
    dv->frame = 500;
    if ((u16) joint->vtx_count <= 4) __assert("dolphin.c", 0x176, "Not enough vertices for dolphin");
    dv->vertex = (u16) joint->vtx_start;
    for (i = 0; i < 5; i++) dv->joint[i] = Ground_801C3FA4(gobj, dp_param->dolphin_joint[i]);
    Ground_InitMapColl(GET_JOBJ(gobj), dp_ground(gobj)->map_id);
}
MU_NO_CONTRACT static void dp_dolphin_frame(Ground_GObj* gobj)
{
    DpDolphin* dv = DPV(DpDolphin, gobj);
    if (dv->frame == dp_param->voice_frame) dp_sound(0, dp_param->dolphin_volume, 128);
    if (dv->frame == dp_param->splash_frame[0] || dv->frame == dp_param->splash_frame[1]) {
        Vec3 pos;
        lb_8000B1CC(Ground_801C3FA4(gobj, 1), NULL, &pos);
        pos.y = dp_param->water_height * Ground_801C0498();
        dp_splash(&pos, dp_param->dolphin_volume, (u8) ((1 - dv->facing) * 127), dp_param->dolphin_splash);
    }
    Ground_UpdateMapColl(gobj);
    {
        int i;
        for (i = 0; i < 5; i++) {
            Vec3 pos;
            lb_8000B1CC(dv->joint[dv->facing == -1 ? 4 - i : i], NULL, &pos);
            mpVtxSetPos(dv->vertex + i, pos.x, pos.y);
        }
        mpJointUpdateBounding(1);
    }
    dv->frame++;
}
static int dp_dolphin_start(HSD_GObj* gobj, int facing, Vec3* pos)
{
    DpDolphin* dv = DPV(DpDolphin, gobj);
    HSD_JObj* joint;
    HSD_JObjClearFlagsAll(GET_JOBJ(gobj), JOBJ_HIDDEN);
    dp_anim(gobj);
    joint = Ground_801C3FA4(gobj, 0);
    joint->translate = *pos;
    joint->scale.x = joint->scale.y = joint->scale.z = dp_param->dolphin_scale;
    HSD_JObjReqAnimAll(joint, dp_param->dolphin_start);
    dv->facing = facing; dv->frame = (int) dp_param->dolphin_start;
    Ground_801C3FA4(gobj, 2)->rotate.y = (f32) ((double) facing * 1.5707963267948966);
    return (int) lbGetJObjEndFrame(Ground_801C3FA4(gobj, 0));
}
static void dp_buoy_create(Ground_GObj* gobj)
{
    BoneDynamicsDescDisc* desc = HSD_ArchiveGetPublicAddress(grDatFiles_801C6330(0)->unk0, "dynamicsdata_flag");
    DynamicsDesc* live = DPV(DynamicsDesc, gobj);
    /* Jev missing_dynamics: assert (0.95). Question: how to translate the
     * function-address comparison at +0964? Answer: assert required root,
     * preserving shipped-data dynamics and a hard failure for missing data. */
    if (desc == NULL) __assert("dolphin.c", 0x260, "dynamicsdata_flag required");
    lb_8000FD48(Ground_801C3FA4(gobj, desc->bone_id), live, desc->dyn_desc.count);
    lb_80011710(&desc->dyn_desc, live);
}
static void dp_buoy_frame(Ground_GObj* gobj)
{
    lb_8001044C(DPV(DynamicsDesc, gobj), NULL, 0, 0.0f, false, 0, 0, false);
}
static int dp_buoy_animation(HSD_GObj* gobj, int animation)
{
    grAnime_801C8138(gobj, dp_ground(gobj)->map_id, animation);
    return (int) lbGetJObjEndFrame(Ground_801C3FA4(gobj, 0));
}
static void dp_sleep(HSD_GObj* gobj, int timer)
{
    DpManager* mv = DPV(DpManager, gobj);
    HSD_JObj* joint = Ground_801C3FA4(gobj, 0);
    mv->state = 0; mv->timer = timer;
    joint->translate.x = 0.0f; joint->translate.y = 9999.0f;
    HSD_JObjSetMtxDirtySub(joint);
    dp_dolphin_hide(mv->dolphin);
}
MU_NO_CONTRACT static void dp_appear(HSD_GObj* gobj, DpPlace* place)
{
    DpManager* mv = DPV(DpManager, gobj);
    HSD_JObj* joint;
    Vec3 pos;
    mv->state = 1; mv->timer = dp_buoy_animation(mv->buoy, 0); mv->facing = place->facing;
    joint = Ground_801C3FA4(gobj, 0);
    joint->translate = BE_VEC3(place->pos);
    HSD_JObjSetMtxDirtySub(joint);
    lb_8000B1CC(Ground_801C3FA4(gobj, 1), NULL, &pos);
    pos.y = dp_param->water_height * Ground_801C0498();
    dp_splash(&pos, 255, (u8) ((1 - place->facing) * 127), dp_param->appear_splash);
    dp_dolphin_hide(mv->dolphin);
}
static void dp_assist(HSD_GObj* gobj, Vec3* pos)
{
    DpManager* mv = DPV(DpManager, gobj);
    grAnime_801C8138(mv->buoy, 19, 3);
    mv->state = 4;
    mv->timer = dp_dolphin_start(mv->dolphin, mv->facing, pos);
}
static void dp_manager_frame(Ground_GObj* gobj)
{
    DpManager* mv = DPV(DpManager, gobj);
    /* Jev manager_clone: single (1.0). Question: one switch for +ABC and
     * +17D0's identical bodies? Answer: yes, retain post-transition decrement. */
    if (mv->timer == 0) switch (mv->state) {
    case 0: {
        int index = HSD_Randi(dp_param->place_count);
        dp_appear(gobj, &DP(dp_param->places)[index]);
        break;
    }
    case 1:
        mv->state = 2; mv->timer = dp_buoy_animation(mv->buoy, 1);
        dp_dolphin_hide(mv->dolphin); break;
    case 2:
        mv->state = 3; mv->timer = dp_buoy_animation(mv->buoy, 2);
        dp_dolphin_hide(mv->dolphin); break;
    case 3: case 4: dp_sleep(gobj, dp_param->sleep); break;
    }
    if (mv->timer >= 0) mv->timer--;
}
MU_NO_CONTRACT static bool dp_detect(HSD_GObj* fighter, Vec3* center)
{
    Fighter* fp = HSD_GObjGetUserData(fighter);
    f32 half = Ground_801C0498() * dp_param->detect_width;
    return fp->cur_pos.y < center->y && fp->cur_pos.x > center->x - half && fp->cur_pos.x < center->x + half;
}
static void dp_check_fighters(HSD_GObj* gobj)
{
    DpManager* mv = DPV(DpManager, gobj);
    Vec3 center;
    int slot;
    if (mv->state == 0 || mv->state == 4) return;
    if (mv->state == 1 && mv->timer >= dp_param->appear_grace) return;
    lb_8000B1CC(Ground_801C3FA4(gobj, dp_param->detect_joint), NULL, &center);
    for (slot = 0; slot < 6; slot++) {
        HSD_GObj* fighter = Player_GetEntityAtIndex(slot, 0);
        if (fighter == NULL || !dp_detect(fighter, &center)) {
            fighter = Player_GetEntityAtIndex(slot, 1);
            if (fighter == NULL || !dp_detect(fighter, &center)) continue;
        }
        {
            Vec3 pos;
            lb_8000B1CC(Ground_801C3FA4(gobj, dp_param->assist_joint), NULL, &pos);
            dp_assist(gobj, &pos);
        }
    }
}
static void dp_manager_create(Ground_GObj* gobj)
{
    DpManager* mv = DPV(DpManager, gobj);
    mv->dolphin = mu_grTSeak_80223908(18);
    mv->buoy = mu_grTSeak_80223908(19);
    lb_8000C1C0(Ground_801C3FA4(mv->buoy, 0), Ground_801C3FA4(gobj, dp_param->buoy_joint));
    HSD_GObj_SetupProc(gobj, dp_check_fighters, 7);
    dp_sleep(gobj, dp_param->sleep);
}
static HSD_DObj* dp_dobj(HSD_JObj* joint, int index)
{
    HSD_DObj* dobj = joint->u.dobj;
    while (index-- > 0) {
        if (dobj->next == NULL) __assert("dolphin.c", 0x1D8, "dobj not found");
        dobj = dobj->next;
    }
    return dobj;
}
static bool dp_shadow(Vec3* fighter, int arg, HSD_JObj* joint)
{
    Vec3 pos; (void) arg; lb_8000B1CC(joint, NULL, &pos); return fighter->y > pos.y;
}
static void dp_init(void)
{
    static const unsigned char tag[] = {0x82,0x63,0x82,0x6B,0x82,0x6F,0x82,0x67,0x82,0x6D,0x82,0x6C,0x82,0x6E,0x82,0x63,0,0};
    int models[8] = {5,6,7,8,9,10,11,12};
    bool alternate = false;
    int i;
    dp_param = Ground_GetYakumonoParam();
    if (dp_param == NULL) __assert("dolphin.c", 0x40C, "yakumono node was not found in dat");
    for (i = 0; i < 5; i++) mu_grTSeak_80223908(i);
    for (i = 0; i < 6; i++) if (Player_GetPlayerSlotType(i) == 0) {
        int slot = Player_GetNametagSlotID(i);
        if (slot != 120) {
            const char* text = GetNameText(slot);
            if (text != NULL && strcmp(text, (const char*) tag) == 0) { alternate = true; break; }
        }
    }
    for (i = 0; i < 8; i++) { int swap = HSD_Randi(8), temp = models[i]; models[i] = models[swap]; models[swap] = temp; }
    for (i = 0; i < 8; i++) {
        HSD_GObj* gobj = mu_grTSeak_80223908(i + 5);
        HSD_JObj* joint;
        grAnime_801C8138(gobj, models[i], 0);
        joint = Ground_801C3FA4(gobj, 0);
        dp_dobj(joint, alternate ? 0 : 2)->flags |= 1;
        dp_dobj(joint, alternate ? 1 : 3)->flags |= 1;
    }
    mu_grTSeak_80223908(14); mu_grTSeak_80223908(15);
    mu_grTSeak_80223908(16); mu_grTSeak_80223908(17); mu_grTSeak_80223908(20);
    Ground_801C39C0(); Ground_801C3BB4();
}
#define DP_ROW(create, frame, flags) { create, dp_unk, frame, mu_gr_gobj_nop, flags }
static StageCallbacks dp_map[] = {
    DP_ROW(dp_anim, mu_gr_gobj_nop, 0), DP_ROW(dp_background_create, dp_background_frame, 0x80000000),
    DP_ROW(dp_anim, mu_gr_gobj_nop, 0), DP_ROW(dp_anim, mu_gr_gobj_nop, 0),
    DP_ROW(dp_reflection_create, mu_gr_gobj_nop, 0),
    DP_ROW(dp_racer_create, mu_gr_gobj_nop, 0), DP_ROW(dp_racer_create, mu_gr_gobj_nop, 0),
    DP_ROW(dp_racer_create, mu_gr_gobj_nop, 0), DP_ROW(dp_racer_create, mu_gr_gobj_nop, 0),
    DP_ROW(dp_racer_create, mu_gr_gobj_nop, 0), DP_ROW(dp_racer_create, mu_gr_gobj_nop, 0),
    DP_ROW(dp_racer_create, mu_gr_gobj_nop, 0), DP_ROW(dp_racer_create, mu_gr_gobj_nop, 0),
    DP_ROW(dp_anim, mu_gr_gobj_nop, 0), DP_ROW(dp_anim, mu_gr_gobj_nop, 0),
    DP_ROW(dp_anim, mu_gr_gobj_nop, 0), DP_ROW(dp_anim, mu_gr_gobj_nop, 0), DP_ROW(dp_anim, mu_gr_gobj_nop, 0),
    DP_ROW(dp_dolphin_create, dp_dolphin_frame, 0), DP_ROW(dp_buoy_create, dp_buoy_frame, 0),
    DP_ROW(dp_manager_create, dp_manager_frame, 0),
};
const MuGrStage mu_gr_dolphin_park = {
    .name = "Dolphin Park", .file = "/GrDp.dat", .map_gobjs = dp_map,
    .on_init = dp_init, .on_demo_init = mu_gr_demo_nop, .on_load = mu_gr_event_nop,
    .on_start = mu_gr_event_nop, .callback4 = mu_gr_false, .on_touch_line = mu_gr_no_touch_line,
    .on_check_shadow_render = dp_shadow, .flags = MU_GR_READY,
};
