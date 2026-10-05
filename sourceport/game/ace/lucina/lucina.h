/* ACE 2.0.0's Lucina, native (PlLu.dat, ftDataLucina; m-ex internal 53, external 52).
 *
 * A clone of Marth: her fighter file exports six m-ex slots (onload, ondeath, move_logic,
 * onrespawn, onlanding, GetTrailData) and MxDt names Marth's retail function for every other
 * slot. Her state table is Marth's row for row but for 12 callbacks in the nine aerial Dancing
 * Blade states. Her special attributes are a MarsAttributes block in her own file, read by
 * Marth's code.
 *
 * Offsets in the comments (+0954) are offsets into the ftFunction code block of PlLu.dat, as in
 * run-source/rel09-ace-native/lucina/listing.txt; names in quotes are the file's own symbols. */
#ifndef MU_ACE_LUCINA_H
#define MU_ACE_LUCINA_H

#include <Runtime/platform.h>

#include <melee/ft/forward.h>
#include <sysdolphin/baselib/forward.h>

#include <melee/ft/kinds/ftMars/forward.h>
#include <melee/ft/kinds/ftMars/types.h>
#include <melee/ft/types.h>

#include <stddef.h>

#include "../../akaneia/mu_ak_fighter.h"

/* The states are Marth's, same ids (341 to 372), animations, flags and move ids. */
#define ftLu_MS_SelfCount ftMs_MS_SelfCount

/* fp->u (fp+222C). Marth's word stays where it is: his retail side special reads and writes it
 * (ftMs_SpecialAirS_Enter). Lucina's own two words sit far behind it. */
typedef struct ftLu_FighterVars {
    /* fp+222C */ u32 x222C;               /* Marth's: an aerial Dancing Blade was started */
    /* fp+2230 */ u8 x4[0xC8 - 0x4];
    /* fp+22F4 */ s32 specialairs_lifted;  /* the first swing's lift was given this airtime */
    /* fp+22F8 */ s32 xCC;
    /* fp+22FC */ s32 specialairs_count;   /* aerial first swings since the last landing */
} ftLu_FighterVars;

_Static_assert(offsetof(ftLu_FighterVars, specialairs_lifted) == 0x22F4 - 0x222C, "fp+22F4");
_Static_assert(offsetof(ftLu_FighterVars, specialairs_count) == 0x22FC - 0x222C, "fp+22FC");
_Static_assert(sizeof(ftLu_FighterVars) <= FIGHTERVARS_SIZE, "inside the fighter vars");

static inline ftLu_FighterVars* ftLu_Vars(Fighter* fp)
{
    return (ftLu_FighterVars*) &fp->u;
}

/* lucina.c */
extern const MotionState ftLu_Init_MotionStateTable[ftLu_MS_SelfCount];

/* lucina_specials.c: the aerial Dancing Blade callbacks that are her own */
/* +0954 */ void ftLu_SpecialAirS1_Phys(HSD_GObj* gobj);
/* +0A48 */ void ftLu_SpecialAirS_Coll(HSD_GObj* gobj);
/* +0A70 */ void ftLu_SpecialAirS2_Phys(HSD_GObj* gobj);
/* +0BA8 */ void ftLu_SpecialS_Land(HSD_GObj* gobj);

#endif
