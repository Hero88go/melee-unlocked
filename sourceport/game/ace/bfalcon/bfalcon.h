/* ACE 2.0.0's B. Falcon, native (PlBF.dat, ftDataBloodF; m-ex internal 45, external 44).
 *
 * A clone of Captain Falcon: his fighter file exports four m-ex slots (onload, ondeath,
 * move_logic, onframe) and MxDt names Captain Falcon's retail function for every other slot.
 * His state table is Captain Falcon's 23 rows with seven callbacks replaced, and six more
 * states (364 to 369): the neutral special can be charged. His special attributes are a
 * ftCaptain_DatAttrs block in his own file, read by Captain Falcon's code.
 *
 * Offsets in the comments (+04B0) are offsets into the ftFunction code block of PlBF.dat, as in
 * run-source/rel09-ace-native/bfalcon/listing.txt; names in quotes are the file's own symbols. */
#ifndef MU_ACE_BFALCON_H
#define MU_ACE_BFALCON_H

#include <Runtime/platform.h>

#include <melee/ft/forward.h>
#include <sysdolphin/baselib/forward.h>

#include <melee/ft/kinds/ftCaptain/forward.h>
#include <melee/ft/kinds/ftCaptain/types.h>
#include <melee/ft/types.h>

#include <stddef.h>

#include "../../akaneia/mu_ak_fighter.h"

/* States 341 to 363 are Captain Falcon's, same ids, animations, flags and move ids. The six
 * behind them are his own; all six are neutral special states (flags and move id of 347, 348). */
typedef enum ftBf_MotionState {
    ftBf_MS_SpecialNCharge2 = ftCa_MS_Count, /* 364 (0x16C) */
    ftBf_MS_SpecialAirNCharge2,              /* 365 (0x16D) */
    ftBf_MS_SpecialNCharge0,                 /* 366 (0x16E) */
    ftBf_MS_SpecialAirNCharge0,              /* 367 (0x16F) */
    ftBf_MS_SpecialNCharge1,                 /* 368 (0x170) */
    ftBf_MS_SpecialAirNCharge1,              /* 369 (0x171) */
    ftBf_MS_Count,
    ftBf_MS_SelfCount = ftBf_MS_Count - ftCo_MS_Count,
} ftBf_MotionState;

/* Their animations follow Captain Falcon's 23 in PlBFAJ.dat (318 to 323). */
typedef enum ftBf_Submotion {
    ftBf_SM_SpecialNCharge2 = ftCa_SM_Count,
    ftBf_SM_SpecialAirNCharge2,
    ftBf_SM_SpecialNCharge0,
    ftBf_SM_SpecialAirNCharge0,
    ftBf_SM_SpecialNCharge1,
    ftBf_SM_SpecialAirNCharge1,
} ftBf_Submotion;

_Static_assert(ftBf_MS_SpecialNCharge2 == 0x16C && ftBf_MS_SpecialAirNCharge1 == 0x171,
               "the state ids PlBF.dat passes to Fighter_ChangeMotionState");
_Static_assert(ftBf_MS_SelfCount == 29, "29 rows in PlBF.dat's move_logic");
_Static_assert(ftBf_SM_SpecialNCharge2 == 318, "animation id of state 364 in PlBF.dat");

/* fp->u (fp+222C). Captain Falcon's two words stay where they are: his retail side special reads
 * and writes them. B. Falcon's own two words sit far behind them. */
typedef struct ftBf_FighterVars {
    /* fp+222C */ u32 during_specials_start; /* Captain Falcon's */
    /* fp+2230 */ u32 during_specials;       /* Captain Falcon's */
    /* fp+2234 */ u8 x8[0xCC - 0x8];
    /* fp+22F8 */ s32 xCC;    /* cleared at load and respawn, never read */
    /* fp+22FC */ s32 charge; /* the neutral special's charge: 0, 1, then 3 or more */
} ftBf_FighterVars;

_Static_assert(offsetof(ftBf_FighterVars, xCC) == 0x22F8 - 0x222C, "fp+22F8");
_Static_assert(offsetof(ftBf_FighterVars, charge) == 0x22FC - 0x222C, "fp+22FC");
_Static_assert(sizeof(ftBf_FighterVars) <= FIGHTERVARS_SIZE, "inside the fighter vars");

static inline ftBf_FighterVars* ftBf_Vars(Fighter* fp)
{
    return (ftBf_FighterVars*) &fp->u;
}

/* The color overlay of a charged punch (ftCo_800BFFD0's id). */
#define ftBf_ColAnim_Charged ((FtColAnim) 0x2E)

/* bfalcon.c */
extern const MotionState ftBf_Init_MotionStateTable[ftBf_MS_SelfCount];

/* bfalcon_specials.c: the callbacks that are his own */
/* +04B0 */ void ftBf_SpecialN_IASA(HSD_GObj* gobj);
/* +088C */ void ftBf_SpecialAirN_IASA(HSD_GObj* gobj);
/* +0C68 */ void ftBf_SpecialAirSStart_IASA(HSD_GObj* gobj);
/* +0CD0 */ void ftBf_SpecialAirS_IASA(HSD_GObj* gobj);
/* +0D38 */ void ftBf_SpecialLw_IASA(HSD_GObj* gobj);
/* +0D4C */ void ftBf_SpecialHiThrow1_Coll(HSD_GObj* gobj);
/* +0DC0 */ void ftBf_SpecialNCharge_Anim(HSD_GObj* gobj);
/* +0E18 */ void ftBf_SpecialNCharge_Coll(HSD_GObj* gobj);
/* +0E58 */ void ftBf_SpecialAirNCharge_Anim(HSD_GObj* gobj);
/* +0ECC */ void ftBf_SpecialAirNCharge2_Coll(HSD_GObj* gobj);
/* +0F44 */ void ftBf_SpecialAirNCharge0_Coll(HSD_GObj* gobj);

#endif
