/* ACE 2.0.0's Sonic BM, native (PlSc.dat, ftDataBSonic; m-ex internal 43, external 42, native
 * kind 0x32 = 50).
 *
 * Sonic BM ships as m-ex PowerPC in PlSc.dat: one "ftFunction" block built without symbols
 * (0x3E90 bytes, 14 exports) and three "itFunction" blocks (articles 0, 1, 2). MxDt names Mario's
 * retail function for the slots the file does not export (ondeath, the item and knockback
 * events, the common double jump, the two demo hooks and the demo state table), which the
 * registry fills. Everything else is the file's own code; none of it is Mario's or the Akaneia
 * Sonic's (0 percent of the chunks are shared with either, run-source/rel09-ace-native/
 * out_clones.txt), so nothing is borrowed from akaneia/sonic.
 *
 *   neutral  a homing attack: a short wind up (343, 344) that leaves a trail of afterimages
 *            (article 1), then a dash (364) at the nearest other player within 45 units marked
 *            by a reticle (article 2); a hit bounces him back (366), a miss ends in a special
 *            fall (367)
 *   side     a spin dash: a charge on the ground (345, 361) or in the air (346, 360) that
 *            releases into a run or a brake (362, 363, 369)
 *   up       a spring (article 0) under him and a jump off it; he and others bounce on it
 *   down     a spin charge (349, 350, 354) with up to five levels kept in a fighter variable,
 *            released into a rolling attack on the ground (358) or in the air (359)
 *
 * The file also rewrites rows of his copy of the common state table at load: the four thrown
 * states (a bone fix up after the retail animation), the three forward smashes (a hold to charge,
 * state 353), and the six fall states (the up special locks out jumps). A side taunt on the
 * D-pad (states 370, 371) and the spin ball effect (m-ex effect 5007) run from his onframe.
 *
 * Offsets in the comments (+0B1C) are offsets into the ftFunction block, "it0 +5C" into the
 * itFunction block of article 0, as in run-source/rel09-ace-native/sonicbm/listing.txt. The file
 * has no symbols: the function names are this port's. "fp+" is a console offset of the Fighter.
 * FUNCTIONS.md in that folder lists every function of the four blocks. */
#ifndef MU_ACE_SONICBM_H
#define MU_ACE_SONICBM_H

#include <Runtime/platform.h>

#include <melee/ef/forward.h>
#include <melee/ft/forward.h>
#include <melee/it/forward.h>
#include <sysdolphin/baselib/forward.h>

#include <dolphin/mtx.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/types.h>
#include <melee/it/kinds/forward.h>
#include <melee/it/kinds/types.h>
#include <melee/it/types.h>
#include <sysdolphin/baselib/tobj.h>

#include <stddef.h>

#include "../../akaneia/mu_ak_fighter.h"
#include "mu_disc.h"

