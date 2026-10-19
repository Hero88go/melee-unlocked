/* Volleyball (internal 77, external 294, "/GrMVb.dat"): the court part of the grFunction block
 * (volleyball.h). Exports 1 (map_gobjs), 3 (onInit), 5 (onLoad); the other slots keep the MxDt
 * row's retail words.
 *
 * Map gobjs: 0 the arena (Sheik's target stage row 0 callbacks), 1 the court (Sheik's row 1), 2 the
 * judge (rules, score, fighters' hitboxes across the net), 3 the net (its joint swings when the ball
 * hits it), 4 the score board, 5 the serve marker over the next ball, 6 the ceiling (an animation
 * when the ball touches its line), 7 a blimp passing at random times, 8 the "deuce" sign (a camera
 * gobj, flag b2).
 *
 * Rules, as the block plays them: the ball is stage item 0; a side scores when the ball lands on the
 * other half, when the other side touches it more than ball_maxhit times in a row, or when the
 * serve marker's timer runs out on its side. Points to score_max (11), deuce with advantage at
 * score_max - 1 each, three games win the match (gm_8016B364: the losers' stocks go to 0). With one
 * player (gm_8016B558() == 1) there is no score: a ball landing on the far side comes back by
 * itself. Fighters never take damage (percent reset every frame) and hitboxes never reach the other
 * side of the net. */
#include "volleyball.h"

#include "../../akaneia/mu_ak_fighter.h"
#include "../common/mu_gr_mex.h"
#include "../peachcastle64/wp1_common.h"

#include <string.h>

#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <melee/cm/camera.h>
#include <melee/db/db.h>
#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/types.h>
#include <melee/gm/gmvs.h>
#include <melee/gr/grcorneria.h>
#include <melee/gr/granime.h>
#include <melee/gr/grdisplay.h>
#include <melee/gr/grlib.h>
#include <melee/gr/ground.h>
#include <melee/gr/inlines.h>
#include <melee/gr/types.h>
#include <melee/if/ifstatus.h>
#include <melee/it/inlines.h>
#include <melee/it/item.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbcollision.h>
#include <melee/mn/types.h>
#include <melee/mp/mplib.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/axdriver.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/state.h>

extern Camera game_camera;

_Static_assert(sizeof(MexgrVbJudge) <= sizeof(((Ground*) 0)->u), "judge fits the Ground union");
_Static_assert(sizeof(MexgrVbNet) <= sizeof(((Ground*) 0)->u), "net fits the Ground union");
_Static_assert(sizeof(MexgrVbRespawn) <= sizeof(((Ground*) 0)->u), "marker fits the Ground union");
_Static_assert(sizeof(MexgrVbScore) <= sizeof(((Ground*) 0)->u), "board fits the Ground union");

/* .bss.volley_data */
MexgrVbData* mexgr_vb_data;

/* ---- .data: the attribute blocks Volley_Start copies ---- */

static const MexgrVbVolleyAttr mexgr_vb_volley_attr = {
    11, 4, 45, 0, 0, 0x148, 6, 0x147, 10, 11, 12, 30, 70, 11, 14, 100, 100, 0, 480, 336,
};

static const MexgrVbCourtAttr mexgr_vb_court_attr = {
    0x24, 0x26, 0, 2, 0, 5, 10.0F, 0x1.19999ap+0F /* 1.1 */,
};

static const MexgrVbNetAttr mexgr_vb_net_attr = {
    6.0F, 0x1.333334p-2F /* 0.3 */, 0x1.ccccccp-1F /* 0.9 */, 2.0F,
};

static const MexgrVbBallAttr mexgr_vb_ball_attr = {
    .phys_kind = 0,
    .physclassic_gravity = 0x1.a1cac0p-5F,       /* 0.051 */
    .physclassic_terminalvel = 0x1.266666p+1F,   /* 2.3 */
    .physmodern_gravity = 0x1.47ae14p-4F,        /* 0.08 */
    .physmodern_drag = 0x1.26e978p-7F,           /* 0.009 */
    .physmodern_bouncedecay = 0x1.ccccccp-1F,    /* 0.9 */
    .friction = 0x1.4e3bcep-8F,                  /* 0.0051 */
    .dmg_min = 95,
    .dmg_decay_amt = 1,
    .dmg_decay_rate = 10,
    .parry_up_kb = 75,
    .parry_expire_frames = 10,
    .parry_hit_freeze_frames = 5,
    .parry_radius = 3.0F,
    .parry_execute_window = 0,
    .land_vel = -0x1.051eb8p-1F,                 /* -0.51 */
    .spin_amt = 0x1.99999ap-4F,                  /* 0.1 */
    .colshift_colstart = { 0xFF, 0xFF, 0xFF, 0xFF },
    .colshift_colend = { 0xFF, 0x50, 0x50, 0xFF },
    .colshift_pctend = 100,
    .hitsfx_num = 5,
    .hitsfx_start = 0,
    .bouncesfx_num = 5,
    .bouncesfx_start = 5,
    .coll_size_x = 6.5F,
    .coll_size_y = 6.5F,
    .trail_color0 = { 0x28, 0xFF, 0xFF, 0xFF },
    .trail_color1 = { 0xFF, 0x7D, 0xFF, 0xFF },
    .trail_color2 = { 0xFF, 0x63, 0x63, 0xFF },
    .trail_blend_mid = 0.5F,
    .trail_width_a = 3.0F,
    .trail_width_b = -3.0F,
};

