/* ACE 2.0.0's Meta Knight, native (PlMk.dat, ftDataMeta; m-ex internal 42, external 41,
 * native kind 0x31).
 *
 * Meta Knight ships as m-ex PowerPC in PlMk.dat: one "ftFunction" block built without symbols
 * (0x3CE0 bytes, 830 relocations, 14 exports) and no "itFunction" block. MxDt names Mario's
 * retail function for the slots he does not export (ondeath, the item drop of slot 18, the
 * attribute reload of slot 25, the demo state table of slot 40); the registry fills those.
 * Everything else is the file's own code: 29 of his 30 states, and five common states whose
 * callbacks his load rewrites in a private copy of the common state table.
 *
 *   neutral  a spin: start, 45 frames of the loop steered with the stick, then an end
 *   side     a drill steered up and down with the stick, at a fixed speed of 1.5
 *   up       a rise steered with the stick, then a special fall
 *   down     a short start; with B held a dash in one of eight stick directions at 2.5, then
 *            one of four follow-ups by stick and button (neutral, forward, back, release)
 *   common   forward tilt chains two more hits on A (354, 369); forward smash holds on A and
 *            scales its hitbox damage with the frames held (367); five air jumps with their
 *            own turn-around rotation (365, 366, and the common 27 and 28)
 *
 * The file carries the m-ex CPU helper (MexCPU_InitSpoofData, InitProc, ProcSpoof, Process):
 * a CPU Meta Knight runs the game's CPU step with his kind spoofed to 0x1C for the call.
 *
 * Offsets in the comments (+0AA0) are offsets into the ftFunction block, as in
 * run-source/rel09-ace-native/metaknight/listing.txt. The file has no symbols: the function
 * names are this port's. "fp+" is a console offset of the Fighter. */
#ifndef MU_ACE_METAKNIGHT_H
#define MU_ACE_METAKNIGHT_H

#include <Runtime/platform.h>

#include <melee/ft/forward.h>
#include <sysdolphin/baselib/forward.h>

#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/types.h>

#include <stddef.h>

#include "../../akaneia/mu_ak_fighter.h"
#include "mu_disc.h"

/* ---- Action states (move_logic, +0120, numbered from ftCo_MS_Count) ---- */
typedef enum ftMetaKnight_MotionState {
    ftMk_MS_SpecialNStart = ftCo_MS_Count, // 341 (0x155)
    ftMk_MS_SpecialNLoop,                  // 342 (0x156): 45 frames
    ftMk_MS_SpecialNEnd,                   // 343 (0x157)
    ftMk_MS_SpecialAirNStart,              // 344 (0x158)
    ftMk_MS_SpecialAirNEnd,                // 345 (0x159)
    ftMk_MS_SpecialSStart,                 // 346 (0x15A)
    ftMk_MS_SpecialSDrill,                 // 347 (0x15B)
    ftMk_MS_SpecialSEnd,                   // 348 (0x15C)
    ftMk_MS_SpecialAirSStart,              // 349 (0x15D)
    ftMk_MS_SpecialAirSEnd,                // 350 (0x15E)
    ftMk_MS_SpecialHiStart,                // 351 (0x15F)
    ftMk_MS_SpecialHiRise,                 // 352 (0x160)
    ftMk_MS_SpecialAirHiStart,             // 353 (0x161)
    ftMk_MS_AttackS3S2,                    // 354 (0x162): the second forward tilt
    ftMk_MS_SpecialLwStart,                // 355 (0x163)
    ftMk_MS_SpecialLwN,                    // 356 (0x164)
    ftMk_MS_SpecialLwF,                    // 357 (0x165)
    ftMk_MS_SpecialLwB,                    // 358 (0x166)
    ftMk_MS_SpecialLwEnd,                  // 359 (0x167)
    ftMk_MS_SpecialAirLwStart,             // 360 (0x168)
    ftMk_MS_SpecialAirLwN,                 // 361 (0x169)
    ftMk_MS_SpecialAirLwF,                 // 362 (0x16A)
    ftMk_MS_SpecialAirLwB,                 // 363 (0x16B)
    ftMk_MS_SpecialAirLwEnd,               // 364 (0x16C)
    ftMk_MS_JumpAerial2,                   // 365 (0x16D): the air jumps after the first
    ftMk_MS_JumpAerialLast,                // 366 (0x16E): the last air jump
    ftMk_MS_AttackS4Hold,                  // 367 (0x16F): forward smash held on A
    ftMk_MS_Empty,                         // 368 (0x170): no callbacks
    ftMk_MS_AttackS3S3,                    // 369 (0x171): the third forward tilt
    ftMk_MS_SpecialHiEnd,                  // 370 (0x172)
    ftMk_MS_Count,
    ftMk_MS_SelfCount = ftMk_MS_Count - ftCo_MS_Count,
} ftMetaKnight_MotionState;

