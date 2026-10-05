/* ACE 2.0.0's Daisy, native (PlDa.dat, ftDataDaisy; m-ex internal 36, external 35).
 *
 * Daisy ships in PlDa.dat as m-ex PowerPC (the "ftFunction" and "itFunction" roots, built
 * without symbols). She is a clone of Peach: the file exports twelve m-ex slots (onload,
 * ondeath, move_logic, the neutral, side and down specials on the ground and in the air,
 * enterfloat, onlanding and slot 45, the trail block) and MxDt names Peach's retail function
 * for every other slot that is set. Her state table is Peach's thirty rows, same animation
 * ids, flags and move ids, with 26 callbacks in 8 rows pointing at code of her own; 15 of
 * those make the same calls as Peach's functions, which are named instead. What is really
 * hers:
 *
 *  - the neutral special holds out her own article 3 (Peach's Toad with another model) and
 *    scatters her own article 4 (the spores); the counter bubble of the ground version is a
 *    constant of the code where Peach reads it from her attributes;
 *  - the down special always pulls her own article 1 (Peach's turnip logic with a fixed
 *    damage, no faces and no rare items) with her own effect;
 *  - the side special on the ground starts with an upward speed of 1.0;
 *  - the float lands through a collision call with no ceiling callback.
 *
 * Her special attributes are Peach's block (ftPe_DatAttrs, 0xC0 bytes) read from her own
 * file, and her fighter variables are Peach's (fp->u.pe).
 *
 * Offsets in the comments (code+0x1260) are offsets into the ftFunction or itFunction code
 * block of PlDa.dat, as in run-source/rel09-ace-native/daisy/listing.txt and FUNCTIONS.md.
 * Console offsets (fp+2244) are Fighter / Item offsets on the GameCube, for cross-checking
 * against the PowerPC; the code itself only uses the decomp's field names. */
#ifndef MU_ACE_DAISY_H
#define MU_ACE_DAISY_H

#include <Runtime/platform.h>

#include <melee/ft/forward.h>
#include <melee/it/forward.h>
#include <sysdolphin/baselib/forward.h>

#include <dolphin/mtx.h>
#include <melee/ft/kinds/ftPeach/forward.h>
#include <melee/ft/kinds/ftPeach/types.h>
#include <melee/ft/types.h>
#include <melee/it/kinds/forward.h>
#include <melee/it/kinds/types.h>
#include <melee/it/types.h>

#include "../../akaneia/mu_ak_fighter.h"
#include "mu_disc.h"

/* ---- Action states ----------------------------------------------------------------------------
 * 341 to 370 are Peach's (ftPeach_MotionState), same ids, animations, flags and move ids, so
 * Peach's names are used as they are. She has no state of her own. */
#define ftDa_MS_SelfCount ftPe_MS_SelfCount

_Static_assert(ftPe_MS_SelfCount == 30, "30 rows in PlDa.dat's move_logic");
_Static_assert(ftPe_MS_Float == 0x155, "the state id PlDa.dat passes at code+0x98C");
_Static_assert(ftPe_MS_SpecialLw == 0x160 && ftPe_MS_SpecialAirLw == 0x161,
               "the state ids at code+0x868 and +0x14F8");
_Static_assert(ftPe_MS_SpecialSStart == 0x162 && ftPe_MS_SpecialAirSStart == 0x165,
               "the state ids at code+0x73C and +0x7B4");
_Static_assert(ftPe_MS_SpecialN == 0x16D && ftPe_MS_SpecialNHit == 0x16E &&
                   ftPe_MS_SpecialAirN == 0x16F && ftPe_MS_SpecialAirNHit == 0x170,
               "the state ids at code+0x614, +0x15FC, +0x698 and +0x18A8");
_Static_assert(sizeof(ftPe_DatAttrs) == 0xC0, "the block PlDa.dat copies at code+0x50");

