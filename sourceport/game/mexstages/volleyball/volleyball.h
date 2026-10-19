/* Volleyball (Akaneia 1.0.1 and ACE 2.0.0, the same file: internal 77, external 294, "/GrMVb.dat"):
 * what the stage code and its ball share. The grFunction block of GrMVb.dat (5,083 instructions with
 * symbols, `py -3.12 grdis.py ak GrMVb.dat` in run-source/rel09-stages, out_dis_GrMVb.txt) is
 * volleyball.c (the court: judge, net, score board, serve marker, ceiling, blimp, "deuce" sign),
 * volleyball_ball.c (the ball code the stage block carries: spawn, hit, parry, trail and debug
 * drawing, the parry fighter states) and volleyball_dbmenu.c (the attribute menu of a develop
 * build); its one itFunction block (1,620 instructions, `... GrMVb.dat it 0`, stage item 0 = m-ex
 * item kind 249) is volleyball_item.c.
 *
 * Console layouts: the block reads its own variables at fixed offsets of the Ground and Item user
 * data (the Ground union at gp+0xC4, the item variables at ip+0xDD4) and of the fighter (motion
 * variables at fp+0x2340). Natively those are the structs below, laid over the same unions (each
 * with a size check), and every decomp field the block reads by offset is named where it is used
 * (the offset in a comment). */
#ifndef MEXGR_VOLLEYBALL_H
#define MEXGR_VOLLEYBALL_H

#include "../common/mu_gr_common.h"

#include <dolphin/gx.h>
#include <melee/ft/forward.h>
#include <melee/gr/types.h>
#include <melee/cm/forward.h>
#include <melee/it/forward.h>
#include <melee/it/kinds/types.h>
#include <melee/it/types.h>
#include <sysdolphin/baselib/forward.h>

/* ---- the attribute blocks (.data.volley_attr, court_attr, net_attr, ball_attr) ----
 * Volley_Start copies them into one allocation (volley_data, 0x104 bytes on the console); the
 * develop menu edits them there word by word (int or float by its own type tables), and every ball
 * takes a copy of the ball block (ip+0xF20), refreshed from volley_data each frame by Judge_Think.
 * Every field is one 4-byte word, as on the console, so the menu can step a word by index. */
typedef struct MexgrVbVolleyAttr {
    /* +00 */ s32 score_max;
    /* +04 */ s32 ball_maxhit;
    /* +08 */ s32 landwait_timer;
    /* +0C */ s32 crowdupset_rallymin;
    /* +10 */ s32 crowdupset_rallymax;
    /* +14 */ s32 crowdupset_sfx;
    /* +18 */ s32 crowdcheer_rallymin;
    /* +1C */ s32 crowdcheer_sfx;
    /* +20 */ s32 point_sfx;
    /* +24 */ s32 game_sfx;
    /* +28 */ s32 deuce_sfx;          /* "duece_sfx" in the menu */
    /* +2C */ s32 autoreturn_anglelow;
    /* +30 */ s32 autoreturn_anglehi;
    /* +34 */ s32 autoreturn_dmglow;
    /* +38 */ s32 autoreturn_dmghi;
    /* +3C */ s32 autoreturn_kbglow;
    /* +40 */ s32 autoreturn_kbghi;
    /* +44 */ s32 autoreturn_bkb;
    /* +48 */ s32 serve_timer;
    /* +4C */ s32 serve_danger_time;
} MexgrVbVolleyAttr;

typedef struct MexgrVbCourtAttr {
    /* +00 */ s32 spawn_genpoint;     /* stage point of the left serve position, + 1 the right */
    /* +04 */ s32 netpos_genpoint;    /* stage point of the net */
    /* +08 */ s32 court_collgroup;
    /* +0C */ s32 barrier_collgroup;  /* the ball skips this collision joint */
    /* +10 */ s32 ceiling_line;       /* collision joint whose touch callback the ceiling sets */
    /* +14 */ s32 ceiling_joint;
    /* +18 */ f32 itembarrier_width;
    /* +1C */ f32 teams_stage_scale;
} MexgrVbCourtAttr;

