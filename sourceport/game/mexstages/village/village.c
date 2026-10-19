/* Village, /GrAc.dat, internal 83 and external 300. Native implementations of every grFunction
 * export and its thirteen map-object rows. Calendar variants load their own map_head archives,
 * palettes, fog and lights before objects are created. No PowerPC executes here. */
#include "village.h"
#include "../../akaneia/mu_ak_fighter.h"
#include <melee/cm/camera.h>
#include <melee/gr/granime.h>
#include <melee/gr/grdatfiles.h>
#include <melee/gr/grlib.h>
#include <melee/gr/grmaterial.h>
#include <melee/gr/ground.h>
#include <melee/gr/stage.h>
#include <melee/it/item.h>
#include <melee/it/itcoll.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <melee/lb/lbarchive.h>
#include <melee/lb/lbvector.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/axdriver.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/fog.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/tobj.h>
#include <string.h>

AcParam* ac_param;
datetime ac_date;
int ac_time, ac_season, ac_event;
AcResidentSlot* ac_database;

/* +1A0C and +1A74 call the static four-slot archive allocator at 801C62B4. Its public base
 * accessor lets this local implementation retain the same first-free order and failure. */
static UnkArchiveStruct* ac_archive_slot(void)
{
    UnkArchiveStruct* slots = grDatFiles_GetArchive();
    int i;
    for (i = 0; i < 4; i++) { if (slots[i].unk0 == NULL) { return &slots[i]; } }
    __assert("grdatfiles.c", 229, "0");
    return NULL;
}

static HSD_Archive* ac_load_map(const char* file)
{
    UnkArchiveStruct* slot = ac_archive_slot();
    HSD_Archive* archive = lbArchive_LoadArchive((char*) file);
    slot->unk0 = archive;
    slot->unk4 = HSD_ArchiveGetPublicAddress(archive, "map_head");
    slot->unk8 = 1;
    return archive;
}

/* +1A74: named native fields replace raw stage globals. Colors are GXColor bytes on both hosts. */
MU_NO_CONTRACT static void ac_load_environment(void)
{
    static const char* files[4] = { "GrAcDy.dat", "GrAcSs.dat", "GrAcNt.dat", "GrAcDn.dat" };
    HSD_Archive* archive = ac_load_map(files[ac_time]);
    GroundParam* param = HSD_ArchiveGetPublicAddress(archive, "grGroundParam");
    HSD_FogDesc* fogdesc;
    LightListSlot* lights;
    GXColor* colors;
    Ground_801C052C(&param->xB8);
    Ground_801C05D4(&param->xBC);
    Ground_801C0544(&param->xC0);
    Ground_801C058C(&param->xC4);
    Ground_801C05EC(&param->xC8);
    Ground_801C05A4(&param->xCC);
    Ground_801C055C(&param->xD0);
    Ground_801C05BC(&param->xD4);
    Ground_801C0574(&param->xD8);
    fogdesc = HSD_ArchiveGetPublicAddress(archive, "fogdesc");
    if (fogdesc != NULL) {
        HSD_Fog* fog = stage_info.x12C->hsd_obj;
        fog->color = fogdesc->color;
        Camera_SetBackgroundColor(fog->color.r, fog->color.g, fog->color.b);
        fog->start = fogdesc->start * stage_info.param->y;
        fog->end = fogdesc->end * stage_info.param->y;
    }
    lights = HSD_ArchiveGetPublicAddress(archive, "map_plit");
    if (lights != NULL) { stage_info.map_plit = lights; }
    colors = HSD_ArchiveGetPublicAddress(archive, "light_color");
    if (colors != NULL) {
        HSD_GObj* gobj;
        for (gobj = HSD_GObjPLinkHead[3]; gobj != NULL; gobj = gobj->next) {
            if (gobj->classifier == 13 && gobj->hsd_obj != NULL) {
                HSD_LObj* light;
                for (light = gobj->hsd_obj; light != NULL; light = light->next) {
                    light->color = *colors++;
                }
            }
        }
    }
}

/* +38F4, the block's own substring search. */
static const char* ac_substring(const char* text, const char* search)
{
    const char *a, *b;
    if (*search == '\0') { return text; }
    for (; *text != '\0'; text++) {
        for (a = text, b = search; *a == *b && *b != '\0'; a++, b++) {}
        if (*b == '\0') { return text; }
    }
    return NULL;
}

