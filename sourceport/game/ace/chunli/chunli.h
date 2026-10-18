/* ACE 2.0.0's Chun-Li, native (PlCn.dat, ftDataChunLi; m-ex internal 55, external 54, native
 * kind 0x3E = 62).
 *
 * Chun-Li ships in PlCn.dat as m-ex PowerPC: one "ftFunction" block (0x3400 bytes, 15 exports)
 * and two "itFunction" blocks (articles 2 and 3), all built without symbols. MxDt calls her a
 * clone of Zelda: the slots she does not export are Zelda's (ondeath ftZd_Init_OnDeath) or the
 * common ones (the double jump), and her state table keeps Zelda's eighteen rows in its first
 * eighteen places. Eleven of her 27 rows are retail Zelda rows with animation -1 that nothing
 * of hers enters; the rest is her own code, which is a Street Fighter move set over the common
 * fighter code:
 *
 *   neutral   341/342: a shot (article 2) from the bone of part 86 on the script's flag
 *   B + A     367: on the ground, B held and A pressed in the first two frames of any state:
 *             a second shot (article 3)
 *   side      345/348 -> 359/360 -> 361/362: a spinning kick at a fixed speed
 *   up        349/352: a rising kick in the shape of Mario's up special
 *   down      355 (ground), 357 (air: a hop at 3.75 forward and 9 up, once per air time)
 *   cancels   onframe: chosen common attacks (jab 1 and 2, forward tilt, up tilt, down tilt,
 *             neutral, up and down air) cancel into a special when they hit and B is pressed
 *             in a frame window; a down tilt let go of before frame 4 becomes her own "light"
 *             down tilt (364), which chains into itself
 *
 * Her special attributes are her own block (0x8C bytes, ftCn_DatAttrs) and her fighter
 * variables are two words of her own (fp+2240 and fp+2244, ftCn_FighterVars).
 *
 * Offsets in the comments (+0890) are offsets into the ftFunction block, as in
 * run-source/rel09-ace-native/chunli/listing.txt and FUNCTIONS.md; "item 2 +0128" is an offset
 * into the itFunction block of article 2. "fp+" is a console offset of the Fighter. The file
 * has no symbols: the function names are this port's. */
#ifndef MU_ACE_CHUNLI_H
#define MU_ACE_CHUNLI_H

#include <Runtime/platform.h>

#include <melee/ft/forward.h>
#include <melee/it/forward.h>
#include <sysdolphin/baselib/forward.h>

#include <dolphin/mtx.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/types.h>
#include <melee/it/kinds/forward.h>
#include <melee/it/kinds/types.h>
#include <melee/it/types.h>

#include <stddef.h>

#include "../../akaneia/mu_ak_fighter.h"
#include "mu_disc.h"

/* ---- Action states (move_logic, +010C, 27 rows numbered from ftCo_MS_Count) -----------------
 * The "Zd" rows are retail Zelda's rows (same flags and move ids, animation -1): nothing in
 * her code enters them. */
typedef enum ftChunLi_MotionState {
    ftCn_MS_SpecialN = ftCo_MS_Count, // 341 (0x155): the shot, ground
    ftCn_MS_SpecialAirN,              // 342 (0x156): the shot, air
    ftCn_MS_ZdSpecialSStart,          // 343: Zelda's
    ftCn_MS_ZdSpecialSLoop,           // 344: Zelda's
    ftCn_MS_SpecialSStart,            // 345 (0x159): the kick, ground start
    ftCn_MS_ZdSpecialAirSStart,       // 346: Zelda's
    ftCn_MS_ZdSpecialAirSLoop,        // 347: Zelda's
    ftCn_MS_SpecialAirSStart,         // 348 (0x15C): the kick, air start
    ftCn_MS_SpecialHi,                // 349 (0x15D)
    ftCn_MS_ZdSpecialHiStart_1,       // 350: Zelda's
    ftCn_MS_ZdSpecialHi,              // 351: Zelda's
    ftCn_MS_SpecialAirHi,             // 352 (0x160)
    ftCn_MS_ZdSpecialAirHiStart_1,    // 353: Zelda's
    ftCn_MS_ZdSpecialAirHi,           // 354: Zelda's
    ftCn_MS_SpecialLw,                // 355 (0x163)
    ftCn_MS_ZdSpecialLw2,             // 356: Zelda's
    ftCn_MS_SpecialAirLw,             // 357 (0x165)
    ftCn_MS_ZdSpecialAirLw2,          // 358: Zelda's
    ftCn_MS_SpecialSLoop,             // 359 (0x167): the kick, ground
    ftCn_MS_SpecialAirSLoop,          // 360 (0x168): the kick, air
    ftCn_MS_SpecialSEnd,              // 361 (0x169)
    ftCn_MS_SpecialAirSEnd,           // 362 (0x16A)
    ftCn_MS_SpecialSEnd2,             // 363 (0x16B): 361's callbacks, animation -1, not entered
    ftCn_MS_AttackLw3Light,           // 364 (0x16C): the light down tilt
    ftCn_MS_ZdSpecialLw2b,            // 365: Zelda's SpecialLw2 callbacks again
    ftCn_MS_AttackHi3Light,           // 366 (0x16E): entered only from its own cancel
    ftCn_MS_SpecialNAlt,              // 367 (0x16F): the B + A shot
    ftCn_MS_Count,
    ftCn_MS_SelfCount = ftCn_MS_Count - ftCo_MS_Count,
} ftChunLi_MotionState;