_Static_assert(ftMk_MS_SpecialNStart == 0x155 && ftMk_MS_SpecialHiEnd == 0x172 &&
                   ftMk_MS_SelfCount == 30,
               "the state numbers PlMk.dat's code passes to Fighter_ChangeMotionState");

/* ---- Fighter variables (fp->u, console fp+222C) ----
 * Mario's retail ondeath (the MxDt default of slot 1) writes Mario's fields at the start of the
 * union; Meta Knight's own words sit far past them. */
typedef struct ftMk_FighterVars {
    /* fp+222C */ u8 x222C[0xC8];
    /* fp+22F4 */ s32 prev_motion; ///< the motion id the last state change saw
    /* fp+22F8 */ s32 parts_on;    ///< 1: every state change shows model part 1 and hides part 2
    /* Native only: the console byte fp+222B (the pad byte after the bit fields of fp+222A),
     * which his code writes and reads as the low byte of the word at fp+2228. */
    /* (none) */ u8 x222B;
} ftMk_FighterVars;

/* ---- State variables (fp->mv, console fp+2340). Words his code reads as an integer in one
 * state and as a float in another are unions. ---- */
typedef union ftMk_Word {
    s32 i;
    float f;
} ftMk_Word;

typedef struct ftMk_MotionVars {
    /* fp+2340 */ u8 x2340[0x24];
    /* fp+2364 */ ftMk_Word x2364; ///< int: a frame counter or a saved motion id; float: an angle
    /* fp+2368 */ ftMk_Word x2368; ///< int: frames held, or the jump turn kind; float: an angle
    /* fp+236C */ s32 x236C;       ///< int: a flag, or the frames of the jump turn
    /* fp+2370 */ u8 x2370[4];     ///< one byte per hitbox: its damage was scaled already
} ftMk_MotionVars;

_Static_assert(offsetof(ftMk_MotionVars, x2364) == 0x24 && offsetof(ftMk_MotionVars, x2370) == 0x30,
               "console offsets of the state variables");
_Static_assert(offsetof(ftMk_FighterVars, prev_motion) == 0xC8 &&
                   offsetof(ftMk_FighterVars, parts_on) == 0xCC,
               "console offsets of the fighter variables");
_Static_assert(sizeof(ftMk_MotionVars) <= sizeof(((Fighter*) 0)->mv), "Meta Knight motion vars");
_Static_assert(sizeof(ftMk_FighterVars) <= sizeof(((Fighter*) 0)->u), "Meta Knight fighter vars");

static inline ftMk_FighterVars* ftMk_Vars(Fighter* fp)
{
    return (ftMk_FighterVars*) &fp->u;
}

static inline ftMk_MotionVars* ftMk_MVars(Fighter* fp)
{
    return (ftMk_MotionVars*) &fp->mv;
}

/* The word at fp+2228 as his code uses it (stw / lwz of the four bytes fp+2228 to fp+222B):
 * the bit fields of fp+2228, fp+2229, fp+222A and the pad byte fp+222B. He writes only 0 and 1;
 * both clear every bit field of the three bytes and put the value in the pad byte. */