/* +1C80: writable playlist chances share the exact MxDt rows used by the stage music picker. */
static void ac_playlist(void)
{
    const MexData* data = mu_gr_mex_data(MU_MXDT_MEXDATA);
    MexPlaylist* playlist = mu_gr_mex_playlist();
    bool time_music = (HSD_PadMasterStatus[0].button & 0x60) != 0;
    unsigned i;
    for (i = 0; i < playlist->count; i++) {
        MexPlaylistSong* song = &DP(playlist->songs)[i];
        const char* name = DP(DP(DP(data->music)->labels)[song->song]);
        int chance = 0;
        if (name != NULL) {
            if (ac_event == 2) { chance = ac_substring(name, "Live") != NULL ? 100 : 0; }
            else if (ac_substring(name, "Live") != NULL) { chance = 0; }
            else if (ac_substring(name, "Dawn") != NULL) { chance = time_music && ac_time == 3 ? 100 : 0; }
            else if (ac_substring(name, "Day") != NULL) { chance = time_music && ac_time == 0 ? 100 : 0; }
            else if (ac_substring(name, "Evening") != NULL) { chance = time_music && ac_time == 1 ? 100 : 0; }
            else if (ac_substring(name, "Night") != NULL) { chance = time_music && ac_time == 2 ? 100 : 0; }
            else { chance = time_music ? 0 : 100; }
        }
        song->chance = chance;
    }
}

static void ac_constant_particles(void)
{
    Vec3 pos = { 0 };
    unsigned date = (unsigned) ac_date.month * 256 + ac_date.day;
    if (date == 0xC18) { grLib_801C96F8(0x753B, 30, &pos); }
    if (date == 0x80C && (u8) (ac_date.hour - 18) <= 2) { grLib_801C96F8(0x753C, 30, &pos); }
    if (date == 0x704 && ac_date.hour > 19) { grLib_801C96F8(0x7534, 30, &pos); }
}

/* OnInit +104. Direction inputs override time after event selection, as on the console. */
static void ac_init(void)
{
    u32 buttons;
    ac_param = mexgr_village_yakumono("village.c", 0x7FB);
    ac_calendar_now(&ac_date);
    ac_season = ac_determine_season();
    ac_time = ac_determine_time();
    ac_event = ac_determine_event();
    ac_database = HSD_ArchiveGetPublicAddress(grDatFiles_801C6330(0)->unk0, "grMurabito");
    if (ac_database == NULL) { __assert("village.c", 0x809, "resident database not found"); }
    buttons = HSD_PadMasterStatus[0].button;
    if (buttons & 8) { ac_time = 1; }
    if (buttons & 4) { ac_time = 2; }
    if (buttons & 2) { ac_time = 3; }
    if (buttons & 1) { ac_time = 0; }
    ac_constant_particles();
    if (ac_event == 1 || ac_event == 2) { ac_load_map("GrAcKk.dat"); }
    else if (ac_event == 3) { ac_load_map("GrAcNy.dat"); }
    else if (ac_event == 4) { ac_load_map("GrAcHm.dat"); }
    ac_load_environment();
    ac_playlist();
    mu_grTSeak_80223908(0);
    mu_grTSeak_80223908(1);
    mu_grTSeak_80223908(7);
    mu_grTSeak_80223908(8);
    mu_grTSeak_80223908(9);
    mu_grTSeak_80223908(2);
    mu_grTSeak_80223908(3);
    mu_grTSeak_80223908(6);
    mu_grTSeak_80223908(10);
    if (ac_event != 0) { mu_grTSeak_80223908(5); }
    Ground_801C39C0();
    Ground_801C3BB4();
}

static bool ac_shadow(Vec3* pos, int unused, HSD_JObj* joint)
{
    Vec3 world;
    (void) unused;
    lb_8000B1CC(joint, NULL, &world);
    return pos->y > world.y;
}

void ac_season_material(HSD_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    UnkStageDat* map = grDatFiles_801C6330(gp->map_id)->unk4;
    HSD_JObj* child = GET_JOBJ(gobj)->child;
    if (child == NULL) { return; }
    HSD_JObjAddAnimAll(child, NULL, DP(DP(DP(map->unk8)[gp->map_id].unk8)[ac_season]), NULL);
    HSD_JObjReqAnimAll(child, 0.0f);
    HSD_JObjAnimAll(child);
}

