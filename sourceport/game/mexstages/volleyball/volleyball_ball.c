/* Volleyball: the ball code of the grFunction block (volleyball.h). SpawnBall creates stage item 0
 * (volleyball_item.c holds its state table and callbacks); this file is what the stage block itself
 * does to the ball: serve, the stage's own copies of the hit and wait routines, its draw function
 * (the model, a fading trail, the debug boxes and the parry circles), and the parry: a guarding or
 * reflecting fighter, or one air dodging, near a ball in play parries it upward and enters custom
 * air states. The parry needs parry_execute_window > 0, which the attributes give only through the
 * develop menu (0 in the file): with the file's values Ball_ParryUpdate and the parry drawing return
 * at once. */
#include "volleyball.h"

#include "../../akaneia/mu_ak_fighter.h"
#include "../common/mu_gr_mex.h"

#include <math.h>
#include <string.h>

#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <melee/cm/camera.h>
#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_0877.h>
#include <melee/ft/ftaction.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcolanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/ftdynamics.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_09F7.h>
#include <melee/ft/kinds/ftCommon/ftCo_AttackAir.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/types.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gmvs.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/it/inlines.h>
#include <melee/it/itdraw.h>
#include <melee/it/item.h>
#include <melee/it/itmaplib.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mtx.h>
#include <sysdolphin/baselib/pobj.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/tev.h>

extern Camera game_camera;

_Static_assert(sizeof(MexgrVbBall) <= sizeof(((Item*) 0)->xDD4_itemVar), "ball fits the item vars");
_Static_assert(sizeof(MexgrVbParryVars) <= sizeof(((Fighter*) 0)->mv), "parry fits the motion vars");

static MexgrVbParryVars* mexgr_vb_parry_vars(Fighter* fp)
{
    return (MexgrVbParryVars*) &fp->mv;
}

/* .data colors of Ball_GX (r, g, b, a) */
static const GXColor mexgr_vb_airparry_color = { 0x80, 0xFF, 0x80, 0x78 };
static const GXColor mexgr_vb_shield_color = { 0x80, 0x80, 0xFF, 0x78 };
static const GXColor mexgr_vb_reflect_color = { 0x80, 0xFF, 0x80, 0x78 };
static const GXColor mexgr_vb_ball_color = { 0xFF, 0x80, 0x80, 0x78 };

/* The GX state every draw routine of the block sets (Net_GX, Ball_GX, Prim_Draw2DSphere,
 * drawBallTrail), drawBallTrail without GXSetTevClampMode. */
static void mexgr_vb_gx_begin_draw(bool clamp)
{
    GXSetColorUpdate(GX_TRUE);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
    GXSetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_GREATER, 0);
    GXSetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
    GXSetZCompLoc(GX_FALSE);
    GXSetNumTexGens(0);
    if (clamp) {
        GXSetTevClampMode(0, 0);
    }
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE,
                  GX_AF_NONE);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXLoadPosMtxImm(HSD_CObjGetViewingMtxPtrDirect(HSD_CObjGetCurrent()), GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
}

static void mexgr_vb_vtx(f32 x, f32 y, const GXColor* c)
{
    GXPosition3f32(x, y, 0.0F);
    GXColor4u8(c->r, c->g, c->b, c->a);
}

/* ---- shared ball routines (both blocks carry the same instructions) ---- */

/* Ball_PlayHitSFX (+2EE4, item +13A0): hit sound n - 1 of the ball's hit sounds. */
void mexgr_vb_play_hit_sfx(HSD_GObj* ball, s32 n)
{
    mu_gr_mex_sfx(n - 1 + mexgr_vb_ball(GET_ITEM(ball))->attr.hitsfx_start);
}

/* processBallTrail (+3D0C, item +1734), the ball's accessory callback while it flies: the trail
 * shifted by one point, the newest at the ball with the velocity's angle (the last angle kept when
 * the ball stands), wrapped to [0, 2 pi). gm_GetDbPauseFlag(1) is called and its answer dropped. */
void mexgr_vb_process_trail(HSD_GObj* ball)
{
    Item* ip = GET_ITEM(ball);
    MexgrVbBall* bv = mexgr_vb_ball(ip);
    int i;
    f32 a;
    for (i = 13; i >= 0; i--) {
        bv->trail[i + 1] = bv->trail[i];
    }
    bv->trail[0].valid = 1;
    bv->trail[0].x = ip->pos.x;
    bv->trail[0].y = ip->pos.y;
    if (ip->x40_vel.x != 0.0F || ip->x40_vel.y != 0.0F) {
        bv->trail[0].angle = atan2f(ip->x40_vel.y, ip->x40_vel.x);
    }
    a = bv->trail[0].angle;
    if (a < 0.0F) {
        do {
            a = (f32) ((f64) a + MEXGR_VB_TAU);
        } while (a < 0.0F);
        bv->trail[0].angle = a;
    }
    a = bv->trail[0].angle;
    if ((f64) a >= MEXGR_VB_TAU) {
        do {
            a = (f32) ((f64) a - MEXGR_VB_TAU);
        } while ((f64) a >= MEXGR_VB_TAU);
        bv->trail[0].angle = a;
    }
    gm_GetDbPauseFlag(1);
}

/* stateWait (+32FC, item +1338): item state 1, the trail on. */
void mexgr_vb_state_wait(HSD_GObj* ball)
{
    Item* ip = GET_ITEM(ball);
    MexgrVbBall* bv = mexgr_vb_ball(ip);
    Item_80268E5C(ball, MEXGR_VB_BALL_WAIT, ITEM_ANIM_UPDATE);
    bv->wait_frames = 0;
    bv->last_pos = ip->pos;
    ip->on_accessory = mexgr_vb_process_trail;
    bv->trail_on = 1;
}

/* Ball_Hit (+2C7C, item +77C): the ball flies off at the knockback and angle its damage gave it
 * (angle 361: 45 degrees, the x sign from the damage direction), in the wait state. For a hit by
 * an attack (`from_attack`), the touch count: the same side's touch with another attack counts one
 * more and plays the next hit sound, the other side's starts at one; and a parried ball gets its
 * parry-hit callback. The stage's copy installs its own Ball_OnParryHit, the item's its own. */
