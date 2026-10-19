/* Mushroom Kingdom 64 (Akaneia 1.0.1 and ACE 2.0.0, the same file: internal 75, external 292),
 * the m-ex grFunction block of GrOMk.dat (1467 instructions with symbols) and its two itFunction
 * blocks (stage item 0, the Piranha Plants, 612 instructions; stage item 1, the POW block, 92).
 * Disassembly: `py -3.12 grdis.py ak GrOMk.dat` (out_dis_GrOMk.txt) and `... it 0`, `... it 1` in
 * run-source/rel09-stages.
 *
 * Exports 1, 3 and 5; the other slots keep the MxDt row's words (Dream Land 64's). Four map gobjs:
 * the scenery (Sheik's row 0), the background, the stage and the two balance lifts.
 *
 * The stage: three warp pipes (a fighter crouching on the top pipe's floor, holding up into the
 * ceiling pipe's line, or holding toward a side pipe's wall goes in, shrinks into it, comes out of
 * one of the other two at random and lands), two Piranha Plants that come out of their pipes when
 * no fighter is near, and a POW block every 600 frames at one of five stage points while none is
 * out (three hits shake the screen; the third destroys it). The lifts: standing weight moves one
 * down and the other up; at the bottom they fall away, blink and come back.
 *
 * Calls of `__init_registers+18` (lift state 3, item 0 onDestroy and FlyPhys) are MexTK's bp(): a
 * blr, nothing natively. The stage's frame switch jumps into a second copy of the function at
 * +ED8; the native switch is the same. */
#include "../peachcastle64/wp1_common.h"

#include <math.h>
#include <melee/cm/camera.h>
#include <melee/ef/eflib.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftdynamics.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_Throw.h>
#include <melee/ft/types.h>
#include <melee/gr/granime.h>
#include <melee/gr/ground.h>
#include <melee/gr/stage.h>
#include <melee/gr/types.h>
#include <melee/it/inlines.h>
#include <melee/it/it_2725.h>
#include <melee/it/item.h>
#include <melee/it/itzako.h>
#include <melee/it/kinds/types.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/mp/mpcoll.h>
#include <melee/mp/mplib.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/random.h>

/* ---- shared math (Math_Vec2Distance, Math_Vec3Distance, Math_Vec2Angle) ---- */

/* The squares and their sum in double, rounded once, then sqrtf. */
static float mexgr_mk_dist2(const Vec3* a, const Vec3* b)
{
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    return sqrtf((float) ((double) dx * dx + (double) dy * dy));
}

static float mexgr_mk_dist3(const Vec3* a, const Vec3* b)
{
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    float dz = a->z - b->z;
    return sqrtf((float) ((double) dx * dx + (double) dy * dy + (double) dz * dz));
}

/* The angle from a to b. */
static float mexgr_mk_angle2(const Vec3* a, const Vec3* b)
{
    return atan2f(b->y - a->y, b->x - a->x);
}

static Vec3 mexgr_mk_joint_pos(HSD_JObj* jobj)
{
    Vec3 pos;
    lb_8000B1CC(jobj, NULL, &pos);
    return pos;
}

/* ---- the stage's state (map gobj 2, console gp+E0 .. gp+13C) ---- */

typedef struct MexgrMkPipe {
    int line;         /* its collision line */
    int kind;         /* that line's kind: 1 floor, 2 ceiling, 4 and 8 walls */
    HSD_JObj* inner;  /* console +8: the pipe's entrance, where a fighter is pulled to first */
    HSD_JObj* mouth;  /* console +C: the far point inside; an exit starts the fighter here */
} MexgrMkPipe;

typedef struct MexgrMkStage {
    int state;            /* gp+E0: 0 wait .. 5 cooldown */
    HSD_GObj* fighter;    /* gp+E4: the fighter in the pipes */
    float vx, vy;         /* gp+E8, gp+EC */
    int timer;            /* gp+F0 */
    MexgrMkPipe pipes[3]; /* gp+F4 */
    int pipe_in;          /* gp+124 */
    int pipe_out;         /* gp+128 */
    Item_GObj* pow;       /* gp+12C: the POW out, NULL none (the POW clears it on destroy) */
    int pow_timer;        /* gp+130 */
    Item_GObj* plants[2]; /* gp+134, gp+138 */
} MexgrMkStage;

typedef struct MexgrMkStageState {
    u8 x0_pad[0x1C];
    MexgrMkStage s;
} MexgrMkStageState;
_Static_assert(sizeof(MexgrMkStageState) <= sizeof(((Ground*) 0)->u), "stage state fits");

static MexgrMkStage* mexgr_mk_stage(Ground_GObj* gobj)
{
    return &((MexgrMkStageState*) &((Ground*) HSD_GObjGetUserData(gobj))->u)->s;
}

/* .rodata.pipe_desc (+804): line, joint of `mouth`, joint of `inner` (the fourth word unused). */
static const int mexgr_mk_pipe_desc[3][4] = {
    { 0xC, 7, 6, 0x28 },
    { 0xE, 9, 8, 0x28 },
    { 0x14, 0xB, 0xA, 0x14 },
};

/* .data.pakkun_desc (+864): scale, joint of the plant's pipe bottom, joint of its top. */
typedef struct MexgrMkPlantDesc {
    float scale;
    int bottom, top;
} MexgrMkPlantDesc;
static const MexgrMkPlantDesc mexgr_mk_plant_desc[2] = { { 1.0F, 7, 6 }, { 1.0F, 9, 8 } };

