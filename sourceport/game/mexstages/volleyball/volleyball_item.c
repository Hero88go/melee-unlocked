/* Volleyball's ball (m-ex item kind 249): itFunction block 0 of GrMVb.dat, 1,620 instructions with
 * symbols (`py -3.12 grdis.py ak GrMVb.dat it 0`). Exports 0 (states), 1 (spawned), 2 (destroyed),
 * 7 (dmg_received). SpawnBall (volleyball_ball.c) creates it.
 *
 * Two physics: classic (phys_kind 0: the hit velocity decays like a fighter's knockback, gravity and
 * friction on a second velocity) and modern (1: drag and gravity on one velocity). Map collisions
 * bounce it (lbVector_Mirror on the touched surface's normal) with a bounce sound; a slow enough
 * fall onto the floor lands it. The net is boxes of the stage data (court1_colldesc), tested as the
 * segment from the last to this position against each box grown by the ball's size: the ball rests
 * on, rolls off or bounces from them. The ball's color goes from colshift_colstart to colshift_colend
 * with its damage, which decays back to dmg_min. */
#include "volleyball.h"

#include "../common/mu_gr_mex.h"

#include <math.h>

#include <dolphin/mtx.h>
#include <melee/ft/fighter.h>
#include <melee/ft/types.h>
#include <melee/gr/ground.h>
#include <melee/it/inlines.h>
#include <melee/it/itgroundcoll.h>
#include <melee/it/item.h>
#include <melee/it/itmaplib.h>
#include <melee/it/types.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/lb/lbvector.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/random.h>

/* The PowerPC divw of the block (waitAnim's damage decay period, a develop menu value): a zero
 * divisor gives 0, or -1 for a negative dividend, as the Gekko leaves it (the Dolphin
 * interpreter's rule), where C would trap. */
static s32 mexit_vb_divw(s32 a, s32 b)
{
    if (b == 0 || (a == (s32) 0x80000000 && b == -1)) {
        return a < 0 ? -1 : 0;
    }
    return a / b;
}

/* null (+9E4): the collision callback the block passes to it_8026E15C and it_8026D62C. */
static void mexit_vb_null(HSD_GObj* gobj)
{
    (void) gobj;
}

/* Ball_PlayBounceSFX (+12A8): a random one of the ball's bounce sounds. */
static void mexit_vb_play_bounce_sfx(HSD_GObj* gobj)
{
    MexgrVbBall* bv = mexgr_vb_ball(GET_ITEM(gobj));
    const s32 start = bv->attr.bouncesfx_start;
    mu_gr_mex_sfx(start + HSD_Randi(bv->attr.bouncesfx_num));
}

/* stateLand (+12E4): item state 4 on the ground, no trail. */
static void mexit_vb_state_land(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Item_80268E5C(gobj, MEXGR_VB_BALL_LAND, ITEM_ANIM_UPDATE);
    it_802762B0(ip);
    mexgr_vb_ball(ip)->trail_on = 0;
}

/* Ball_OnParryHit (+13D4), the item copy: as the stage's (volleyball_ball.c), except that the
 * parrier's new x18FD comes from the DamageFall table entry its x18F8 names. */
static void mexit_vb_on_parry_hit(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
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
    fp->dmg.x18FD = (u8) DISC_RAW(Fighter_804D6530[fp->dmg.x18F8 * 2 + 1]);
    ip->exited_hitlag = mexgr_vb_on_parry_launch;
}

/* Ball_Hit (+77C), the item copy. */
static void mexit_vb_ball_hit(HSD_GObj* gobj, s32 from_attack)
{
    mexgr_vb_ball_hit(gobj, from_attack, mexit_vb_on_parry_hit);
}

/* onSpawn (+60), export 1: "li r3, 0" into a void slot. */
static void mexit_vb_spawned(HSD_GObj* gobj)
{
    (void) gobj;
}

/* OnDestroy (+68), export 2: a lone blr. */
static void mexit_vb_destroyed(HSD_GObj* gobj)
{
    (void) gobj;
}