void mexgr_vb_ball_hit(HSD_GObj* ball, s32 from_attack, HSD_GObjEvent on_parry_hit)
{
    Item* ip = GET_ITEM(ball);
    MexgrVbBall* bv = mexgr_vb_ball(ip);
    f32 kb = ip->xCC8_knockback;
    const f32 scale = p_ftCommonData->x100;
    f32 deg;
    f32 rad;
    f32 c;
    f32 s;
    HSD_GObj* src;
    Fighter* fp;
    s32 attack;
    s32 side;
    if (ip->xCAC_angle == 361) {
        deg = 45.0F;
    } else {
        deg = (f32) ip->xCAC_angle;
    }
    rad = (f32) ((f64) deg * MEXGR_VB_DEG2RAD);
    c = cosf(rad);
    s = sinf(rad);
    if (ip->xCCC_incDamageDirection > 0.0F) {
        c = -c;
    }
    c = c * kb;
    c = c * scale;
    bv->vel.x = c;
    kb = kb * s;
    kb = kb * scale;
    bv->vel.y = kb;
    bv->vel2.y = 0.0F;
    mexgr_vb_state_wait(ball);
    it_802762BC(ip);
    if (from_attack == 0) {
        return;
    }
    src = ip->xCEC_fighterGObj;
    if (src == NULL) {
        src = ip->xCF0_itemGObj;
        if (src == NULL) {
            return;
        }
    }
    fp = GET_FIGHTER(src);
    attack = fp->x2074.x2088;
    if (gm_8016B168() == 1) {
        side = fp->team;
        if (bv->last_side == side) {
            if (bv->last_attack != attack) {
                bv->hits += 1;
                mexgr_vb_play_hit_sfx(ball, bv->hits);
            }
        } else {
            bv->hits = 1;
            mexgr_vb_play_hit_sfx(ball, 1);
        }
    } else {
        MexgrVbJudge* jp;
        side = fp->player_idx;
        jp = mexgr_vb_judge();
        if (mexgr_vb_slot_side(jp, side) == mexgr_vb_slot_side(jp, bv->last_side)) {
            if (bv->last_attack != attack) {
                bv->hits += 1;
                mexgr_vb_play_hit_sfx(ball, bv->hits);
            }
        } else {
            bv->hits = 1;
            mexgr_vb_play_hit_sfx(ball, 1);
        }
    }
    bv->last_attack = attack;
    bv->last_side = side;
    if (bv->parry != 0 && ip->xCEC_fighterGObj != NULL) {
        ip->entered_hitlag = on_parry_hit;
        bv->parry = 2;
        bv->parry_fighter = ip->xCEC_fighterGObj;
    }
}

/* Ball_OnParryLaunch (+3E18, item +1844), the exited_hitlag callback of a parry hit: with the
 * parrying fighter's stick held, the ball's direction turns toward the stick, by the stick's
 * share across the velocity (squared) times p_ftCommonData->x1A8 degrees; then the parry ends. */
void mexgr_vb_on_parry_launch(HSD_GObj* ball)
{
    Item* ip = GET_ITEM(ball);
    MexgrVbBall* bv = mexgr_vb_ball(ip);
    Fighter* fp = GET_FIGHTER(bv->parry_fighter);
    const f32 sx = fp->input.lstick[0].x;
    const f32 sy = fp->input.lstick[0].y;
    if (sx != 0.0F || sy != 0.0F) {
        Vec3 v = bv->vel;
        f32 len2;
        f32 t;
        f32 u;
        len2 = v.y * v.y;
        t = v.x * v.x;
        len2 = len2 + t;
        if ((f64) len2 >= 0x1.4f8b588e368f1p-17 /* 1e-5 */) {
            const ftCommonData* cd = p_ftCommonData;
            const f32 dir = atan2f(v.y, v.x);
            Vec3 stick;
            Vec3 cross;
            f32 turn;
            f32 max;
            f32 speed;
            t = v.x * sy;
            u = sx * v.y;
            u = u - t;
            turn = (f32) (((f64) u * (f64) u) / (f64) len2);
            max = (f32) ((f64) cd->x1A8 * MEXGR_VB_DEG2RAD);
            speed = sqrtf(len2);
            stick.x = sx;
            stick.y = sy;
            stick.z = 0.0F;
            PSVECCrossProduct(&v, &stick, &cross);
            if (cross.z < 0.0F) {
                turn = -turn;
            }
            turn = turn * max;
            turn = turn + dir;
            bv->vel.x = cosf(turn) * speed;
            bv->vel.y = sinf(turn) * speed;
        }
    }
    bv->parry = 0;
    bv->parry_fighter = NULL;
}

/* ---- the stage block's own routines ---- */

/* Ball_OnParryHit (+3364), the entered_hitlag callback the stage's Ball_Hit installs: parry hitlag
 * for the ball and (+ 2 frames) its parrier, the parrier's DamageFall table entry refreshed (its
 * count byte from the entry its x18FD names), the launch next. The stage block calls its Ball_Hit
 * only with from_attack 0, so on the console this copy is never installed; it is kept as the
 * block has it. */
static void mexgr_vb_stage_on_parry_hit(HSD_GObj* ball)
{
    Item* ip = GET_ITEM(ball);
    MexgrVbBall* bv = mexgr_vb_ball(ip);
    Fighter* fp;
    s32 n;
    if (ip->xCEC_fighterGObj == NULL) {
        return;
    }
    fp = GET_FIGHTER(ip->xCEC_fighterGObj);
    n = bv->attr.parry_hit_freeze_frames;
    ip->xCBC_hitlagFrames = ip->xCBC_hitlagFrames + (f32) n;
    fp->dmg.x195c_hitlag_frames = fp->dmg.x195c_hitlag_frames + (f32) (n + 2);
    fp->dmg.x18FD = (u8) DISC_RAW(Fighter_804D6530[fp->dmg.x18FD * 2 + 1]);
    ip->exited_hitlag = mexgr_vb_on_parry_launch;
}

