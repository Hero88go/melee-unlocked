/* Native stage block, offsets refer to GrSv.dat grFunction (Saturn Valley, internal 92,
 * external 309). The coffee break is in saturnvalley_coffee.c, the custom stage shadow in
 * saturnvalley_shadow.c, routines no export reaches in saturnvalley_unused.c. */
#include "saturnvalley.h"

#include <math_native.h>
#include <string.h>

#include <melee/cm/camera.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/types.h>
#include <melee/gr/granime.h>
#include <melee/gr/grdatfiles.h>
#include <melee/gr/grlib.h>
#include <melee/gr/grmaterial.h>
#include <melee/gr/ground.h>
#include <melee/gr/stage.h>
#include <melee/it/forward.h>
#include <melee/it/it_2725.h>
#include <melee/it/itcoll.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <melee/mp/mpcoll.h>
#include <melee/mp/mplib.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/axdriver.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/generator.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/random.h>

SvParam* sv_param;

/* Ground user data of each map gobj; the console keeps them from Ground +E0 on. */
typedef struct SvSimpleVars {
    float angle;      /* console +E0 */
    float target;     /* +E4 */
    int timer;        /* +E8 */
    int anim_timer;   /* +EC */
} SvSimpleVars;
typedef struct SvStatueVars {
    HSD_GObj* self;   /* console +E0 */
    int state;        /* +E4 */
    int hp;           /* +E8 */
    int coll_joint;   /* +EC */
    int timer;        /* +F0 */
    Item_GObj* hurt;  /* +F4 */
} SvStatueVars;
typedef struct SvRunnerVars {
    int timer;        /* console +E0 */
    int state;        /* +E4 */
    int next;         /* +E8 */
    u8 mode;          /* +EC */
    int quake_timer;  /* +F0 */
    CmSubject* subject; /* +F4 */
} SvRunnerVars;
SV_FITS(SvSimpleVars); SV_FITS(SvStatueVars); SV_FITS(SvRunnerVars); SV_FITS(SvFloorVars);

/* +0DDC MEX_PlayStageSoundRaw: a sound of the stage's own bank (MEX_GetData(16) stage sound row
 * of Stage_8022519C(Stage_80225194())), pan 0x80, track 0, channel 6. */
int sv_sound(int id, int volume)
{
    int bank = mu_gr_mex_stage_bank(Stage_8022519C(Stage_80225194()));
    return HSD_AudioSFXStartParam(bank * 10000 + id, (u8) volume, 0x80, 0, 6);
}

/* +2F58 StepValue. The callers below inline the same step with one difference the inline form
 * keeps: an unordered start leaves the value as it is (sv_approach). */
float sv_step(float value, float target, float step)
{
    if (value < target) {
        value += step;
        return value <= target ? value : target;
    }
    if (value <= target) {
        return value;
    }
    value -= step;
    return value >= target ? value : target;
}
float sv_approach(float value, float target, float step)
{
    if (value < target) {
        value += step;
        if (!(value <= target)) value = target;
    } else if (value > target) {
        value -= step;
        if (!(value >= target)) value = target;
    }
    return value;
}

static Ground* sv_ground(HSD_GObj* g) { return HSD_GObjGetUserData(g); }
static bool sv_unk(HSD_GObj* g) { return g != NULL; }
static void sv_delete(Ground_GObj* g) { (void) g; }

/* +0104 Points_OnCreation, +2F8C Saturn_OnCreation. */
void sv_anim0(HSD_GObj* g) { grAnime_801C8138(g, sv_ground(g)->map_id, 0); }

/* +0114 Sky_OnCreation, +0EC8 Clouds_OnCreation (the same code): the halfword at Ground +10
 * loses 0x2000 (x10 b2) and the three bits 0xE0 of +11, then gets 0x40. */