/* .data.court1_colldesc and .data.court_colldescs */
static MexgrVbCollRect mexgr_vb_court1_colldesc[2] = {
    { -0.5F, 0.0F, 1.0F, 22.5F, 1, 0x1.a3ff08p-2F /* 0.410153 */ },
    { -1.0F, -1.0F, 2.0F, 1.0F, 2, 0.0F },
};
static MexgrVbCollDescs mexgr_vb_court_colldescs = { 2, mexgr_vb_court1_colldesc };

/* .rodata.gobjs_to_scale.2 */
static const u8 mexgr_vb_gobjs_to_scale[3] = { 0, 2, 6 };

/* ---- helpers ---- */

MexgrVbJudge* mexgr_vb_judge(void)
{
    return mexgr_vb_judge_of(HSD_GObjGetUserData(Ground_GetMapGObj(2)));
}

/* One byte of the judge's console layout, gp+0x10C .. gp+0x128 (see volleyball.h). */
static s8 mexgr_vb_judge_byte(MexgrVbJudge* jp, s32 off)
{
    if (off >= 0x10C && off < 0x110) {
        return (s8) (((u32) jp->rally >> (8 * (3 - (off - 0x10C)))) & 0xFF);
    }
    if (off >= 0x110 && off < 0x116) {
        return jp->team_side[off - 0x110];
    }
    if (off >= 0x116 && off < 0x11C) {
        return jp->slot_side[off - 0x116];
    }
    if (off >= 0x11C && off < 0x120) {
        return (s8) (((u32) jp->blimp_timer >> (8 * (3 - (off - 0x11C)))) & 0xFF);
    }
    if (off == 0x120) {
        return (s8) jp->blimp_idx;
    }
    if (off >= 0x121 && off < 0x129) {
        return (s8) jp->blimp_order[off - 0x121];
    }
    {
        static int logged;
        if (!logged) {
            logged = 1;
            OSReport("[mex] Volleyball: side table read at gp+0x%X, outside the judge; 0\n",
                     (int) off);
        }
    }
    return 0;
}

s8 mexgr_vb_team_side(MexgrVbJudge* jp, s32 index)
{
    return mexgr_vb_judge_byte(jp, 0x110 + index);
}

s8 mexgr_vb_slot_side(MexgrVbJudge* jp, s32 index)
{
    return mexgr_vb_judge_byte(jp, 0x116 + index);
}

/* 80223ABC (Sheik's stageGObj1_OnInit, the table names its retail address): collision joints
 * bound, animation set 0. */
static void mexgr_vb_tseak_coll_anim(Ground_GObj* gobj)
{
    Ground_InitMapCollAndAnim(gobj);
}

/* The digit animations of the score board: HSD_JObjReqAnimByFlags(joint, 0x10, frame),
 * HSD_JObjAnim, then every texture animation of the joint stopped. */
static void mexgr_vb_set_digit(HSD_JObj* joint, f32 frame)
{
    HSD_JObjReqAnimByFlags(joint, 0x10, frame);
    HSD_JObjAnim(joint);
    HSD_ForeachAnim(joint, JOBJ_TYPE, TOBJ_MASK, HSD_AObjStopAnim, AOBJ_ARG_AOV, NULL, NULL);
}

/* ---- the judge (map gobj 2) ---- */

static void mexgr_vb_judge_think(HSD_GObj* gobj);

/* Judge_OnCreate (+124) */
static void mexgr_vb_judge_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    MexgrVbJudge* jp = mexgr_vb_judge_of(gp);
    u8* slot;
    grAnime_801C8138(gobj, gp->map_id, 0);
    jp->state = 0;
    jp->score[0] = 0;
    jp->score[1] = 0;
    jp->adv[0] = 0;
    jp->adv[1] = 0;
    jp->games[0] = 0;
    jp->games[1] = 0;
    jp->rally = 0;
    jp->last_side = -1;
    jp->ball = NULL;
    jp->blimp_idx = 0;
    jp->blimp_timer = HSD_Randi(600) + 1200;
    jp->blimp_order[0] = 0;
    jp->blimp_order[1] = 1;
    jp->blimp_order[2] = 2;
    jp->blimp_order[3] = 3;
    jp->blimp_order[4] = 4;
    jp->blimp_order[5] = 5;
    jp->blimp_order[6] = 6;
    jp->blimp_order[7] = 7;
    /* each entry swapped with a random one (the draw before the load, as the block) */
    for (slot = &jp->blimp_order[0]; slot != &jp->blimp_order[8]; slot++) {
        const int j = HSD_Randi(8);
        const u8 tmp = *slot;
        *slot = jp->blimp_order[j];
        jp->blimp_order[j] = tmp;
    }
    HSD_GObj_SetupProc(gobj, mexgr_vb_judge_think, 0xC);
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
}

/* Judge_OnFrame (+240): a blimp (map gobj 7) every 1200 to 1799 frames, its route the next of the
 * shuffled animation sets; then the collision sync. */
static void mexgr_vb_judge_frame(Ground_GObj* gobj)
{
    MexgrVbJudge* jp = mexgr_vb_judge_of(HSD_GObjGetUserData(gobj));
    if (Ground_GetMapGObj(7) == NULL) {
        jp->blimp_timer -= 1;
        if (jp->blimp_timer <= 0) {
            HSD_GObj* blimp = mu_grTSeak_80223908(7);
            Ground* bgp = HSD_GObjGetUserData(blimp);
            u8 idx;
            grAnime_801C8138(blimp, bgp->map_id, jp->blimp_order[jp->blimp_idx]);
            idx = (u8) (jp->blimp_idx + 1);
            if (idx > 7) {
                idx = 0;
            }
            jp->blimp_idx = idx;
            jp->blimp_timer = HSD_Randi(600) + 1200;
        }
    }
    Ground_UpdateMapColl(gobj);
    lb_800115F4();
}