_Static_assert(ftCn_MS_SelfCount == 27, "27 rows in PlCn.dat's move_logic");
_Static_assert(ftCn_MS_SpecialN == 0x155 && ftCn_MS_SpecialAirN == 0x156 &&
                   ftCn_MS_SpecialSStart == 0x159 && ftCn_MS_SpecialAirSStart == 0x15C &&
                   ftCn_MS_SpecialHi == 0x15D && ftCn_MS_SpecialAirHi == 0x160 &&
                   ftCn_MS_SpecialLw == 0x163 && ftCn_MS_SpecialAirLw == 0x165 &&
                   ftCn_MS_SpecialSLoop == 0x167 && ftCn_MS_SpecialAirSLoop == 0x168 &&
                   ftCn_MS_SpecialSEnd == 0x169 && ftCn_MS_SpecialAirSEnd == 0x16A &&
                   ftCn_MS_AttackLw3Light == 0x16C && ftCn_MS_AttackHi3Light == 0x16E &&
                   ftCn_MS_SpecialNAlt == 0x16F,
               "the state numbers PlCn.dat's code passes to Fighter_ChangeMotionState");

/* The common states her code tests or enters, by the numbers of the file. */
_Static_assert(ftCo_MS_Attack11 == 0x2C && ftCo_MS_Attack12 == 0x2D &&
                   ftCo_MS_AttackS3S == 0x35 && ftCo_MS_AttackHi3 == 0x38 &&
                   ftCo_MS_AttackLw3 == 0x39 && ftCo_MS_AttackAirN == 0x41 &&
                   ftCo_MS_AttackAirHi == 0x44 && ftCo_MS_AttackAirLw == 0x45,
               "the states the onframe cancel tests (+07F4 to +16F8)");
_Static_assert(ftCo_MS_SquatWait == 0x28 && ftCo_MS_LandingAirF == 0x47 &&
                   ftCo_MS_CliffCatch == 0xFC && ftCo_MS_DeadDown == 0,
               "the common states entered at +2698 and +21B4, tested at +1704");

/* ---- Special attributes (ftData->ext_attr, 0x8C bytes, disc data; values of PlCn.dat) ------
 * Only the words her code reads are named. */
typedef struct ftCn_DatAttrs {
    /* +00 */ float x0;                          ///< 0.1067, not read
    /* +04 */ float x4;                          ///< 1.21, not read
    /* +08 */ float specialhi_momentum_stick_range; ///< 0.009 (+1CA0)
    /* +0C */ float specialhi_angle_diff;        ///< the integer 23, read as a float (+1D18)
    /* +10 */ float specialhi_reverse_stick_range; ///< 0.4 (+1CB8)
    /* +14 */ float x14;                         ///< 0, not read
    /* +18 */ float specials_vel_x;              ///< 1.45: the kick's speed (+2248, +2368)
    /* +1C */ float x1C;                         ///< 1.6, not read
    /* +20 */ float x20;                         ///< 0
    /* +24 */ float x24;                         ///< 0
    /* +28 */ float x28;                         ///< 0.01
    /* +2C */ float x2C;                         ///< 0.08
    /* +30 */ float x30;                         ///< 0.2
    /* +34 */ float specialairsend_friction;     ///< 0.1 (+259C)
    /* +38 */ float x38;                         ///< 0.0012
    /* +3C */ float x3C;                         ///< 0.07
    /* +40 */ float x40;                         ///< 3
    /* +44 */ float specialhi_freefall_mobility; ///< 0.74 (+1C50)
    /* +48 */ float specialhi_landing_lag;       ///< 20 (+1C48, +2FD0, +2FE4)
    /* +4C */ float x4C;                         ///< 0.27
    /* +50 */ float specialhi_drift_accel;       ///< 0.5 (+1DE8)
    /* +54 */ float x54;                         ///< 0.27
    /* +58 */ float specialhi_drift_max;         ///< 0.85 (+1DE4)
    /* +5C */ float x5C;                         ///< 0.5
    /* +60 */ float specialairlw_terminal_vel;   ///< 1.0 (+20DC)
    /* +64 */ float x64;                         ///< 0.7
    /* +68 */ float x68;                         ///< 0.6
    /* +6C */ float specialairlw_gravity;        ///< 1.7 (+20E0)
    /* +70 */ float specialairlw_vel_x_decay;    ///< 0.07 (+2100)
    /* +74 */ float x74;                         ///< 4
    /* +78 */ float x78;                         ///< 0.1
    /* +7C */ float x7C;                         ///< 0
    /* +80 */ float x80;                         ///< 0
    /* +84 */ float x84;                         ///< 0
    /* +88 */ u32 attacklw3_light_frame;         ///< 4: the last frame a released down tilt
                                                 ///< turns light (+0E34)
} DISC_STRUCT ftCn_DatAttrs;