static void mexgr_vb_stage_ball_hit(HSD_GObj* ball, s32 from_attack)
{
    mexgr_vb_ball_hit(ball, from_attack, mexgr_vb_stage_on_parry_hit);
}

/* resetBallTrail (+393C) */
static void mexgr_vb_reset_trail(HSD_GObj* ball)
{
    MexgrVbBall* bv = mexgr_vb_ball(GET_ITEM(ball));
    int i;
    for (i = 0; i < 15; i++) {
        bv->trail[i].valid = 0;
    }
}

/* Mix (+5504): a + (b - a) t per channel, as (1 - t) a + t b in floats, truncated. */
static void mexgr_vb_mix(GXColor* out, const GXColor* a, const GXColor* b, f32 t)
{
    const f32 it = 1.0F - t;
    f32 x;
    f32 y;
    x = (f32) (s32) a->r * it;
    y = (f32) (s32) b->r * t;
    out->r = (u8) (s32) (x + y);
    x = (f32) (s32) a->g * it;
    y = (f32) (s32) b->g * t;
    out->g = (u8) (s32) (x + y);
    x = (f32) (s32) a->b * it;
    y = (f32) (s32) b->b * t;
    out->b = (u8) (s32) (x + y);
    x = (f32) (s32) a->a * it;
    y = (f32) (s32) b->a * t;
    out->a = (u8) (s32) (x + y);
}

/* Item_DamageColorBlend (+508C): the trail color from the ball's damage past dmg_min, through
 * trail_color0, 1 and 2 (trail_blend_mid the middle). */
static void mexgr_vb_damage_color(Item* ip, GXColor* out)
{
    MexgrVbBall* bv = mexgr_vb_ball(ip);
    f32 d = (f32) ip->xC9C;
    const f32 top = (f32) (bv->attr.dmg_min + bv->attr.colshift_pctend);
    f32 mid;
    if (d > top) {
        d = top;
    }
    d = d - (f32) bv->attr.dmg_min;
    d = d / top;
    mid = bv->attr.trail_blend_mid;
    if (mid > d) {
        d = d / mid;
        mexgr_vb_mix(out, &bv->attr.trail_color0, &bv->attr.trail_color1, d);
    } else {
        d = d - mid;
        d = (f32) ((f64) d / (1.0 - (f64) mid));
        mexgr_vb_mix(out, &bv->attr.trail_color1, &bv->attr.trail_color2, d);
    }
}

/* CubicHermite (+5178): the Catmull-Rom point at t between p1 and p2. */
static f32 mexgr_vb_hermite(f32 p0, f32 p1, f32 p2, f32 p3, f32 t)
{
    f32 a0 = -p0;
    f32 a;
    f32 b;
    f32 c;
    f32 q3;
    f32 k;
    a0 = a0 * 0.5F;
    q3 = p3 * 0.5F;
    a = p1 * 3.0F;
    a = a * 0.5F;
    a = a + a0;
    k = p2 * 3.0F;
    k = k * 0.5F;
    a = a - k;
    a = a + q3;
    a = a * t;
    a = a * t;
    a = a * t;
    b = p1 * 5.0F;
    b = b * 0.5F;
    b = p0 - b;
    k = p2 + p2;
    b = b + k;
    b = b - q3;
    b = b * t;
    b = b * t;
    a = a + b;
    c = p2 * 0.5F;
    c = c + a0;
    c = c * t;
    a = a + c;
    return a + p1;
}

/* drawBallPoint (+5200): the two vertices of the trail strip at one point. */
static void mexgr_vb_draw_point(const MexgrVbBallAttr* attr, f32 x, f32 y, f32 angle, f32 scale,
                                const GXColor* color)
{
    const f32 wa = scale * attr->trail_width_a;
    const f32 wb = scale * attr->trail_width_b;
    f32 c;
    f32 s;
    f32 x0;
    f32 y0;
    f32 x1;
    f32 y1;
    c = cosf(angle);
    s = sinf(angle);
    s = s * wa;
    x0 = c - s;
    c = cosf(angle);
    y0 = c * wa;
    s = sinf(angle);
    y0 = y0 + s;
    c = cosf(angle);
    s = sinf(angle);
    s = s * wb;
    x1 = c - s;
    c = cosf(angle);
    y1 = c * wb;
    s = sinf(angle);
    x0 = x0 + x;
    y0 = y0 + y;
    mexgr_vb_vtx(x0, y0, color);
    x1 = x1 + x;
    y1 = y1 + s;
    y1 = y1 + y;
    mexgr_vb_vtx(x1, y1, color);
}

/* drawBallTrail (+3FC0): while the ball moves with its trail on, a strip through the trail points
 * and the Catmull-Rom points between them, fading out, in the damage color; with the camera's
 * debug display a direction mark first. */