/* Judge_IncrementScore (+1D0C): a point for `scorer`; `server` serves next. */
void mexgr_vb_increment_score(HSD_GObj* judge, s32 scorer, s32 server)
{
    MexgrVbJudge* jp = mexgr_vb_judge_of(HSD_GObjGetUserData(judge));
    MexgrVbData* vd = mexgr_vb_data;
    HSD_GObj* board;
    s32 max1;
    jp->state = 3;
    jp->wait = vd->volley.landwait_timer;
    if (jp->single != 0) {
        return;
    }
    jp->server = server;
    max1 = vd->volley.score_max - 1;
    if (jp->score[0] == max1 && max1 == jp->score[1]) {
        /* deuce: an advantage first, the point with it */
        if (jp->adv[scorer] != 0) {
            jp->score[scorer] += 1;
        } else if (jp->adv[server] == 1) {
            jp->adv[server] = 0;
        } else {
            jp->adv[scorer] = 1;
        }
    } else {
        jp->score[scorer] += 1;
        max1 = vd->volley.score_max - 1;
        if (jp->score[0] == max1 && max1 == jp->score[1]) {
            mu_grTSeak_80223908(8);   /* the "deuce" sign */
        }
    }
    board = Ground_GetMapGObj(4);
    mexgr_vb_score_of(HSD_GObjGetUserData(board))->dirty = 1;
    {
        const s32 max = mexgr_vb_data->volley.score_max;
        if (jp->score[0] == max || max == jp->score[1]) {
            int i;
            mu_gr_mex_sfx(mexgr_vb_data->volley.game_sfx);
            Ground_801C53EC(0x145);
            for (i = 10; i != 0; i--) {
                Vec3 pos;
                f32 r;
                f32 k;
                s32 n;
                lb_8000B1CC(Ground_801C3FA4(board, 0xF), NULL, &pos);
                r = HSD_Randf();
                n = HSD_Randi(40);
                r = r * -20.0F;
                k = (f32) n;
                r = r + k;
                pos.x = pos.x + r;
                r = HSD_Randf();
                n = HSD_Randi(40);
                r = r * -20.0F;
                k = (f32) n;
                r = r + k;
                pos.y = pos.y + r;
                grLib_801C96F8(0x7532, 0x1E, &pos);
                pos.x = -pos.x;
                grLib_801C96F8(0x7532, 0x1E, &pos);
            }
            return;
        }
    }
    mu_gr_mex_sfx(mexgr_vb_data->volley.point_sfx);
    {
        MexgrVbVolleyAttr* va = &mexgr_vb_data->volley;
        if (jp->rally >= va->crowdupset_rallymin && jp->rally <= va->crowdupset_rallymax) {
            lbAudioAx_8002411C(va->crowdupset_sfx);
        } else if (jp->rally >= va->crowdcheer_rallymin) {
            lbAudioAx_8002411C(va->crowdcheer_sfx);
        }
    }
}

/* stateExplode (+1C98): the ball hidden, intangible and still, item state 5. */
void mexgr_vb_state_explode(HSD_GObj* ball)
{
    Item* ip = GET_ITEM(ball);
    HSD_JObjSetFlagsAll(GET_JOBJ(ball), JOBJ_HIDDEN);
    ip->xACC_itemHurtbox[0].state = HurtCapsule_Intangible;
    ip->x40_vel.x = 0.0F;
    ip->x40_vel.y = 0.0F;
    ip->x40_vel.z = 0.0F;
    Item_80268E5C(ball, MEXGR_VB_BALL_EXPLODE, ITEM_ANIM_UPDATE);
    mexgr_vb_ball(ip)->trail_on = 0;
}