/* ---- Action states (move_logic, +01D4): 31 rows numbered from 341 ---- */
enum ftSc_MotionState {
    ftSc_MS_Empty0 = 341,        /* 0x155: empty row (as Mario's) */
    ftSc_MS_Empty1,              /* 0x156: empty row */
    ftSc_MS_SpecialN,            /* 0x157: homing wind up, entered from the ground */
    ftSc_MS_SpecialAirN,         /* 0x158: homing wind up, in the air */
    ftSc_MS_SpecialS,            /* 0x159: spin dash start, ground */
    ftSc_MS_SpecialAirS,         /* 0x15A: spin dash start, air */
    ftSc_MS_SpecialHi,           /* 0x15B: spring jump, ground */
    ftSc_MS_SpecialAirHi,        /* 0x15C: spring jump, air */
    ftSc_MS_SpecialLw,           /* 0x15D: spin charge start, ground */
    ftSc_MS_SpecialAirLw,        /* 0x15E: spin charge start, air */
    ftSc_MS_Unused15F,           /* 0x15F: animation 14, ftCo_800C7158; nothing enters it */
    ftSc_MS_Unused160,           /* 0x160: animation 15, ftCo_800C7200; nothing enters it */
    ftSc_MS_AttackS4Hold,        /* 0x161: forward smash held (from the common rows) */
    ftSc_MS_SpecialLwCharge,     /* 0x162: spin charge, ground */
    ftSc_MS_SpecialLwStop,       /* 0x163: roll stopped on the ground */
    ftSc_MS_SpecialAirLwCharge,  /* 0x164: spin charge, air */
    ftSc_MS_SpecialLwCancel,     /* 0x165: charge let go with no level change */
    ftSc_MS_SpecialLwRoll,       /* 0x166: roll on the ground */
    ftSc_MS_SpecialAirLwRoll,    /* 0x167: roll in the air */
    ftSc_MS_SpecialAirSCharge,   /* 0x168: spin dash charge, air */
    ftSc_MS_SpecialSCharge,      /* 0x169: spin dash charge, ground */
    ftSc_MS_SpecialSBrake,       /* 0x16A: spin dash let go against the stick (a brake) */
    ftSc_MS_SpecialAirSEnd,      /* 0x16B: air spin dash end, then a special fall */
    ftSc_MS_SpecialNDash,        /* 0x16C: homing dash */
    ftSc_MS_SpecialNLanding,     /* 0x16D: homing dash landed (common LandingAir callbacks) */
    ftSc_MS_SpecialNRebound,     /* 0x16E: homing dash hit something */
    ftSc_MS_SpecialNMiss,        /* 0x16F: homing dash reached its point or timed out */
    ftSc_MS_SpecialAirLwEnd,     /* 0x170: air roll end */
    ftSc_MS_SpecialAirSRelease,  /* 0x171: air spin dash let go after the charge was started */
    ftSc_MS_AppealSL,            /* 0x172: side taunt, facing left */
    ftSc_MS_AppealSR,            /* 0x173: side taunt, facing right */
    ftSc_MS_End
};

#define FTSC_MS_COUNT (ftSc_MS_End - ftSc_MS_Empty0)
_Static_assert(FTSC_MS_COUNT == 31, "31 rows in PlSc.dat's move_logic");
_Static_assert(ftSc_MS_Empty0 == ftCo_MS_Count, "m-ex numbers a fighter's rows from 341");

/* ---- Special attributes (ftData->ext_attr). The file does not copy them: [onload] points
 * fp->dat_attrs at the block inside PlSc.dat, so they are read in place (big-endian). The code
 * reads +04 to +14 only. The rest of the block (+18 on: 2, 10, 10, 0.625, 1.6, 2, 1, 2, 0.5, 2.6,
 * 2, 0.05, 0.03, 0.1, then words equal to Mario's from +50) has no reader. ---- */
typedef struct ftSc_DatAttrs {
    /* +00 */ float x00;                 ///< 3.0, no reader
    /* +04 */ s32 speciallw_level_max;   ///< 5: the spin charge's last level
    /* +08 */ float speciallw_speed;     ///< 1.9: the roll's speed at level 0
    /* +0C */ float speciallw_speed_add; ///< 0.3: speed added per level
    /* +10 */ s32 speciallw_damage;      ///< 8: the ground roll's damage at level 0
    /* +14 */ s32 speciallw_damage_add;  ///< 2: damage added per level
} DISC_STRUCT ftSc_DatAttrs;

static inline ftSc_DatAttrs* ftSc_Attrs(Fighter* fp)
{
    return (ftSc_DatAttrs*) fp->dat_attrs;
}

/* ---- Fighter variables (fp->u, console fp+222C). His own words are fp+22D4 to fp+22F8. The
 * slots before them are left to Mario's struct: MxDt's ondeath is Mario's retail routine, which
 * writes Mario's words at the start of the union. ---- */