static void sv_sky_create(HSD_GObj* g)
{
    Ground* gp = sv_ground(g);
    gp->x11_flags.b012 = 2;
    gp->x10_flags.b2 = 0;
    grAnime_801C8138(g, gp->map_id, 0);
}
/* +0138 Sky_OnFrame: a jump to lb_800115F4. */
static void sv_sky_frame(HSD_GObj* g) { (void) g; lb_800115F4(); }
/* +001C FloorShadow_OnCreation: clears 0x2000 only, starts no animation. */
static void sv_floor_shadow_create(HSD_GObj* g) { sv_ground(g)->x10_flags.b2 = 0; }

/* ---- Mr. Saturn (map gobj 7, created by map gobj 8) ---- */

static float sv_simple_turn(void)
{
    float range = sv_param->x18;
    float random = HSD_Randf();
    float scaled = random * sv_param->x18;
    return mu_fmadds(-range, 0.5f, scaled);
}
/* +0244 SaturnSimple_OnCreation: the joint lookup's result is not used. */
static void sv_simple_create(HSD_GObj* g)
{
    SvSimpleVars* v = SV_VARS(SvSimpleVars, g);
    int lo;
    Ground_801C3FA4(g, 0);
    grAnime_801C8138(g, sv_ground(g)->map_id, 0);
    v->angle = sv_simple_turn();
    lo = sv_param->x20;
    v->timer = lo + HSD_Randi(sv_param->x24 - lo);
    v->target = v->angle;
    v->anim_timer = 0;
}
/* +02F8 SaturnSimple_OnFrame. Console detail kept: the wait timer set once a turn is done is
 * HSD_Randi(x24) - x20, not the creation's x20 + HSD_Randi(x24 - x20). */
static void sv_simple_frame(HSD_GObj* g)
{
    SvSimpleVars* v = SV_VARS(SvSimpleVars, g);
    HSD_JObj* j = Ground_801C3FA4(g, 0);
    float angle;
    if (v->timer > 0) {
        if (--v->timer == 0) {
            grAnime_801C8138(g, sv_ground(g)->map_id, 1);
            v->anim_timer = 60;
            v->target = sv_simple_turn();
        }
    } else if (v->target == j->rotate.y) {
        int random = HSD_Randi(sv_param->x24);
        v->timer = random - sv_param->x20;
    }
    if (v->anim_timer > 0 && --v->anim_timer == 0) {
        grAnime_801C8138(g, sv_ground(g)->map_id, 0);
    }
    angle = sv_approach(v->angle, v->target, sv_param->x1C);
    v->angle = angle;
    if (j->rotate.y != angle) {
        j->rotate.y = angle;
        HSD_JObjSetMtxDirtySub(j);
    }
}
/* +36AC DarkenModel: diffuse r, g, b each lose `amount` when above it, over the whole tree. */
static void sv_darken(HSD_JObj* j, int amount)
{
    for (; j != NULL; j = j->next) {
        for (HSD_DObj* d = j->u.dobj; d != NULL; d = d->next) {
            HSD_Material* m = d->mobj->mat;
            if (m->diffuse.r > amount) m->diffuse.r -= amount;
            if (m->diffuse.g > amount) m->diffuse.g -= amount;
            if (m->diffuse.b > amount) m->diffuse.b -= amount;
        }
        if (j->child != NULL) sv_darken(j->child, amount);
    }
}
static const char* sv_joint_name(HSD_JObj* j)
{
    return DP(((HSD_Joint*) (mu_uptr) j->id)->class_name);
}
/* +376C SaturnSimple_Generate: one Mr. Saturn on a path joint; a "water" child gets the ripple
 * effect, a "cave" child darkens the new model. */
static void sv_simple_generate(HSD_JObj* parent)
{
    HSD_GObj* g = mu_grTSeak_80223908(7);
    HSD_JObj* model = Ground_801C3FA4(g, 0);
    lb_8000C1C0(model, parent);
    for (HSD_JObj* c = parent->child; c != NULL; c = c->next) {
        const char* name = sv_joint_name(c);
        if (name == NULL) continue;
        if (strcmp(name, "water") == 0) grLib_801C97DC(30004, 30, c);
        if (strcmp(sv_joint_name(c), "cave") == 0) sv_darken(model, 100);
    }
}
/* +3880 SaturnPath_OnCreation. Console details kept: the FighterKind test is 11 (Nana; Ness is
 * external id 11), the name test is "Lucas"; the first fighter that matches raises the chance. */