/* Judge_Think (+1030), the judge's priority 12 proc. */
static void mexgr_vb_judge_think(HSD_GObj* gobj)
{
    MexgrVbJudge* jp = mexgr_vb_judge_of(HSD_GObjGetUserData(gobj));
    Vec3 net;
    HSD_GObj* cur;
    bool teams;

    ifStatus_802F6898();
    if (jp->ball != NULL) {
        mexgr_vb_ball(GET_ITEM(jp->ball))->attr = mexgr_vb_data->ball;
    }
    Ground_801C2D24(mexgr_vb_data->court.netpos_genpoint, &net);

    switch (jp->state) {
    case 1:
        if (GET_ITEM(jp->ball)->msid == MEXGR_VB_BALL_WAIT) {
            jp->state = 2;
        }
        break;
    case 2: {
        HSD_GObj* ball = jp->ball;
        Item* ip = GET_ITEM(ball);
        MexgrVbBall* bv = mexgr_vb_ball(ip);
        s32 side;
        if (gm_8016B168() == 1) {
            side = mexgr_vb_team_side(jp, bv->last_side);
        } else {
            side = mexgr_vb_slot_side(jp, bv->last_side);
        }
        if (jp->last_side != side) {
            jp->last_side = side;
            jp->rally += 1;
            mexgr_vb_score_of(HSD_GObjGetUserData(Ground_GetMapGObj(4)))->dirty = 1;
        }
        if (bv->hits > mexgr_vb_data->volley.ball_maxhit) {
            /* too many touches: the other side scores, this side serves */
            s32 scorer = 1;
            mexgr_vb_state_explode(ball);
            if (side != 0) {
                side = 1;
                scorer = 0;
            }
            lbAudioAx_8002411C(mexgr_vb_data->volley.crowdupset_sfx);
            mexgr_vb_increment_score(gobj, scorer, side);
            break;
        }
        if (bv->landed == 0) {
            break;
        }
        bv->landed = 0;
        {
            /* the half the ball landed on: 1 at or right of the net (and unordered), as the
             * block's mfcr of "x < net" */
            const s32 right = !(ip->pos.x < net.x);
            if (jp->single != 0 && right != jp->single_side) {
                mexgr_vb_ball_auto_return(ball, ip->pos.x < net.x ? -1.0F : 1.0F);
                break;
            }
            if (right == 0) {
                mexgr_vb_increment_score(gobj, 1, 0);
            } else {
                mexgr_vb_increment_score(gobj, 0, 1);
            }
            mexgr_vb_ball_outplay_enter(ball);
        }
        break;
    }
    case 3: {
        s32 max;
        s32 won;
        jp->wait -= 1;
        if (jp->wait > 0) {
            break;
        }
        max = mexgr_vb_data->volley.score_max;
        if (jp->score[0] == max) {
            won = 0;
        } else if (max == jp->score[1]) {
            won = 1;
        } else {
            /* the next point */
            jp->state = 1;
            Item_8026A8EC(jp->ball);
            jp->ball = NULL;
            jp->rally = 0;
            jp->last_side = -1;
            if (jp->single != 0) {
                jp->ball = mexgr_vb_spawn_ball(jp->single_side);
            } else {
                jp->ball = mexgr_vb_spawn_ball(jp->server);
            }
            mexgr_vb_score_of(HSD_GObjGetUserData(Ground_GetMapGObj(4)))->dirty = 1;
            break;
        }
        jp->games[won] += 1;
        if (jp->games[won] == 3) {
            /* the match: every fighter on the losing half loses its stocks */
            Vec3 at;
            HSD_GObj* f;
            gm_8016B33C(5);
            gm_8016B328();
            gm_8016B364(0x144);
            Ground_801C2D24(mexgr_vb_data->court.netpos_genpoint, &at);
            for (f = HSD_GObjPLinkHead[8]; f != NULL; f = f->next) {
                Fighter* fp = GET_FIGHTER(f);
                bool lose;
                if (jp->score[0] > jp->score[1]) {
                    lose = fp->cur_pos.x > at.x;
                } else {
                    lose = fp->cur_pos.x < at.x;
                }
                if (lose) {
                    Player_SetStocks(fp->player_idx, 0);
                    Player_SetFallsByIndex(fp->player_idx, fp->is_sub_fighter, 1);
                }
            }
            break;
        }
        /* the next game */
        jp->state = 1;
        Item_8026A8EC(jp->ball);
        jp->ball = NULL;
        jp->rally = 0;
        jp->last_side = -1;
        jp->score[0] = 0;
        jp->score[1] = 0;
        jp->adv[0] = 0;
        jp->adv[1] = 0;
        jp->ball = mexgr_vb_spawn_ball(jp->server);
        mexgr_vb_score_of(HSD_GObjGetUserData(Ground_GetMapGObj(4)))->dirty = 1;
        break;
    }
    default:
        break;
    }

    /* Items other than the ball that move inside the net's band go away. */
    cur = HSD_GObjPLinkHead[9];
    while (cur != NULL) {
        Item* ip = GET_ITEM(cur);
        HSD_GObj* item = cur;
        f32 half;
        f32 lo;
        f32 hi;
        cur = cur->next;
        if (ip->kind == MEXGR_VB_BALL_KIND) {
            continue;
        }
        half = mexgr_vb_data->court.itembarrier_width * 0.5F;
        lo = net.x - half;
        if (ip->pos.x <= lo) {
            continue;
        }
        hi = net.x + half;
        if (ip->pos.x >= hi) {
            continue;
        }
        if (ip->x40_vel.x != 0.0F || ip->x40_vel.y != 0.0F) {
            Item_8026A8EC(item);
        }
    }

    /* Fighters: a hitbox that crosses the net is cancelled; every live hitbox counts as having hit
     * every fighter of the other side already; nobody keeps damage. */
    teams = gm_8016B168();
    {
        HSD_GObj* f;
        for (f = HSD_GObjPLinkHead[8]; f != NULL; f = f->next) {
            Fighter* fp = GET_FIGHTER(f);
            HSD_GObj* o;
            int k;
            for (k = 0; k < 4; k++) {
                HitCapsule* hb = &fp->x914[k];
                s32 side;
                f32 half;
                f32 edge;
                if (hb->state == HitCapsule_Disabled) {
                    continue;
                }
                if (!teams) {
                    side = mexgr_vb_slot_side(jp, fp->player_idx);
                } else {
                    side = mexgr_vb_team_side(jp, fp->team);
                }
                if (side == 0) {
                    half = hb->scale * 0.5F;
                    edge = half + hb->x4C.x;
                    if (!(edge > net.x)) {
                        edge = half + hb->x58.x;
                        if (!(net.x < edge)) {
                            continue;
                        }
                    }
                } else if (side == 1) {
                    half = hb->scale * 0.5F;
                    edge = hb->x4C.x - half;
                    if (!(edge < net.x)) {
                        edge = hb->x58.x - half;
                        if (!(net.x > edge)) {
                            continue;
                        }
                    }
                } else {
                    continue;
                }
                hb->state = HitCapsule_Disabled;
                efSync_Spawn(0x41C, NULL, &hb->x4C);
                HSD_AudioSFXStartParam(0x6A, 0x80, 0x80, 0x9C, 7);
            }
            for (o = HSD_GObjPLinkHead[8]; o != NULL; o = o->next) {
                Fighter* other;
                if (f == o) {
                    continue;
                }
                other = GET_FIGHTER(o);
                if (!teams) {
                    if (mexgr_vb_slot_side(jp, other->player_idx) ==
                        mexgr_vb_slot_side(jp, fp->player_idx))
                    {
                        continue;
                    }
                } else if (other->team == fp->team) {
                    continue;
                }
                for (k = 0; k < 4; k++) {
                    HitCapsule* hb = &fp->x914[k];
                    if (hb->state == HitCapsule_Disabled) {
                        continue;
                    }
                    if (!lbColl_8000ACFC(other, hb)) {
                        lbColl_80008688(hb, 0, other);
                    }
                }
            }
            fp->dmg.x1830_percent = 0.0F;
            Player_SetHUDDamage(fp->player_idx, 0);
        }
    }
    {
        int slot;
        for (slot = 0; slot != 6; slot++) {
            if (Player_GetPlayerSlotType(slot) == Gm_PKind_NA) {
                continue;
            }
            memset(Player_GetStaleMoveTableIndexPtr(slot), 0, 0x2C);
        }
    }
}

