/* ACE 2.0.0's Metal Sonic, native (PlNm.dat, ftDataMetalsonic; m-ex internal 49, external 48,
 * native kind 0x38 = 56). Effect file 50 (EfNmData.dat), sound bank 89 (metal_sonic.ssm).
 *
 * Metal Sonic ships as m-ex PowerPC in PlNm.dat: one "ftFunction" block built without symbols
 * (0x578C bytes, 716 relocations, 10 exports) and one "itFunction" block (article 0). He is a
 * Fox clone: MxDt names Fox's retail function for every slot the file does not export (the two
 * side special entries, the item and knockback events, the common double jump), and 14 of his
 * 35 state rows are Fox's four callbacks unchanged. None of his code is the Akaneia Sonic's
 * (run-source/rel09-ace-native/metalsonic/identity_PlNm_PlSn.txt: 4 pieces of 6 words in all,
 * bare returns), so nothing is taken from akaneia/sonic.
 *
 *   neutral  a charged shot. Both entries (ground and air) go to 344. Held B in frames 15 to 141
 *            starts the wind up again (a color flash); let go there, the shot of article 0 at
 *            speed 7 that lives 35 frames; let go in frames 10 to 14, a shot at speed 5 that
 *            lives 8 frames. Held to the end: 345, which costs him 8 percent at its frame 1.
 *   side     a charged dash: Fox's entries into his own start rows (347, 350). Held B charges up
 *            to frame 120 (then the animation stops, flashing); let go, a dash of 2 to 9 times
 *            his facing by the charge frame (348, 351), then Fox's end rows.
 *   up       Fox's Firefox rows with his own entries (no Ascend charge effect hook) and his own
 *            launch animation callback (a special fall at the end).
 *   down     Fox's Reflector rows; his callbacks run Fox's and then clear the effect hook, so
 *            no reflector effect is armed by them, and the ground rows drop the friction.
 *
 * At load he also rewrites three rows of a copy of the common state table: the back air's
 * physics (a short hop forward at frames 11 to 12) and the physics of the down smash and the
 * back throw (the animation's own movement, then no ground speed from frame 44).
 *
 * Offsets in the comments (+3618) are offsets into the ftFunction block, "it0 +58" into the
 * itFunction block, as in run-source/rel09-ace-native/metalsonic/listing.txt. The file has no
 * symbols: every name is this port's. "fp+" is a console offset of the Fighter. FUNCTIONS.md in
 * that folder lists every function and data word of both blocks. */
#ifndef MU_ACE_METALSONIC_H
#define MU_ACE_METALSONIC_H

#include <Runtime/platform.h>

#include <melee/ft/forward.h>
#include <melee/it/forward.h>
#include <sysdolphin/baselib/forward.h>

#include <dolphin/mtx.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftFox/forward.h>
#include <melee/ft/kinds/ftFox/types.h>
#include <melee/ft/types.h>
#include <melee/it/kinds/forward.h>
#include <melee/it/kinds/types.h>
#include <melee/it/types.h>

#include <stddef.h>

#include "../../akaneia/mu_ak_fighter.h"
#include "mu_disc.h"

/* ---- Action states (move_logic, +01E0 to +0640): 35 rows numbered from 341, in Fox's order.
 * Rows 8, 11 to 13, 15 to 18 and 29 to 34 are Fox's four callbacks (tables.txt). ---- */
enum ftNm_MotionState {
    ftNm_MS_SpecialNStart = 341, /* 0x155: neutral wind up, ground row (never entered) */
    ftNm_MS_SpecialNLoop,        /* 0x156: held to the end on the ground row (8 percent) */
    ftNm_MS_SpecialNEnd,         /* 0x157: shot, ground row */
    ftNm_MS_SpecialAirNStart,    /* 0x158: neutral wind up; both entries come here */
    ftNm_MS_SpecialAirNLoop,     /* 0x159: held to the end (8 percent), then a special fall */
    ftNm_MS_SpecialAirNEnd,      /* 0x15A: shot */
    ftNm_MS_SpecialSStart,       /* 0x15B: dash charge, ground (Fox's entry) */
    ftNm_MS_SpecialS,            /* 0x15C: dash, ground */
    ftNm_MS_SpecialSEnd,         /* 0x15D: Fox's row */
    ftNm_MS_SpecialAirSStart,    /* 0x15E: dash charge, air (Fox's entry) */
    ftNm_MS_SpecialAirS,         /* 0x15F: dash, air */
    ftNm_MS_SpecialAirSEnd,      /* 0x160: Fox's row; both dashes end here */
    ftNm_MS_SpecialHiHold,       /* 0x161: Fox's row */
    ftNm_MS_SpecialHiHoldAir,    /* 0x162: Fox's row */
    ftNm_MS_SpecialHi,           /* 0x163: Fox's row, his animation callback */
    ftNm_MS_SpecialAirHi,        /* 0x164: Fox's row */
    ftNm_MS_SpecialHiLanding,    /* 0x165: Fox's row */
    ftNm_MS_SpecialHiFall,       /* 0x166: Fox's row */
    ftNm_MS_SpecialHiBound,      /* 0x167: Fox's row */
    ftNm_MS_SpecialLwStart,      /* 0x168: reflector rows: Fox's with his anim and phys */
    ftNm_MS_SpecialLwLoop,
    ftNm_MS_SpecialLwHit,
    ftNm_MS_SpecialLwEnd,
    ftNm_MS_SpecialLwTurn,
    ftNm_MS_SpecialAirLwStart,
    ftNm_MS_SpecialAirLwLoop,
    ftNm_MS_SpecialAirLwHit,
    ftNm_MS_SpecialAirLwEnd,
    ftNm_MS_SpecialAirLwTurn,
    ftNm_MS_AppealSStartR, /* 0x172 to 0x177: Fox's taunt rows */
    ftNm_MS_AppealSStartL,
    ftNm_MS_AppealSR,
    ftNm_MS_AppealSL,
    ftNm_MS_AppealSEndR,
    ftNm_MS_AppealSEndL,
    ftNm_MS_End
};