/* .rodata.pow_desc (+16DC): the stage points a POW appears at. */
static const int mexgr_mk_pow_points[5] = { 0x20, 0x21, 0x22, 0x23, 0x24 };

/* ---- stage item 0: the Piranha Plant (its variables, used by the stage too) ---- */

typedef struct MexitMkPlant {
    u8 x0_pad[0x20];
    /* +20 ip+DF4 */ int timer;
    /* +24 ip+DF8 */ int hp;
    /* +28 ip+DFC */ float fall;
    /* +2C ip+E00 */ float xE00;
    /* +30 ip+E04 */ float kb_x;
    /* +34 ip+E08 */ float kb_y;
    /* +38 ip+E0C */ float home_x, home_y;  /* down in its pipe */
    /* +40 ip+E14 */ float top_x, top_y;    /* out */
} MexitMkPlant;
_Static_assert(sizeof(MexitMkPlant) <= sizeof(((Item*) 0)->xDD4_itemVar), "plant vars fit");

static MexitMkPlant* mexit_mk_plant(Item* ip) { return (MexitMkPlant*) &ip->xDD4_itemVar; }

/* stateWait (+4BC; the stage block's copy at +178C is the same): 300 frames down, unhittable. */
static void mexit_mk_plant_wait(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
    mexit_mk_plant(ip)->timer = 300;
    ip->x40_vel.x = 0.0F;
    ip->x40_vel.y = 0.0F;
    ip->xACC_itemHurtbox[0].state = (HurtCapsuleState) 2;
    ip->xDAA_flag.b7 = 1;
}

/* ---- the stage ---- */

/* Pipe_WaitStart (+834) */
static void mexgr_mk_pipe_wait_start(Ground_GObj* gobj)
{
    MexgrMkStage* s = mexgr_mk_stage(gobj);
    s->fighter = NULL;
    s->pipe_in = -1;
    s->pipe_out = -1;
    s->vx = 0.0F;
    s->vy = 0.0F;
    s->state = 0;
}

/* Pipe_Check (+FC4): the first fighter, in list order, that meets a pipe's condition on that
 * pipe's line (by the line's kind: crouching (motion 0x28) with the stick down 0.6 on the floor,
 * motion 0xFA with the stick up 0.6 under the ceiling, the stick 0.6 into a wall it touches). A
 * fighter it holds is let go (ftCommon_8007DB58, ftCo_800DCFD4, ftCo_800DC920). */
static bool mexgr_mk_pipe_check(Ground_GObj* gobj)
{
    MexgrMkStage* s = mexgr_mk_stage(gobj);
    HSD_GObj* fgobj;
    for (fgobj = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; fgobj != NULL; fgobj = fgobj->next) {
        Fighter* fp = HSD_GObjGetUserData(fgobj);
        u32 env = fp->coll_data.env_flags;
        int i;
        for (i = 0; i < 3; i++) {
            const MexgrMkPipe* p = &s->pipes[i];
            bool hit = false;
            switch (p->kind) {
            case 1:
                hit = (env & 0x18000) && fp->coll_data.floor.index == p->line &&
                      fp->motion_id == 0x28 && (double) fp->input.lstick[0].y <= -0x1.3333333333333p-1;
                break;
            case 2:
                hit = (env & 0x6000) && fp->coll_data.ceiling.index == p->line &&
                      fp->motion_id == 0xFA && (double) fp->input.lstick[0].y >= 0x1.3333333333333p-1;
                break;
            case 4:
                hit = (env & 0xFC0) && fp->coll_data.right_facing_wall.index == p->line &&
                      (double) fp->input.lstick[0].x <= -0x1.3333333333333p-1;
                break;
            case 8:
                hit = (env & 0x3F) && fp->coll_data.left_facing_wall.index == p->line &&
                      (double) fp->input.lstick[0].x >= 0x1.3333333333333p-1;
                break;
            default:
                break;
            }
            if (!hit) {
                continue;
            }
            if (fp->x1A5C != NULL) {
                Fighter_GObj* held = fp->x1A5C;
                ftCommon_8007DB58(held);
                ftCo_800DCFD4(held);
                ftCo_800DC920(fgobj, held);
            }
            s->pipe_in = i;
            s->fighter = fgobj;
            return true;
        }
    }
    return false;
}

/* The fighter steps by the stage's velocity. */
static void mexgr_mk_fighter_step(MexgrMkStage* s, Fighter* fp)
{
    fp->cur_pos.x = fp->cur_pos.x + s->vx;
    fp->cur_pos.y = fp->cur_pos.y + s->vy;
}

/* Velocity covering `d` along `angle` in `frames` (fmuls, then fdivs). */
static void mexgr_mk_set_vel(MexgrMkStage* s, float angle, float d, float frames)
{
    s->vx = cosf(angle) * d / frames;
    s->vy = sinf(angle) * d / frames;
}

/* Pipe_CorrectStart (+11A4): 5 frames to the pipe's inner point; the fighter goes still, its
 * effects go, its physics and collision callbacks are cleared; the plants go down. */
