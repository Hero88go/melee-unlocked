/* ACE 2.0.0's Metal Mario, native (PlMM.dat, ftDataMetalMario; m-ex internal 46, external 45).
 *
 * Metal Mario ships in PlMM.dat as m-ex PowerPC (the "ftFunction" and "itFunction" roots, built
 * without symbols). He is a clone of Mario: the file exports six m-ex slots (onload, move_logic,
 * specialn, specialairn, specialhi, specialairhi) and MxDt names Mario's retail function for
 * every other slot. His state table is Mario's ten rows with the sixteen callbacks of the
 * neutral and up special states pointing at code of his own (eight of them make the same calls
 * as Mario's functions, which are named instead), and one more state (351): a dive the up
 * special goes into while B is held. His neutral special throws
 * his own article 0 in place of Mario's fireball. His special attributes are Mario's block
 * (ftMario_DatAttrs, the same values as PlMr.dat), read from his own file.
 *
 * Offsets in the comments (code+0x698) are offsets into the ftFunction or itFunction code block
 * of PlMM.dat, as in run-source/rel09-ace-native/metalmario/listing.txt and FUNCTIONS.md.
 * Console offsets (fp+2210) are Fighter / Item offsets on the GameCube, for cross-checking
 * against the PowerPC; the code itself only uses the decomp's field names. */
#ifndef MU_ACE_METALMARIO_H
#define MU_ACE_METALMARIO_H

#include <Runtime/platform.h>

#include <melee/ft/forward.h>
#include <melee/it/forward.h>
#include <sysdolphin/baselib/forward.h>

#include <dolphin/mtx.h>
#include <melee/ft/kinds/ftMario/forward.h>
#include <melee/ft/kinds/ftMario/types.h>
#include <melee/ft/types.h>
#include <melee/it/kinds/forward.h>
#include <melee/it/kinds/types.h>
#include <melee/it/types.h>

#include "../../akaneia/mu_ak_fighter.h"
#include "mu_disc.h"

/* ---- Action states ----------------------------------------------------------------------------
 * 341 to 350 are Mario's (ftMario_MotionState), same ids, animations, flags and move ids, so
 * Mario's names are used as they are. One state of his own follows. */
typedef enum ftMM_MotionState {
    ftMM_MS_SpecialHiDive = ftMr_MS_Count, /* 351 (0x15F) */
    ftMM_MS_Count,
    ftMM_MS_SelfCount = ftMM_MS_Count - ftCo_MS_Count,
} ftMM_MotionState;

/* Its animation follows Mario's eight in PlMMAJ.dat (303). */
#define ftMM_SM_SpecialHiDive ftMr_SM_Count

_Static_assert(ftMM_MS_SpecialHiDive == 0x15F, "the state id PlMM.dat passes at code+0x730");
_Static_assert(ftMM_MS_SelfCount == 11, "11 rows in PlMM.dat's move_logic");
_Static_assert(ftMM_SM_SpecialHiDive == 303, "animation id of state 351 in PlMM.dat");

/* ---- Articles (ftData->x48_items, in the order of his MxDt item lookup: 325, 326, 327) ---- */
enum {
    ftMM_Article_Fire = 0, /* item kind 325 on ACE 2.0.0: the neutral special's projectile */
    ftMM_Article_Unused = 1, /* 326: no article in the file */
    ftMM_Article_Cape = 2, /* 327: registered by onload; no code and nothing creates it */
    ftMM_Article_Count = 1, /* articles with code */
};

/* ---- metalmario_specialn.c ---- */
/* code+0x224 */ void ftMM_SpecialN_Enter(HSD_GObj* gobj);
/* code+0x2A4 */ void ftMM_SpecialAirN_Enter(HSD_GObj* gobj);
/* code+0x514 */ void ftMM_SpecialN_Coll(HSD_GObj* gobj);
/* code+0x608 */ void ftMM_SpecialAirN_Coll(HSD_GObj* gobj);
/* code+0xB08 */ void ftMM_SpecialN_FireSpawn(HSD_GObj* gobj);

/* ---- metalmario_specialhi.c ---- */
/* code+0x324 */ void ftMM_SpecialHi_Enter(HSD_GObj* gobj);
/* code+0x38C */ void ftMM_SpecialAirHi_Enter(HSD_GObj* gobj);
/* code+0x698, +0x8E8 */ void ftMM_SpecialHi_Anim(HSD_GObj* gobj);
/* code+0x758, +0x9A8 */ void ftMM_SpecialHi_IASA(HSD_GObj* gobj);
/* code+0x890, +0xA40 */ void ftMM_SpecialHi_Coll(HSD_GObj* gobj);
/* code+0xA98 */ void ftMM_SpecialHiDive_Phys(HSD_GObj* gobj);

/* ---- metalmario_items.c ---- */
/* code+0xE24 */ void itMM_Fire_Spawn(HSD_GObj* owner_gobj, Vec3* pos, ItemKind kind,
                                      float facing_dir);
extern const ItemLogicTable itMM_Articles[ftMM_Article_Count];

/* ---- metalmario.c ---- */
extern const MotionState ftMM_Init_MotionStateTable[ftMM_MS_SelfCount];

#endif