static void ac_season_create(Ground_GObj* gobj)
{
    mexgr_village_anim0_create(gobj);
    ac_season_material(gobj);
}

static void ac_slider_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, ac_event == 2 ? 0 : 1);
}

static void ac_floor_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    AcFloor* floor = AC_VARS(AcFloor, gp);
    mexgr_village_anim0_create(gobj);
    gp->x10_flags.b5 = 1;
    floor->balloon = NULL;
    ac_season_material(gobj);
    ac_population_create(gobj);
    if (ac_event == 1 || ac_event == 2) {
        HSD_JObj* root = Ground_801C3FA4(mu_grTSeak_80223908(4), 0);
        lb_8000B1CC(Ground_801C3FA4(gobj, 4), NULL, &root->translate);
    }
}

MU_NO_CONTRACT static void ac_floor_frame(Ground_GObj* gobj)
{
    AcFloor* floor = AC_GET(AcFloor, gobj);
    if (floor->timer > 0) { floor->timer--; }
    else if (Ground_GetMapGObj(11) == NULL) {
        float base = (float) (ac_param->balloon_min + ac_param->balloon_wait);
        float range = (float) (ac_param->balloon_max - ac_param->balloon_min);
        float product;
        floor->balloon = mu_grTSeak_80223908(11);
        product = range * HSD_Randf();
        floor->timer = (int) (product + base);
    }
    lb_800115F4();
}

static void ac_platform_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    mexgr_village_anim0_create(gobj);
    gp->x10_flags.b5 = 1;
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
}

static void ac_platform_frame(Ground_GObj* gobj) { Ground_UpdateMapColl(gobj); }

static void ac_train_create(Ground_GObj* gobj)
{
    AC_GET(AcTimer, gobj)->timer = ac_param->train_wait;
    mexgr_village_anim0_create(gobj);
}

f32 ac_anim_frame(HSD_JObj* joint)
{
    for (; joint != NULL; joint = joint->child) {
        HSD_DObj* dobj;
        if (joint->aobj != NULL) { return joint->aobj->curr_frame; }
        for (dobj = joint->u.dobj; dobj != NULL; dobj = dobj->next) {
            HSD_TObj* texture;
            if (dobj->aobj != NULL) { return dobj->aobj->curr_frame; }
            if (dobj->mobj->aobj != NULL) { return dobj->mobj->aobj->curr_frame; }
            for (texture = dobj->mobj->tobj; texture != NULL; texture = texture->next) {
                if (texture->aobj != NULL) { return texture->aobj->curr_frame; }
            }
        }
    }
    return -1.0f;
}

static void ac_train_frame(Ground_GObj* gobj)
{
    AcTimer* timer = AC_GET(AcTimer, gobj);
    float frame;
    if (timer->timer > 0) { timer->timer--; return; }
    frame = ac_anim_frame(Ground_801C3FA4(gobj, 0));
    if (!(frame >= (float) ac_param->train_first && frame <= (float) ac_param->train_last)) {
        HSD_JObj* joint = Ground_801C3FA4(gobj, 1);
        Vec3 pos = { 0 };
        if (joint != NULL) { lb_8000B1CC(joint, NULL, &pos); grLib_801C96F8(0x7530, 30, &pos); }
        timer->timer = ac_param->train_wait;
    }
}

static void ac_clock_create(Ground_GObj* gobj)
{
    mexgr_village_anim0_create(gobj);
    AC_GET(AcTimer, gobj)->timer = 0;
}

MU_NO_CONTRACT static void ac_clock_frame(Ground_GObj* gobj)
{
    AcTimer* timer = AC_GET(AcTimer, gobj);
    HSD_JObj *minute, *hour;
    datetime date;
    if (timer->timer != 0) { timer->timer--; return; }
    minute = Ground_801C3FA4(gobj, 2);
    hour = Ground_801C3FA4(gobj, 3);
    if (minute != NULL && hour != NULL) {
        ac_calendar_now(&date);
        minute->rotate.y = (float) (((double) date.minute / 60.0) * 6.283185307179586);
        hour->rotate.y = (float) (((double) (date.hour % 12) / 12.0) * 6.283185307179586);
        HSD_JObjSetMtxDirtySub(minute);
        HSD_JObjSetMtxDirtySub(hour);
    }
    timer->timer = 30;
}