static inline void ftMk_Word2228_Set(Fighter* fp, u8 value)
{
    fp->x2228_b0 = 0;
    fp->x2228_b1 = 0;
    fp->is_sandbag = 0;
    fp->x2228_b3 = 0;
    fp->x2228_b5 = 0;
    fp->used_tether = 0;
    fp->x2228_b7 = 0;
    fp->x2229_b0 = 0;
    fp->x2229_b1 = 0;
    fp->x2229_b2 = 0;
    fp->x2229_b3 = 0;
    fp->x2229_b4 = 0;
    fp->no_normal_motion = 0;
    fp->x2229_b6 = 0;
    fp->no_kb = 0;
    fp->x222A_b0 = 0;
    fp->x222A_b1 = 0;
    fp->x222A_b2 = 0;
    fp->x222A_b3 = 0;
    fp->x222A_b5 = 0;
    fp->x222A_b6 = 0;
    fp->x222A_b7 = 0;
    ftMk_Vars(fp)->x222B = value;
}

static inline bool ftMk_Word2228_IsZero(Fighter* fp)
{
    return !(fp->x2228_b0 | fp->x2228_b1 | fp->is_sandbag | fp->x2228_b3 | fp->x2228_b5 |
             fp->used_tether | fp->x2228_b7 | fp->x2229_b0 | fp->x2229_b1 | fp->x2229_b2 |
             fp->x2229_b3 | fp->x2229_b4 | fp->no_normal_motion | fp->x2229_b6 | fp->no_kb |
             fp->x222A_b0 | fp->x222A_b1 | fp->x222A_b2 | fp->x222A_b3 | fp->x222A_b5 |
             fp->x222A_b6 | fp->x222A_b7 | ftMk_Vars(fp)->x222B);
}

/* The Fighter_ChangeMotionState call nearly every routine makes (+2CB8 0.0, +2D34 1.0). */
#define FTMK_CHANGE(gobj, msid) \
    Fighter_ChangeMotionState((gobj), (msid), Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL)
/* The same from the current animation frame (the landings and the walk-offs). */
#define FTMK_CHANGE_AT_FRAME(gobj, fp, msid) \
    Fighter_ChangeMotionState((gobj), (msid), Ft_MF_None, (fp)->cur_anim_frame, 1.0f, 0.0f, NULL)

/* Buttons of fp->input (HSD_Pad). +0D40 tests held & 0x10C00: X, Y and bit 16. */
#define FTMK_PAD_JUMP (HSD_PAD_XY | (1 << 16))

/* ---- metaknight.c ---- */
extern const MotionState ftMk_Init_MotionStateTable[ftMk_MS_SelfCount];
/* +0840 (slot 32) */ void ftMk_Init_EnterDoubleJump(HSD_GObj* gobj);

/* ---- metaknight_attacks.c: the common states his load rewrites, and 354, 365 to 369 ---- */
/* +0AA0 */ void ftMk_AttackS4S_Anim(HSD_GObj* gobj);
/* +0C50 */ void ftMk_AttackS3S_Anim(HSD_GObj* gobj);
/* +0C9C */ void ftMk_AttackS3S_IASA(HSD_GObj* gobj);
/* +0CE4 */ void ftMk_JumpAerial_Anim(HSD_GObj* gobj);
/* +1F6C */ void ftMk_AttackS3S2_IASA(HSD_GObj* gobj);
/* +2B10 */ void ftMk_JumpAerial_IASA(HSD_GObj* gobj);
/* +2B14 */ void ftMk_JumpAerial_Phys(HSD_GObj* gobj);
/* +2B48 */ void ftMk_AttackS4Hold_Anim(HSD_GObj* gobj);

/* ---- metaknight_specialn.c ---- */
/* +04E0 */ void ftMk_SpecialN_Enter(HSD_GObj* gobj);
/* +0504 */ void ftMk_SpecialAirN_Enter(HSD_GObj* gobj);
/* +1328 */ void ftMk_SpecialNStart_Anim(HSD_GObj* gobj);
/* +13B8 */ void ftMk_SpecialNStart_Coll(HSD_GObj* gobj);
/* +1404 */ void ftMk_SpecialNLoop_Anim(HSD_GObj* gobj);
/* +149C */ void ftMk_SpecialNLoop_Phys(HSD_GObj* gobj);
/* +1630 */ void ftMk_SpecialNLoop_Coll(HSD_GObj* gobj);
/* +16AC */ void ftMk_SpecialNEnd_Anim(HSD_GObj* gobj);
/* +16F8 */ void ftMk_SpecialAirN_Phys(HSD_GObj* gobj);
/* +16FC */ void ftMk_SpecialAirNStart_Coll(HSD_GObj* gobj);
/* +1748 */ void ftMk_SpecialAirNEnd_Anim(HSD_GObj* gobj);
/* +17CC */ void ftMk_SpecialAirNEnd_Coll(HSD_GObj* gobj);