typedef struct ftSc_FighterVars {
    /* fp+222C */ u8 mario_[0x22D4 - 0x222C];
    /* fp+22D4 */ s32 spring_live;         ///< 1 from the spring's spawn; 0 kills the spring
    /* fp+22D8 */ EF_Effect* spin_fx;      ///< the last spin ball effect (5007); never cleared
    /* fp+22DC */ s32 spin_fx_state;       ///< 0 none, 1 armed, 2 spawned in the dash charge,
                                           ///< 3 spawned in the run
    /* fp+22E0 */ HSD_TObjTevDesc* shoe_tev; ///< "TEV_Costume%d_Shoe_Effects" of PlSc.dat
    /* fp+22E4 */ HSD_TObjTevDesc* spin_tev; ///< "TEV_Costume%d_Spin_Effects" of PlSc.dat
    /* fp+22E8 */ s32 x22E8;               ///< cleared at load, no reader
    /* fp+22EC */ s32 side_used;           ///< side special used (once per air time)
    /* fp+22F0 */ s32 neutral_used;        ///< neutral special used
    /* fp+22F4 */ s32 up_used;             ///< up special used (also locks the fall's jumps)
    /* fp+22F8 */ s32 lw_level;            ///< spin charge level, 0 to attrs +04
} ftSc_FighterVars;

static inline ftSc_FighterVars* ftSc_FV(Fighter* fp)
{
    return (ftSc_FighterVars*) &fp->u;
}

/* ---- State variables (fp->mv, console fp+2340). One layout for every state, as the file uses
 * the same words across states. Every field is 4 bytes: the console offsets hold. ---- */
typedef struct ftSc_MotionVars {
    /* fp+2340 */ s32 hi_air;      ///< up special: 1 when started in the air (the spring's state)
    /* fp+2344 */ s32 hi_bounced;  ///< up special: 1 after a bounce on his own spring
    /* fp+2348 */ s32 x2348;
    /* fp+234C */ s32 x234C;
    /* fp+2350 */ float best_dist; ///< homing: distance to the target found so far (45 at start)
    /* fp+2354 */ Vec3 target;     ///< homing: the point he dashes at
    /* fp+2360 */ s32 timer;       ///< homing: frames left of the dash (30)
    /* fp+2364 */ s32 slot;        ///< homing: target player slot, -1 none; forward smash: the
                                   ///< state to return to; kept across the roll states
    /* fp+2368 */ s32 count;       ///< hold frames (forward smash) or roll frames left
    /* fp+236C */ s32 flag;        ///< per state: turned / released / charge started
    /* fp+2370 */ u8 hit_done[4];  ///< forward smash: hitbox damage already scaled. The file
                                   ///< also stores the WORD 1 here, which on the console is the
                                   ///< bytes 0, 0, 0, 1: ftSc_MV_SetWord2370 keeps that.
} ftSc_MotionVars;

static inline ftSc_MotionVars* ftSc_MV(Fighter* fp)
{
    return (ftSc_MotionVars*) &fp->mv;
}

/* stw rN, 0x2370(fp) with the value 0 or 1, as the big-endian word it is on the console. */
static inline void ftSc_MV_SetWord2370(Fighter* fp, u32 value)
{
    u8* b = ftSc_MV(fp)->hit_done;
    b[0] = (u8) (value >> 24);
    b[1] = (u8) (value >> 16);
    b[2] = (u8) (value >> 8);
    b[3] = (u8) value;
}

/* ---- Articles (ftData->x48_items, in the order of his MxDt item lookup: 320, 321, 322). The
 * file's list has six entries; [onload] registers the first three, the ones with code. ---- */
enum {
    ftSc_Article_Spring = 0, /* item kind 320 on ACE 2.0.0: the up special's spring */
    ftSc_Article_Trail = 1,  /* item kind 321: an afterimage of the homing and spin moves */
    ftSc_Article_Mark = 2,   /* item kind 322: the homing attack's target mark */
    ftSc_Article_Count = 3,
};

/* Item variables (ip->xDD4_itemVar, console item+DD4). */
typedef struct itSc_TrailVars {
    /* +DD4 */ HSD_GObj* owner;  ///< the Sonic BM that made the chain
    /* +DD8 */ HSD_GObj* parent; ///< the fighter (first link) or the previous link
    /* +DDC */ s32 index;        ///< 0 for the first link, then one more per link
    /* +DE0 */ s32 delay;        ///< frames until the next link (5), then -1
    /* +DE4 */ s32 painted;      ///< 1 once the texture colors were set
} itSc_TrailVars;