_Static_assert(sizeof(ftCn_DatAttrs) == 0x8C, "the size the load and the respawn event copy");

/* ---- Fighter variables (fp->u, console fp+222C) ----
 * Zelda's ondeath (MxDt's default for slot 1) clears u.zd.x222C, a pointer: it ends before
 * +0x14 on the console and on the native build. */
typedef struct ftCn_FighterVars {
    /* fp+222C */ u8 x222C[0x14];       ///< not used by her code
    /* fp+2240 */ s32 specialairs_used; ///< 1 after the air side special, until landing
    /* fp+2244 */ s32 specialairlw_used; ///< 1 after the air down special, until landing
} ftCn_FighterVars;

_Static_assert(offsetof(ftCn_FighterVars, specialairs_used) == 0x14 &&
                   offsetof(ftCn_FighterVars, specialairlw_used) == 0x18,
               "fp+2240 and fp+2244");
_Static_assert(sizeof(ftCn_FighterVars) <= sizeof(((Fighter*) 0)->u), "her variables fit");

static inline ftCn_DatAttrs* ftCn_Attrs(Fighter* fp)
{
    return (ftCn_DatAttrs*) fp->dat_attrs;
}

static inline ftCn_FighterVars* ftCn_Vars(Fighter* fp)
{
    return (ftCn_FighterVars*) &fp->u;
}

/* ---- Articles (ftData->x48_items, in the order of her MxDt item lookup: 346 to 350) --------
 * Her load hands the first four to the item code by index (m-ex MEX_IndexFighterItem). 0 and 1
 * have no code and nothing of hers creates them; 4 is in the lookup and never registered. */
enum {
    ftCn_Article_0 = 0,    /* 346: no code */
    ftCn_Article_1 = 1,    /* 347: no code */
    ftCn_Article_Shot = 2, /* 348: the neutral special's shot */
    ftCn_Article_ShotAlt = 3, /* 349: the B + A shot */
    ftCn_Article_Count = 5, /* 350 is the fifth entry of the lookup */
};

/* The part whose bone the two shots start from (+296C: lwz r3, 0x560(r7), fp->parts + 86 * 0x10
 * on the console). */
#define FTCN_SHOT_PART 86

/* The main stick bits of HSD_Pad (HSD_PadADConvertCheck1, controller.c: up, down, left, right). */
#define FTCN_PAD_LS_UP (1u << 16)
#define FTCN_PAD_LS_DOWN (1u << 17)
#define FTCN_PAD_LS_LEFT (1u << 18)
#define FTCN_PAD_LS_RIGHT (1u << 19)
#define FTCN_PAD_LS_ANY (0xFu << 16)

/* The transition flags of her side special's ground and air swaps (+1A9C, +1BAC, +24E0,
 * +2604: lis 0x0C4C, ori 0x5080). */
#define FTCN_SPECIALS_COLL_MF ((MotionFlags) 0x0C4C5080)

/* ---- chunli.c ---- */
extern const MotionState ftCn_Init_MotionStateTable[ftCn_MS_SelfCount];
/* +1754 [MexCPU_InitProc] and the rest of the m-ex CPU helper. */
void ftCn_Cpu_InitSpoofData(HSD_GObj* gobj);
/* An empty callback (+1840, +1910, +1A28, +1B38, +1FE0, +2230, +2350, +2464, +2564, +2874:
 * each a lone blr). */
void ftCn_Empty(HSD_GObj* gobj);