#define FTNM_MS_COUNT (ftNm_MS_End - ftNm_MS_SpecialNStart)
_Static_assert(FTNM_MS_COUNT == 35, "35 rows in PlNm.dat's move_logic");
_Static_assert(ftNm_MS_SpecialNStart == ftCo_MS_Count, "m-ex numbers a fighter's rows from 341");
_Static_assert(ftNm_MS_End == ftFx_MS_Count, "his rows are Fox's rows, one for one");

/* ---- Special attributes: Fox's block (ftFox_DatAttrs, 0xD4 bytes). The file copies 0xD4 bytes
 * of ftData->ext_attr to fp+2D8 and points fp+2D4 at the copy, as Fox's load does. His values
 * differ from Fox's at +68 (Firefox duration 20, Fox 30), +B0 (reflector, 4 against 1), +BC and
 * +D4 to +FC (data.txt). His own code reads +98 and +A4 only. ---- */
#define FTNM_ATTRS_SIZE 0xD4 /* li r5, 0xD4 at +0040 and +0924 */

/* ---- Fighter variables (fp->u, console fp+222C). Fox's word comes first (MxDt's Fox routines
 * and the Fox load use u.fx); the file writes one more word, fp+2234 (cleared at death, read by
 * nothing). A pointer is 8 bytes natively, so the words are fields, not offsets. ---- */
typedef struct ftNm_FighterVars {
    /* fp+222C */ HSD_GObj* fox_blaster; ///< u.fx.x222C_blasterGObj (Fox's routines)
    /* fp+2230 */ s32 x2230;             ///< no reader, no writer
    /* fp+2234 */ s32 x2234;             ///< cleared by [ondeath], no reader
} ftNm_FighterVars;

static inline ftNm_FighterVars* ftNm_FV(Fighter* fp)
{
    return (ftNm_FighterVars*) &fp->u;
}

/* ---- State variables (fp->mv): his own code only writes Fox's reflector words (2340, 2348,
 * 234C) in the ground down special entry, so fp->mv.fx is used as it is. ---- */

/* ---- Article 0 (item kind 333 on ACE 2.0.0; MxDt item lookup 333 to 336, only article 0 has
 * code and only article 0 is handed to m-ex). The file's list (ftData+0x48) has six slots, five
 * of them set (slot 3 is empty); Fox's load also hands the first three to retail kinds (Fox's laser 0x36, blaster 0x4A and
 * illusion), as it does for Fox. ---- */
enum {
    ftNm_Article_Shot = 0,
    ftNm_Article_Count = 1,
};

/* The article's special attributes (article x4, disc data): 35, 20, 1.9, 2.9, 1.6, 1.6. The
 * code reads the first word only. */
typedef struct itNm_Attrs {
    /* +00 */ float lifetime;
    /* +04 */ float x4;
    /* +08 */ float x8;
    /* +0C */ float xC;
    /* +10 */ float x10;
    /* +14 */ float x14;
} DISC_STRUCT itNm_Attrs;

/* Item variables (ip->xDD4_itemVar): the spawn stores the owner at ip+DD8; nothing reads it. */
typedef struct itNm_ItemVars {
    /* ip+DD4 */ s32 x0;
    /* ip+DD8 */ HSD_GObj* owner;
} itNm_ItemVars;

/* The bone the shots leave from: fp->parts index 0x160 / 0x10. */
#define FTNM_SHOT_BONE 22

/* m-ex's CPU spoof word (+09E4): [onload] stores 1 before it installs the proc. Read as a retail
 * fighter kind that is Ft_Kind_Fox; read as an m-ex internal id it is also Fox (internal 1). */
#define FTNM_CPU_SPOOF_KIND Ft_Kind_Fox

/* ---- metalsonic.c ---- */
extern const MotionState ftNm_Init_MotionStateTable[FTNM_MS_COUNT];