typedef struct MexgrVbNetAttr {
    /* +00 */ f32 mag_max;
    /* +04 */ f32 tightness;
    /* +08 */ f32 decay;
    /* +0C */ f32 kb_mult;
} MexgrVbNetAttr;

typedef struct MexgrVbBallAttr {
    /* +00 ip+F20 */ s32 phys_kind;           /* 0 classic, 1 modern */
    /* +04 ip+F24 */ f32 physclassic_gravity;
    /* +08 ip+F28 */ f32 physclassic_terminalvel;
    /* +0C ip+F2C */ f32 physmodern_gravity;
    /* +10 ip+F30 */ f32 physmodern_drag;
    /* +14 ip+F34 */ f32 physmodern_bouncedecay;
    /* +18 ip+F38 */ f32 friction;
    /* +1C ip+F3C */ s32 dmg_min;
    /* +20 ip+F40 */ s32 dmg_decay_amt;
    /* +24 ip+F44 */ s32 dmg_decay_rate;
    /* +28 ip+F48 */ s32 parry_up_kb;
    /* +2C ip+F4C */ s32 parry_expire_frames;
    /* +30 ip+F50 */ s32 parry_hit_freeze_frames;
    /* +34 ip+F54 */ f32 parry_radius;
    /* +38 ip+F58 */ s32 parry_execute_window;   /* 0: the parry system is off */
    /* +3C ip+F5C */ f32 land_vel;
    /* +40 ip+F60 */ f32 spin_amt;
    /* +44 ip+F64 */ GXColor colshift_colstart;
    /* +48 ip+F68 */ GXColor colshift_colend;
    /* +4C ip+F6C */ s32 colshift_pctend;
    /* +50 ip+F70 */ s32 hitsfx_num;
    /* +54 ip+F74 */ s32 hitsfx_start;
    /* +58 ip+F78 */ s32 bouncesfx_num;
    /* +5C ip+F7C */ s32 bouncesfx_start;
    /* +60 ip+F80 */ f32 coll_size_x;
    /* +64 ip+F84 */ f32 coll_size_y;
    /* the words past the menu's 26 */
    /* +68 ip+F88 */ GXColor trail_color0;
    /* +6C ip+F8C */ GXColor trail_color1;
    /* +70 ip+F90 */ GXColor trail_color2;
    /* +74 ip+F94 */ f32 trail_blend_mid;
    /* +78 ip+F98 */ f32 trail_width_a;
    /* +7C ip+F9C */ f32 trail_width_b;
} MexgrVbBallAttr;

/* .data.court1_colldesc: a box of the net (absolute stage coordinates) and its kind. */
typedef struct MexgrVbCollRect {
    /* +00 */ f32 x;
    /* +04 */ f32 y;
    /* +08 */ f32 w;
    /* +0C */ f32 h;
    /* +10 */ s32 kind;    /* 1 the net post, 2 the floor of the net's top */
    /* +14 */ f32 angle;
} MexgrVbCollRect;

/* .data.court_colldescs */
typedef struct MexgrVbCollDescs {
    /* +0 */ s32 count;
    /* +4 */ MexgrVbCollRect* rects;
} MexgrVbCollDescs;

/* volley_data (the console's 0x104 byte allocation). */
typedef struct MexgrVbData {
    /* +00 */ MexgrVbVolleyAttr volley;
    /* +50 */ MexgrVbCourtAttr court;
    /* +70 */ MexgrVbNetAttr net;
    /* +80 */ MexgrVbBallAttr ball;
    /* +100 */ MexgrVbCollDescs* colldescs;
} MexgrVbData;

/* .bss.volley_data: the pointer itself (a block static). */
extern MexgrVbData* mexgr_vb_data;

/* ---- the ball's item variables (ip+0xDD4 on the console) ---- */
typedef struct MexgrVbTrailPoint {
    /* +0 */ s32 valid;
    /* +4 */ f32 x;
    /* +8 */ f32 y;
    /* +C */ f32 angle;
} MexgrVbTrailPoint;