/* Judge_OnGo (+2138), registered with Ground_801C10B8: the first ball. */
static void mexgr_vb_judge_go(HSD_GObj* gobj)
{
    MexgrVbJudge* jp = mexgr_vb_judge_of(HSD_GObjGetUserData(gobj));
    if (jp->single != 0) {
        jp->ball = mexgr_vb_spawn_ball(jp->single_side);
    } else {
        jp->ball = mexgr_vb_spawn_ball(-1);
    }
    jp->state = 1;
}

/* ---- the net (map gobj 3) ---- */

/* Net_KnockbackThink (+1804), priority 12: a ball that hit the net pushes its joint 2 away from
 * the ball by the ball's speed (clamped); the joint springs back. */
static void mexgr_vb_net_think(HSD_GObj* gobj)
{
    MexgrVbNet* np = (MexgrVbNet*) &((Ground*) HSD_GObjGetUserData(gobj))->u;
    MexgrVbJudge* jp = mexgr_vb_judge();
    HSD_JObj* joint = Ground_801C3FA4(gobj, 2);
    Vec3 d;
    f32 a;
    f32 b;
    if (jp->ball != NULL) {
        Item* ip = GET_ITEM(jp->ball);
        MexgrVbBall* bv = mexgr_vb_ball(ip);
        if (bv->net_hit != 0) {
            f32 angle;
            f32 mag;
            f32 c;
            bv->net_hit = 0;
            angle = atan2f(np->pos.y - ip->pos.y, np->pos.x - ip->pos.x);
            mag = sqrtf((f32) ((f64) ip->x40_vel.x * (f64) ip->x40_vel.x +
                               (f64) ip->x40_vel.y * (f64) ip->x40_vel.y));
            c = mexgr_vb_data->net.mag_max;
            if (!(c < mag)) {
                c = -c;
                if (!(c > mag)) {
                    c = mag;
                }
            }
            a = cosf(angle) * c;
            np->offset.x = mexgr_vb_data->net.kb_mult * a;
            a = sinf(angle) * c;
            np->offset.y = mexgr_vb_data->net.kb_mult * a;
        }
    }
    PSVECAdd(&joint->translate, &np->offset, &joint->translate);
    PSVECSubtract(&joint->translate, &np->pos, &d);
    a = d.x * mexgr_vb_data->net.tightness;
    b = np->offset.x - a;
    np->offset.x = b;
    a = d.y * mexgr_vb_data->net.tightness;
    np->offset.y = np->offset.y - a;
    np->offset.x = mexgr_vb_data->net.decay * b;
    np->offset.y = mexgr_vb_data->net.decay * np->offset.y;
    HSD_JObjSetMtxDirtySub(joint);
}

/* Net_GX (+19B8): the stage draw, then with the camera's debug display (x399_b3) in pass 2 the
 * net's boxes as translucent red strips. */
static void mexgr_vb_net_draw(HSD_GObj* gobj, intptr_t pass)
{
    MexgrVbNet* np;
    int i;
    grDisplay_801C5DB0(gobj, pass);
    if (!game_camera.x399_b3 || pass != 2) {
        return;
    }
    np = (MexgrVbNet*) &((Ground*) HSD_GObjGetUserData(gobj))->u;
    GXSetColorUpdate(GX_TRUE);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
    GXSetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_GREATER, 0);
    GXSetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
    GXSetZCompLoc(GX_FALSE);
    GXSetNumTexGens(0);
    GXSetTevClampMode(0, 0);
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
    for (i = 0; i < np->colls->count; i++) {
        const MexgrVbCollRect* r = &np->colls->rects[i];
        const f32 x0 = r->x;
        const f32 x1 = x0 + r->w;
        const f32 y0 = r->y;
        const f32 y1 = y0 + r->h;
        GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, 5);
        GXPosition3f32(x1, y1, 0.0F);
        GXColor4u8(0xFF, 0x80, 0x80, 0x78);
        GXPosition3f32(x0, y1, 0.0F);
        GXColor4u8(0xFF, 0x80, 0x80, 0x78);
        GXPosition3f32(x1, y0, 0.0F);
        GXColor4u8(0xFF, 0x80, 0x80, 0x78);
        GXPosition3f32(x0, y1, 0.0F);
        GXColor4u8(0xFF, 0x80, 0x80, 0x78);
        GXPosition3f32(x0, y0, 0.0F);
        GXColor4u8(0xFF, 0x80, 0x80, 0x78);
    }
    HSD_StateInvalidate(-1);
}