/* ---- metaknight_specials.c ---- */
/* +0528 */ void ftMk_SpecialS_Enter(HSD_GObj* gobj);
/* +054C */ void ftMk_SpecialAirS_Enter(HSD_GObj* gobj);
/* +17D8 */ void ftMk_SpecialSStart_Anim(HSD_GObj* gobj);
/* +189C */ void ftMk_SpecialS_GroundColl(HSD_GObj* gobj);
/* +18E8 */ void ftMk_SpecialSDrill_Anim(HSD_GObj* gobj);
/* +1978 */ void ftMk_SpecialSDrill_Phys(HSD_GObj* gobj);
/* +1AEC */ void ftMk_SpecialSDrill_Coll(HSD_GObj* gobj);
/* +1B6C */ void ftMk_SpecialSEnd_Anim(HSD_GObj* gobj);
/* +1BB8 */ void ftMk_SpecialAirSStart_Phys(HSD_GObj* gobj);
/* +1BD0 */ void ftMk_SpecialAirSStart_Coll(HSD_GObj* gobj);
/* +1C1C */ void ftMk_SpecialAirSEnd_Anim(HSD_GObj* gobj);
/* +1CA0 */ void ftMk_SpecialAirSEnd_Phys(HSD_GObj* gobj);
/* +1CA4 */ void ftMk_SpecialAirSEnd_Coll(HSD_GObj* gobj);

/* ---- metaknight_specialhi.c ---- */
/* +0570 */ void ftMk_SpecialHi_Enter(HSD_GObj* gobj);
/* +0594 */ void ftMk_SpecialAirHi_Enter(HSD_GObj* gobj);
/* +1D10 */ void ftMk_SpecialHiStart_Anim(HSD_GObj* gobj);
/* +1DE4 */ void ftMk_SpecialHiRise_Anim(HSD_GObj* gobj);
/* +1E50 */ void ftMk_SpecialHiRise_Phys(HSD_GObj* gobj);
/* +1F10 */ void ftMk_SpecialHi_AirColl(HSD_GObj* gobj);
/* +1F68 */ void ftMk_SpecialAirHiStart_Anim(HSD_GObj* gobj);
/* +2BD8 */ void ftMk_SpecialHiEnd_Anim(HSD_GObj* gobj);
/* +2C58 */ void ftMk_SpecialHiEnd_Phys(HSD_GObj* gobj);

/* ---- metaknight_speciallw.c ---- */
/* +05C4 */ void ftMk_SpecialLw_Enter(HSD_GObj* gobj);
/* +0624 */ void ftMk_SpecialAirLw_Enter(HSD_GObj* gobj);
/* +1FB4 */ void ftMk_SpecialLwStart_Anim(HSD_GObj* gobj);
/* +2444 */ void ftMk_SpecialLwStart_Phys(HSD_GObj* gobj);
/* +2448 */ void ftMk_SpecialLw_Coll(HSD_GObj* gobj);
/* +2494 */ void ftMk_SpecialLwDash_Anim(HSD_GObj* gobj);
/* +24E8 */ void ftMk_SpecialLwDash_Phys(HSD_GObj* gobj);
/* +24EC */ void ftMk_SpecialAirLwStart_Anim(HSD_GObj* gobj);
/* +2990 */ void ftMk_SpecialAirLwStart_Phys(HSD_GObj* gobj);
/* +29A8 */ void ftMk_SpecialAirLw_Coll(HSD_GObj* gobj);
/* +2A30 */ void ftMk_SpecialAirLwDash_Anim(HSD_GObj* gobj);
/* +2ABC */ void ftMk_SpecialAirLwDash_Phys(HSD_GObj* gobj);
/* +2E8C */ void ftMk_SpecialLw_Effect(HSD_GObj* gobj);

#endif