typedef struct MexgrVbBall {
    /* ip+DD4 */ Vec3 last_pos;
    /* ip+DE0 */ Vec3 vel;           /* the hit velocity */
    /* ip+DEC */ Vec3 vel2;          /* gravity and friction part (classic physics) */
    /* ip+DF8 */ MexgrVbCollDescs* colls;
    /* ip+DFC */ u8 prev_sides;
    /* ip+DFD */ u8 sides;           /* LineVsRect side bits of this frame's net boxes */
    /* ip+E00 */ s32 frames;
    /* ip+E04 */ s32 wait_frames;
    /* ip+E08 */ s32 hits;           /* touches by the same side */
    /* ip+E0C */ s32 last_side;      /* team, or player slot, of the last touch; -1 none, 5 auto */
    /* ip+E10 */ s32 last_attack;    /* attack instance of the last touch */
    /* ip+E14 */ s32 landed;
    /* ip+E18 */ s32 net_hit;
    /* ip+E1C */ s32 parry;          /* 0 none, 1 parried, 2 parry hit */
    /* ip+E20 */ s32 parry_timer;
    /* ip+E24 */ HSD_GObj* parry_fighter;
    /* ip+E28 */ s32 trail_on;
    /* ip+E2C */ s32 xE2C;           /* never read nor written by either block */
    /* ip+E30 */ MexgrVbTrailPoint trail[15];
    /* ip+F20 */ MexgrVbBallAttr attr;
} MexgrVbBall;

static inline MexgrVbBall* mexgr_vb_ball(Item* ip)
{
    return (MexgrVbBall*) &ip->xDD4_itemVar;
}

/* The fighter's motion variables during the parry states (fp+0x2340). */
typedef struct MexgrVbParryVars {
    /* fp+2340 */ HSD_GObj* ball;
    /* fp+2344 */ s32 attacked;
} MexgrVbParryVars;

/* The ball's item state numbers (item_state_table of the itFunction block). */
enum {
    MEXGR_VB_BALL_SPAWN = 0,
    MEXGR_VB_BALL_WAIT = 1,
    MEXGR_VB_BALL_PARRYWAIT = 2,
    MEXGR_VB_BALL_OUTPLAY = 3,
    MEXGR_VB_BALL_LAND = 4,
    MEXGR_VB_BALL_EXPLODE = 5,
};

/* The kind stateSpawn writes into the ball (ip+0x10 = 0xD2); Judge_Think skips it when it clears
 * items from the net. */
#define MEXGR_VB_BALL_KIND 0xD2

/* ---- the judge (map gobj 2, Ground union at gp+0xC4) ---- */
typedef struct MexgrVbJudge {
    u8 x0_pad[0x1C];
    /* gp+E0 */ s32 state;           /* 0 before the start, 1 serve, 2 rally, 3 point over */
    /* gp+E4 */ s32 score[2];
    /* gp+EC */ s32 adv[2];          /* advantage after deuce */
    /* gp+F4 */ s32 games[2];
    /* gp+FC */ HSD_GObj* ball;
    /* gp+100 */ s32 wait;           /* frames until the next serve */
    /* gp+104 */ s32 server;         /* side that serves next */
    /* gp+108 */ s32 last_side;
    /* gp+10C */ s32 rally;
    /* gp+110 */ s8 team_side[6];    /* side of each team (teams) */
    /* gp+116 */ s8 slot_side[6];    /* side of each player slot */
    /* gp+11C */ s32 blimp_timer;
    /* gp+120 */ u8 blimp_idx;
    /* gp+121 */ u8 blimp_order[8];
    /* gp+12C */ s8 single;          /* one player: practice, no score */
    /* gp+12D */ s8 single_side;
    /* gp+130 */ HSD_GObj* single_fighter;
} MexgrVbJudge;