/* ---- Articles (ftData->x48_items, in the order of her MxDt item lookup: 287 to 293) ----------
 * Her load hands the first five to the item code by index. 0 and 2 are also registered under
 * Peach's retail item kinds, because the side and up specials are Peach's retail code. */
enum {
    ftDa_Article_Explode = 0, /* 287, and retail It_Kind_Peach_Explode: no code */
    ftDa_Article_Veg = 1,     /* 288: the down special's pull */
    ftDa_Article_Parasol = 2, /* 289, and retail It_Kind_Peach_Parasol: no code */
    ftDa_Article_Toad = 3,    /* 290: the neutral special's counter */
    ftDa_Article_Spore = 4,   /* 291: what the counter scatters */
    ftDa_Article_Count = 5,   /* 292 and 293 are in MxDt's list and nothing registers them */
};

/* The transition flags her code passes when a special goes between the ground and the air
 * (code+0x16D4 and three more; code+0x14E4 and +0x155C). The same two values as the static
 * coll_mf of ftpeachspecialn.c and ftpeachspeciallw.c. */
#define FTDA_SPECIALN_COLL_MF ((MotionFlags) 0x0C4C508C)
#define FTDA_SPECIALLW_COLL_MF ((MotionFlags) 0x0C4C5080)

/* ---- daisy.c ---- */
/* code+0x1BA4 */ void ftDa_Init_OnDeath2(HSD_GObj* gobj);
/* code+0xA54 */ void ftDa_Float_Coll(HSD_GObj* gobj);
extern const MotionState ftDa_Init_MotionStateTable[ftDa_MS_SelfCount];

/* ---- daisy_specialn.c ---- */
/* code+0x5C8 */ void ftDa_SpecialN_Enter(HSD_GObj* gobj);
/* code+0x65C */ void ftDa_SpecialAirN_Enter(HSD_GObj* gobj);
/* code+0xC60 */ void ftDa_SpecialN_Anim(HSD_GObj* gobj);
/* code+0xDB0 */ void ftDa_SpecialN_Coll(HSD_GObj* gobj);
/* code+0xDFC */ void ftDa_SpecialNHit_Anim(HSD_GObj* gobj);
/* code+0xE78 */ void ftDa_SpecialNHit_Coll(HSD_GObj* gobj);
/* code+0xEC4 */ void ftDa_SpecialAirN_Anim(HSD_GObj* gobj);
/* code+0x10D8 */ void ftDa_SpecialAirN_Coll(HSD_GObj* gobj);
/* code+0x1124 */ void ftDa_SpecialAirNHit_Anim(HSD_GObj* gobj);
/* code+0x11E8 */ void ftDa_SpecialAirNHit_Coll(HSD_GObj* gobj);
/* The owner's side of the Toad article going away (the tail of code+0x1BA4, and inlined in the
 * article's code): thaw it, forget it, drop the two callbacks. */
void ftDa_SpecialN_ToadGone(HSD_GObj* gobj);

/* ---- daisy_speciallw.c ---- */
/* code+0x700 */ void ftDa_SpecialS_Enter(HSD_GObj* gobj);
/* code+0x7E4 */ void ftDa_SpecialLw_Enter(HSD_GObj* gobj);
/* code+0x8C4 */ void ftDa_SpecialAirLw_Enter(HSD_GObj* gobj);
/* code+0xAD8 */ void ftDa_SpecialLw_Coll(HSD_GObj* gobj);
/* code+0xB70 */ void ftDa_SpecialAirLw_Coll(HSD_GObj* gobj);

/* ---- daisy_items.c ---- */
/* code+0x1C90 */ Item_GObj* itDa_Veg_Spawn(HSD_GObj* owner_gobj, ItemKind kind,
                                           float facing_dir);
/* code+0x1FC4 */ void itDa_Toad_Remove(Item_GObj* item_gobj);
extern ItemLogicTable* const itDa_ArticleTables[ftDa_Article_Count];

#endif