static void mexgr_vb_draw_trail(HSD_GObj* ball)
{
    Item* ip = GET_ITEM(ball);
    MexgrVbBall* bv = mexgr_vb_ball(ip);
    GXColor color;
    s32 n;
    s32 i;
    if (ip->x40_vel.x == 0.0F && ip->x40_vel.y == 0.0F) {
        return;
    }
    if (bv->trail_on == 0) {
        return;
    }
    mexgr_vb_gx_begin_draw(false);
    if (game_camera.x399_b3) {
        const f32 a = bv->trail[0].angle;
        static const GXColor white = { 0x80, 0xFF, 0xFF, 0xFF };
        static const GXColor yellow = { 0x80, 0xFF, 0x00, 0xFF };
        static const GXColor magenta = { 0x80, 0x00, 0xFF, 0xFF };
        f32 ax;
        f32 ay;
        f32 bx;
        f32 by;
        f32 t;
        f32 px;
        f32 py;
        ax = cosf(a);
        t = sinf(a);
        t = t * 8.0F;
        ax = ax - t;
        ay = cosf(a);
        ay = ay * 8.0F;
        t = sinf(a);
        ay = ay + t;
        bx = cosf(a);
        t = sinf(a);
        t = t * -8.0F;
        bx = bx - t;
        by = cosf(a);
        by = by * -8.0F;
        t = sinf(a);
        by = by + t;
        GXBegin(GX_LINES, GX_VTXFMT0, 4);
        mexgr_vb_vtx(ip->pos.x, ip->pos.y, &white);
        px = ip->pos.x;
        t = cosf(bv->trail[0].angle);
        t = t * 7.0F;
        px = t + px;
        py = ip->pos.y;
        t = sinf(bv->trail[0].angle);
        t = t * 7.0F;
        py = t + py;
        mexgr_vb_vtx(px, py, &white);
        ax = ax + bv->trail[0].x;
        ay = ay + bv->trail[0].y;
        mexgr_vb_vtx(ax, ay, &yellow);
        bx = bx + bv->trail[0].x;
        by = by + bv->trail[0].y;
        mexgr_vb_vtx(bx, by, &magenta);
    }
    n = 0;
    for (i = 0; i < 15; i++) {
        if (bv->trail[i].valid != 0) {
            n++;
        }
    }
    color.r = 0x80;
    color.g = 0xFF;
    color.b = 0xFF;
    mexgr_vb_damage_color(ip, &color);
    GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, (u16) (((n - 1 + n) << 1) & 0xFFFE));
    if (n > 0) {
        const f32 count = (f32) n;
        for (i = 0; i < n; i++) {
            const MexgrVbTrailPoint* p = &bv->trail[i];
            const MexgrVbTrailPoint* prev;
            const MexgrVbTrailPoint* next2;
            f32 fade = (f32) i;
            f32 hx;
            f32 hy;
            fade = fade / count;
            fade = 1.0F - fade;
            color.a = (u8) (s32) (fade * 255.0F);
            mexgr_vb_draw_point(&bv->attr, p->x, p->y, p->angle, fade, &color);
            if (i >= n - 1) {
                continue;
            }
            prev = i <= 1 ? p : p - 1;
            next2 = i >= n - 2 ? p + 1 : p + 2;
            hx = mexgr_vb_hermite(prev->x, p->x, p[1].x, next2->x, 0.5F);
            hy = mexgr_vb_hermite(prev->y, p->y, p[1].y, next2->y, 0.5F);
            mexgr_vb_draw_point(&bv->attr, hx, hy, p->angle,
                                (f32) ((f64) fade - (1.0 / (f64) n) * 0.5), &color);
        }
    }
    HSD_StateInvalidate(-1);
    HSD_StateInitTev();
    HSD_ClearVtxDesc();
}

/* Prim_Draw2DSphere (+4550): a disc of 16 slices around pos (x, y). */
static void mexgr_vb_draw_disc(const Vec3* pos, f32 radius, const GXColor* color)
{
    f32 base = 0.0F;
    int q;
    mexgr_vb_gx_begin_draw(true);
    GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, 0x40);
    for (q = 4; q != 0; q--) {
        const f32 b = base;
        int j = 0;
        f32 fj = 0.0F;
        base = (f32) ((f64) base + MEXGR_VB_HALF_PI);
        do {
            f32 a0;
            f32 a1;
            f32 c0;
            f32 s0;
            f32 c1;
            f32 s1;
            f32 t;
            a0 = fj * 0x1.921fb6p-2F;   /* pi / 8 */
            a0 = a0 + b;
            j++;
            fj = (f32) j;
            a1 = fj * 0x1.921fb6p-2F;
            a1 = a1 + b;
            c0 = cosf(a0) * radius;
            s0 = sinf(a0) * radius;
            c1 = cosf(a1) * radius;
            s1 = sinf(a1);
            mexgr_vb_vtx(pos->x, pos->y, color);
            t = c0 + pos->x;
            c0 = s0 + pos->y;
            mexgr_vb_vtx(t, c0, color);
            c1 = c1 + pos->x;
            s1 = s1 * radius;
            s1 = s1 + pos->y;
            mexgr_vb_vtx(c1, s1, color);
            mexgr_vb_vtx(pos->x, pos->y, color);
        } while (j != 4);
    }
    HSD_StateInvalidate(-1);
}

/* CircleVsCircle (+48F0) */
static bool mexgr_vb_circles(const Vec3* a, f32 ra, const Vec3* b, f32 rb)
{
    const f32 dx = a->x - b->x;
    const f32 dy = a->y - b->y;
    const f32 d = sqrtf((f32) ((f64) dx * (f64) dx + (f64) dy * (f64) dy));
    const f32 r = ra + rb;
    return d < r;
}

/* The air dodge circle of Ball_GX and Ball_ParryUpdate: the center of the fighter's desired ECB,
 * and the full shield's size (p_ftCommonData x2D4, x2D8, x264 as the block combines them) times
 * the fighter's shield size and model scale. */
static f32 mexgr_vb_air_circle(Fighter* fp, Vec3* pos)
{
    const ftCommonData* cd = p_ftCommonData;
    f32 t;
    f32 r;
    f32 lo;
    f32 k;
    t = fp->coll_data.desired_ecb.right.x + fp->coll_data.desired_ecb.left.x;
    t = t * 0.5F;
    pos->x = t + fp->coll_data.cur_pos.x;
    t = fp->coll_data.desired_ecb.top.y + fp->coll_data.desired_ecb.bottom.y;
    t = t * 0.5F;
    pos->y = t + fp->coll_data.cur_pos.y;
    pos->z = 0.0F;
    lo = cd->x2D4;
    k = cd->x264;
    r = cd->x2D8;
    r = r - lo;
    r = r + lo;
    lo = 1.0F - k;
    r = r * lo;
    r = r + k;
    r = r * fp->co_attrs.initial_shield_size;
    return r * ftCommon_GetModelScale(fp);
}

