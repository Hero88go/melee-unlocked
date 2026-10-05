/* ACE 2.0.0's Zero, native (PlZx.dat, ftDataZero; m-ex internal 51, external 50).
 *
 * A clone of Link: MxDt names Link's retail function for every slot he has (21 rows, internal 6),
 * and his file exports ten slots: onload, ondeath, move_logic, specialn, specialairn,
 * specialairs, onactionstatechange, onrespawn, onlanding and GetTrailData. So the ground side
 * special, both up specials, both down specials, the item and knockback events and the double
 * jump enter through Link's retail functions, into Zero's own state table.
 *
 * The state table is Link's 21 rows with other callbacks in 13 of them and one row more (362).
 * What the moves are, read from the code:
 *   neutral  a buster shot (article 0) at frame 39; B held through frames 37 and 38 holds the
 *            pose and fires the charged shot (article 1)
 *   side     a dash: ground speed 2.2 then 0.6 from frame 18; cancelled from frame 4 by A, the
 *            C stick or a jump; in the air one dash per airtime at 1.6
 *   up       Link's entry, then his own rise: 1.75 up for the first frames with a turn or a
 *            push by the stick, a slow fall from frame 21, special fall at frame 38
 *   down     a dive: forward on the ground, 2 forward and 3 down in the air, with its own
 *            landing state (362)
 * None of Link's items is created by his code: his load registers his two articles only, not
 * the bow, arrow, boomerang, bomb or hookshot (zero.c, the note on Link's accessory callbacks).
 *
 * His special attributes are 0x10 bytes, where Link's code that still runs for him reads a
 * ftLk_DatAttrs: see run-source/rel09-ace-native/zero/NOTES.md, "Link's block".
 *
 * Offsets in the comments (+1308) are offsets into the ftFunction block of PlZx.dat, as in
 * run-source/rel09-ace-native/zero/listing.txt; names in quotes are the file's own symbols. */
#ifndef MU_ACE_ZERO_H
#define MU_ACE_ZERO_H

#include <Runtime/platform.h>

#include <melee/ft/forward.h>
#include <melee/it/forward.h>
#include <sysdolphin/baselib/forward.h>

#include <dolphin/mtx.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftLink/forward.h>
#include <melee/ft/kinds/ftMars/types.h>
#include <melee/ft/types.h>
#include <melee/it/kinds/forward.h>
#include <melee/it/kinds/types.h>
#include <melee/it/types.h>

#include <stddef.h>

#include "../../akaneia/mu_ak_fighter.h"
#include "mu_disc.h"

/* ---- Action states: Link's 21 (341 to 361, same ids) and one more ---- */
enum {
    ftZx_MS_SpecialN = ftLk_MS_SpecialNStart,       // 344
    ftZx_MS_SpecialAirN = ftLk_MS_SpecialAirNStart, // 347
    ftZx_MS_SpecialAirS = ftLk_MS_SpecialAirS1,     // 353
    ftZx_MS_SpecialAirHi = ftLk_MS_SpecialAirHi,    // 357
    ftZx_MS_SpecialAirLwFall = ftLk_MS_AirCatch,    // 360: the dive after it left the floor
    ftZx_MS_SpecialLwLand = ftLk_MS_Count,          // 362
    ftZx_MS_Count,
    ftZx_MS_SelfCount = ftZx_MS_Count - ftCo_MS_Count, // 22
};

/* Animation id of state 362 (Link's table ends at 313). */
#define ftZx_SM_SpecialLwLand (ftLk_SM_AirCatchHit + 1) // 314

/* ---- Special attributes (ftData->ext_attr, 0x10 bytes, disc data; values of PlZx.dat) ---- */
typedef struct ftZx_DatAttrs {
    /* +00 */ float specialn_item_vel_x;    ///< 2.7
    /* +04 */ float specialn_item_offset_x; ///< 0, from bone 64, times facing
    /* +08 */ float specialn_item_offset_y; ///< 0
    /* +0C */ float xC;                     ///< 10, not read by his code
} DISC_STRUCT ftZx_DatAttrs;

_Static_assert(sizeof(ftZx_DatAttrs) == 0x10, "the size [ResetAttributes] copies");

/* ---- Fighter variables (fp->u, console fp+222C). Link's words stay where they are: his retail
 * routines that still run read and write them (ftLk_SpecialS_Enter reads fp+222C). ---- */
typedef struct ftZx_FighterVars {
    /* fp+222C */ u32 x222C;           ///< Link's "used_boomerang"
    /* fp+2230 */ s32 x2230;           ///< Link's x4; cleared by [OnRespawn]
    /* fp+2234 */ u8 x8[0xCC - 0x8];
    /* fp+22F8 */ s32 specialairs_used; ///< 1 after an air dash, until landing or a ledge
    /* fp+22FC */ s32 specialairs_count; ///< counted up by the dash, no reader
} ftZx_FighterVars;

_Static_assert(offsetof(ftZx_FighterVars, x2230) == 0x2230 - 0x222C, "fp+2230");
_Static_assert(offsetof(ftZx_FighterVars, specialairs_used) == 0x22F8 - 0x222C, "fp+22F8");
_Static_assert(offsetof(ftZx_FighterVars, specialairs_count) == 0x22FC - 0x222C, "fp+22FC");
_Static_assert(sizeof(ftZx_FighterVars) <= FIGHTERVARS_SIZE, "inside the fighter vars");