/* +2074 writes old digits to DObjs 4..7 and new digits to 8..11, then selects the first
 * changing decimal position's animation, ordered minute tens through second ones. */
static void ac_countdown_board(HSD_GObj* gobj, datetime* date)
{
    AcEvent* event = AC_GET(AcEvent, gobj);
    int old_minute = event->minute, old_second = event->second;
    ac_material_frame(gobj, 4, old_second % 10);
    ac_material_frame(gobj, 5, old_second / 10);
    ac_material_frame(gobj, 6, old_minute % 10);
    ac_material_frame(gobj, 7, old_minute / 10);
    event->minute = 59 - date->minute;
    event->second = 59 - date->second;
    ac_material_frame(gobj, 8, event->second % 10);
    ac_material_frame(gobj, 9, event->second / 10);
    ac_material_frame(gobj, 10, event->minute % 10);
    ac_material_frame(gobj, 11, event->minute / 10);
    if (event->minute / 10 != old_minute / 10) { ac_add_animation(gobj, 3); }
    else if (event->minute % 10 != old_minute % 10) { ac_add_animation(gobj, 2); }
    else if (event->second / 10 != old_second / 10) { ac_add_animation(gobj, 1); }
    else if (event->second % 10 != old_second % 10) { ac_add_animation(gobj, 0); }
}

/* +4D8. Jev question: the console calls HSD_ForeachAnim(AOBJ_ARG_AF,HSD_AObjSetRate)
 * without loading a double. Use explicit zero or one? Answer: zero_rate, confidence 0.98
 * (jev_state.json, request 4387dd37ffbffb77). Zero freezes the manually selected board digits.
 * This resolves the missing vararg without a native undefined read. */
static void ac_event_create(Ground_GObj* gobj)
{
    if (ac_event == 1 || ac_event == 2 || ac_event == 4) {
        mexgr_village_anim0_create(gobj);
        if (ac_event == 1) {
            HSD_DObj* dobj = Ground_801C3FA4(gobj, 0)->u.dobj;
            int i;
            for (i = 0; i <= 7; i++) {
                if (i == 6 || i == 7) { HSD_DObjSetFlags(dobj, 1); }
                if (i < 7) {
                    if (dobj->next == NULL) { __assert("village.c", 0x1D8, "dobj not found"); }
                    dobj = dobj->next;
                }
            }
        }
        if ((ac_event == 1 || ac_event == 2) && ac_time == 0) {
            int i;
            for (i = 2; i <= 5; i++) { HSD_JObjSetFlags(Ground_801C3FA4(gobj, i), JOBJ_HIDDEN); }
        }
    } else if (ac_event == 3) {
        AcEvent* event = AC_GET(AcEvent, gobj);
        HSD_JObj* child = GET_JOBJ(gobj)->child;
        UnkStageDat* map = grDatFiles_801C6330(5)->unk4;
        datetime date;
        event->minute = event->second = event->celebrated = 0;
        if (child == NULL) { return; }
        HSD_JObjAddAnimAll(child, NULL, DP(DP(DP(map->unk8)[5].unk8)[1]), NULL);
        HSD_ForeachAnim(child, JOBJ_TYPE, 0xFB7F, HSD_AObjSetRate, AOBJ_ARG_AF, 0.0);
        HSD_JObjReqAnimAll(child, 0.0f);
        HSD_JObjAnimAll(child);
        lb_8000B1CC(Ground_801C3FA4(Ground_GetMapGObj(7), 4), NULL, &child->translate);
        HSD_JObjSetMtxDirtySub(child);
        ac_calendar_now(&date);
        ac_countdown_board(gobj, &date);
    }
}

static void ac_stage_sound(int index)
{
    int bank = mu_gr_mex_stage_bank(Stage_8022519C(Stage_80225194()));
    HSD_AudioSFXStartParam(bank * 10000 + index, 255, 127, 0, 6);
}