/* A guard state: GuardOn to GuardReflect (0xB2 to 0xB6), or Yoshi's own (0x155 to 0x159). */
static bool mexgr_vb_guarding(Fighter* fp)
{
    const s32 ms = fp->motion_id;
    if ((u32) (ms - 0xB2) <= 4) {
        return true;
    }
    return fp->kind == Ft_Kind_Yoshi && (u32) (ms - 0x155) <= 4;
}

/* The reflect bubble's world center and radius. */
static f32 mexgr_vb_reflect_circle(Fighter* fp, Vec3* pos)
{
    Vec3 scale;
    HSD_JObjSetMtxDirtySub(fp->reflect_hit.bone);
    lb_8000B1CC(fp->reflect_hit.bone, &fp->reflect_hit.offset, pos);
    HSD_MtxGetScale(fp->reflect_hit.bone->mtx, &scale);
    return scale.x * fp->reflect_hit.size;
}

/* Ball_GX (+340C): the model, then in pass 2 the trail; with the camera's debug display the
 * ball's collision box; with the parry on, the circles the parry tests: the ball's (its develop
 * display bit, xDAA bit 6), and for each fighter whose display bit (x21FC b6) is set its reflect
 * bubble and shield while guarding, and its air dodge circle in any state past 0xEB within the
 * parry window. */
static void mexgr_vb_ball_draw(HSD_GObj* gobj, intptr_t pass)
{
    Item* ip;
    MexgrVbBall* bv;
    HSD_GObj* f;
    it_8026EECC(gobj, pass);
    if (pass != 2) {
        return;
    }
    ip = GET_ITEM(gobj);
    bv = mexgr_vb_ball(ip);
    mexgr_vb_draw_trail(gobj);
    if (game_camera.x399_b3) {
        static const GXColor box = { 0x80, 0xFF, 0x80, 0x78 };
        f32 hx;
        f32 hy;
        f32 x0;
        f32 x1;
        f32 y0;
        f32 y1;
        mexgr_vb_gx_begin_draw(true);
        hx = bv->attr.coll_size_x * 0.5F;
        x0 = ip->pos.x - hx;
        x1 = ip->pos.x + hx;
        hy = bv->attr.coll_size_y * 0.5F;
        y1 = ip->pos.y + hy;
        y0 = ip->pos.y - hy;
        GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, 5);
        mexgr_vb_vtx(x1, y1, &box);
        mexgr_vb_vtx(x0, y1, &box);
        mexgr_vb_vtx(x1, y0, &box);
        mexgr_vb_vtx(x0, y1, &box);
        mexgr_vb_vtx(x0, y0, &box);
        HSD_StateInvalidate(-1);
    }
    if (bv->attr.parry_execute_window <= 0) {
        return;
    }
    if (ip->xDAA_byte & 2) {
        mexgr_vb_draw_disc(&ip->pos, bv->attr.parry_radius, &mexgr_vb_ball_color);
    }
    for (f = HSD_GObjPLinkHead[8]; f != NULL; f = f->next) {
        Fighter* fp = GET_FIGHTER(f);
        if (!(fp->x21FC_flag.b6)) {
            continue;
        }
        if (mexgr_vb_guarding(fp)) {
            if (fp->reflecting) {
                Vec3 pos;
                const f32 r = mexgr_vb_reflect_circle(fp, &pos);
                mexgr_vb_draw_disc(&pos, r, &mexgr_vb_reflect_color);
            }
            if (fp->x221B_b0) {
                Vec3 pos;
                Vec3 scale;
                lb_8000B1CC(fp->shield_hit.bone, NULL, &pos);
                HSD_MtxGetScale(fp->shield_hit.bone->mtx, &scale);
                mexgr_vb_draw_disc(&pos, scale.x, &mexgr_vb_shield_color);
            }
        }
        if (fp->motion_id > 0xEB) {
            const f32 frame = fp->cur_anim_frame;
            if (!(frame < 0.0F || frame != frame) &&
                !(frame > (f32) bv->attr.parry_execute_window || frame != frame))
            {
                Vec3 pos;
                const f32 r = mexgr_vb_air_circle(fp, &pos);
                mexgr_vb_draw_disc(&pos, r, &mexgr_vb_airparry_color);
            }
        }
    }
}

/* ---- the parry ---- */

/* Ball_ParryInit (+4960): the parry hit: straight up at parry_up_kb (the knockback formula of the
 * block multiplied by 0.0, so only the base stays), away from the parrier. */
static void mexgr_vb_parry_init(HSD_GObj* ball, const Vec3* from)
{
    Item* ip = GET_ITEM(ball);
    MexgrVbBall* bv = mexgr_vb_ball(ip);
    const s32 up = bv->attr.parry_up_kb;
    f32 dir;
    f64 kb;
    bv->parry = 1;
    if (from->x > ip->pos.x) {
        dir = -1.0F;
    } else {
        dir = 1.0F;
    }
    kb = (f64) ip->xC9C * 0.1;
    kb = kb + (f64) 0.0F;
    kb = kb * (f64) ip->xCC_item_attr->x1C_damage_mul;
    kb = kb * 1.4;
    kb = kb + (f64) 18.0F;
    kb = kb * (f64) 0.0F;
    kb = kb + (f64) up;
    ip->xCC8_knockback = (f32) kb;
    ip->xCAC_angle = 90;
    ip->xCCC_incDamageDirection = dir;
    mexgr_vb_stage_ball_hit(ball, 0);
}

/* Ball_ParryWaitEnter (+4A48): item state 2, still. */
static void mexgr_vb_parry_wait_enter(HSD_GObj* ball)
{
    Item* ip = GET_ITEM(ball);
    MexgrVbBall* bv = mexgr_vb_ball(ip);
    Item_80268E5C(ball, MEXGR_VB_BALL_PARRYWAIT, ITEM_ANIM_UPDATE);
    bv->last_pos = ip->pos;
    ip->x40_vel.x = 0.0F;
    ip->x40_vel.y = 0.0F;
    ip->x40_vel.z = 0.0F;
    bv->vel.x = 0.0F;
    bv->vel.y = 0.0F;
    bv->vel.z = 0.0F;
    bv->vel2.x = 0.0F;
    bv->vel2.y = 0.0F;
    bv->vel2.z = 0.0F;
    ip->on_accessory = mexgr_vb_process_trail;
}