static void sv_path_create(HSD_GObj* g)
{
    HSD_JObj* node = Ground_801C3FA4(g, 0)->child;
    float chance = sv_param->x10;
    for (int slot = 0; slot < 6; ++slot) {
        HSD_GObj* f;
        const char* name;
        if (Player_GetPlayerSlotType(slot) == 3) continue;
        f = Player_GetEntity(slot);
        if (f == NULL) continue;
        if (((Fighter*) HSD_GObjGetUserData(f))->kind == 11) {
            chance = sv_param->x14;
            break;
        }
        name = mu_gr_mex_fighter_name(Player_GetPlayerCharacter(slot));
        if (name != NULL && strcmp("Lucas", name) == 0) {
            chance = sv_param->x14;
            break;
        }
    }
    for (; node != NULL; node = node->next) {
        if (HSD_Randf() <= chance) sv_simple_generate(node);
    }
}

/* ---- the statues (map gobj 5, two of them, created by the floor) ---- */

void sv_statue_state(HSD_GObj* g, int state);
/* +2634 Statue_OnHurt, the grMaterial hurt callback (damage in f1, Ground in r4). */
static void sv_statue_hurt(Item_GObj* item, Ground* gp, Vec3* pos, HSD_GObj* source, f32 damage)
{
    SvStatueVars* v = (SvStatueVars*) (void*) &gp->u;
    SvFloorVars* floor;
    (void) item; (void) pos; (void) source;
    v->hp = (int) ((float) v->hp - damage);
    if (v->hp > 0) {
        Ground* self = sv_ground(v->self);
        ((SvStatueVars*) (void*) &self->u)->state = 1;
        grAnime_801C8138(v->self, self->map_id, 1);
        return;
    }
    floor = SV_VARS(SvFloorVars, Ground_GetMapGObj(3));
    if (v->self == floor->statue[0]) {
        sv_statue_state(floor->statue[0], 2);
        sv_statue_state(floor->statue[1], 3);
    } else {
        sv_statue_state(floor->statue[0], 3);
        sv_statue_state(floor->statue[1], 2);
    }
}
/* +2508 Statue_StateChange: 0 standing (hit points, hurtbox on, collision on, the glow effect),
 * 2 and 3 broken (sound 9, intangible, collision off, a dust effect, the respawn timer). */
void sv_statue_state(HSD_GObj* g, int state)
{
    SvStatueVars* v = SV_VARS(SvStatueVars, g);
    v->state = state;
    grAnime_801C8138(g, sv_ground(g)->map_id, state);
    if (state == 0) {
        v->hp = sv_param->x08;
        it_80271508(v->hurt, HurtCapsule_Enabled);
        mpJointListAdd(v->coll_joint);
        hsd_8039EFAC(0, 30, 30003, Ground_801C3FA4(g, 1));
    } else if ((unsigned) (state - 2) <= 1) {
        Vec3 pos;
        sv_sound(9, 0x80);
        it_80271508(v->hurt, HurtCapsule_Intangible);
        mpLib_80057BC0(v->coll_joint);
        lb_8000B1CC(Ground_801C3FA4(g, 0), NULL, &pos);
        pos.y += 5.0f;
        grLib_801C96F8(0x1F1, 0, &pos);
        v->timer = sv_param->x0C;
    }
}
/* +2728 Statue_OnCreation. It runs inside grTSeak_80223908(5), before the floor stores the
 * statue's collision joint, so its state 0 adds collision joint 0 once (kept). */
