/* ACE's Dr. Luigi, native.
 *
 * Dr. Luigi ships in PlDl.dat as m-ex PowerPC (the "ftFunction" and "itFunction" roots, built
 * without symbols). He is Luigi with one move changed: the neutral special throws a pill in
 * place of the fireball. His move table is Luigi's own table (the same 18 action states,
 * animation ids, flags and callbacks, ftLuigi/forward.h and ftluigi.c) except the collision
 * callback of the two neutral special states; his special attributes are Luigi's block
 * (ftLuigiAttributes, 0x98 bytes, read from his own file); every m-ex slot he does not export is
 * Luigi's function in MxDt. What he adds is listed here, each routine with its offset in the
 * disc's code block (run-source/rel09-ace-native/drluigi/listing.txt and FUNCTIONS.md).
 *
 * Console offsets in comments are Fighter / Item offsets on the GameCube, for cross-checking
 * against the PowerPC; the code itself only uses the decomp's field names. */
#ifndef MU_ACE_DRLUIGI_H
#define MU_ACE_DRLUIGI_H

#include <Runtime/platform.h>

#include <melee/ft/forward.h>
#include <melee/it/forward.h>
#include <sysdolphin/baselib/forward.h>

#include <dolphin/mtx.h>
#include <melee/ft/kinds/ftLuigi/forward.h>
#include <melee/ft/types.h>
#include <melee/it/kinds/forward.h>
#include <melee/it/kinds/types.h>
#include <melee/it/types.h>

#include "../../akaneia/mu_ak_fighter.h"
#include "mu_disc.h"

/* ---- Action states ----------------------------------------------------------------------------
 * Numbered from ftCo_MS_Count (341) exactly as Luigi's (ftLuigi_MotionState), so Luigi's names
 * are used as they are: ftLg_MS_SpecialN is 341, ftLg_MS_SpecialAirN is 342, and the table has
 * ftLg_MS_SelfCount (18) rows. */
#define ftDl_MS_SelfCount ftLg_MS_SelfCount

/* ---- Articles (ftData->x48_items, in the order of his MxDt item lookup) ---- */
enum {
    ftDl_Article_Pill = 0, /* item kind 328 on ACE 2.0.0 */
    ftDl_Article_Count,
};

/* The pill's special attributes (article x4, disc data; the values are those of PlDl.dat). */
typedef struct itDrLuigiPill_Attrs {
    /* +00 */ float speed;    ///< 0.9
    /* +04 */ float angle;    ///< 0, radians: only its cosine is used
    /* +08 */ float lifetime; ///< 50 frames
    /* +0C */ float xC;       ///< 0.85, not read by his code
    /* +10 */ float x10;      ///< 1.2, not read by his code
    /* +14 */ int hit_sfx;    ///< 5026: sound 26 of his own bank (m-ex ids from 5000)
} DISC_STRUCT itDrLuigiPill_Attrs;

/* ---- drluigi_specialn.c ---- */
void ftDl_SpecialN_Enter(HSD_GObj* gobj);
void ftDl_SpecialAirN_Enter(HSD_GObj* gobj);
void ftDl_SpecialN_Coll(HSD_GObj* gobj);
void ftDl_SpecialAirN_Coll(HSD_GObj* gobj);
void ftDl_SpecialN_PillSpawn(HSD_GObj* gobj);

/* ---- drluigi_items.c ---- */
void itDl_Pill_Spawn(HSD_GObj* owner_gobj, Vec3* pos, ItemKind kind, float facing_dir);
extern const ItemLogicTable itDl_Articles[ftDl_Article_Count];

/* ---- drluigi.c ---- */
extern const MotionState ftDl_Init_MotionStateTable[ftDl_MS_SelfCount];

#endif