static inline ftZx_DatAttrs* ftZx_Attrs(Fighter* fp)
{
    return (ftZx_DatAttrs*) fp->dat_attrs;
}

static inline ftZx_FighterVars* ftZx_Vars(Fighter* fp)
{
    return (ftZx_FighterVars*) &fp->u;
}

/* ---- Articles (ftData->x48_items, in the order of his MxDt item lookup) ---- */
enum {
    ftZx_Article_Shot = 0,    /* item kind 338 on ACE 2.0.0, "Spawn_Blaster" */
    ftZx_Article_ChargedShot, /* item kind 339, "Spawn_Blaster2" */
    ftZx_Article_Count,
};

/* Both articles' special attributes (article x4, disc data): the code reads the first word only.
 * Both: 25, 20, 1.9, 2.9, 1.6, 1.6. */
typedef struct itZx_Attrs {
    /* +00 */ float lifetime;
    /* +04 */ float x4;
    /* +08 */ float x8;
    /* +0C */ float xC;
    /* +10 */ float x10;
    /* +14 */ float x14;
} DISC_STRUCT itZx_Attrs;

/* The model bone both shots leave from (fp->parts index: 0x400 / 0x10). */
#define FTZX_SPECIALN_BONE 64

/* The kind a CPU Zero poses as while the game's CPU code runs: the second argument of
 * MexCPU_InitSpoofData in [OnLoad] (+0098, li r4, 0x14), written to fp->kind as is. */
#define ftZx_CpuSpoofKind Ft_Kind_CLink

/* ---- zero.c ---- */
extern const MotionState ftZx_Init_MotionStateTable[ftZx_MS_SelfCount];

/* ---- zero_specialn.c ---- */
/* +0408 */ void ftZx_SpecialN_Enter(HSD_GObj* gobj);
/* +0448 */ void ftZx_SpecialAirN_Enter(HSD_GObj* gobj);
/* +0630 */ void ftZx_SpecialN_Anim(HSD_GObj* gobj);
/* +0670 */ void ftZx_SpecialN_IASA(HSD_GObj* gobj);
/* +07EC */ void ftZx_SpecialN_Phys(HSD_GObj* gobj);
/* +080C */ void ftZx_SpecialN_Coll(HSD_GObj* gobj);
/* +084C */ void ftZx_SpecialAirN_Anim(HSD_GObj* gobj);
/* +088C */ void ftZx_SpecialAirN_IASA(HSD_GObj* gobj);
/* +0A08 */ void ftZx_SpecialAirN_Phys(HSD_GObj* gobj);
/* +0A28 */ void ftZx_SpecialAirN_Coll(HSD_GObj* gobj);

/* ---- zero_specials.c ---- */
/* +0488 */ void ftZx_SpecialAirS_Enter(HSD_GObj* gobj);
/* +0B4C */ void ftZx_SpecialS_IASA(HSD_GObj* gobj);
/* +0DA4 */ void ftZx_SpecialS_Phys(HSD_GObj* gobj);
/* +0E30 */ void ftZx_SpecialS_Coll(HSD_GObj* gobj);
/* +0EB0 */ void ftZx_SpecialAirS_Anim(HSD_GObj* gobj);
/* +0F04 */ void ftZx_SpecialAirS_IASA(HSD_GObj* gobj);
/* +1150 */ void ftZx_SpecialAirS_Phys(HSD_GObj* gobj);
/* +11E8 */ void ftZx_SpecialAirS_Coll(HSD_GObj* gobj);

/* ---- zero_specialhi.c ---- */
/* +1234 */ void ftZx_SpecialHi_Anim(HSD_GObj* gobj);
/* +12B0 */ void ftZx_SpecialHi_Phys(HSD_GObj* gobj);
/* +12DC */ void ftZx_SpecialAirHi_IASA(HSD_GObj* gobj);
/* +1308 */ void ftZx_SpecialAirHi_Phys(HSD_GObj* gobj);

/* ---- zero_speciallw.c ---- */
/* +17D0 */ void ftZx_SpecialLw_Phys(HSD_GObj* gobj);
/* +189C */ void ftZx_SpecialLw_Coll(HSD_GObj* gobj);
/* +1914 */ void ftZx_SpecialAirLw_Phys(HSD_GObj* gobj);
/* +19A4 */ void ftZx_SpecialAirLw_Coll(HSD_GObj* gobj);
/* +1AEC */ void ftZx_SpecialAirLwFall_Anim(HSD_GObj* gobj);
/* +1AF0 */ void ftZx_SpecialAirLwFall_IASA(HSD_GObj* gobj);
/* +1AF4 */ void ftZx_SpecialAirLwFall_Phys(HSD_GObj* gobj);
/* +1BBC */ void ftZx_SpecialAirLwFall_Coll(HSD_GObj* gobj);
/* +1CF0 */ void ftZx_SpecialLwLand_Coll(HSD_GObj* gobj);

/* ---- zero_items.c ---- */
/* +24C8 */ void itZx_Shot_Spawn(HSD_GObj* owner_gobj, Vec3* pos, Vec3* vel, float facing_dir);
/* +23A0 */ void itZx_ChargedShot_Spawn(HSD_GObj* owner_gobj, Vec3* pos, Vec3* vel,
                                        float facing_dir);
extern const ItemLogicTable itZx_Articles[ftZx_Article_Count];

#endif