/* Net_OnCreate (+2F4) */
static void mexgr_vb_net_create(Ground_GObj* gobj)
{
    MexgrVbNet* np = (MexgrVbNet*) &((Ground*) HSD_GObjGetUserData(gobj))->u;
    np->offset.x = 0.0F;
    np->offset.y = 0.0F;
    np->offset.z = 0.0F;
    lb_8000B1CC(Ground_801C3FA4(gobj, 2), NULL, &np->pos);
    np->colls = mexgr_vb_data->colldescs;
    HSD_GObj_SetupProc(gobj, mexgr_vb_net_think, 0xC);
    gobj->render_cb = mexgr_vb_net_draw;
}

/* ---- the score board (map gobj 4) ---- */

/* Score_OnCreate (+384): digits at frame 0, the advantage marks (joints 7, 8) hidden, and a camera
 * subject over the board. The board joint's world position is computed into a local the block
 * never reads (the call stays: it sets the joint's matrices up). */
static void mexgr_vb_score_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    MexgrVbScore* sp = mexgr_vb_score_of(gp);
    HSD_JObj* jobj;
    CmSubject* cs;
    Vec3 pos;
    grAnime_801C8138(gobj, gp->map_id, 0);
    sp->dirty = 0;
    jobj = GET_JOBJ(gobj);
    HSD_JObjReqAnimAllByFlags(jobj, 0x10, 0.0F);
    HSD_JObjAnimAll(jobj);
    HSD_ForeachAnim(jobj, JOBJ_TYPE, TOBJ_MASK, HSD_AObjStopAnim, AOBJ_ARG_AOV, NULL, NULL);
    HSD_JObjSetFlags(Ground_801C3FA4(gobj, 7), JOBJ_HIDDEN);
    HSD_JObjSetFlags(Ground_801C3FA4(gobj, 8), JOBJ_HIDDEN);
    lb_8000B1CC(jobj, NULL, &pos);
    cs = Camera_80029020();
    cs->ext.h.x = -30.0F;
    cs->ext.h.y = 30.0F;
    cs->ext.v.x = 60.0F;
    cs->ext.v.y = 40.0F;
    cs->target_ext.h.x = -30.0F;
    cs->target_ext.h.y = 30.0F;
    cs->target_ext.v.x = 60.0F;
    cs->target_ext.v.y = 40.0F;
    sp->cam = cs;
}

/* Score_OnFrame (+49C): when marked, the two scores (joints 1, 2 and 3, 4: tens and ones), the
 * rally count (5, 6), the games (9 to 11 and 12 to 14) and the advantage marks (7, 8). */
static void mexgr_vb_score_frame(Ground_GObj* gobj)
{
    MexgrVbScore* sp = mexgr_vb_score_of(HSD_GObjGetUserData(gobj));
    MexgrVbJudge* jp;
    s32 rally;
    int j;
    int k;
    if (sp->dirty == 0) {
        return;
    }
    sp->dirty = 0;
    jp = mexgr_vb_judge();
    rally = jp->rally;
    for (k = 0, j = 1; j != 5; k++, j += 2) {
        const s32 score = jp->score[k];
        const s32 tens = score / 10;
        mexgr_vb_set_digit(Ground_801C3FA4(gobj, j), (f32) (u8) (tens - (tens / 10) * 10));
        mexgr_vb_set_digit(Ground_801C3FA4(gobj, j + 1), (f32) (u8) (score - tens * 10));
    }
    {
        const s32 tens = rally / 10;
        mexgr_vb_set_digit(Ground_801C3FA4(gobj, 5), (f32) (u8) (tens - (tens / 10) * 10));
        mexgr_vb_set_digit(Ground_801C3FA4(gobj, 6), (f32) (u8) (rally - tens * 10));
    }
    for (k = 0; k < 2; k++) {
        int i;
        for (i = 0; i != 3; i++) {
            mexgr_vb_set_digit(Ground_801C3FA4(gobj, k * 3 + 9 + i),
                               jp->games[k] > i ? 1.0F : 0.0F);
        }
    }
    {
        const s32 adv0 = jp->adv[0];
        HSD_JObj* mark = Ground_801C3FA4(gobj, 7);
        if (adv0 == 0) {
            HSD_JObjSetFlags(mark, JOBJ_HIDDEN);
        } else {
            HSD_JObjClearFlags(mark, JOBJ_HIDDEN);
        }
    }
    {
        const s32 adv1 = jp->adv[1];
        HSD_JObj* mark = Ground_801C3FA4(gobj, 8);
        if (adv1 != 0) {
            HSD_JObjClearFlags(mark, JOBJ_HIDDEN);
        } else {
            HSD_JObjSetFlags(mark, JOBJ_HIDDEN);
        }
    }
}

/* ---- the serve marker (map gobj 5, created by SpawnBall over each new ball) ---- */

/* Respawn_OnCreate (+868) */
static void mexgr_vb_respawn_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
    gp->x20[1] = 0;
}

/* Respawn_OnFrame (+8A4): 0 appearing, 1 waiting (2 after serve_danger_time frames), 3 leaving.
 * With the serve timer (more than one player) a ball not served in serve_timer frames explodes and
 * the other side scores. */