/* onTakeDamage (+6C), export 7: in the parry wait, a hit by anyone but the parrier ends the
 * parrier's parry (its motion variables); then the hit. The block reads the parrier's user data
 * without a check; a parry wait without a parrier (the item's variables are not cleared at spawn)
 * is skipped here instead of reading address 0x2C. */
static bool mexit_vb_dmg_received(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexgrVbBall* bv = mexgr_vb_ball(ip);
    if (ip->msid == MEXGR_VB_BALL_PARRYWAIT && bv->parry_fighter != ip->xCEC_fighterGObj) {
        if (bv->parry_fighter != NULL) {
            MexgrVbParryVars* pv = (MexgrVbParryVars*) &GET_FIGHTER(bv->parry_fighter)->mv;
            pv->ball = NULL;
            pv->attacked = 1;
        } else {
            static int logged;
            if (!logged) {
                logged = 1;
                OSReport("[mex] Volleyball: ball hit in the parry wait with no parrier\n");
            }
        }
    }
    mexit_vb_ball_hit(gobj, 1);
    return false;
}

/* ---- physics ---- */

/* Ball_Physics (+9EC) */
static void mexit_vb_physics(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexgrVbBall* bv = mexgr_vb_ball(ip);
    if (bv->attr.phys_kind == 0) {
        const ftCommonData* cd = p_ftCommonData;
        const f32 gx = bv->vel2.x;
        f32 gy = bv->vel2.y;
        f32 vx = bv->vel.x;
        f32 vy = bv->vel.y;
        f32 fr;
        f32 decay;
        f32 len2;
        f32 t;
        if (ip->msid != MEXGR_VB_BALL_LAND) {
            const f32 term = -bv->attr.physclassic_terminalvel;
            gy = gy - bv->attr.physclassic_gravity;
            if (gy < term) {
                gy = term;
            }
        }
        if (!(gx <= 0.0F)) {
            fr = bv->attr.friction;
            if (gx < fr) {
                fr = 0.0F;
            } else {
                fr = gx - fr;
            }
        } else {
            fr = bv->attr.friction;
            if (gx > -fr) {
                fr = 0.0F;
            } else {
                fr = gx + fr;
            }
        }
        decay = cd->x204_knockbackFrameDecay;
        len2 = (f32) ((f64) vx * (f64) vx + (f64) vy * (f64) vy);
        if (!(decay <= len2)) {
            vx = 0.0F;
            vy = 0.0F;
        } else {
            const f32 angle = atan2f(vy, vx);
            t = cosf(angle);
            t = decay * t;
            vx = vx - t;
            t = sinf(angle);
            t = decay * t;
            vy = vy - t;
        }
        ip->x40_vel.x = vx + fr;
        ip->x40_vel.y = vy + gy;
        bv->vel.x = vx;
        bv->vel.y = vy;
        bv->vel2.x = fr;
        bv->vel2.y = gy;
    } else if (bv->attr.phys_kind == 1) {
        const f32 dir = atan2f(-bv->vel.y, -bv->vel.x);
        f32 vx = bv->vel.x;
        f32 len2;
        f32 t;
        f32 vy;
        len2 = (f32) ((f64) vx * (f64) vx + (f64) bv->vel.y * (f64) bv->vel.y);
        t = len2 * bv->attr.physmodern_drag;
        t = t * cosf(dir);
        bv->vel.x = t + vx;
        vy = bv->vel.y;
        t = sinf(dir);
        len2 = len2 * bv->attr.physmodern_drag;
        len2 = len2 * t;
        len2 = len2 + vy;
        len2 = len2 - bv->attr.physmodern_gravity;
        bv->vel.y = len2;
        ip->x40_vel = bv->vel;
    }
    bv->last_pos = ip->pos;
    {
        HSD_JObj* jobj;
        f32 t;
        lb_80011E24(GET_JOBJ(gobj), &jobj, 1, -1);
        t = ip->x40_vel.x * bv->attr.spin_amt;
        jobj->rotate.y = jobj->rotate.y + t;
        t = ip->x40_vel.y * bv->attr.spin_amt;
        jobj->rotate.x = jobj->rotate.x + t;
    }
}