/* ---- chunli_attacks.c ---- */
/* +07BC */ void ftCn_Init_OnFrame(HSD_GObj* gobj);
/* +263C */ void ftCn_AttackLw3Light_Anim(HSD_GObj* gobj);
/* +26A8 */ void ftCn_AttackLw3Light_IASA(HSD_GObj* gobj);
/* +2718 */ void ftCn_AttackLw3Light_Coll(HSD_GObj* gobj);
/* +27B0 */ void ftCn_AttackHi3Light_IASA(HSD_GObj* gobj);
/* +2820 */ void ftCn_AttackHi3Light_Coll(HSD_GObj* gobj);
/* +2A7C */ void ftCn_AttackLw3Light_Cancel(HSD_GObj* gobj);
/* +2FEC */ void ftCn_AttackHi3Light_Cancel(HSD_GObj* gobj);

/* ---- chunli_specialn.c ---- */
/* +046C */ void ftCn_SpecialN_Enter(HSD_GObj* gobj);
/* +04E4 */ void ftCn_SpecialAirN_Enter(HSD_GObj* gobj);
/* +17F4 */ void ftCn_SpecialN_Anim(HSD_GObj* gobj);
/* +1848 */ void ftCn_SpecialN_Coll(HSD_GObj* gobj);
/* +18C4 */ void ftCn_SpecialAirN_Anim(HSD_GObj* gobj);
/* +1918 */ void ftCn_SpecialAirN_Coll(HSD_GObj* gobj);
/* +2954 */ void ftCn_SpecialN_OnAccessory4(HSD_GObj* gobj);
/* +2CBC */ void ftCn_SpecialNAlt_OnAccessory4(HSD_GObj* gobj);

/* ---- chunli_specials.c ---- */
/* +29D0 */ void ftCn_SpecialS_Enter(HSD_GObj* gobj);
/* +0564 */ void ftCn_SpecialAirS_Enter(HSD_GObj* gobj);
/* +19D8 */ void ftCn_SpecialSStart_Anim(HSD_GObj* gobj);
/* +1A2C */ void ftCn_SpecialSStart_Phys(HSD_GObj* gobj);
/* +1A5C */ void ftCn_SpecialSStart_Coll(HSD_GObj* gobj);
/* +1B6C */ void ftCn_SpecialAirSStart_Coll(HSD_GObj* gobj);
/* +21C4 */ void ftCn_SpecialSLoop_Anim(HSD_GObj* gobj);
/* +2234 */ void ftCn_SpecialSLoop_Phys(HSD_GObj* gobj);
/* +225C */ void ftCn_SpecialSLoop_Coll(HSD_GObj* gobj);
/* +22E4 */ void ftCn_SpecialAirSLoop_Anim(HSD_GObj* gobj);
/* +237C */ void ftCn_SpecialAirSLoop_Coll(HSD_GObj* gobj);
/* +2468 */ void ftCn_SpecialSEnd_Phys(HSD_GObj* gobj);
/* +2498 */ void ftCn_SpecialSEnd_Coll(HSD_GObj* gobj);
/* +2568 */ void ftCn_SpecialAirSEnd_Phys(HSD_GObj* gobj);
/* +25BC */ void ftCn_SpecialAirSEnd_Coll(HSD_GObj* gobj);

/* ---- chunli_specialhi.c ---- */
/* +05C4 */ void ftCn_SpecialHi_Enter(HSD_GObj* gobj);
/* +062C */ void ftCn_SpecialAirHi_Enter(HSD_GObj* gobj);
/* +1BF8 */ void ftCn_SpecialHi_Anim(HSD_GObj* gobj);
/* +1C70 */ void ftCn_SpecialHi_IASA(HSD_GObj* gobj);
/* +1D90 */ void ftCn_SpecialHi_Phys(HSD_GObj* gobj);
/* +1E0C */ void ftCn_SpecialHi_Coll(HSD_GObj* gobj);
/* +1F4C */ void ftCn_SpecialAirHi_Coll(HSD_GObj* gobj);

/* ---- chunli_speciallw.c ---- */
/* +067C */ void ftCn_SpecialLw_Enter(HSD_GObj* gobj);
/* +06CC */ void ftCn_SpecialAirLw_Enter(HSD_GObj* gobj);
/* +1FE8 */ void ftCn_SpecialLw_Coll(HSD_GObj* gobj);
/* +20C0 */ void ftCn_SpecialAirLw_Phys(HSD_GObj* gobj);
/* +2150 */ void ftCn_SpecialAirLw_Coll(HSD_GObj* gobj);

/* ---- chunli_items.c ---- */
/* +325C */ void itCn_Shot_Spawn(HSD_GObj* owner_gobj, Vec3* pos, ItemKind kind,
                                 float facing_dir);
extern ItemLogicTable* const itCn_ArticleTables[ftCn_Article_Count];

#endif