static void mexgr_mk_pipe_correct_start(Ground_GObj* gobj)
{
    MexgrMkStage* s = mexgr_mk_stage(gobj);
    Fighter_GObj* fg = s->fighter;
    Fighter* fp = GET_FIGHTER(fg);
    Vec3 target = mexgr_mk_joint_pos(s->pipes[s->pipe_in].inner);
    float d = mexgr_mk_dist2(&fp->cur_pos, &target);
    float a = mexgr_mk_angle2(&fp->cur_pos, &target);
    int i;
    mexgr_mk_set_vel(s, a, d, 5.0F);
    s->timer = 5;
    ftColl_8007B760(fg, 1);
    fp->x2219_b5 = 1;
    efLib_DestroyAll(fg);
    ftCommon_8007E2FC(fg);
    fp->x68C_transNPos.x = 0.0F;
    fp->x68C_transNPos.y = 0.0F;
    fp->x68C_transNPos.z = 0.0F;
    ftCommon_8007DB58(fg);
    fp->coll_cb = NULL;
    fp->phys_cb = NULL;
    ftCommon_8007D5D4(fp);
    for (i = 0; i < 2; i++) {
        Item* pip = GET_ITEM(s->plants[i]);
        pip->pos.x = mexit_mk_plant(pip)->home_x;
        pip->pos.y = mexit_mk_plant(pip)->home_y;
        mexit_mk_plant_wait(s->plants[i]);
    }
    s->state = 1;
}

/* Pipe_InStart (+1314): 35 frames from the mouth to the inner point, with sound 0. */
static void mexgr_mk_pipe_in_start(Ground_GObj* gobj)
{
    MexgrMkStage* s = mexgr_mk_stage(gobj);
    const MexgrMkPipe* p = &s->pipes[s->pipe_in];
    Vec3 mouth = mexgr_mk_joint_pos(p->mouth);
    Vec3 inner = mexgr_mk_joint_pos(p->inner);
    float d = mexgr_mk_dist2(&inner, &mouth);
    float a = mexgr_mk_angle2(&inner, &mouth);
    mexgr_mk_set_vel(s, a, d, 35.0F);
    ftColl_8007B760(s->fighter, 1);
    mu_gr_mex_sfx(0);
    s->timer = 35;
    s->state = 2;
}

/* The shrink step: the trophy scale * 0.02 per stage scale unit. */
static float mexgr_mk_shrink(Fighter* fp)
{
    float step = fp->co_attrs.trophy_scale * 0.02F;
    return 1.0F / Ground_801C0498() * step;
}

/* Pipe_DelayStart (+1410): the exit pipe (40% the first, 40% the second, 20% the third, never the
 * way in); the fighter appears at its mouth, faces its way, waits hidden 30 frames. */
static void mexgr_mk_pipe_delay_start(Ground_GObj* gobj)
{
    MexgrMkStage* s = mexgr_mk_stage(gobj);
    Fighter_GObj* fg = s->fighter;
    Fighter* fp = GET_FIGHTER(fg);
    const MexgrMkPipe* p;
    Vec3 mouth, inner;
    float d, a;
    int out;
    for (;;) {
        int r = HSD_Randi(100);
        out = r <= 39 ? 0 : r <= 79 ? 1 : 2;
        if (out != s->pipe_in) {
            break;
        }
    }
    s->pipe_out = out;
    p = &s->pipes[out];
    mouth = mexgr_mk_joint_pos(p->mouth);
    inner = mexgr_mk_joint_pos(p->inner);
    fp->cur_pos.x = mouth.x;
    fp->cur_pos.y = mouth.y;
    d = mexgr_mk_dist2(&mouth, &inner);
    a = mexgr_mk_angle2(&mouth, &inner);
    mexgr_mk_set_vel(s, a, d, 40.0F);
    if ((double) s->vx > 0x1.47ae147ae147bp-7) {
        fp->facing_dir = 1.0F;
    } else if ((double) s->vx < -0x1.47ae147ae147bp-7) {
        fp->facing_dir = -1.0F;
    }
    Fighter_ChangeMotionState(fg, 0xE, 0, 0.0F, 1.0F, -1.0F, NULL);
    fp->invisible = 1;
    ftColl_8007B760(fg, 1);
    fp->coll_cb = NULL;
    fp->phys_cb = NULL;
    s->timer = 30;
    s->state = 3;
}

/* Pipe_ExitStart (+15FC): shrunk to 35 steps (at least 0.1), sound 0, 40 frames out. */
static void mexgr_mk_pipe_exit_start(Ground_GObj* gobj)
{
    MexgrMkStage* s = mexgr_mk_stage(gobj);
    Fighter_GObj* fg = s->fighter;
    float scale;
    ftColl_8007B760(fg, 1);
    scale = 1.0F - mexgr_mk_shrink(GET_FIGHTER(fg)) * 35.0F;
    if ((double) scale < 0x1.999999999999ap-4) {
        scale = 0.1F;
    }
    ftLib_SetScale(fg, scale);
    mu_gr_mex_sfx(0);
    s->timer = 40;
    s->state = 4;
}

/* Pipe_CooldownStart (+16C4) */
static void mexgr_mk_pipe_cooldown_start(Ground_GObj* gobj)
{
    MexgrMkStage* s = mexgr_mk_stage(gobj);
    s->timer = 60;
    s->state = 5;
}

/* Pipe_WaitThink (+980) */
static void mexgr_mk_pipe_wait_think(Ground_GObj* gobj)
{
    if (mexgr_mk_pipe_check(gobj)) {
        mexgr_mk_pipe_correct_start(gobj);
    }
}