static void sv_statue_create(HSD_GObj* g)
{
    Ground* gp = sv_ground(g);
    SvStatueVars* v = SV_VARS(SvStatueVars, g);
    gp->x11_flags.b012 = 3;
    v->hurt = grMaterial_801C8CFC(0, 0, gp, Ground_801C3FA4(g, 0), NULL, sv_statue_hurt, NULL);
    grMaterial_801C8DE0(v->hurt, 0.0f, 0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 10.0f);
    grMaterial_801C8E08(v->hurt);
    v->self = g;
    sv_statue_state(g, 0);
}
/* +27D8 Statue_OnFrame. */
static void sv_statue_frame(HSD_GObj* g)
{
    SvStatueVars* v = SV_VARS(SvStatueVars, g);
    if ((unsigned) (v->state - 2) <= 1) {
        if (v->timer > 0) {
            --v->timer;
        } else {
            sv_statue_state(g, 0);
            sv_sound(8, 0x80);
        }
    }
    mpLib_80055E9C(v->coll_joint);
}

/* ---- the runner (map gobj 6) ---- */

/* +285C Runner_StateChange. State 6 (vanish, from the coffee break) keeps the animation and adds
 * material animation 6 instead, unless the runner is in state 0. */
void sv_runner_state(HSD_GObj* g, int state)
{
    Ground* gp = sv_ground(g);
    SvRunnerVars* v = SV_VARS(SvRunnerVars, g);
    SvRunnerState* e = &DP(sv_param->x04)[state];
    if (state != 6) {
        HSD_JObjClearFlagsAll(GET_JOBJ(g), JOBJ_HIDDEN);
        grAnime_801C8138(g, gp->map_id, e->anim);
    } else if (v->state != 0) {
        HSD_JObj* j = Ground_801C3FA4(g, 0);
        UnkArchiveStruct* file = grDatFiles_801C6330(gp->map_id);
        HSD_MatAnimJoint* mat = DP(DP(DP(file->unk4->unk8)[gp->map_id].unk8)[6]);
        HSD_JObjRemoveAnimByFlags(j, 4);
        HSD_JObjAddAnimAll(j, NULL, mat, NULL);
        HSD_JObjReqAnimAllByFlags(j, 4, 0.0f);
    }
    if (e->collide) mpJointListAdd(3);
    else mpLib_80057BC0(3);
    v->timer = e->timer_min + HSD_Randi(e->timer_max - e->timer_min);
    v->state = state;
    v->next = e->next;
    v->quake_timer = 0;
    v->mode = e->mode;
    v->subject->force_inactive = e->camera == 0;
    if (e->sfx != -1) sv_sound(e->sfx, 0xFF);
}
/* +2A10 Runner_OnCreation: a camera subject (off until a state turns it on), collision, state 0,
 * and the stage shadow on joint depth 3. */
static void sv_runner_create(HSD_GObj* g)
{
    Ground* gp = sv_ground(g);
    SvRunnerVars* v = SV_VARS(SvRunnerVars, g);
    CmSubject* subject;
    gp->x11_flags.b012 = 3;
    gp->x10_flags.b2 = 0;
    subject = v->subject = Camera_80029020();
    subject->force_inactive = 1;
    subject->target_ext.v.x = 40.0f;
    subject->target_ext.h.x = -20.0f;
    subject->target_ext.v.y = 20.0f;
    subject->target_ext.h.y = 20.0f;
    Ground_InitMapColl(GET_JOBJ(g), gp->map_id);
    sv_runner_state(g, 0);
    sv_shadow_create(GET_JOBJ(g), 3, 0x100, 30.0f);
}
static void sv_runner_dust(HSD_GObj* g)
{
    Vec3 pos;
    lb_8000B1CC(Ground_801C3FA4(g, 3), NULL, &pos);
    grLib_801C96F8(0x204, 0, &pos);
    lb_8000B1CC(Ground_801C3FA4(g, 4), NULL, &pos);
    grLib_801C96F8(0x204, 0, &pos);
}
/* +2AC4 Runner_OnFrame: modes 1 (one stomp at the animation's last frame), 2 (a stomp every 7
 * frames), 3 (one big quake). */