static void mexgr_vb_respawn_frame(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    MexgrVbRespawn* rp = (MexgrVbRespawn*) &gp->u;
    const s32 state = gp->x20[1];
    if (state > 2) {
        if (state == 3 && grAnime_801C83D0(gobj, 0, 7) == 1) {
            Ground_801C4A08(gobj);
        }
        return;
    }
    if (state <= 0) {
        if (state == 0 && grAnime_801C84A4(gobj, 0, 7) == 1) {
            grAnime_801C8138(gobj, gp->map_id, 1);
            gp->x20[1] = 1;
        }
        return;
    }
    {
        HSD_GObj* ball = rp->ball;
        Item* ip = GET_ITEM(ball);
        if (rp->timed != 0) {
            rp->frames += 1;
            if (rp->frames == mexgr_vb_data->volley.serve_danger_time) {
                grAnime_801C8138(gobj, gp->map_id, 2);
                gp->x20[1] = 2;
            } else if (rp->frames >= mexgr_vb_data->volley.serve_timer) {
                HSD_GObj* judge;
                s32 scorer = 1;
                s32 server = rp->side;
                mexgr_vb_state_explode(ball);
                judge = Ground_GetMapGObj(2);
                if (server != 0) {
                    server = 1;
                    scorer = 0;
                }
                mexgr_vb_increment_score(judge, scorer, server);
                rp->done = 1;
                grAnime_801C8138(gobj, gp->map_id, 3);
                gp->x20[1] = 3;
                return;
            }
        }
        if (rp->done != 0) {
            if (rp->done == 1) {
                grAnime_801C8138(gobj, gp->map_id, 3);
                gp->x20[1] = 3;
            }
            return;
        }
        if (ip->msid == MEXGR_VB_BALL_SPAWN) {
            return;
        }
        rp->done = 1;
        grAnime_801C8138(gobj, gp->map_id, 3);
        gp->x20[1] = 3;
    }
}

/* ---- the ceiling (map gobj 6) ---- */

/* Ceiling_OnCeilTouch (+1FE4), the ceiling line's collision joint callback (user data: the
 * ceiling's Ground). */
static void mexgr_vb_ceiling_touch(void* user_data, int joint_id, CollData* coll, int coll_x50,
                                   mpLib_GroundEnum ground_kind, float arg5)
{
    Ground* gp = user_data;
    (void) joint_id;
    (void) coll;
    (void) coll_x50;
    (void) ground_kind;
    (void) arg5;
    if (gp->x20[1] == 1) {
        return;
    }
    grAnime_801C8138(gp->gobj, gp->map_id, 1);
    gp->x20[1] = 1;
}

/* Ceiling_OnCreate (+A90) */
static void mexgr_vb_ceiling_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
    gp->x20[1] = 0;
    mpJointSetCb2(mexgr_vb_data->court.ceiling_line, gp, mexgr_vb_ceiling_touch);
}

/* Ceiling_OnFrame (+AE8) */
static void mexgr_vb_ceiling_frame(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    if (gp->x20[1] != 1) {
        return;
    }
    if (grAnime_801C83D0(gobj, 0, 7) == 1) {
        grAnime_801C8138(gobj, gp->map_id, 0);
        gp->x20[1] = 0;
    }
}

/* ---- the blimp (map gobj 7) ---- */

/* Blimp_OnCreate (+B68): a lone blr (Judge_OnFrame picks its animation). */
static void mexgr_vb_blimp_create(Ground_GObj* gobj)
{
    (void) gobj;
}

/* Blimp_OnFrame (+B6C): gone at the end of its route. */
static void mexgr_vb_blimp_frame(Ground_GObj* gobj)
{
    if (grAnime_801C83D0(gobj, 0, 7) == 1) {
        Ground_801C4A08(gobj);
    }
}

/* ---- the "deuce" sign (map gobj 8) ---- */

/* Exclam_OnCreate (+BB4): drawn with Corneria's smashTaunt_801E24AC (the block writes its retail
 * address into gobj->render_cb). */
static void mexgr_vb_exclam_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
    gp->x20[1] = 0;
    ((MexgrVbExclam*) &gp->u)->frames = 0;
    gobj->render_cb = smashTaunt_801E24AC;
}

/* Exclam_OnFrame (+C0C): the deuce sound on its 8th frame; gone when its animation ends. */
static void mexgr_vb_exclam_frame(Ground_GObj* gobj)
{
    MexgrVbExclam* ep = (MexgrVbExclam*) &((Ground*) HSD_GObjGetUserData(gobj))->u;
    ep->frames += 1;
    if (ep->frames == 8) {
        mu_gr_mex_sfx(mexgr_vb_data->volley.deuce_sfx);
    }
    if (grAnime_801C83D0(gobj, 0, 7) == 1) {
        Ground_801C4A08(gobj);
    }
}

/* ---- map_gobjs (+0): OnCreation, OnUnk, OnFrame, OnDeletion, flags ---- */
static StageCallbacks mexgr_vb_map[9] = {
    { mexgr_wp1_tseak_anim, mexgr_wp1_tseak_false, mexgr_wp1_tseak_nop, mexgr_wp1_tseak_nop, 0 },
    { mexgr_vb_tseak_coll_anim, mexgr_wp1_tseak_false, mexgr_wp1_tseak_coll, mexgr_wp1_tseak_nop, 0 },
    { mexgr_vb_judge_create, mexgr_wp1_tseak_false, mexgr_vb_judge_frame, mexgr_wp1_tseak_nop, 0 },
    { mexgr_vb_net_create, mexgr_wp1_tseak_false, mu_gr_gobj_nop, mexgr_wp1_tseak_nop, 0 },
    { mexgr_vb_score_create, mexgr_wp1_tseak_false, mexgr_vb_score_frame, mexgr_wp1_tseak_nop, 0 },
    { mexgr_vb_respawn_create, mexgr_wp1_tseak_false, mexgr_vb_respawn_frame, mexgr_wp1_tseak_nop, 0 },
    { mexgr_vb_ceiling_create, mexgr_wp1_tseak_false, mexgr_vb_ceiling_frame, mexgr_wp1_tseak_nop, 0 },
    { mexgr_vb_blimp_create, mexgr_wp1_tseak_false, mexgr_vb_blimp_frame, mexgr_wp1_tseak_nop, 0 },
    { mexgr_vb_exclam_create, mexgr_wp1_tseak_false, mexgr_vb_exclam_frame, mexgr_wp1_tseak_nop,
      0x20000000 },
};