/* Pipe_CorrectThink (+9C4) */
static void mexgr_mk_pipe_correct_think(Ground_GObj* gobj)
{
    MexgrMkStage* s = mexgr_mk_stage(gobj);
    mexgr_mk_fighter_step(s, GET_FIGHTER(s->fighter));
    ftColl_8007B760(s->fighter, 1);
    if (--s->timer <= 0) {
        mexgr_mk_pipe_in_start(gobj);
    }
}

/* Pipe_InThink (+A4C): into the pipe, shrinking; hidden at 17 frames left. */
static void mexgr_mk_pipe_in_think(Ground_GObj* gobj)
{
    MexgrMkStage* s = mexgr_mk_stage(gobj);
    Fighter_GObj* fg = s->fighter;
    Fighter* fp = GET_FIGHTER(fg);
    float scale;
    mexgr_mk_fighter_step(s, fp);
    ftColl_8007B760(fg, 1);
    scale = fp->x34_scale.y - mexgr_mk_shrink(fp);
    if (scale > 0.0F) {
        ftLib_SetScale(fg, scale);
    }
    if (s->timer == 17) {
        fp->invisible = 1;
    }
    if (--s->timer <= 0) {
        mexgr_mk_pipe_delay_start(gobj);
    }
}

/* Pipe_DelayThink (+B5C) */
static void mexgr_mk_pipe_delay_think(Ground_GObj* gobj)
{
    MexgrMkStage* s = mexgr_mk_stage(gobj);
    if (--s->timer > 0) {
        ftColl_8007B760(s->fighter, 1);
        return;
    }
    mexgr_mk_pipe_exit_start(gobj);
}

/* Pipe_ExitThink (+BAC): out of the mouth, growing back from 35 frames left, shown at 20; at the
 * end full size, on the floor under it (within 10 below, from 2 above) or falling. */
static void mexgr_mk_pipe_exit_think(Ground_GObj* gobj)
{
    MexgrMkStage* s = mexgr_mk_stage(gobj);
    Fighter_GObj* fg = s->fighter;
    Fighter* fp = GET_FIGHTER(fg);
    ftCo_8009E0A8(fg);
    mexgr_mk_fighter_step(s, fp);
    if (s->timer <= 35) {
        float scale = mexgr_mk_shrink(fp) + fp->x34_scale.y;
        if (scale < 1.0F) {
            ftLib_SetScale(fg, scale);
        }
    }
    if (s->timer == 20) {
        fp->invisible = 0;
    }
    if (--s->timer > 0) {
        ftColl_8007B760(fg, 1);
        return;
    }
    {
        float g, top, bottom;
        Vec3 at, normal;
        int line;
        u32 flags;
        ftLib_SetScale(fg, 1.0F);
        fp->x2219_b5 = 0;
        g = Ground_801C0498();
        top = (g + g) + fp->cur_pos.y;
        bottom = top - g * 10.0F;
        if (mpCheckFloor(fp->cur_pos.x, top, fp->cur_pos.x, bottom, 0.0F, &at, &line, &flags,
                         &normal, -1, -1, -1, NULL, NULL) == 1)
        {
            fp->cur_pos = at;
            fp->coll_data.floor.index = line;
            ft_8008A2BC(fg);
        } else {
            ftCo_Fall_Enter(fg);
        }
        fp->coll_data.cur_pos = fp->cur_pos;
        mpCollPrev(&fp->coll_data);
        fp->coll_cb(fg);
        mexgr_mk_pipe_cooldown_start(gobj);
    }
}

/* Pipe_CooldownThink (+DB0) */
static void mexgr_mk_pipe_cooldown_think(Ground_GObj* gobj)
{
    MexgrMkStage* s = mexgr_mk_stage(gobj);
    if (--s->timer > 0) {
        return;
    }
    mexgr_mk_pipe_wait_start(gobj);
}

/* Plant_Spawn (+87C): a plant on its pipe's bottom joint (it_8027B5B0), scaled by the stage. */
static Item_GObj* mexgr_mk_plant_spawn(Ground_GObj* gobj, const MexgrMkPlantDesc* desc)
{
    HSD_JObj* root = GET_JOBJ(gobj);
    HSD_JObj* bottom;
    HSD_JObj* top;
    Vec3 bp, tp;
    Item_GObj* plant;
    Item* ip;
    MexitMkPlant* v;
    lb_80011E24(root, &bottom, desc->bottom, -1);
    lb_80011E24(root, &top, desc->top, -1);
    lb_8000B1CC(bottom, NULL, &bp);
    lb_8000B1CC(top, NULL, &tp);
    plant = it_8027B5B0(mu_gr_item_kind(0), NULL, bottom, NULL, true);
    /* The block uses the new item without a NULL check. */
    ip = GET_ITEM(plant);
    ip->scl = desc->scale * Ground_801C0498();
    v = mexit_mk_plant(ip);
    v->home_x = bp.x;
    v->home_y = bp.y;
    v->top_x = tp.x;
    v->top_y = tp.y;
    return plant;
}

/* Pow_Spawn (+DE8): stage item 1 at one of the five points. */
static Item_GObj* mexgr_mk_pow_spawn(void)
{
    ItemKind kind = mu_gr_item_kind(1);
    Vec3 pos;
    Ground_801C2D24(mexgr_mk_pow_points[HSD_Randi(5)], &pos);
    return it_8027B5B0(kind, &pos, NULL, NULL, true);
}