typedef struct itSc_OwnerVars {
    /* +DD4 */ HSD_GObj* owner;
} itSc_OwnerVars;

/* m-ex ids of his own files (EfScData.dat, effect file 43, which his MxDt entry names; sound
 * bank 85, bmsonic.ssm). */
#define FTSC_EFFECT_SPIN 5007 /* +3ADC: the spin ball, on his root joint */
#define FTSC_SFX_CHARGE 5004  /* 0x138C: a charge step of the down special */
#define FTSC_SFX_SPRING 5007  /* 0x138F: the spring */
#define FTSC_SFX_ROLL 5010    /* 0x1392: the roll */

/* Bit 17 of the button word (andis. 2 at +1720): the stick held down, as the game builds the
 * word. No HSD_PAD_ name exists for it. */
#define FTSC_PAD_STICK_DOWN (1u << 17)

/* ---- sonicbm.c ---- */
extern const MotionState ftSc_Init_MotionStateTable[FTSC_MS_COUNT];
/* +3ADC */ void ftSc_SpinFx_Spawn(HSD_GObj* gobj);
/* +33F8 */ float ftSc_Distance(float x1, float y1, float x2, float y2);
/* +1610 */ void ftSc_AttackS4Hold_Anim(HSD_GObj* gobj);
/* +28F8 */ void ftSc_AppealS_Anim(HSD_GObj* gobj);

/* ---- sonicbm_specialn.c ---- */
/* +05B4 */ void ftSc_SpecialN_Enter(HSD_GObj* gobj);
/* +063C */ void ftSc_SpecialAirN_Enter(HSD_GObj* gobj);
/* +0D18 */ void ftSc_SpecialN_Anim(HSD_GObj* gobj);
/* +0EF4 */ void ftSc_SpecialN_Phys(HSD_GObj* gobj);
/* +0EF8 */ void ftSc_SpecialN_Coll(HSD_GObj* gobj);
/* +242C */ void ftSc_SpecialNDash_Anim(HSD_GObj* gobj);
/* +24DC */ void ftSc_SpecialNDash_IASA(HSD_GObj* gobj);
/* +24F0 */ void ftSc_SpecialNDash_Phys(HSD_GObj* gobj);
/* +25F4 */ void ftSc_SpecialNDash_Coll(HSD_GObj* gobj);
/* +2684 */ void ftSc_SpecialNRebound_Anim(HSD_GObj* gobj);
/* +26D0 */ void ftSc_SpecialNRebound_Phys(HSD_GObj* gobj);
/* +26D4 */ void ftSc_SpecialNRebound_Coll(HSD_GObj* gobj);
/* +272C */ void ftSc_SpecialNMiss_Anim(HSD_GObj* gobj);
/* +27B0 */ void ftSc_SpecialNMiss_Phys(HSD_GObj* gobj);
/* +27B4 */ void ftSc_SpecialNMiss_Coll(HSD_GObj* gobj);

/* ---- sonicbm_specials.c ---- */
/* +06F8 */ void ftSc_SpecialS_Enter(HSD_GObj* gobj);
/* +073C */ void ftSc_SpecialAirS_Enter(HSD_GObj* gobj);
/* +0EFC */ void ftSc_SpecialS_Anim(HSD_GObj* gobj);
/* +0F88 */ void ftSc_SpecialS_Phys(HSD_GObj* gobj);
/* +0F8C */ void ftSc_SpecialS_Coll(HSD_GObj* gobj);
/* +0FD8 */ void ftSc_SpecialAirS_Anim(HSD_GObj* gobj);
/* +1068 */ void ftSc_SpecialAirS_Phys(HSD_GObj* gobj);
/* +1080 */ void ftSc_SpecialAirS_Coll(HSD_GObj* gobj);
/* +2110 */ void ftSc_SpecialAirSCharge_Anim(HSD_GObj* gobj);
/* +219C */ void ftSc_SpecialSCharge_Phys(HSD_GObj* gobj);
/* +21BC */ void ftSc_SpecialSCharge_Anim(HSD_GObj* gobj);
/* +2314 */ void ftSc_SpecialSBrake_Anim(HSD_GObj* gobj);
/* +2360 */ void ftSc_SpecialSBrake_IASA(HSD_GObj* gobj);
/* +2380 */ void ftSc_SpecialAirSEnd_Anim(HSD_GObj* gobj);
/* +23E8 */ void ftSc_SpecialAirSEnd_Phys(HSD_GObj* gobj);
/* +28AC */ void ftSc_SpecialAirSRelease_Anim(HSD_GObj* gobj);