/* LineVsRect (+1498): the segment p0 to p1 against an axis box (slab test). The hit point, the
 * normal of the side hit (x and y only) and the side bit (0x80 left, 0x40 right, 0x10 bottom,
 * 0x20 top); a corner hit (equal entry times) returns 1 with neither. The block compares each
 * slab time with NAN (INFINITY * 0), which is never equal: those tests never return. */
static bool mexit_vb_line_vs_rect(Vec3* p0, Vec3* p1, const MexgrVbCollRect* r,
                                  Vec3* hit, Vec2* normal, u8* sides)
{
    Vec3 d;
    f32 inv_y;
    f32 inv_x;
    f32 ty_far;
    f32 tx_far;
    f32 ty_near;
    f32 tx_near;
    f32 t_near;
    f32 t_far;
    f32 nan_v;
    f32 t;
    PSVECSubtract(p1, p0, &d);
    if (d.x == 0.0F && d.y == 0.0F) {
        return false;
    }
    inv_y = 1.0F / d.y;
    t = r->y + r->h;
    t = t - p0->y;
    ty_far = t * inv_y;
    nan_v = (f32) (0x1.7e43c8800759cp+996 * 0x1.7e43c8800759cp+996);   /* 1e300 squared */
    nan_v = nan_v * 0.0F;
    if (ty_far == nan_v) {
        return false;
    }
    inv_x = 1.0F / d.x;
    t = r->x + r->w;
    t = t - p0->x;
    tx_far = t * inv_x;
    if (tx_far == nan_v) {
        return false;
    }
    t = r->y - p0->y;
    ty_near = t * inv_y;
    if (ty_near == nan_v) {
        return false;
    }
    t = r->x - p0->x;
    tx_near = t * inv_x;
    if (tx_near == nan_v) {
        return false;
    }
    if (tx_near > tx_far) {
        t = tx_near;
        tx_near = tx_far;
        tx_far = t;
    }
    if (ty_near > ty_far) {
        t = ty_near;
        ty_near = ty_far;
        ty_far = t;
    }
    if (tx_near > ty_far) {
        return false;
    }
    if (tx_far < ty_near) {
        return false;
    }
    t_near = tx_near > ty_near ? tx_near : ty_near;
    t_far = ty_far > tx_far ? tx_far : ty_far;
    if (t_near > 1.0F) {
        return false;
    }
    if (t_far < 0.0F) {
        return false;
    }
    t = d.x * t_near;
    hit->x = t + p0->x;
    t = d.y * t_near;
    hit->y = p0->y + t;
    hit->z = 0.0F;
    if (tx_near > ty_near) {
        if (inv_x < 0.0F) {
            normal->x = 1.0F;
            normal->y = 0.0F;
            *sides |= 0x40;
        } else {
            normal->x = -1.0F;
            normal->y = 0.0F;
            *sides |= 0x80;
        }
        return true;
    }
    if (tx_near >= ty_near) {
        return true;
    }
    if (inv_y < 0.0F) {
        normal->x = 0.0F;
        normal->y = 1.0F;
        *sides |= 0x20;
    } else {
        normal->x = 0.0F;
        normal->y = -1.0F;
        *sides |= 0x10;
    }
    return true;
}