/* Stage_Create (+F4) */
static void mexgr_mk_stage_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    MexgrMkStage* s = mexgr_mk_stage(gobj);
    int i;
    grAnime_801C8138(gobj, gp->map_id, 0);
    for (i = 0; i < 3; i++) {
        HSD_JObj* joints[2];
        s->pipes[i].line = mexgr_mk_pipe_desc[i][0];
        s->pipes[i].kind = mpLineGetKind(s->pipes[i].line);
        lb_80011E24(GET_JOBJ(gobj), joints, mexgr_mk_pipe_desc[i][1], mexgr_mk_pipe_desc[i][2], -1);
        s->pipes[i].mouth = joints[0];
        s->pipes[i].inner = joints[1];
    }
    mexgr_mk_pipe_wait_start(gobj);
    s->plants[0] = mexgr_mk_plant_spawn(gobj, &mexgr_mk_plant_desc[0]);
    s->plants[1] = mexgr_mk_plant_spawn(gobj, &mexgr_mk_plant_desc[1]);
    s->pow = NULL;
    s->pow_timer = 600;
}

/* POW item variables: the stage state it clears on destroy, its hits left. */
typedef struct MexitMkPow {
    u8 x0_pad[0x20];
    /* +20 ip+DF4 */ MexgrMkStage* stage;
    /* +24 ip+DF8 */ int hits;
} MexitMkPow;
_Static_assert(sizeof(MexitMkPow) <= sizeof(((Item*) 0)->xDD4_itemVar), "POW vars fit");

static MexitMkPow* mexit_mk_pow(Item* ip) { return (MexitMkPow*) &ip->xDD4_itemVar; }

/* Stage_Update (+1EC) */
static void mexgr_mk_stage_update(Ground_GObj* gobj)
{
    MexgrMkStage* s = mexgr_mk_stage(gobj);
    switch ((unsigned int) s->state) {
    case 0: mexgr_mk_pipe_wait_think(gobj); break;
    case 1: mexgr_mk_pipe_correct_think(gobj); break;
    case 2: mexgr_mk_pipe_in_think(gobj); break;
    case 3: mexgr_mk_pipe_delay_think(gobj); break;
    case 4: mexgr_mk_pipe_exit_think(gobj); break;
    case 5: mexgr_mk_pipe_cooldown_think(gobj); break;
    default: break;
    }
    if (s->pow == NULL && --s->pow_timer <= 0) {
        s->pow = mexgr_mk_pow_spawn();
        /* The block stores into it without a NULL check. */
        mexit_mk_pow(GET_ITEM(s->pow))->stage = s;
        s->pow_timer = 600;
    }
    lb_800115F4();
}

/* ---- the lifts (map gobj 3) ---- */

typedef struct MexgrMkLift {
    float y;          /* +0: how far down */
    float load;       /* +4: this frame's standing weight */
    float fall;       /* +8: fall speed when they drop */
    float limit;      /* +C: the lowest point */
    float base;       /* +10: the platform joint's rest translate.y */
    HSD_JObj* jobj;   /* +14: the platform joint */
    HSD_JObj* shown;  /* +18: the joint that blinks */
} MexgrMkLift;

typedef struct MexgrMkLifts {
    int state;        /* gp+E0: 0 balance, 1 falling, 2 gone, 3 coming back */
    int timer;        /* gp+E4 */
    MexgrMkLift lift[2];  /* gp+E8, gp+104 */
} MexgrMkLifts;

typedef struct MexgrMkLiftsState {
    u8 x0_pad[0x1C];
    MexgrMkLifts l;
} MexgrMkLiftsState;
_Static_assert(sizeof(MexgrMkLiftsState) <= sizeof(((Ground*) 0)->u), "lift state fits");

static MexgrMkLifts* mexgr_mk_lifts_of(Ground* gp) { return &((MexgrMkLiftsState*) &gp->u)->l; }

/* Lift_OnStand (+E50), the lift lines' callback: weight 0.005 per unit of coll_x50. */
static void mexgr_mk_lift_stand(void* user_data, int joint_id, CollData* coll, int coll_x50,
                                mpLib_GroundEnum ground_kind, float delta_y)
{
    MexgrMkLifts* l;
    (void) coll;
    (void) ground_kind;
    (void) delta_y;
    if ((unsigned int) joint_id > 1) {
        return;
    }
    l = mexgr_mk_lifts_of(user_data);
    l->lift[joint_id].load = l->lift[joint_id].load + (float) coll_x50 * 0.005F;
}

/* Lift_UpdatePos (+EAC): a goes down by its load, b up by it. */
static void mexgr_mk_lift_move(MexgrMkLift* a, MexgrMkLift* b)
{
    float t = a->load;
    a->y = a->y - t;
    b->y = b->y + t;
}

/* Lift_Create (+2D8) */
static void mexgr_mk_lift_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    MexgrMkLifts* l = mexgr_mk_lifts_of(gp);
    HSD_JObj* platform[2];
    HSD_JObj* blink[2];
    HSD_JObj* bottom[2];
    int i;
    grAnime_801C8138(gobj, gp->map_id, 0);
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
    l->state = 0;
    lb_80011E24(GET_JOBJ(gobj), platform, 3, 6, -1);
    lb_80011E24(GET_JOBJ(gobj), blink, 4, 7, -1);
    lb_80011E24(GET_JOBJ(gobj), bottom, 2, 5, -1);
    for (i = 0; i < 2; i++) {
        MexgrMkLift* lift = &l->lift[i];
        Vec3 low, at;
        mpJointSetCb1(i, gp, mexgr_mk_lift_stand);
        lift->load = 0.0F;
        lift->y = 0.0F;
        lb_8000B1CC(bottom[i], NULL, &low);
        lb_8000B1CC(platform[i], NULL, &at);
        lift->base = platform[i]->translate.y;
        lift->limit = low.y - at.y;
        lift->jobj = platform[i];
        lift->shown = blink[i];
    }
}

