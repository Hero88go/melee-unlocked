/* GrDo.dat, exports 1 and 3..9. The location, platform and sound state
 * machines follow the grFunction block; duplicate .text routines share bodies. */
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
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <melee/mp/mplib.h>
#include <melee/gr/stage.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/axdriver.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/object.h>
#include <sysdolphin/baselib/psstructs.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/wobj.h>
#include <dolphin/gx.h>
#include <math.h>

typedef struct DfParam {
    s32 water_window, collision_frame, fade_frames, fade_out_frame, camera_frame;
    s32 travel_frames, location_wait;
    f32 animation_rate;
    u8 platform[12];
    s32 announce_min, announce_max, shine_min, shine_max;
    s32 shine_distance, ambient_distance[2];
    u8 floor_volume[2];
} DISC_STRUCT DfParam;
typedef struct DfBackground {
    int travel, wait, location, previous;
    u8 alternate, water;
    f32 water_y;
    int unused[2];
    f32 water_offset[9];
    int announce, ambient[2];
} DfBackground;
typedef struct DfFloor { HSD_GObj* platform; int fade, timer, sound[2]; } DfFloor;
typedef struct DfSky { HSD_LObj* light[2]; } DfSky;
#define DF(type, gobj) ((type*) (void*) &((Ground*) HSD_GObjGetUserData(gobj))->u)
_Static_assert(sizeof(DfBackground) <= sizeof(((Ground*) 0)->u), "background state fits");
static DfParam* df_param;
/* camera.c exports this native camera state; the block reads its collision-display bit. */
extern Camera game_camera;
static Ground* df_ground(HSD_GObj* gobj) { return HSD_GObjGetUserData(gobj); }
static bool df_false(Ground_GObj* gobj) { (void) gobj; return false; }
static void df_anim(Ground_GObj* gobj) { grAnime_801C8138(gobj, df_ground(gobj)->map_id, 0); }
static int df_sound(int id, int volume, int pan)
{
    int bank = mu_gr_mex_stage_bank(Stage_8022519C(Stage_80225194()));
    return HSD_AudioSFXStartParam(bank * 10000 + id, (u8) volume, (u8) pan, 0, 6);
}
/* +1274: coordinate differences round to float before double square/sum. */
MU_NO_CONTRACT static void df_volume(int sound, Vec3* pos, f32 distance, int low, int high)
{
    Vec3 eye;
    f32 x, y, z, ratio;
    HSD_CObjGetEyeVector(GET_COBJ(Camera_80030A50()), &eye);
    x = pos->x - eye.x; y = pos->y - eye.y; z = pos->z - eye.z;
    ratio = sqrtf((f32) (((double) y * y + (double) x * x) + (double) z * z)) / distance;
    if (ratio < 1.0f) {
        f32 product = (f32) (high - low) * (1.0f - ratio);
        low = (u8) (int) (product + (f32) low);
    }
    HSD_AudioSFXSetVolumeEx(sound, (u8) low);
}
MU_NO_CONTRACT static int df_random(int low, int high)
{
    f32 product = (f32) (high - low) * HSD_Randf();
    return (int) ((f32) low + product);
}
/* +1B54: replace joint animation only, retaining material and shape tracks. */
static void df_joint_animation(HSD_GObj* gobj, int animation)
{
    Ground* gp = df_ground(gobj);
    UnkArchiveStruct* archive = grDatFiles_801C6330(gp->map_id);
    HSD_JObj* joint = GET_JOBJ(gobj)->child;
    if (joint != NULL) {
        struct UnkStageDat_x8_t* desc = &DP(archive->unk4->unk8)[gp->map_id];
        HSD_AnimJointSlot* slots = DP(desc->unk4);
        lb_8000B804(joint->child, DP(DP(desc->unk0)->child));
        HSD_JObjRemoveAnimByFlags(joint, 1);
        HSD_JObjAddAnimAll(joint, DP(slots[animation]), NULL, NULL);
        HSD_JObjReqAnimAllByFlags(joint, 1, 0.0f);
        HSD_JObjAnimAll(joint);
    }
}
static void df_alpha(HSD_JObj* joint, f32 alpha)
{
    HSD_DObj* dobj;
    if (joint == NULL) return;
    for (dobj = joint->u.dobj; dobj != NULL; dobj = dobj->next) {
        if (dobj->mobj != NULL) dobj->mobj->mat->alpha = alpha;
    }
    df_alpha(joint->next, alpha);
    df_alpha(joint->child, alpha);
}
/* +18FC: rebuild the platform gobj, then set the eleven floor collisions. */
static void df_set_platform(HSD_GObj* gobj, int kind)
{
    static const int first[5] = { 0, 1, 3, 5, 8 };
    static const int last[5] = { 0, 2, 4, 7, 10 };
    DfFloor* fv = DF(DfFloor, gobj);
    int i;
    if (fv->platform != NULL) { Ground_801C4A08(fv->platform); fv->platform = NULL; }
    if (kind >= 1 && kind <= 4) fv->platform = mu_grTSeak_80223908(kind + 2);
    if (kind == 0) mpLib_80057BC0(0); else mpJointListAdd(0);
    for (i = 1; i < 11; i++) mpLib_80057BC0(i);
    if (kind >= 1 && kind <= 4) for (i = first[kind]; i <= last[kind]; i++) mpJointListAdd(i);
}
static void df_fade_out(void)
{
    HSD_GObj* floor = Ground_GetMapGObj(2);
    DfFloor* fv = DF(DfFloor, floor);
    fv->timer = df_param->fade_frames; fv->fade = 2;
    df_joint_animation(floor, 1);
}
static void df_fade_in(int kind)
{
    HSD_GObj* floor = Ground_GetMapGObj(2);
    DfFloor* fv = DF(DfFloor, floor);
    fv->timer = df_param->fade_frames; fv->fade = 1;
    df_set_platform(floor, kind);
    df_joint_animation(floor, 0);
}
static void df_floor_create(Ground_GObj* gobj)
{
    DfFloor* fv = DF(DfFloor, gobj);
    fv->fade = fv->timer = 0; fv->platform = NULL;
    fv->sound[0] = df_sound(5, df_param->floor_volume[0], 127);
    fv->sound[1] = df_sound(7, df_param->floor_volume[1], 127);
    df_set_platform(gobj, 1);
    Ground_InitMapColl(GET_JOBJ(gobj), df_ground(gobj)->map_id);
}
MU_NO_CONTRACT static void df_floor_frame(Ground_GObj* gobj)
{
    DfFloor* fv = DF(DfFloor, gobj);
    if (fv->timer > 0) {
        f32 alpha;
        int i;
        fv->timer--;
        alpha = fv->fade == 0 ? 1.0f : (f32) fv->timer / (f32) df_param->fade_frames;
        if (fv->fade == 1) alpha = 1.0f - alpha;
        df_alpha(GET_JOBJ(gobj), alpha);
        for (i = 0; i < 2; i++) if (fv->sound[i] != -1) {
            HSD_AudioSFXSetVolumeEx(fv->sound[i], (u8) (int) ((f32) df_param->floor_volume[i] * alpha));
        }
        if (fv->platform != NULL) df_alpha(GET_JOBJ(fv->platform), alpha);
    } else if (fv->fade == 2) { fv->fade = 0; df_set_platform(gobj, 0); }
    Ground_UpdateMapColl(gobj);
}
static void df_platform_create(Ground_GObj* gobj)
{
    df_anim(gobj);
    Ground_InitMapColl(GET_JOBJ(gobj), df_ground(gobj)->map_id);
    lb_8000C2F8(Ground_801C3FA4(gobj, 0), Ground_801C3FA4(Ground_GetMapGObj(2), 1));
}
static void df_platform_frame(Ground_GObj* gobj) { Ground_UpdateMapColl(gobj); }
/* +1118: splash on downward water-surface crossing, no fighter interaction. */
MU_NO_CONTRACT static bool df_splash(Ground_GObj* stage, Fighter_GObj* fighter, Vec3* out)
{
    DfBackground* bv = DF(DfBackground, stage);
    Fighter* fp = HSD_GObjGetUserData(fighter);
    (void) out;
    if (bv->water && fp->cur_pos.y < bv->water_y && fp->prev_pos.y > bv->water_y) {
        Vec3 pos = { fp->cur_pos.x, bv->water_y, fp->cur_pos.z };
        HSD_Generator* generator = grLib_801C96F8(0x7530, 30, &pos);
        if (generator != NULL) {
            /* +1178 reads CmSubject +50, target_ext.v.z. */
            f32 scale = fp->x890_cameraBox->target_ext.v.z / 10.0f;
            generator->appsrt->scale.x *= scale;
            generator->appsrt->scale.y *= scale;
            generator->appsrt->scale.z *= scale;
        }
        df_sound(4, 255, 127);
    }
    return false;
}
/* +F6C draws the debug water level only when collision display is selected. */
static void df_draw(HSD_GObj* gobj, intptr_t pass)
{
    DfBackground* bv = DF(DfBackground, gobj);
    grDisplay_801C5DB0(gobj, pass);
    if (pass == 2 && bv->water && game_camera.x399_b4) {
        Mtx view;
        GXColor color = { 255, 255, 255, 255 };
        GXSetCullMode(GX_CULL_NONE); GXClearVtxDesc();
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
        HSD_CObjGetViewingMtx(HSD_CObjGetCurrent(), view);
        GXSetCurrentMtx(GX_PNMTX0); GXLoadPosMtxImm(view, GX_PNMTX0);
        mpLib_SetupDraw(color);
        GXBegin(GX_LINES, GX_VTXFMT0, 2);
        GXPosition3f32(-400.0f, bv->water_y, 0.0f);
        GXPosition3f32(400.0f, bv->water_y, 0.0f);
    }
}
static void df_background_coll(HSD_GObj* gobj)
{
    DfBackground* bv = DF(DfBackground, gobj);
    int i;
    for (i = 0; i < 9; i++) {
        if (bv->location == i + 2) mpJointListAdd(i + 11);
        else mpLib_80057BC0(i + 11);
    }
}
static void df_clear_coll(void) { int i; for (i = 11; i < 20; i++) mpLib_80057BC0(i); }
static void df_visibility(HSD_GObj* gobj, int clear)
{
    DfBackground* bv = DF(DfBackground, gobj);
    static const int location[3] = { 3, 8, 10 };
    int i;
    for (i = 0; i < 3; i++) {
        HSD_JObj* joint = Ground_801C3FA4(gobj, i + 5);
        if (bv->location == location[i] && !clear) HSD_JObjSetFlags(joint, JOBJ_HIDDEN);
        else HSD_JObjClearFlags(joint, JOBJ_HIDDEN);
    }
}
static void df_moving_coll(HSD_GObj* gobj, int location)
{
    DfBackground* bv = DF(DfBackground, gobj);
    if ((unsigned) location > 1) {
        HSD_JObj* collision = Ground_801C3FA4(gobj, 31);
        HSD_JObj* moving = Ground_801C3FA4(gobj, location + 18);
        Vec3 offset = { 0.0f, bv->water_offset[location - 2], 0.0f }, pos;
        lb_8000B1CC(moving, NULL, &collision->translate);
        HSD_JObjSetMtxDirtySub(collision);
        lb_8000B1CC(moving, &offset, &pos);
        bv->water_y = pos.y;
    }
    Ground_UpdateMapColl(gobj);
}
static void df_location(HSD_GObj* gobj)
{
    DfBackground* bv = DF(DfBackground, gobj);
    int animation = 0;
    bv->previous = bv->location;
    switch (bv->location) {
    case 0: bv->location = 1; return;
    case 1:
        if (bv->alternate == 0) { bv->location = 2; bv->alternate = 1; animation = 0; }
        else { bv->location = 3; bv->alternate = 0; animation = 1; }
        break;
    case 2: if (HSD_Randf() > 0.5f) { bv->location = 4; animation = 2; }
            else { bv->location = 5; animation = 3; } break;
    case 3: if (HSD_Randf() > 0.5f) { bv->location = 5; animation = 4; }
            else { bv->location = 6; animation = 5; } break;
    case 4: bv->location = 7; animation = 6; break;
    case 5: if (HSD_Randf() > 0.5f) { bv->location = 7; animation = 7; }
            else { bv->location = 8; animation = 8; } break;
    case 6: bv->location = 8; animation = 9; break;
    case 7: if (HSD_Randf() > 0.5f) { bv->location = 9; animation = 10; }
            else { bv->location = 10; animation = 11; } break;
    case 8: if (HSD_Randf() > 0.5f) { bv->location = 9; animation = 12; }
            else { bv->location = 10; animation = 13; } break;
    case 9: bv->location = 1; animation = 14; break;
    case 10: bv->location = 1; animation = 15; break;
    }
    df_joint_animation(gobj, animation);
    HSD_ForeachAnim(GET_JOBJ(gobj), JOBJ_TYPE, 0xFB7F, HSD_AObjSetRate, AOBJ_ARG_AF,
                    (double) df_param->animation_rate);
}
static void df_background_create(Ground_GObj* gobj)
{
    DfBackground* bv = DF(DfBackground, gobj);
    Vec3 reference, pos;
    int i;
    HSD_GObjGXLink_8039084C(gobj);
    GObj_SetupGXLink(gobj, df_draw, 3, 0);
    lb_8000B1CC(Ground_801C3FA4(gobj, 29), NULL, &reference);
    for (i = 0; i < 9; i++) {
        lb_8000B1CC(Ground_801C3FA4(gobj, i + 20), NULL, &pos);
        bv->water_offset[i] = reference.y - pos.y;
    }
    df_anim(gobj);
    bv->wait = bv->travel = 0;
    bv->location = bv->previous = 1;
    bv->alternate = HSD_Randi(2); bv->water = 0; bv->announce = 0;
    bv->ambient[0] = df_sound(8, 0, 127); bv->ambient[1] = df_sound(9, 0, 127);
    Ground_InitMapColl(GET_JOBJ(gobj), df_ground(gobj)->map_id);
    df_clear_coll();
    ftCo_800C07F8(gobj, 6, df_splash);
}
static void df_background_frame(Ground_GObj* gobj)
{
    DfBackground* bv = DF(DfBackground, gobj);
    Vec3 pos;
    if (bv->ambient[0] != -1) {
        lb_8000B1CC(Ground_801C3FA4(gobj, 14), NULL, &pos);
        df_volume(bv->ambient[0], &pos, (f32) df_param->ambient_distance[0], 0, 200);
    }
    if (bv->ambient[1] != -1) {
        lb_8000B1CC(Ground_801C3FA4(gobj, 28), NULL, &pos);
        df_volume(bv->ambient[1], &pos, (f32) df_param->ambient_distance[1], 0, 127);
    }
    if (bv->announce != 0) bv->announce--;
    else { bv->announce = df_random(df_param->announce_min, df_param->announce_max); df_sound(6, 127, 127); }
    if (bv->travel > 0) {
        bv->travel--;
        if (bv->travel == df_param->camera_frame) {
            grAnime_801C8138(Ground_GetMapGObj(0), 0, bv->location + 2);
            Ground_801C3BB4(); Ground_801C39C0();
        }
        if (bv->travel == df_param->travel_frames - df_param->camera_frame) {
            grAnime_801C8138(Ground_GetMapGObj(0), 0, 0);
            Ground_801C3BB4(); Ground_801C39C0();
        }
        if (bv->travel == df_param->collision_frame) { df_background_coll(gobj); df_visibility(gobj, 0); }
        if (bv->travel == df_param->travel_frames - df_param->collision_frame) { df_clear_coll(); df_visibility(gobj, 1); }
        if (bv->travel >= df_param->travel_frames - df_param->water_window) {
            df_moving_coll(gobj, bv->previous);
            bv->water = bv->previous != 1;
            if (bv->previous != 1) goto fade_out;
        } else if (bv->travel <= df_param->collision_frame) {
            df_moving_coll(gobj, bv->location);
            bv->water = bv->location != 1;
            if (bv->location == 1) goto fade_in;
        } else bv->water = 0;
fade_out:
        if ((unsigned) bv->location > 1 && bv->travel == df_param->fade_out_frame) df_fade_out();
fade_in:
        if ((unsigned) bv->previous > 1 && bv->travel == df_param->travel_frames - 1) {
            df_fade_in(df_param->platform[bv->previous]);
        }
    } else if (bv->wait > 0) bv->wait--;
    else { df_location(gobj); bv->travel = df_param->travel_frames; bv->wait = df_param->location_wait; }
    lb_800115F4();
}
/* +AF8 jump table points at an identical copy at +1C44. */
static void df_shine_create(Ground_GObj* gobj)
{
    const int map = df_ground(gobj)->map_id;
    int target = 0;
    df_anim(gobj);
    if (map >= 7 && map <= 10) { *DF(int, gobj) = 0; target = 17; }
    else if (map == 11) target = 15;
    else if (map == 12) target = 3;
    else if (map == 13) target = 4;
    else if (map == 15 || map == 16) { df_ground(gobj)->x10_flags.b2 = 0; target = map == 15 ? 29 : 17; }
    lb_8000C2F8(Ground_801C3FA4(gobj, 0), Ground_801C3FA4(Ground_GetMapGObj(1), target));
}
static void df_shine_frame(Ground_GObj* gobj)
{
    int* timer = DF(int, gobj);
    if (df_ground(gobj)->map_id != 7) return;
    if (*timer != 0) (*timer)--;
    else {
        Vec3 pos;
        int sound;
        lb_8000B1CC(Ground_801C3FA4(gobj, 0), NULL, &pos);
        sound = df_sound(HSD_Randi(4), 255, 127);
        df_volume(sound, &pos, (f32) df_param->shine_distance, 0, 255);
        HSD_AudioSFXSetPan(sound, (u8) HSD_Randi(255));
        *timer = df_random(df_param->shine_min, df_param->shine_max);
    }
}
static void df_sky_create(Ground_GObj* gobj)
{
    DfSky* sky = DF(DfSky, gobj);
    df_anim(gobj);
    lb_8000C2F8(Ground_801C3FA4(gobj, 0), Ground_801C3FA4(Ground_GetMapGObj(1), 29));
    sky->light[0] = sky->light[1] = NULL;
    df_ground(gobj)->x10_flags.b2 = 0;
}
MU_NO_CONTRACT static void df_sky_frame(Ground_GObj* gobj)
{
    DfSky* sky = DF(DfSky, gobj);
    HSD_JObj* joint;
    Vec3 pos;
    int i;
    if (sky->light[0] == NULL && HSD_GObjPLinkHead[0]->prev != NULL) {
        HSD_GObj* scan;
        for (scan = HSD_GObjPLinkHead[0]->prev; scan != NULL; scan = scan->next) {
            if (scan->classifier == 12 || scan->classifier == 13) {
                HSD_LObj* light = GET_LOBJ(scan);
                if (light != NULL) {
                    light = light->next;
                    if (light != NULL) sky->light[scan->classifier == 12 ? 0 : 1] = light;
                }
            }
        }
    }
    joint = Ground_801C3FA4(gobj, 2);
    if (joint == NULL) return;
    lb_8000B1CC(joint, NULL, &pos);
    pos.x *= 0.1f; pos.y *= 0.1f; pos.z *= 0.1f;
    for (i = 0; i < 2; i++) if (sky->light[i] != NULL) sky->light[i]->position->pos = pos;
}
static void df_init(void)
{
    static const int order[] = { 0, 1, 2, 14, 7, 8, 9, 10, 11, 12, 13, 15, 16 };
    int i;
    df_param = Ground_GetYakumonoParam();
    if (df_param == NULL) __assert("delfino.c", 0x475, "yakumono node was not found in dat");
    for (i = 0; i < (int) (sizeof(order) / sizeof(order[0])); i++) mu_grTSeak_80223908(order[i]);
    Ground_801C39C0(); Ground_801C3BB4();
}
#define DF_ROW(create, frame, flags) { create, df_false, frame, mu_gr_gobj_nop, flags }
static StageCallbacks df_map[] = {
    DF_ROW(df_anim, mu_gr_gobj_nop, 0), DF_ROW(df_background_create, df_background_frame, 0xC0000000),
    DF_ROW(df_floor_create, df_floor_frame, 0),
    DF_ROW(df_platform_create, df_platform_frame, 0), DF_ROW(df_platform_create, df_platform_frame, 0),
    DF_ROW(df_platform_create, df_platform_frame, 0), DF_ROW(df_platform_create, df_platform_frame, 0),
    DF_ROW(df_shine_create, df_shine_frame, 0), DF_ROW(df_shine_create, df_shine_frame, 0),
    DF_ROW(df_shine_create, df_shine_frame, 0), DF_ROW(df_shine_create, df_shine_frame, 0),
    DF_ROW(df_shine_create, df_shine_frame, 0), DF_ROW(df_shine_create, df_shine_frame, 0),
    DF_ROW(df_shine_create, df_shine_frame, 0), DF_ROW(df_sky_create, df_sky_frame, 0),
    DF_ROW(df_shine_create, df_shine_frame, 0), DF_ROW(df_shine_create, df_shine_frame, 0),
};
const MuGrStage mu_gr_delfino_plaza = {
    .name = "Delfino Plaza", .file = "/GrDo.dat", .map_gobjs = df_map,
    .on_init = df_init, .on_demo_init = mu_gr_demo_nop, .on_load = mu_gr_event_nop,
    .on_start = mu_gr_event_nop, .callback4 = mu_gr_false, .on_touch_line = mu_gr_no_touch_line,
    .on_check_shadow_render = mu_gr_shadow_always, .flags = MU_GR_READY,
};