/* Ball_ParryExit (+5730): the parry's expiry timer, the wait state. */
static void mexgr_vb_parry_exit(HSD_GObj* ball)
{
    MexgrVbBall* bv = mexgr_vb_ball(GET_ITEM(ball));
    bv->parry_timer = bv->attr.parry_expire_frames;
    mexgr_vb_state_wait(ball);
}

/* The fighter's take_dmg_2_cb that Fighter_EnterParryAttackAir sets is Ball_ParryExit itself, so
 * the console calls it with the fighter's gobj: it would write the fighter at the ball's offsets
 * and change an item state on a fighter (undefined; it only happens with the parry on, a develop
 * menu setting). Natively the call is logged and does nothing. */
static void mexgr_vb_parry_exit_from_fighter(HSD_GObj* gobj)
{
    static int logged;
    (void) gobj;
    if (!logged) {
        logged = 1;
        OSReport("[mex] Volleyball: Ball_ParryExit called on a fighter (console: undefined); "
                 "skipped\n");
    }
}

/* Fighter_ParryAirAttackPhys (+5780): the first frame an aerial's hitbox is out ends the ball's
 * parry wait; before that the fighter has no physics. */
static void mexgr_vb_parry_air_attack_phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    MexgrVbParryVars* pv = mexgr_vb_parry_vars(fp);
    if (pv->attacked == 0) {
        int k;
        for (k = 0; k < 4; k++) {
            if (fp->x914[k].state != HitCapsule_Disabled) {
                break;
            }
        }
        if (k == 4) {
            return;
        }
        if (pv->ball != NULL) {
            mexgr_vb_parry_exit(pv->ball);
        }
        pv->attacked = 1;
    }
    ft_80084DB0(gobj);
}

/* Fighter_EnterParryAttackAir (+5760) */
static void mexgr_vb_enter_parry_attack_air(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->phys_cb = mexgr_vb_parry_air_attack_phys;
    fp->take_dmg_2_cb = mexgr_vb_parry_exit_from_fighter;
}

/* Fighter_ParryAirAnim (+568C): at the animation's end the ball's parry wait ends, Fall. */
static void mexgr_vb_parry_air_anim(HSD_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        HSD_GObj* ball = mexgr_vb_parry_vars(GET_FIGHTER(gobj))->ball;
        if (ball != NULL) {
            mexgr_vb_parry_exit(ball);
        }
        ftCo_Fall_Enter(gobj);
    }
}

/* Fighter_ParryAirIASA (+56DC): an aerial (through the block's data word holding the retail
 * ftCo_AttackAir_CheckItemThrowInput) keeps the parry state's custom physics. */
static void mexgr_vb_parry_air_iasa(HSD_GObj* gobj)
{
    if (ftCo_AttackAir_CheckItemThrowInput(gobj)) {
        mexgr_vb_enter_parry_attack_air(gobj);
    }
}

/* Fighter_ParryAirPhys (+5728), Fighter_ParryAirColl (+572C): lone blrs. */
static void mexgr_vb_parry_air_nop(HSD_GObj* gobj)
{
    (void) gobj;
}

/* .data.state_parryenterair: a MotionState; Fighter_CustomStateChange reads only its callbacks. */
static MotionState mexgr_vb_state_parryenterair = {
    0x102, 0x00340111, { 0x01000000 },
    mexgr_vb_parry_air_anim, mexgr_vb_parry_air_iasa, mexgr_vb_parry_air_nop,
    mexgr_vb_parry_air_nop, ftCamera_UpdateCameraBox,
};

/* Fighter_CustomStateChange (+537C): motion state 0xEF entered with Ft_MF_SkipAnim, then the
 * animation of motion state `anim_of` (its own MotionState row's animation) started by hand, the
 * motion id 500 and anim id 0 recorded, and `state`'s callbacks. */
static void mexgr_vb_custom_state(HSD_GObj* gobj, FtMotionId anim_of, const MotionState* state,
                                  f32 start, f32 speed, f32 blend)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const MotionState* ms;
    s32 anim;
    if (fp->x18 >= anim_of) {
        ms = &fp->x1C_actionStateList[anim_of];
    } else {
        ms = &fp->x20_actionStateList[anim_of - fp->x18];
    }
    Fighter_ChangeMotionState(gobj, 0xEF, Ft_MF_SkipAnim, 0.0F, 1.0F, 0.0F, NULL);
    anim = ms->anim_id;
    fp->x594_s32 = DP(fp->ft_data->xC)[anim].x10_animCurrFlags;
    fp->motion_id = 500;
    fp->anim_id = 0;
    ftCo_8009E7B4(fp, &fp->x28[ms->anim_id]);
    ftData_80085CD8(fp, fp, ms->anim_id);
    ftAnim_8006EBE8(gobj, start, speed, blend);
    ftAnim_8006E9B4(gobj);
    if (start == 0.0F) {
        ftCo_800C0408(gobj);
        ftAction_80073240(gobj);
    } else {
        ftAction_8007349C(gobj);
    }
    fp->anim_cb = state->anim_cb;
    fp->input_cb = state->input_cb;
    fp->phys_cb = state->phys_cb;
    fp->coll_cb = state->coll_cb;
    fp->cam_cb = state->cam_cb;
}

/* Fighter_EnterParryAir (+4ACC): the air dodge animation (0xE6) from frame 2 at double speed with
 * the parry callbacks, the ball in the motion variables, effects and sound. ftCo_8009F834's float
 * argument is left in f1 by the calls before it on the console (an unset value); effect 0x406
 * takes the function's default path, which does not read it: 0 here. */