static void sv_runner_frame(HSD_GObj* g)
{
    SvRunnerVars* v = SV_VARS(SvRunnerVars, g);
    if (!v->subject->force_inactive) {
        lb_8000B1CC(Ground_801C3FA4(g, 1), NULL, &v->subject->pos);
    }
    if (v->timer > 0) --v->timer;
    else if (v->next != -1) sv_runner_state(g, v->next);
    switch (v->mode) {
    case 2:
        if (v->quake_timer > 0) {
            --v->quake_timer;
        } else {
            v->quake_timer = 6;
            Camera_RequestQuake((CmQuakeKind) 1, NULL);
            sv_runner_dust(g);
        }
        break;
    case 3:
        if (v->quake_timer == 0) {
            Vec3 zero = { 0.0f, 0.0f, 0.0f };
            Camera_RequestQuake((CmQuakeKind) 3, NULL);
            grLib_801C96F8(0xF3, 0, &zero);
            v->quake_timer = 1;
        }
        break;
    case 1: {
        float frame = lbGetJObjCurrFrame(GET_JOBJ(g));
        float last = lbGetJObjEndFrame(GET_JOBJ(g)) - 1.0f;
        if (frame == last) {
            Camera_RequestQuake((CmQuakeKind) 3, NULL);
            sv_sound(4, 0xFF);
            sv_runner_dust(g);
        }
        break;
    }
    }
    Ground_UpdateMapColl(g);
}

/* ---- the floor (map gobj 3): statues, the two warp areas, the coffee gate ---- */

/* +1D00 WarpArea_ContainsCollData: any ECB point (bottom, top, left, right) inside the area. */
static bool sv_area_has_ecb(const SvWarpArea* a, CollData* cd)
{
    Vec2 points[4] = { cd->ecb.bottom, cd->ecb.top, cd->ecb.left, cd->ecb.right };
    for (int i = 0; i < 4; ++i) {
        float x = cd->cur_pos.x + points[i].x;
        float y = cd->cur_pos.y + points[i].y;
        if (x >= a->min.x && y >= a->min.y && x <= a->max.x && y <= a->max.y) return true;
    }
    return false;
}
/* +1DE0 WarpArea_OnWarpFighter: the three x20B0 points follow the fighter. */
static void sv_warp_shift(HSD_GObj* fighter, const Vec3* d)
{
    Fighter* fp = HSD_GObjGetUserData(fighter);
    for (int i = 0; i < 3; ++i) {
        fp->x20B0[i].x0.x += d->x;
        fp->x20B0[i].x0.y += d->y;
        fp->x20B0[i].x0.z += d->z;
    }
}
/* +1E68 WarpArea_CheckWarp: a fighter touching `from` moves to `to` (x set, y kept relative to
 * the destinations), a grounded one onto the floor under it; every position copy, the top joint,
 * the collision state and its coll_cb, sound 11, and a slot in the floor's warped list. */