/* Ball_NetCollision (+C84) */
static void mexit_vb_net_collision(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexgrVbBall* bv = mexgr_vb_ball(ip);
    s32 i;
    /* sp+0x44: the normal LineVsRect writes (x and y; lbVector_Mirror reads only those). A corner
     * hit returns 1 without writing it, so the console bounces with the normal a previous box of
     * this call left there, or stack contents on the first box: kept across boxes here, from 0. */
    Vec3 normal = { 0.0F, 0.0F, 0.0F };
    bv->prev_sides = bv->sides;
    bv->sides = 0;
    for (i = 0; i < bv->colls->count; i++) {
        const MexgrVbCollRect* src = &bv->colls->rects[i];
        MexgrVbCollRect box = { 0 };
        Vec3 hit;
        u8 sides = 0;
        Vec3 v;
        f32 t;
        f32 decay;
        f32 bx;
        f32 bw;
        f32 by;
        f32 bh;
        {
            const f32 sx = bv->attr.coll_size_x;
            const f32 sy = bv->attr.coll_size_y;
            t = sx * 0.5F;
            box.x = src->x - t;
            t = sy * 0.5F;
            box.y = src->y - t;
            box.w = src->w + sx;
            box.h = src->h + sy;
        }
        if (!mexit_vb_line_vs_rect(&bv->last_pos, &ip->pos, &box, &hit, (Vec2*) &normal, &sides)) {
            goto merge;
        }
        if ((sides & 0xC0) && src->kind == 2) {
            goto merge;
        }
        decay = p_ftCommonData->x1BC;
        bx = box.x;
        bw = box.w;
        by = box.y;
        bh = box.h;
        Ground_GetMapGObj(2);
        if (bv->attr.phys_kind == 0) {
            v.x = bv->vel.x + bv->vel2.x;
            v.y = bv->vel.y + bv->vel2.y;
            v.z = 0.0F;
        } else {
            v = bv->vel;
        }
        if (sides & 0x20) {
            f32 cx;
            t = bw * 0.5F;
            cx = t + bx;
            if ((u32) src->kind <= 1) {
                f32 vy = v.y;
                if (vy > bv->attr.land_vel) {
                    /* slow on the top: rest on it and slide off its slope */
                    f32 a;
                    f32 s;
                    ip->pos.y = (f32) ((f64) normal.y * 0x1.47ae147ae147bp-7 /* 0.01 */ + (f64) hit.y);
                    if (cx >= hit.x) {
                        a = (f32) ((f64) src->angle + MEXGR_VB_PI);
                    } else {
                        a = -src->angle;
                    }
                    s = sinf(a);
                    s = vy * s;
                    if (s < 0.0F) {
                        s = -s;
                    }
                    if (cx >= hit.x) {
                        s = v.x - s;
                    } else {
                        s = s + v.x;
                    }
                    v.x = s;
                    v.y = 0.0F;
                    if (bv->attr.phys_kind == 0) {
                        bv->vel.x = 0.0F;
                        bv->vel.y = 0.0F;
                        bv->vel2 = v;
                    } else if (bv->attr.phys_kind == 1) {
                        bv->vel = v;
                    }
                    goto net_hit;
                }
                {
                    /* fast: bounce from the top's slope */
                    f32 top = by + bh;
                    f32 a;
                    f32 nx;
                    f32 s;
                    Vec3 n;
                    if (cx >= hit.x) {
                        a = (f32) ((f64) src->angle + MEXGR_VB_PI);
                        nx = cx + cosf(a);
                        s = sinf(a);
                        s = top + s;
                        top = top - s;
                        top = -top;
                        cx = cx - nx;
                    } else {
                        a = -src->angle;
                        nx = cx + cosf(a);
                        s = sinf(a);
                        s = top + s;
                        top = s - top;
                        top = -top;
                        cx = nx - cx;
                    }
                    n.x = top;
                    n.y = cx;
                    n.z = 0.0F;
                    lbVector_Normalize(&n);
                    normal.x = n.x;
                    normal.y = n.y;
                    if ((sides & 0x20) && src->kind == 2) {
                        goto land;
                    }
                    goto bounce;
                }
            }
            if (src->kind == 2) {
                goto land;
            }
            goto bounce;
        land:
            bv->landed = 1;
            if (v.y > bv->attr.land_vel) {
                mexit_vb_state_land(gobj);
            } else {
                it_802762BC(ip);
            }
        }
    bounce:
        lbVector_Mirror(&v, &normal);
        v.x = v.x * decay;
        v.y = v.y * decay;
        if (bv->attr.phys_kind == 0) {
            bv->vel2.x = 0.0F;
            bv->vel2.y = 0.0F;
        }
        bv->vel = v;
        mexit_vb_play_bounce_sfx(gobj);
        if (sides & 0x30) {
            f32 edge;
            ip->pos.y = (f32) ((f64) normal.y * 0x1.47ae147ae147bp-7 + (f64) hit.y);
            edge = box.x + box.w;
            if (ip->pos.x > edge) {
                ip->pos.x = edge;
            } else if (ip->pos.x < box.x) {
                ip->pos.x = box.x;
            }
        } else if (sides & 0xC0) {
            f32 edge;
            ip->pos.x = (f32) ((f64) normal.x * 0x1.47ae147ae147bp-7 + (f64) hit.x);
            edge = box.y + box.h;
            if (ip->pos.y > edge) {
                ip->pos.y = edge;
            } else if (ip->pos.y < box.y) {
                ip->pos.y = box.y;
            }
        }
    net_hit:
        if (hit.y >= 12.5F) {
            bv->net_hit = 1;
        }
    merge:
        bv->sides = (u8) (bv->sides | (sides & 0xF0));
    }
}