/* ---- sonicbm_specialhi.c ---- */
/* +07F0 */ void ftSc_SpecialHi_Enter(HSD_GObj* gobj);
/* +08A4 */ void ftSc_SpecialAirHi_Enter(HSD_GObj* gobj);
/* +10F4 */ void ftSc_SpecialHi_Anim(HSD_GObj* gobj);
/* +1244 */ void ftSc_SpecialHi_IASA(HSD_GObj* gobj);
/* +12F4 */ void ftSc_SpecialHi_Phys(HSD_GObj* gobj);
/* +13B8 */ void ftSc_SpecialHi_Coll(HSD_GObj* gobj);

/* ---- sonicbm_speciallw.c ---- */
/* +095C */ void ftSc_SpecialLw_Enter(HSD_GObj* gobj);
/* +09C4 */ void ftSc_SpecialAirLw_Enter(HSD_GObj* gobj);
/* +1404 */ void ftSc_SpecialLw_Anim(HSD_GObj* gobj);
/* +1498 */ void ftSc_SpecialLw_Coll(HSD_GObj* gobj);
/* +14F8 */ void ftSc_SpecialAirLw_Anim(HSD_GObj* gobj);
/* +158C */ void ftSc_SpecialAirLw_Phys(HSD_GObj* gobj);
/* +15C4 */ void ftSc_SpecialAirLw_Coll(HSD_GObj* gobj);
/* +16A0 */ void ftSc_SpecialLwCharge_Anim(HSD_GObj* gobj);
/* +189C */ void ftSc_SpecialLwStop_Anim(HSD_GObj* gobj);
/* +1930 */ void ftSc_SpecialAirLwCharge_Anim(HSD_GObj* gobj);
/* +19CC */ void ftSc_SpecialLwCancel_Anim(HSD_GObj* gobj);
/* +1A18 */ void ftSc_SpecialLwRoll_Anim(HSD_GObj* gobj);
/* +1BE0 */ void ftSc_SpecialLwRoll_IASA(HSD_GObj* gobj);
/* +1C9C */ void ftSc_SpecialLwRoll_Phys(HSD_GObj* gobj);
/* +1DD8 */ void ftSc_SpecialAirLwRoll_Anim(HSD_GObj* gobj);
/* +1F90 */ void ftSc_SpecialAirLwRoll_IASA(HSD_GObj* gobj);
/* +20DC */ void ftSc_SpecialAirLwRoll_Phys(HSD_GObj* gobj);
/* +27C4 */ void ftSc_SpecialAirLwEnd_Anim(HSD_GObj* gobj);
/* +2810 */ void ftSc_SpecialAirLwEnd_Phys(HSD_GObj* gobj);
/* +2860 */ void ftSc_SpecialAirLwEnd_Coll(HSD_GObj* gobj);

/* ---- sonicbm_items.c ---- */
/* +31FC */ void itSc_Trail_Spawn(HSD_GObj* owner_gobj, HSD_GObj* parent_gobj, ItemKind kind,
                                  s32 index, s32 msid, float z);
/* +3434 */ void itSc_Mark_Spawn(HSD_GObj* owner_gobj, ItemKind kind, Vec3* pos);
/* +3680 */ void itSc_Spring_Spawn(HSD_GObj* owner_gobj, Vec3* pos, ItemKind kind, s32 air);
extern const ItemLogicTable itSc_Articles[ftSc_Article_Count];

#endif