static void mexgr_mk_lift_place(MexgrMkLift* a, MexgrMkLift* b)
{
    a->jobj->translate.y = a->y + a->base;
    b->jobj->translate.y = b->y + b->base;
}

/* Lift_Update (+444). `hi` is the lift further down, `lo` the other (lift 1 when equal). */
static void mexgr_mk_lift_update(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    MexgrMkLifts* l = mexgr_mk_lifts_of(gp);
    MexgrMkLift* hi;
    MexgrMkLift* lo;
    if (l->lift[0].y > l->lift[1].y) {
        hi = &l->lift[0];
        lo = &l->lift[1];
    } else {
        lo = &l->lift[0];
        hi = &l->lift[1];
    }
    switch (l->state) {
    case 0:
        if (lo->load == 0.0F && hi->load == 0.0F) {
            /* Nobody on them: the lower one rises back by 0.22, both stop level. */
            hi->load = 0.22F;
            mexgr_mk_lift_move(hi, lo);
            if (hi->y < 0.0F) {
                hi->y = 0.0F;
                lo->y = 0.0F;
            }
        } else {
            mexgr_mk_lift_move(lo, hi);
            mexgr_mk_lift_move(hi, lo);
        }
        if (hi->y > hi->limit) {
            l->state = 1;
            l->timer = 180;
            hi->y = hi->limit;
            lo->y = -hi->limit;
            hi->fall = 0.0F;
            lo->fall = 0.0F;
        }
        mexgr_mk_lift_place(hi, lo);
        break;
    case 1:
        if (--l->timer <= 0) {
            l->state = 2;
            l->timer = 120;
            break;
        }
        {
            int i;
            for (i = 0; i < 2; i++) {
                MexgrMkLift* lift = &l->lift[i];
                float f = lift->fall + 0.1F;
                if (f > 2.0F) {
                    f = 2.0F;
                }
                lift->fall = f;
                lift->shown->translate.y = lift->shown->translate.y - lift->fall;
            }
        }
        break;
    case 2:
        if (--l->timer > 0) {
            break;
        }
        l->state = 3;
        l->lift[0].shown->translate.y = 0.0F;
        l->lift[0].load = 0.0F;
        l->lift[1].shown->translate.y = 0.0F;
        l->lift[1].load = 0.0F;
        mpLib_80057BC0(0);
        mpLib_80057BC0(1);
        l->timer = 0;
        break;
    case 3:
        if ((++l->timer & 1) == 0) {
            hi->shown->flags ^= JOBJ_HIDDEN;
            lo->shown->flags ^= JOBJ_HIDDEN;
        }
        if (lo->load == 0.0F && hi->load == 0.0F) {
            hi->load = 0.22F;
            mexgr_mk_lift_move(hi, lo);
            if (hi->y < 0.0F) {
                hi->y = 0.0F;
                lo->y = 0.0F;
                l->state = 0;
                mpJointListAdd(0);
                mpJointListAdd(1);
                hi->shown->flags &= ~JOBJ_HIDDEN;
                lo->shown->flags &= ~JOBJ_HIDDEN;
            }
        } else {
            mexgr_mk_lift_move(lo, hi);
            mexgr_mk_lift_move(hi, lo);
        }
        mexgr_mk_lift_place(hi, lo);
        break;
    default:
        break;
    }
    HSD_JObjSetMtxDirtySub(GET_JOBJ(gobj));
    Ground_UpdateMapColl(gobj);
    hi->load = 0.0F;
    lo->load = 0.0F;
}

/* map_gobjs (+0) */
static StageCallbacks mexgr_mk_map[4] = {
    { mexgr_wp1_tseak_anim, mexgr_wp1_tseak_false, mexgr_wp1_tseak_nop, mexgr_wp1_tseak_nop, 0 },
    { mexgr_wp1_background_create, mexgr_wp1_tseak_false, mexgr_wp1_tseak_coll, mexgr_wp1_tseak_nop, 0 },
    { mexgr_mk_stage_create, mexgr_wp1_tseak_false, mexgr_mk_stage_update, mexgr_wp1_tseak_nop, 0xC0000000 },
    { mexgr_mk_lift_create, mexgr_wp1_tseak_false, mexgr_mk_lift_update, mexgr_wp1_tseak_nop, 0 },
};

/* onInit (+50) */
static void mexgr_mk_init(void)
{
    mexgr_wp1_init(4);
}

/* ---- stage item 0: the Piranha Plant ---- */

static bool mexit_mk_false(Item_GObj* gobj)
{
    (void) gobj;
    return false;
}

static void mexit_mk_nop(Item_GObj* gobj) { (void) gobj; }

/* Moves from the item's position toward (x, y) in 60 frames. */
static void mexit_mk_plant_toward(Item* ip, float x, float y)
{
    Vec3 to = { x, y, 0.0F };
    float d = mexgr_mk_dist2(&ip->pos, &to);
    float a = mexgr_mk_angle2(&ip->pos, &to);
    ip->x40_vel.x = cosf(a) * d / 60.0F;
    ip->x40_vel.y = sinf(a) * d / 60.0F;
}