/* gp+0x110 + index and gp+0x116 + index the way the console reads them: an index of -1 (no touch
 * yet) reads the byte before the array (the last team byte for the slot array, the low byte of
 * the big-endian rally count for the team array). */
s8 mexgr_vb_team_side(MexgrVbJudge* jp, s32 index);
s8 mexgr_vb_slot_side(MexgrVbJudge* jp, s32 index);

static inline MexgrVbJudge* mexgr_vb_judge_of(Ground* gp)
{
    return (MexgrVbJudge*) &gp->u;
}

/* The judge's Ground (Ground_GetMapGObj(2)->user_data). */
MexgrVbJudge* mexgr_vb_judge(void);

/* ---- the respawn marker (map gobj 5) ---- */
typedef struct MexgrVbRespawn {
    u8 x0_pad[0x1C];
    /* gp+E0 */ HSD_GObj* ball;
    /* gp+E4 */ s32 frames;
    /* gp+E8 */ s32 side;
    /* gp+EC */ s32 timed;           /* the serve timer runs (not one player) */
    /* gp+F0 */ s32 done;
} MexgrVbRespawn;

/* ---- the net (map gobj 3) ---- */
typedef struct MexgrVbNet {
    u8 x0_pad[0x1C];
    /* gp+E0 */ Vec3 pos;
    /* gp+EC */ Vec3 offset;         /* the net joint's push, decays back to pos */
    /* gp+F8 */ MexgrVbCollDescs* colls;
} MexgrVbNet;

/* ---- the score board (map gobj 4) ---- */
typedef struct MexgrVbScore {
    u8 x0_pad[0x1C];
    /* gp+E0 */ s32 dirty;
    /* gp+F4 */ CmSubject* cam;
} MexgrVbScore;

static inline MexgrVbScore* mexgr_vb_score_of(Ground* gp)
{
    return (MexgrVbScore*) &gp->u;
}

/* ---- the "deuce" sign (map gobj 8) ---- */
typedef struct MexgrVbExclam {
    u8 x0_pad[0x1C];
    /* gp+E0 */ s32 frames;
} MexgrVbExclam;

/* ---- stage code the ball needs (volleyball.c) ---- */
void mexgr_vb_state_explode(HSD_GObj* ball);
void mexgr_vb_increment_score(HSD_GObj* judge, s32 scorer, s32 server);

/* ---- ball code of the stage block (volleyball_ball.c) ---- */
HSD_GObj* mexgr_vb_spawn_ball(s32 side);
void mexgr_vb_ball_auto_return(HSD_GObj* ball, f32 dir);
void mexgr_vb_ball_outplay_enter(HSD_GObj* ball);
void mexgr_vb_state_spawn(HSD_GObj* ball);

/* Shared by both blocks (each carries its own copy; the copies are the same instructions apart from
 * the ones named below). */
void mexgr_vb_state_wait(HSD_GObj* ball);
void mexgr_vb_play_hit_sfx(HSD_GObj* ball, s32 n);
void mexgr_vb_process_trail(HSD_GObj* ball);
void mexgr_vb_on_parry_launch(HSD_GObj* ball);
/* Ball_Hit: the two copies differ only in the Ball_OnParryHit they install. */
void mexgr_vb_ball_hit(HSD_GObj* ball, s32 from_attack, HSD_GObjEvent on_parry_hit);

/* ---- the develop menu (volleyball_dbmenu.c) ---- */
void mexgr_vb_dbmenu_init(void);

/* ---- the ball item (volleyball_item.c) ---- */
extern const ItemLogicTable mexit_vb_items[1];

/* Floats of the two blocks' constant pools that are not plain decimal literals. */
#define MEXGR_VB_DEG2RAD 0x1.1df4722d4405fp-6   /* double 0x3F91DF4722D4405F */
#define MEXGR_VB_TAU 0x1.921fb54442d18p+2       /* 2 pi */
#define MEXGR_VB_PI 0x1.921fb54442d18p+1
#define MEXGR_VB_HALF_PI 0x1.921fb54442d18p+0

#endif