/* ---- setup ---- */

/* Volley_Start (+C84), from onInit: the attributes, the judge, net, board and ceiling, the first
 * serve at "Go", the develop menu in a develop build, and with more than two players the arena,
 * judge and ceiling models scaled by teams_stage_scale. The console allocates 0x104 bytes; the
 * native struct holds a host pointer at +0x100. */
static void mexgr_vb_start(void)
{
    MexgrVbData* vd = mu_gr_mex_calloc(sizeof(MexgrVbData));
    HSD_GObj* judge;
    mexgr_vb_data = vd;
    vd->volley = mexgr_vb_volley_attr;
    vd->court = mexgr_vb_court_attr;
    vd->net = mexgr_vb_net_attr;
    vd->ball = mexgr_vb_ball_attr;
    vd->colldescs = &mexgr_vb_court_colldescs;
    judge = mu_grTSeak_80223908(2);
    mu_grTSeak_80223908(3);
    mu_grTSeak_80223908(4);
    mu_grTSeak_80223908(6);
    Ground_801C10B8(judge, mexgr_vb_judge_go);
    if ((s32) DbLevel > 3) {
        mexgr_vb_dbmenu_init();
    }
    if (gm_8016B558() > 2) {
        int i;
        for (i = 0; i < 3; i++) {
            HSD_JObj* jobj = GET_JOBJ(Ground_GetMapGObj(mexgr_vb_gobjs_to_scale[i]));
            jobj->scale.x = jobj->scale.x * mexgr_vb_data->court.teams_stage_scale;
            jobj->scale.y = jobj->scale.y * mexgr_vb_data->court.teams_stage_scale;
            jobj->scale.z = jobj->scale.z * mexgr_vb_data->court.teams_stage_scale;
            HSD_JObjSetMtxDirtySub(jobj);
        }
    }
}

/* Volley_InitSinglePlayer (+E28), from onLoad: with one player, its fighter and the half it starts
 * on. The block takes the fighter of the first slot in use; with every slot empty it would read an
 * unset register (gm_8016B558() == 1 says one slot is in use, so that is not reached; natively the
 * judge then stays in the scoring mode). */
static void mexgr_vb_init_single(void)
{
    MexgrVbJudge* jp = mexgr_vb_judge();
    HSD_GObj* f = NULL;
    int slot;
    if (gm_8016B558() != 1) {
        jp->single = 0;
        return;
    }
    for (slot = 0; slot != 6; slot++) {
        if (Player_GetPlayerSlotType(slot) != Gm_PKind_NA) {
            f = Player_GetEntity(slot);
            break;
        }
    }
    if (f == NULL) {
        OSReport("[mex] Volleyball: one player but no slot in use; scoring mode\n");
        jp->single = 0;
        return;
    }
    {
        Fighter* fp = GET_FIGHTER(f);
        Vec3 net;
        Ground_801C2D24(mexgr_vb_data->court.netpos_genpoint, &net);
        jp->single_side = !(fp->cur_pos.x < net.x);
        jp->single_fighter = f;
        jp->single = 1;
    }
}

/* Volley_InitTeams (+EEC), from onLoad: the half of each team (the first member's start) or of
 * each player slot (-1 for an empty one). */
static void mexgr_vb_init_teams(void)
{
    MexgrVbJudge* jp = mexgr_vb_judge();
    Vec3 net;
    int slot;
    Ground_801C2D24(mexgr_vb_data->court.netpos_genpoint, &net);
    if (gm_8016B168() == 1) {
        jp->team_side[0] = -1;
        jp->team_side[1] = -1;
        jp->team_side[2] = -1;
        jp->team_side[3] = -1;
        jp->team_side[4] = -1;
        jp->team_side[5] = -1;
        for (slot = 0; slot != 6; slot++) {
            Fighter* fp;
            if (Player_GetPlayerSlotType(slot) == Gm_PKind_NA) {
                continue;
            }
            fp = GET_FIGHTER(Player_GetEntity(slot));
            if (mexgr_vb_team_side(jp, fp->team) != -1) {
                continue;
            }
            jp->team_side[fp->team] = !(fp->cur_pos.x < net.x);
        }
    } else {
        for (slot = 0; slot != 6; slot++) {
            s8 side = -1;
            if (Player_GetPlayerSlotType(slot) != Gm_PKind_NA) {
                Fighter* fp = GET_FIGHTER(Player_GetEntity(slot));
                side = !(fp->cur_pos.x < net.x);
            }
            jp->slot_side[slot] = side;
        }
    }
}

/* onInit (+B4) */
static void mexgr_vb_init(void)
{
    mu_grTSeak_80223908(0);
    mu_grTSeak_80223908(1);
    mexgr_vb_start();
    Ground_801C39C0();
    Ground_801C3BB4();
}

/* onLoad (+EC): no timer (the rules' timer_enabled and timer_counts_up bits cleared), then the
 * player setup. */
static void mexgr_vb_load(void)
{
    struct StartMeleeRules* rules = gm_GetStartMeleeRules();
    rules->timer_enabled = 0;
    rules->timer_counts_up = 0;
    mexgr_vb_init_single();
    mexgr_vb_init_teams();
}

const MuGrStage mu_gr_volleyball = {
    .name = "Volleyball",
    .file = "/GrMVb.dat",
    .map_gobjs = mexgr_vb_map,
    .on_init = mexgr_vb_init,
    .on_load = mexgr_vb_load,
    .items = mexit_vb_items,
    .item_count = 1,
    .flags = MU_GR_READY,
};