static void ac_event_frame(Ground_GObj* gobj)
{
    AcEvent* event = AC_GET(AcEvent, gobj);
    datetime date;
    if (ac_event != 3 || event->celebrated != 0) { return; }
    ac_calendar_now(&date);
    if (date.hour == 23) {
        if (event->minute == 59 - date.minute && event->second == 59 - date.second) { return; }
        ac_countdown_board(gobj, &date);
        if (event->minute == 0 && event->second <= 10) { ac_stage_sound(event->second == 0 ? 3 : 2); }
    } else {
        HSD_JObj* joint = Ground_801C3FA4(gobj, 0);
        UnkStageDat* map;
        AcFloor* floor;
        Vec3 pos = { 0 };
        int i;
        event->celebrated = 1;
        grLib_801C96F8(0x7534, 30, &pos);
        lb_8000B1CC(joint, NULL, &pos);
        grLib_801C96F8(0x753E, 30, &pos);
        ac_stage_sound(1);
        floor = AC_GET(AcFloor, Ground_GetMapGObj(7));
        for (i = 0; i < 6; i++) {
            if (floor->resident[i] != NULL) {
                ac_set_action(floor->resident[i], 3, 500);
                ac_set_mood(floor->resident[i], 1, 500);
            }
        }
        ac_add_animation(gobj, 4);
        map = grDatFiles_801C6330(5)->unk4;
        HSD_JObjAddAnimAll(joint, NULL, DP(DP(DP(map->unk8)[5].unk8)[0]), NULL);
        HSD_JObjReqAnimAll(joint, 0.0f);
        HSD_JObjAnimAll(joint);
    }
}

/* +298C: prize detached on pop. The first two physics masses determine the initial impulse. */
MU_NO_CONTRACT static void ac_balloon_hurt(Item_GObj* item, Ground* gp, Vec3* hit,
                                          HSD_GObj* source, f32 damage)
{
    AcBalloon* balloon = AC_VARS(AcBalloon, gp);
    HSD_JObj* joint;
    Vec3 effect = { 0 }, offset;
    AcPhysics* p = balloon->physics;
    (void) item; (void) hit; (void) source; (void) damage;
    lb_8000B1CC(Ground_801C3FA4(balloon->self, 0), NULL, &balloon->pos);
    grAnime_801C8138(balloon->self, gp->map_id, 6);
    balloon->popped = 1;
    joint = Ground_801C3FA4(balloon->self, 0);
    joint->translate = balloon->pos;
    HSD_JObjSetMtxDirtySub(joint);
    if (balloon->hurtbox != NULL) { it_80271508(balloon->hurtbox, HurtCapsule_Intangible); }
    ac_stage_sound(0);
    joint = Ground_801C3FA4(balloon->self, 1);
    if (joint != NULL) { lb_8000B1CC(joint, NULL, &effect); grLib_801C96F8(0x7531, 30, &effect); }
    p->mass[0].fixed = 0;
    offset.x = p->mass[1].pos.x - p->mass[0].pos.x;
    offset.y = p->mass[1].pos.z - p->mass[0].pos.z;
    lbVector_NormalizeXY(&offset);
    p->mass[0].pos.x += offset.x;
    p->mass[0].pos.z += offset.y;
    if (balloon->prize != NULL) {
        Item* ip = HSD_GObjGetUserData(balloon->prize);
        ip->xDCC_flag.b3 = 1;
        ip->xDC8_word.flags.x15 = 1;
        Item_80268E5C(balloon->prize, 0, ITEM_ANIM_UPDATE);
        balloon->prize = NULL;
    }
}

static void ac_balloon_destroy(HSD_GObj* gobj)
{
    AcBalloon* balloon = AC_GET(AcBalloon, gobj);
    if (balloon->prize != NULL) { Item_8026A8EC(balloon->prize); }
    if (balloon->hurtbox != NULL) { Item_8026A8EC(balloon->hurtbox); }
    if (balloon->physics != NULL) { HSD_Free(balloon->physics); }
}