static void mexgr_vb_enter_parry_air(HSD_GObj* gobj, HSD_GObj* ball)
{
    Fighter* fp = GET_FIGHTER(gobj);
    MexgrVbParryVars* pv;
    Vec3 offset = { 0.0F, 0.0F, 0.0F };
    Vec3 range = { 0.0F, 0.0F, 0.0F };
    mexgr_vb_custom_state(gobj, 0xE6, &mexgr_vb_state_parryenterair, 2.0F, 2.0F, 0.0F);
    pv = mexgr_vb_parry_vars(fp);
    pv->attacked = 0;
    pv->ball = ball;
    ftCommon_8007E2FC(gobj);
    ftCommon_8007DB24(gobj);
    ftCo_800BFFD0(fp, 0x44, false);
    ft_PlaySFX(fp, 0x80, 0x7F, 0x40);
    ftCo_8009F834(gobj, 0x406, 0, 0, 0, &offset, &range, 0.0F);
}

/* ftCo_80093790 (ft/kinds/ftCommon/ftCo_Guard.c, static there), which Ball_ParryUpdate calls by its
 * retail address for a reflecting fighter: the same calls in the same order as the decomp body,
 * including its Source Port shield color hook. */
static void mexgr_vb_guard_reflect_fx(HSD_GObj* gobj)
{
    extern int mu_te_shield_color(int slot, unsigned int* rgb);
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_JObj* joint;
    unsigned int rgb;
    ftCommon_8007DB24(gobj);
    joint = fp->parts[DP(fp->ft_data->x8)->x11].joint;
    if (mu_te_shield_color(fp->player_idx, &rgb)) {
        efSync_Spawn(1050, gobj, joint, rgb);
    } else {
        const int offset = Player_GetUnk45(fp->player_idx) << 2;
        const u8* color = Fighter_804D650C + offset;
        efSync_Spawn(1050, gobj, joint, (color[0] << 16) | (color[1] << 8) | color[2]);
    }
    fp->x2219_b0 = true;
    ft_PlaySFX(fp, 128, 127, 64);
}

/* Ball_ParryUpdate (+3960), the ball's priority 13 proc: with the parry on, after a parry's expiry
 * count, a ball in the wait state is parried by a reflecting, guarding fighter whose reflect
 * bubble meets the parry radius, or by an air dodging (0xEC) fighter within the parry window whose
 * air circle meets it; the air dodger enters the parry states. */
static void mexgr_vb_parry_update(HSD_GObj* ball)
{
    Item* ip = GET_ITEM(ball);
    MexgrVbBall* bv = mexgr_vb_ball(ip);
    HSD_GObj* f;
    if (bv->attr.parry_execute_window <= 0) {
        return;
    }
    if (bv->parry != 0) {
        if (bv->parry_timer <= 0) {
            return;
        }
        bv->parry_timer -= 1;
        if (bv->parry_timer > 0) {
            return;
        }
        bv->parry = 0;
    }
    if (ip->msid != MEXGR_VB_BALL_WAIT) {
        return;
    }
    for (f = HSD_GObjPLinkHead[8]; f != NULL; f = f->next) {
        Fighter* fp = GET_FIGHTER(f);
        if (mexgr_vb_guarding(fp) && fp->reflecting) {
            Vec3 pos;
            const f32 r = mexgr_vb_reflect_circle(fp, &pos);
            if (mexgr_vb_circles(&pos, r, &ip->pos, bv->attr.parry_radius)) {
                mexgr_vb_parry_init(ball, &pos);
                mexgr_vb_guard_reflect_fx(f);
                continue;
            }
        }
        if (fp->motion_id == 0xEC) {
            const f32 frame = fp->cur_anim_frame;
            if (frame < 0.0F || frame != frame) {
                continue;
            }
            if (frame > (f32) bv->attr.parry_execute_window || frame != frame) {
                continue;
            }
            {
                Vec3 pos;
                const f32 r = mexgr_vb_air_circle(fp, &pos);
                if (mexgr_vb_circles(&pos, r, &ip->pos, bv->attr.parry_radius)) {
                    mexgr_vb_parry_init(ball, &pos);
                    mexgr_vb_parry_wait_enter(ball);
                    mexgr_vb_enter_parry_air(f, ball);
                }
            }
        }
    }
}

/* ---- serve ---- */

/* stateSpawn (+2F14): item state 0, the counters, the ball's damage at dmg_min, its kind 0xD2,
 * its own draw function in link 6, the trail cleared, the parry proc. */
void mexgr_vb_state_spawn(HSD_GObj* ball)
{
    Item* ip = GET_ITEM(ball);
    MexgrVbBall* bv = mexgr_vb_ball(ip);
    Item_80268E5C(ball, MEXGR_VB_BALL_SPAWN, ITEM_ANIM_UPDATE);
    bv->frames = 0;
    bv->hits = 0;
    bv->last_attack = -1;
    bv->parry = 0;
    ip->xC9C = bv->attr.dmg_min;
    bv->last_side = -1;
    bv->vel.x = 0.0F;
    bv->vel.y = 0.0F;
    bv->vel.z = 0.0F;
    bv->vel2.x = 0.0F;
    bv->vel2.y = 0.0F;
    bv->vel2.z = 0.0F;
    bv->last_pos = ip->pos;
    bv->sides = 0;
    ip->kind = MEXGR_VB_BALL_KIND;
    HSD_GObjGXLink_8039084C(ball);
    GObj_SetupGXLink(ball, mexgr_vb_ball_draw, 6, 0);
    mexgr_vb_reset_trail(ball);
    bv->trail_on = 0;
    HSD_GObj_SetupProc(ball, mexgr_vb_parry_update, 0xD);
}

/* SpawnBall (+24E4): stage item 0 in the air 10 above the serve point of `side` (-1: a random
 * side), the ball skipping the barrier collision group, its own copy of the ball attributes, a
 * camera subject, and a new serve marker over it. The block spawns kind 5000 with no parent;
 * m-ex's Item_8026862C hook (codes.gct C2 at Item_8026862C+1C) turns 5000 + n without a fighter
 * parent into item n of the current stage's MxDt list, which is mu_gr_item_kind(n): the kind is
 * given that way here. */