static bool sv_warp(HSD_GObj* floor, SvWarpArea* from, SvWarpArea* to, HSD_GObj* fighter)
{
    Fighter* fp = HSD_GObjGetUserData(fighter);
    SvFloorVars* v = SV_VARS(SvFloorVars, floor);
    Vec3 old, delta;
    float y;
    if (!sv_area_has_ecb(from, &fp->coll_data)) return false;
    old = fp->cur_pos;
    y = fp->cur_pos.y - from->dest.y;
    y = y + to->dest.y;
    fp->cur_pos.y = y;
    fp->cur_pos.x = to->dest.x;
    if (fp->ground_or_air == GA_Ground) {
        float scale = Ground_801C0498();
        float top = mu_fmadds(scale, 2.0f, to->dest.y);
        float bottom = mu_fmadds(-scale, 10.0f, top);
        Vec3 pos, normal;
        int line;
        u32 flags;
        if (mpCheckFloor(to->dest.x, top, to->dest.x, bottom, 0.0f, &pos, &line, &flags, &normal,
                         -1, -1, -1, NULL, NULL) == 1)
        {
            fp->cur_pos = pos;
            fp->coll_data.floor.index = line;
        }
    }
    delta.x = fp->cur_pos.x - old.x;
    delta.y = fp->cur_pos.y - old.y;
    delta.z = fp->cur_pos.z - old.z;
    fp->prev_pos = fp->cur_pos;
    fp->coll_data.last_pos = fp->cur_pos;
    fp->coll_data.x28_vec = fp->cur_pos;
    fp->coll_data.prev_pos = fp->cur_pos;
    fp->parts[ftParts_GetBoneIndex(fp, 0)].joint->translate = fp->cur_pos;
    HSD_JObjSetMtxDirtySub(GET_JOBJ(fighter));
    ftColl_8007AE80(fighter);
    fp->coll_data.cur_pos = fp->cur_pos;
    mpCollPrev(&fp->coll_data);
    if (fp->coll_cb != NULL) fp->coll_cb(fighter);
    sv_warp_shift(fighter, &delta);
    mu_gr_mex_sfx(11);
    for (int i = 0; i < 6; ++i) {
        if (v->warped[i] == NULL) {
            v->warped[i] = fighter;
            break;
        }
    }
    return true;
}
/* +013C Floor_ProcSkipInterpolation (priority 11): items owned by a fighter warped this frame
 * skip their interpolation; then the list is cleared. */
static void sv_floor_skip_interpolation(HSD_GObj* g)
{
    SvFloorVars* v = SV_VARS(SvFloorVars, g);
    for (HSD_GObj* item = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_ITEM]; item != NULL; item = item->next) {
        HSD_GObj* owner = ((Item*) HSD_GObjGetUserData(item))->owner;
        if (owner == NULL) continue;
        for (int i = 0; i < 6; ++i) {
            if (owner == v->warped[i]) {
                it_802725D4(item);
                break;
            }
        }
    }
    for (int i = 0; i < 6; ++i) v->warped[i] = NULL;
}
/* +2290 Floor_CreateStatue. */
static HSD_GObj* sv_floor_statue(HSD_JObj* parent, int joint_id)
{
    HSD_GObj* g = mu_grTSeak_80223908(5);
    lb_8000C2F8(Ground_801C3FA4(g, 0), parent);
    SV_VARS(SvStatueVars, g)->coll_joint = joint_id;
    mpLib_800552B0(joint_id, GET_JOBJ(g), 1);
    mpLib_80055E9C(joint_id);
    mpLib_80057424(joint_id);
    return g;
}
/* +2340 Floor_OnCreation. */
static void sv_floor_create(HSD_GObj* g)
{
    Ground* gp = sv_ground(g);
    SvFloorVars* v = SV_VARS(SvFloorVars, g);
    gp->x10_flags.b5 = 1;
    grAnime_801C8138(g, gp->map_id, 0);
    v->statue[0] = sv_floor_statue(Ground_801C3FA4(g, 1), 1);
    v->statue[1] = sv_floor_statue(Ground_801C3FA4(g, 2), 2);
    lb_8000B1CC(Ground_801C3FA4(g, 3), NULL, &v->area[0].min);
    lb_8000B1CC(Ground_801C3FA4(g, 4), NULL, &v->area[0].max);
    lb_8000B1CC(Ground_801C3FA4(g, 7), NULL, &v->area[0].dest);
    lb_8000B1CC(Ground_801C3FA4(g, 5), NULL, &v->area[1].min);
    lb_8000B1CC(Ground_801C3FA4(g, 6), NULL, &v->area[1].max);
    lb_8000B1CC(Ground_801C3FA4(g, 8), NULL, &v->area[1].dest);
    HSD_GObj_SetupProc(g, sv_floor_skip_interpolation, 11);
    grLib_801C97DC(30000, 30, Ground_801C3FA4(g, 9));
    grLib_801C97DC(30001, 30, Ground_801C3FA4(g, 10));
    grLib_801C97DC(30001, 30, Ground_801C3FA4(g, 11));
    v->gate.started = 0;
    v->gate.ftcmd = HSD_ArchiveGetPublicAddress(grDatFiles_GetArchive()->unk0, "ftcmd");
    if (v->gate.ftcmd == NULL) __assert("saturn.c", 0x181, "could not find ftcmd");
}
/* +21B8 Floor_OnFrame: each fighter (both of a pair) warps A to B, else B to A; then the coffee
 * gate. */