/* ---- metalsonic_specialn.c ---- */
/* +0640 */ void ftNm_SpecialN_Enter(HSD_GObj* gobj);
/* +06B8 */ void ftNm_SpecialAirN_Enter(HSD_GObj* gobj);
/* +3618 */ void ftNm_SpecialNStart_Anim(HSD_GObj* gobj);
/* +3804 */ void ftNm_SpecialNStart_IASA(HSD_GObj* gobj);
/* +3854 */ void ftNm_SpecialNStart_Phys(HSD_GObj* gobj);
/* +396C */ void ftNm_SpecialN_Coll(HSD_GObj* gobj);
/* +39B8 */ void ftNm_SpecialNLoop_Anim(HSD_GObj* gobj);
/* +3A3C */ void ftNm_SpecialN_Phys(HSD_GObj* gobj);
/* +3A90 */ void ftNm_SpecialNEnd_Anim(HSD_GObj* gobj);
/* +3ADC */ void ftNm_SpecialAirNStart_Anim(HSD_GObj* gobj);
/* +3CC8 */ void ftNm_SpecialAirNStart_Phys(HSD_GObj* gobj);
/* +3E1C */ void ftNm_SpecialAirN_Coll(HSD_GObj* gobj);
/* +3E74 */ void ftNm_SpecialAirNLoop_Anim(HSD_GObj* gobj);
/* +3F14 */ void ftNm_SpecialAirNLoop_Coll(HSD_GObj* gobj);
/* +3F24 */ void ftNm_SpecialAirNEnd_Anim(HSD_GObj* gobj);

/* ---- metalsonic_specials.c ---- */
/* +3F70 */ void ftNm_SpecialSStart_Anim(HSD_GObj* gobj);
/* +3FBC */ void ftNm_SpecialSStart_IASA(HSD_GObj* gobj);
/* +43B4 */ void ftNm_SpecialSStart_Phys(HSD_GObj* gobj);
/* +4404 */ void ftNm_SpecialSStart_Coll(HSD_GObj* gobj);
/* +4494 */ void ftNm_SpecialS_Anim(HSD_GObj* gobj);
/* +4530 */ void ftNm_SpecialS_IASA(HSD_GObj* gobj);
/* +459C */ void ftNm_SpecialAirSStart_Anim(HSD_GObj* gobj);
/* +4604 */ void ftNm_SpecialAirSStart_IASA(HSD_GObj* gobj);
/* +4A18 */ void ftNm_SpecialAirSStart_Phys(HSD_GObj* gobj);
/* +4A84 */ void ftNm_SpecialAirSStart_Coll(HSD_GObj* gobj);
/* +4B14 */ void ftNm_SpecialAirS_Anim(HSD_GObj* gobj);
/* +4B9C */ void ftNm_SpecialAirS_IASA(HSD_GObj* gobj);
/* +4BF4 */ void ftNm_SpecialAirS_Phys(HSD_GObj* gobj);

/* ---- metalsonic_specialhi.c ---- */
/* +0730 */ void ftNm_SpecialHi_Enter(HSD_GObj* gobj);
/* +07A4 */ void ftNm_SpecialAirHi_Enter(HSD_GObj* gobj);
/* +4C08 */ void ftNm_SpecialHi_Anim(HSD_GObj* gobj);

/* ---- metalsonic_speciallw.c ---- */
/* +0818 */ void ftNm_SpecialLw_Enter(HSD_GObj* gobj);
/* +08A4 */ void ftNm_SpecialAirLw_Enter(HSD_GObj* gobj);
/* +4C70 */ void ftNm_SpecialLwStart_Anim(HSD_GObj* gobj);
/* +4CB4 */ void ftNm_SpecialLw_Phys(HSD_GObj* gobj);
/* +4CEC */ void ftNm_SpecialLwLoop_Anim(HSD_GObj* gobj);
/* +4D30 */ void ftNm_SpecialLwHit_Anim(HSD_GObj* gobj);
/* +4D74 */ void ftNm_SpecialLwEnd_Anim(HSD_GObj* gobj);
/* +4DB8 */ void ftNm_SpecialLwEnd_Phys(HSD_GObj* gobj);
/* +4DBC */ void ftNm_SpecialLwTurn_Anim(HSD_GObj* gobj);
/* +4E00 */ void ftNm_SpecialAirLwStart_Anim(HSD_GObj* gobj);
/* +4E44 */ void ftNm_SpecialAirLwLoop_Anim(HSD_GObj* gobj);
/* +4E88 */ void ftNm_SpecialAirLwHit_Anim(HSD_GObj* gobj);
/* +4ECC */ void ftNm_SpecialAirLwEnd_Anim(HSD_GObj* gobj);
/* +4F10 */ void ftNm_SpecialAirLwTurn_Anim(HSD_GObj* gobj);

/* ---- metalsonic_items.c ---- */
/* +54B0 */ void itNm_Shot_Spawn(HSD_GObj* owner_gobj, Vec3* pos, Vec3* vel, float facing_dir);
/* +567C */ void itNm_ShortShot_Spawn(HSD_GObj* owner_gobj, Vec3* pos, Vec3* vel,
                                      float facing_dir);
extern const ItemLogicTable itNm_Articles[ftNm_Article_Count];

#endif