/* stateOut (+714) */
static void mexit_mk_plant_out(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexitMkPlant* v = mexit_mk_plant(ip);
    Item_80268E5C(gobj, 1, ITEM_ANIM_UPDATE);
    v->timer = 60;
    mexit_mk_plant_toward(ip, v->top_x, v->top_y);
    ip->xACC_itemHurtbox[0].state = (HurtCapsuleState) 0;
}

/* stateStay (+7D4) */
static void mexit_mk_plant_stay(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Item_80268E5C(gobj, 2, ITEM_ANIM_UPDATE);
    mexit_mk_plant(ip)->timer = 60;
    ip->x40_vel.x = 0.0F;
    ip->x40_vel.y = 0.0F;
    ip->xACC_itemHurtbox[0].state = (HurtCapsuleState) 0;
}

/* stateIn (+828) */
static void mexit_mk_plant_in(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexitMkPlant* v = mexit_mk_plant(ip);
    Item_80268E5C(gobj, 3, ITEM_ANIM_UPDATE);
    v->timer = 60;
    mexit_mk_plant_toward(ip, v->home_x, v->home_y);
    ip->xACC_itemHurtbox[0].state = (HurtCapsuleState) 0;
}

/* stateFly (+51C): knocked away (as Saffron City 64's Pokemon), upside down, hurtbox off. */
static void mexit_mk_plant_fly(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexitMkPlant* v = mexit_mk_plant(ip);
    float kb = ip->xCC8_knockback;
    float scale = p_ftCommonData->x100;
    float angle = ip->xCAC_angle == 361 ? 45.0F : (float) ip->xCAC_angle;
    float c, s;
    angle = (float) ((double) angle * 0x1.1df4722d4405fp-6);
    c = cosf(angle);
    s = sinf(angle);
    v->kb_x = kb * c * scale;
    v->kb_y = kb * s * scale;
    v->fall = 0.0F;
    v->xE00 = 0.0F;
    GET_JOBJ(gobj)->rotate.x = 3.1415927F;
    ip->xACC_itemHurtbox[0].state = (HurtCapsuleState) 0;
    Item_80268E5C(gobj, 4, ITEM_ANIM_UPDATE);
}

/* stateDead (+8F0): 1500 frames out of play. */
static void mexit_mk_plant_dead(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Item_80268E5C(gobj, 5, ITEM_ANIM_UPDATE);
    mexit_mk_plant(ip)->timer = 1500;
    GET_JOBJ(gobj)->rotate.x = 0.0F;
    ip->xDAA_flag.b7 = 0;
    ip->x40_vel.x = 0.0F;
    ip->x40_vel.y = 0.0F;
    ip->xACC_itemHurtbox[0].state = (HurtCapsuleState) 2;
}

/* checkNearbyFighters (+67C): a fighter within `radius` (3D distance) of `pos`. */
static bool mexit_mk_fighter_near(Vec3* pos, float radius)
{
    HSD_GObj* fgobj;
    for (fgobj = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; fgobj != NULL; fgobj = fgobj->next) {
        Fighter* fp = HSD_GObjGetUserData(fgobj);
        if (mexgr_mk_dist3(pos, &fp->cur_pos) <= radius) {
            return true;
        }
    }
    return false;
}

/* waitAnim (+110): down for the wait; then out once no fighter is within the pipe's length plus
 * two stage units. */
static bool mexit_mk_plant_wait_anim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexitMkPlant* v = mexit_mk_plant(ip);
    Vec3 home, top;
    float g, radius;
    if (v->timer > 0) {
        v->timer--;
    }
    if (v->timer != 0) {
        return false;
    }
    home.x = v->home_x;
    home.y = v->home_y;
    home.z = 0.0F;
    top.x = v->top_x;
    top.y = v->top_y;
    top.z = 0.0F;
    radius = mexgr_mk_dist2(&home, &top);
    g = Ground_801C0498();
    radius = (g + g) + radius;
    if (!mexit_mk_fighter_near(&ip->pos, radius)) {
        mexit_mk_plant_out(gobj);
    }
    return false;
}

/* OutAnim (+1CC), StayAnim (+220), InAnim (+274): the next state after 60 frames. */
static bool mexit_mk_plant_out_anim(Item_GObj* gobj)
{
    if (--mexit_mk_plant(GET_ITEM(gobj))->timer <= 0) {
        mexit_mk_plant_stay(gobj);
    }
    return false;
}

static bool mexit_mk_plant_stay_anim(Item_GObj* gobj)
{
    if (--mexit_mk_plant(GET_ITEM(gobj))->timer <= 0) {
        mexit_mk_plant_in(gobj);
    }
    return false;
}

static bool mexit_mk_plant_in_anim(Item_GObj* gobj)
{
    if (--mexit_mk_plant(GET_ITEM(gobj))->timer <= 0) {
        mexit_mk_plant_wait(gobj);
    }
    return false;
}