/* The map bounce of waitColl (+3E4): with the ball touching a floor, ceiling or wall, the velocity
 * mirrored on that surface's normal and scaled (classic: p_ftCommonData->x1BC on the sum of both
 * velocities, the second cleared; modern: physmodern_bouncedecay), a bounce sound; a floor touch
 * marks the ball landed, and lands it when it falls slower than land_vel. */
static void mexit_vb_map_bounce(HSD_GObj* gobj, Item* ip, MexgrVbBall* bv, bool classic)
{
    const s32 env = ip->x378_itemColl.env_flags;
    Vec3* n;
    Vec3 v;
    f32 k;
    if (((env & ~0x1000) & 0x1FFFF) == 0) {
        return;
    }
    if (classic) {
        k = p_ftCommonData->x1BC;
        v.x = bv->vel.x + bv->vel2.x;
        v.y = bv->vel.y + bv->vel2.y;
        v.z = 0.0F;
    } else {
        k = bv->attr.physmodern_bouncedecay;
        v = bv->vel;
    }
    if (env & 0x18000) {
        bv->landed = 1;
        if (v.y > bv->attr.land_vel) {
            mexit_vb_state_land(gobj);
            return;
        }
        n = &ip->x378_itemColl.floor.normal;
        it_802762BC(ip);
    } else if (env & 0x6000) {
        n = &ip->x378_itemColl.ceiling.normal;
    } else if (env & 0xFC0) {
        n = &ip->x378_itemColl.right_facing_wall.normal;
    } else {
        /* env & 0x3F: the masks above cover every bit the first test lets through */
        n = &ip->x378_itemColl.left_facing_wall.normal;
    }
    if (classic) {
        bv->vel2.x = 0.0F;
        bv->vel2.y = 0.0F;
    }
    lbVector_Mirror(&v, n);
    v.x = k * v.x;
    v.y = k * v.y;
    bv->vel.x = v.x;
    bv->vel.y = v.y;
    mexit_vb_play_bounce_sfx(gobj);
}

/* ---- states ---- */

/* spawnAnim (+D0), and the other states' "li r3, 0" animation callbacks */
static bool mexit_vb_false(HSD_GObj* gobj)
{
    (void) gobj;
    return false;
}

/* spawnPhys (+D8), explodePhys (+750): the last position kept. */
static void mexit_vb_keep_pos(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    mexgr_vb_ball(ip)->last_pos = ip->pos;
}

/* spawnColl (+FC) */
static bool mexit_vb_spawn_coll(HSD_GObj* gobj)
{
    it_8026E15C(gobj, mexit_vb_null);
    return false;
}

/* waitAnim (+128): every dmg_decay_rate frames the damage decays by dmg_decay_amt down to
 * dmg_min; the model's material color follows the damage (colshift_colstart to colshift_colend
 * over colshift_pctend). */