static void ac_balloon_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    AcBalloon* balloon = AC_VARS(AcBalloon, gp);
    balloon->self = gobj;
    grAnime_801C8138(gobj, gp->map_id, HSD_Randi(6));
    balloon->hurtbox = grMaterial_801C8CFC(0, 0, gp, Ground_801C3FA4(gobj, 0), NULL, ac_balloon_hurt, NULL);
    if (balloon->hurtbox != NULL) {
        grMaterial_801C8DE0(balloon->hurtbox, 0, 0, 0, 0, 0, 0, 5.0f);
        grMaterial_801C8E08(balloon->hurtbox);
    }
    if (Item_80266F3C()) {
        SpawnItem spawn = { 0 };
        Item* ip;
        spawn.kind = 0x12;
        balloon->prize = mu_ak_item_create(&spawn);
        ip = HSD_GObjGetUserData(balloon->prize);
        ip->physics_updated = NULL;
        ip->collided = NULL;
        ip->xDCC_flag.b3 = 0;
        ip->xDC8_word.flags.x15 = 0;
    } else { balloon->prize = NULL; }
    gp->x1C_callback = ac_balloon_destroy;
    balloon->physics = mu_gr_mex_calloc(sizeof(AcPhysics));
    balloon->physics->iterations = 2;
    balloon->physics->stiffness = 1.0f;
    ac_rope_create(balloon->physics, Ground_801C3FA4(gobj, 0), 1, 4);
    balloon->physics->mass[0].fixed = 1;
}

static void ac_balloon_frame(Ground_GObj* gobj)
{
    AcBalloon* balloon = AC_GET(AcBalloon, gobj);
    AcPhysics* p = balloon->physics;
    int i;
    if (!balloon->popped) {
        lb_8000B1CC(Ground_801C3FA4(gobj, 0), NULL, &p->mass[0].anchor);
    }
    ac_physics_update(p);
    for (i = 0; i < p->mass_count; i++) {
        HSD_JObj* joint = p->mass[i].joint;
        if (joint == NULL) { continue; }
        joint->flags |= JOBJ_USER_DEF_MTX | JOBJ_MTX_INDEP_PARENT | JOBJ_MTX_INDEP_SRT;
        joint->mtx[0][3] = p->mass[i].pos.x;
        joint->mtx[1][3] = p->mass[i].pos.y;
        joint->mtx[2][3] = 0.0f;
        HSD_JObjSetMtxDirtySub(joint);
    }
    if (balloon->prize != NULL) {
        HSD_JObj* joint = Ground_801C3FA4(gobj, 4);
        if (joint != NULL) {
            Item* ip = HSD_GObjGetUserData(balloon->prize);
            Vec3 offset = { 0.0f, -1.0f, 0.0f };
            lb_8000B1CC(joint, &offset, &ip->pos);
        }
    }
    if (balloon->popped) {
        HSD_JObj* joint = Ground_801C3FA4(balloon->self, 0);
        joint->translate = balloon->pos;
        HSD_JObjSetMtxDirtySub(joint);
    }
    if (!lb_8000B09C(Ground_801C3FA4(gobj, 0))) { Ground_801C4A08(gobj); }
}

#define AC_ROW(create, frame) { create, mexgr_village_unk, frame, mexgr_village_deletion, 0 }
static StageCallbacks ac_map[13] = {
    AC_ROW(mexgr_village_anim0_create, mu_gr_gobj_nop),
    AC_ROW(ac_season_create, mu_gr_gobj_nop),
    AC_ROW(mexgr_village_background_create, mu_gr_gobj_nop),
    AC_ROW(mexgr_village_anim0_create, mu_gr_gobj_nop),
    AC_ROW(ac_slider_create, mu_gr_gobj_nop),
    AC_ROW(ac_event_create, ac_event_frame),
    AC_ROW(ac_train_create, ac_train_frame),
    { ac_floor_create, mexgr_village_unk, ac_floor_frame, mexgr_village_deletion, 0xC0000000 },
    AC_ROW(ac_platform_create, ac_platform_frame),
    AC_ROW(ac_clock_create, ac_clock_frame),
    { mexgr_village_anim0_create, mexgr_village_unk, mu_gr_gobj_nop, mexgr_village_deletion, 0x80000000 },
    AC_ROW(ac_balloon_create, ac_balloon_frame),
    AC_ROW(ac_villager_create, ac_villager_frame),
};

const MuGrStage mu_gr_village = {
    .name = "Village", .file = "/GrAc.dat", .map_gobjs = ac_map,
    .on_init = ac_init, .on_demo_init = mu_gr_demo_nop,
    .on_load = mu_gr_event_nop, .on_start = mu_gr_event_nop,
    .callback4 = mu_gr_false, .on_touch_line = mu_gr_no_touch_line,
    .on_check_shadow_render = ac_shadow,
    .flags = MU_GR_READY,
};