HSD_GObj* mexgr_vb_spawn_ball(s32 side)
{
    MexgrVbNet* np = (MexgrVbNet*) &((Ground*) HSD_GObjGetUserData(Ground_GetMapGObj(3)))->u;
    MexgrVbJudge* jp = mexgr_vb_judge();
    SpawnItem spawn;
    Vec3 pt;
    HSD_GObj* ball;
    Item* ip;
    MexgrVbBall* bv;
    CmSubject* cs;
    HSD_GObj* marker;
    MexgrVbRespawn* rp;
    if (side == -1) {
        side = HSD_Randi(2);
    }
    Ground_801C2D24(side + mexgr_vb_data->court.spawn_genpoint, &pt);
    spawn.x0_parent_gobj = NULL;
    spawn.x4_parent_gobj2 = NULL;
    spawn.kind = mu_gr_item_kind(5000 - 5000);
    spawn.hold_kind = 8;
    spawn.x10 = 0;
    spawn.pos.x = pt.x;
    spawn.pos.y = pt.y + 10.0F;
    spawn.pos.z = 0.0F;
    spawn.prev_pos = spawn.pos;
    spawn.vel.x = 0.0F;
    spawn.vel.y = 0.0F;
    spawn.vel.z = 0.0F;
    spawn.facing_dir = 1.0F;
    spawn.x3C_damage = 0;
    spawn.x3E = 0;
    spawn.x40 = 0;
    spawn.x44_flag.b0 = 1;
    spawn.x48_ground_or_air = GA_Air;
    ball = mu_ak_item_create(&spawn);
    ip = GET_ITEM(ball);
    bv = mexgr_vb_ball(ip);
    ip->x378_itemColl.joint_id_skip = mexgr_vb_data->court.barrier_collgroup;
    bv->colls = np->colls;
    bv->attr = mexgr_vb_data->ball;
    mexgr_vb_state_spawn(ball);
    cs = Camera_80029020();
    cs->ext.h.x = -25.0F;
    cs->ext.h.y = 25.0F;
    cs->ext.v.x = 20.0F;
    cs->ext.v.y = 0.0F;
    cs->target_ext.h.x = -25.0F;
    cs->target_ext.h.y = 25.0F;
    cs->target_ext.v.x = 20.0F;
    cs->target_ext.v.y = 0.0F;
    cs->pos = ip->pos;
    cs->bone_pos = ip->pos;
    ip->x520_cameraBox = cs;
    ip->xDCD_flag.b01 = 1;
    jp->server = side;
    marker = mu_grTSeak_80223908(5);
    rp = (MexgrVbRespawn*) &((Ground*) HSD_GObjGetUserData(marker))->u;
    rp->ball = ball;
    rp->side = side;
    rp->timed = jp->single == 0;
    GET_JOBJ(marker)->translate = pt;
    return ball;
}

/* Ball_AutoReturn (+22B4), one player: a ball landing on the far half is sent back with random
 * damage, angle and knockback growth (the knockback formula in doubles, as the block), touch count
 * cleared and its side "5" (no player). */
void mexgr_vb_ball_auto_return(HSD_GObj* ball, f32 dir)
{
    Item* ip = GET_ITEM(ball);
    MexgrVbBall* bv = mexgr_vb_ball(ip);
    MexgrVbVolleyAttr* va = &mexgr_vb_data->volley;
    s32 kbg;
    s32 dmg;
    s32 angle;
    s32 total;
    f64 kb;
    f64 t;
    kbg = va->autoreturn_kbglow;
    kbg = kbg + HSD_Randi(va->autoreturn_kbghi - kbg);
    va = &mexgr_vb_data->volley;
    dmg = va->autoreturn_dmglow;
    dmg = dmg + HSD_Randi(va->autoreturn_dmghi - dmg);
    va = &mexgr_vb_data->volley;
    angle = va->autoreturn_anglelow;
    angle = angle + HSD_Randi(va->autoreturn_anglehi - angle);
    total = ip->xC9C + dmg;
    ip->xC9C = total;
    kb = (f64) (total * dmg) * 0.05;
    t = (f64) total * 0.1;
    kb = kb + t;
    kb = kb * (f64) ip->xCC_item_attr->x1C_damage_mul;
    kb = kb * 1.4;
    kb = kb + (f64) 18.0F;
    t = (f64) kbg * 0.01;
    kb = kb * t;
    kb = kb + (f64) mexgr_vb_data->volley.autoreturn_bkb;
    ip->xCC8_knockback = (f32) kb;
    ip->xCAC_angle = angle;
    ip->xCCC_incDamageDirection = dir;
    mexgr_vb_stage_ball_hit(ball, 0);
    bv->hits = 0;
    bv->last_side = 5;
    mexgr_vb_play_hit_sfx(ball, 1);
}

/* Ball_OutPlayEnter (+2460): item state 3 after a point, intangible, a dark color overlay, no
 * trail. */
void mexgr_vb_ball_outplay_enter(HSD_GObj* ball)
{
    Item* ip = GET_ITEM(ball);
    Item_80268E5C(ball, MEXGR_VB_BALL_OUTPLAY, ITEM_ANIM_UPDATE);
    ip->xACC_itemHurtbox[0].state = HurtCapsule_Intangible;
    ip->x548_colorOverlay.x30_color_red = 0.0F;
    ip->x548_colorOverlay.x34_color_green = 0.0F;
    ip->x548_colorOverlay.x38_color_blue = 0.0F;
    ip->x548_colorOverlay.x3C_color_alpha = 130.0F;
    ip->x548_colorOverlay.x40_colorblend_red = 0.0F;
    ip->x548_colorOverlay.x44_colorblend_green = 0.0F;
    ip->x548_colorOverlay.x48_colorblend_blue = 0.0F;
    ip->x548_colorOverlay.x4C_colorblend_alpha = 0.0F;
    ip->x548_colorOverlay.x7C_color_enable = 1;
    mexgr_vb_ball(ip)->trail_on = 0;
}