static bool mexit_vb_wait_anim(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexgrVbBall* bv = mexgr_vb_ball(ip);
    const s32 rate = bv->attr.dmg_decay_rate;
    const s32 amt = bv->attr.dmg_decay_amt;
    const s32 min = bv->attr.dmg_min;
    HSD_JObj* jobj;
    HSD_Material* mat;
    GXColor end;
    GXColor start;
    f32 d;
    f32 top;
    f32 t;
    f32 k;
    bv->wait_frames += 1;
    bv->frames += 1;
    if (bv->frames == mexit_vb_divw(bv->frames, rate) * rate) {
        const s32 left = ip->xC9C - amt;
        ip->xC9C = left < min ? min : left;
    }
    lb_80011E24(GET_JOBJ(gobj), &jobj, 1, -1);
    end = bv->attr.colshift_colend;
    start = bv->attr.colshift_colstart;
    mat = jobj->u.dobj->mobj->mat;
    d = (f32) ip->xC9C;
    top = (f32) (bv->attr.dmg_min + bv->attr.colshift_pctend);
    if (d > top) {
        d = top;
    }
    t = (f32) bv->attr.dmg_min;
    t = d - t;
    t = t / top;
    k = (f32) ((s32) end.r - (s32) start.r);
    k = k * t;
    mat->diffuse.r = (u8) (s32) (k + (f32) (s32) start.r);
    /* the block adds green's start to blue and blue's start to green (equal in the file's colors) */
    k = (f32) ((s32) end.b - (s32) start.b);
    k = k * t;
    mat->diffuse.b = (u8) (s32) (k + (f32) (s32) start.g);
    k = (f32) ((s32) end.g - (s32) start.g);
    k = k * t;
    mat->diffuse.g = (u8) (s32) (k + (f32) (s32) start.b);
    k = (f32) ((s32) end.a - (s32) start.a);
    k = k * t;
    mat->diffuse.a = (u8) (s32) (k + (f32) (s32) start.a);
    return false;
}

/* waitPhys (+3C0), Ball_OutPlayPhys (+6A8), landPhys (+6F8) */
static void mexit_vb_phys(HSD_GObj* gobj)
{
    mexit_vb_physics(gobj);
}

/* waitColl (+3E4): the net, the map (it_8026E15C), then the map bounce of the ball's physics. */
static bool mexit_vb_wait_coll(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexgrVbBall* bv = mexgr_vb_ball(ip);
    Ground_GetMapGObj(2);
    mexit_vb_net_collision(gobj);
    it_8026E15C(gobj, mexit_vb_null);
    if (bv->attr.phys_kind == 0) {
        mexit_vb_map_bounce(gobj, ip, bv, true);
    } else if (bv->attr.phys_kind == 1) {
        mexit_vb_map_bounce(gobj, ip, bv, false);
    }
    return false;
}

/* Ball_ParryWaitPhys (+64C): "li r3, 0" into a void slot. */
static void mexit_vb_phys_nop(HSD_GObj* gobj)
{
    (void) gobj;
}

/* Ball_ParryWaitColl (+654): the net and the map, no bounce. */
static bool mexit_vb_parrywait_coll(HSD_GObj* gobj)
{
    Ground_GetMapGObj(2);
    mexit_vb_net_collision(gobj);
    it_8026E15C(gobj, mexit_vb_null);
    return false;
}

/* Ball_OutPlayColl (+6CC) */
static bool mexit_vb_outplay_coll(HSD_GObj* gobj)
{
    mexit_vb_wait_coll(gobj);
    return false;
}

/* landColl (+71C): the ground collision of a landed item. */
static bool mexit_vb_land_coll(HSD_GObj* gobj)
{
    it_8026D62C(gobj, mexit_vb_null);
    return false;
}

/* item_state_table (+0) */
static ItemStateTable mexit_vb_states[6] = {
    { 0, mexit_vb_false, mexit_vb_keep_pos, mexit_vb_spawn_coll },
    { 1, mexit_vb_wait_anim, mexit_vb_phys, mexit_vb_wait_coll },
    { 1, mexit_vb_false, mexit_vb_phys_nop, mexit_vb_parrywait_coll },
    { 1, mexit_vb_false, mexit_vb_phys, mexit_vb_outplay_coll },
    { 0, mexit_vb_false, mexit_vb_phys, mexit_vb_land_coll },
    { 2, mexit_vb_false, mexit_vb_keep_pos, mexit_vb_false },
};

/* Exports 0, 1, 2, 7. */
const ItemLogicTable mexit_vb_items[1] = {
    {
        .states = mexit_vb_states,
        .spawned = mexit_vb_spawned,
        .destroyed = mexit_vb_destroyed,
        .dmg_received = mexit_vb_dmg_received,
    },
};