/* FlyPhys (+2D0): as Saffron City 64's Pokemon. */
static void mexit_mk_plant_fly_phys(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexitMkPlant* v = mexit_mk_plant(ip);
    float fall = v->fall;
    float keep = v->xE00;
    float vx = v->kb_x;
    float vy = v->kb_y;
    float decay = p_ftCommonData->x204_knockbackFrameDecay;
    float dir;
    fall = fall - 0.06F;
    if (fall < -4.0F) {
        fall = -4.0F;
    }
    dir = atan2f(vy, vx);
    vx = vx - cosf(dir) * decay;
    vy = vy - sinf(dir) * decay;
    ip->x40_vel.x = vx;
    ip->x40_vel.y = fall + vy;
    v->kb_x = vx;
    v->kb_y = vy;
    v->fall = fall;
    v->xE00 = keep;
}

/* FlyColl (+3B8): past a blast zone it is out of play. */
static bool mexit_mk_plant_fly_coll(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    if (ip->pos.x > Stage_GetBlastZoneRightOffset() || ip->pos.x < Stage_GetBlastZoneLeftOffset() ||
        ip->pos.y > Stage_GetBlastZoneTopOffset() || ip->pos.y < Stage_GetBlastZoneBottomOffset())
    {
        mexit_mk_plant_dead(gobj);
    }
    return false;
}

/* DeadAnim (+444): back in its pipe after 1500 frames, 10 damage to go. */
static bool mexit_mk_plant_dead_anim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexitMkPlant* v = mexit_mk_plant(ip);
    if (--v->timer > 0) {
        return false;
    }
    mexit_mk_plant_wait(gobj);
    v->hp = 10;
    ip->xC9C = 0;
    ip->pos.x = v->home_x;
    ip->pos.y = v->home_y;
    return false;
}

/* onSpawn (+60): waiting, 50 damage to go, xDCC_flag.b3 off. */
static void mexit_mk_plant_spawned(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    mexit_mk_plant_wait(gobj);
    mexit_mk_plant(ip)->hp = 50;
    ip->xC9C = 0;
    ip->xDCC_flag.b3 = 0;
}

/* onTakeDamage (+D0) */
static bool mexit_mk_plant_dmg_received(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    if (ip->xC9C >= mexit_mk_plant(ip)->hp) {
        mexit_mk_plant_fly(gobj);
    }
    return false;
}

static ItemStateTable mexit_mk_plant_states[6] = {
    { 0, mexit_mk_plant_wait_anim, mexit_mk_nop, mexit_mk_false },
    { 1, mexit_mk_plant_out_anim, mexit_mk_nop, mexit_mk_false },
    { 1, mexit_mk_plant_stay_anim, mexit_mk_nop, mexit_mk_false },
    { 1, mexit_mk_plant_in_anim, mexit_mk_nop, mexit_mk_false },
    { 0, mexit_mk_false, mexit_mk_plant_fly_phys, mexit_mk_plant_fly_coll },
    { 0, mexit_mk_plant_dead_anim, mexit_mk_nop, mexit_mk_false },
};

/* ---- stage item 1: the POW block ---- */

/* stateWait (+F4) */
static void mexit_mk_pow_wait(Item_GObj* gobj)
{
    Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
}

/* stateHit (+11C): squashed (scale.y 0.5), unhittable, 30 frames of shaking, sound 1. */
static void mexit_mk_pow_hit(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Item_80268E5C(gobj, 1, ITEM_ANIM_UPDATE);
    ip->x5D4_hitboxes[0].hit.x43_b2 = 1;   /* the word at ip+614 |= 0x20 */
    GET_JOBJ(gobj)->scale.y = 0.5F;
    ip->xACC_itemHurtbox[0].state = (HurtCapsuleState) 2;
    mexit_mk_pow(ip)->hits = 30;
    mu_gr_mex_sfx(1);
}

static void mexit_mk_pow_spawned(Item_GObj* gobj) { mexit_mk_pow_wait(gobj); }

/* onDestroy (+44): the stage may drop the next one. */
static void mexit_mk_pow_destroyed(Item_GObj* gobj)
{
    mexit_mk_pow(GET_ITEM(gobj))->stage->pow = NULL;
}

/* onTakeDamage (+5C) */
static bool mexit_mk_pow_dmg_received(Item_GObj* gobj)
{
    mexit_mk_pow_hit(gobj);
    return false;
}

/* hitAnim (+9C): a quake each frame; the item goes when the count runs out. */
static bool mexit_mk_pow_hit_anim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Camera_RequestQuake(1, NULL);
    return --mexit_mk_pow(ip)->hits == 0;
}

static ItemStateTable mexit_mk_pow_states[2] = {
    { 0, mexit_mk_false, mexit_mk_nop, mexit_mk_false },
    { 1, mexit_mk_pow_hit_anim, mexit_mk_nop, mexit_mk_false },
};

/* Exports 0, 1, 2, 7 for both. */
static const ItemLogicTable mexit_mk_items[2] = {
    { .states = mexit_mk_plant_states, .spawned = mexit_mk_plant_spawned, .destroyed = mexit_mk_nop,
      .dmg_received = mexit_mk_plant_dmg_received },
    { .states = mexit_mk_pow_states, .spawned = mexit_mk_pow_spawned,
      .destroyed = mexit_mk_pow_destroyed, .dmg_received = mexit_mk_pow_dmg_received },
};

const MuGrStage mu_gr_mushroom_kingdom_64 = {
    .name = "Mushroom Kingdom 64",
    .file = "/GrOMk.dat",
    .map_gobjs = mexgr_mk_map,
    .on_init = mexgr_mk_init,
    .on_load = mu_gr_event_nop,
    .items = mexit_mk_items,
    .item_count = 2,
    .flags = MU_GR_READY,
};