bool sv_floor_warp(HSD_GObj* floor, HSD_GObj* fighter)
{
    SvFloorVars* v = SV_VARS(SvFloorVars, floor);
    if (sv_warp(floor, &v->area[0], &v->area[1], fighter)) return true;
    return sv_warp(floor, &v->area[1], &v->area[0], fighter);
}
static void sv_floor_frame(HSD_GObj* g)
{
    SvFloorVars* v = SV_VARS(SvFloorVars, g);
    for (int slot = 0; slot < 6; ++slot) {
        for (int index = 0; index < 2; ++index) {
            HSD_GObj* fighter = Player_GetEntityAtIndex(slot, index);
            if (fighter != NULL) sv_floor_warp(g, fighter);
        }
    }
    sv_coffee_check(&v->gate);
}

/* +41D4 OnInit. */
static void sv_init(void)
{
    HSD_SisSlot* sis;
    sv_param = Ground_GetYakumonoParam();
    if (sv_param == NULL) __assert("saturn.c", 0x70C, "yakumono node was not found in dat");
    sv_shadow_install();
    mu_grTSeak_80223908(0);
    mu_grTSeak_80223908(1);
    mu_grTSeak_80223908(2);
    mu_grTSeak_80223908(3);
    mu_grTSeak_80223908(4);
    mu_grTSeak_80223908(8);
    mu_grTSeak_80223908(6);
    sis = HSD_ArchiveGetPublicAddress(grDatFiles_GetArchive()->unk0, "SIS_GrKouhi");
    if (sis == NULL) __assert("saturn.c", 0x720, "could not find SIS_GrKouhi");
    HSD_SisLib_804D1124[1] = sis;
    Ground_801C39C0();
    Ground_801C3BB4();
}

#define SV_CB(create, frame, bits) { create, sv_unk, frame, sv_delete, { bits } }
static StageCallbacks sv_maps[] = {
    SV_CB(sv_anim0, mu_gr_gobj_nop, 0),                     /* 0 Points */
    SV_CB(sv_sky_create, sv_sky_frame, 0),                  /* 1 Sky */
    SV_CB(sv_sky_create, mu_gr_gobj_nop, 0xC0000000),       /* 2 Clouds */
    SV_CB(sv_floor_create, sv_floor_frame, 0),              /* 3 Floor */
    SV_CB(sv_floor_shadow_create, mu_gr_gobj_nop, 0),       /* 4 FloorShadow */
    SV_CB(sv_statue_create, sv_statue_frame, 0),            /* 5 Statue */
    SV_CB(sv_runner_create, sv_runner_frame, 0),            /* 6 Runner */
    SV_CB(sv_simple_create, sv_simple_frame, 0),            /* 7 SaturnSimple */
    SV_CB(sv_path_create, mu_gr_gobj_nop, 0),               /* 8 SaturnPath */
    SV_CB(sv_coffee_create, sv_coffee_frame, 0),            /* 9 Coffee */
};
const MuGrStage mu_gr_saturnvalley = {
    .name = "Saturn Valley", .file = "/GrSv.dat", .map_gobjs = sv_maps,
    .on_init = sv_init, .on_demo_init = mu_gr_demo_nop,
    .on_load = mu_gr_event_nop, .on_start = mu_gr_event_nop,
    .callback4 = mu_gr_false, .on_touch_line = mu_gr_no_touch_line,
    .on_check_shadow_render = sv_shadow_check, .flags = MU_GR_READY,
};
