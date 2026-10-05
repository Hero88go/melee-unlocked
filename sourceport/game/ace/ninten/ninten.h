/* ACE 2.0.0's Ninten, native (PlNt.dat, ftDataNinten; m-ex internal 48, external 47).
 *
 * Ninten ships in PlNt.dat as m-ex PowerPC (the "ftFunction" and "itFunction" roots, built
 * with symbols). He is his own fighter, written from Akaneia's Lucas: his 28 states are
 * Lucas's first 28 (same ids 341 to 368, same animation ids, flags and move ids), and 88 of
 * his 194 routines are Lucas's routines byte for byte after relocation
 * (run-source/rel09-ace-native/ninten/lucas_identity.txt). Where a routine is Lucas's, the
 * function of sourceport/game/akaneia/lucas/ is called or named in his tables; the rest is
 * ported here from the disassembly.
 *
 *   neutral special  PK Hypnosis: Lucas's PK Freeze code, article 0
 *   side special     a slingshot pellet (article 1): Lucas's PK Fire without the recoil
 *   up special       his own move (no article)
 *   down special     Lucas's PSI Magnet with changes
 *   forward smash    a bat (article 2): Lucas's stick, another sound, another attribute offset
 *
 * Offsets in the comments (code+0x3644) are offsets into the ftFunction or itFunction code
 * block of PlNt.dat, as in run-source/rel09-ace-native/ninten/listing.txt and FUNCTIONS.md.
 * Console offsets (fp+2244) are Fighter / Item offsets on the GameCube. */
#ifndef MU_ACE_NINTEN_H
#define MU_ACE_NINTEN_H

#include <Runtime/platform.h>

#include <melee/ft/forward.h>
#include <melee/it/forward.h>
#include <sysdolphin/baselib/forward.h>

#include <dolphin/mtx.h>
#include <melee/ft/types.h>
#include <melee/it/kinds/forward.h>
#include <melee/it/kinds/types.h>
#include <melee/it/types.h>

#include "../../akaneia/lucas/lucas.h"
#include "../../akaneia/mu_ak_fighter.h"
#include "mu_disc.h"

/* ---- Action states ----------------------------------------------------------------------------
 * Lucas's ids (ftLc_MS_AttackS4 = 341 to ftLc_MS_SpecialAirLwTurn = 368) are used as they
 * are: his table has the same 28 rows in the same order. */
#define ftNt_MS_SelfCount 28

_Static_assert(ftLc_MS_SpecialAirLwTurn - ftLc_MS_AttackS4 + 1 == ftNt_MS_SelfCount,
               "28 rows in PlNt.dat's move_logic");

/* ---- Articles (ftData->x48_items, in the order of his MxDt item lookup: 329 to 332) ----------
 * His load registers the first three. The fourth kind of MxDt's list (332) has no article in
 * the file's code: it is the PK Hypnosis of Kirby's copy (PlKbCpNt.dat), as Lucas's 276. */
enum {
    ftNt_Article_PKHypnosis = 0, /* 329: Lucas's PK Freeze article code */
    ftNt_Article_Pellet = 1,     /* 330: the slingshot's shot */
    ftNt_Article_Bat = 2,        /* 331: the forward smash's bat */
    ftNt_Article_Count = 3,
};

_Static_assert((int) ftNt_Article_PKHypnosis == (int) ftLc_Art_PKFreeze &&
                   (int) ftNt_Article_Pellet == (int) ftLc_Art_PKFire,
               "Lucas's spawn routines ask for articles 0 and 1 by index");

/* ---- Fighter variables (fp+222C) --------------------------------------------------------------
 * The same words as Lucas's up to fp+2240; fp+2244 is the bat where Lucas keeps his PK
 * Thunder (his stick is at fp+2248). */
typedef struct ftNinten_FighterVars {
    /* fp+222C */ int x222C;
    /* fp+2230 */ int x2230[4];
    /* fp+2240 */ Item_GObj* pkhypnosis_gobj;
    /* fp+2244 */ Item_GObj* bat_gobj;
} ftNinten_FighterVars;

static inline ftNinten_FighterVars* ftNt_Vars(Fighter* fp)
{
    _Static_assert(sizeof(ftNinten_FighterVars) <= sizeof(fp->u), "Ninten vars overflow fp->u");
    return (ftNinten_FighterVars*) &fp->u;
}

/* ---- Special attributes: ft_data->ext_attr, 0x108 bytes of disc data --------------------------
 * +00 to +57 have Lucas's layout (the neutral and side specials read them through
 * ftLucasAttributes). The rest is named where his own code reads it. */
typedef struct ftNintenAttributes {
    /* +000 */ u8 x0_lucas_layout[0x58];        /* ftLucasAttributes +00 to +57 */
    /* +058 */ s32 x58_HI_LOOP1;
    /* +05C */ s32 x5C_HI_LOOP2;
    /* +060 */ s32 x60_HI_GRAVITY_DELAY;
    /* +064 */ float x64_unused;
    /* +068 */ float x68_HI_FALL_ACCEL;
    /* +06C */ float x6C_HI_SPEED;
    /* +070 */ float x70_unused;
    /* +074 */ float x74_HI_DECEL;
    /* +078 */ float x78_unused;
    /* +07C */ float x7C_HI_WALL_ANGLE;
    /* +080 */ float x80_unused;
    /* +084 */ float x84_unused;
    /* +088 */ float x88_HI_LANDING_LAG;
    /* +08C */ float x8C_unused[4];
    /* +09C */ float x9C_HI_TURN_STICK;
    /* +0A0 */ float xA0_LW_MIN_FRAMES;
    /* +0A4 */ float xA4_LW_TURN_FRAMES;
    /* +0A8 */ s32 xA8_LW_COUNTER;
    /* +0AC */ s32 xAC_LW_COUNTER_ACTION;
    /* +0B0 */ s32 xB0_LW_SFX_DELAY;
    /* +0B4 */ float xB4_LW_AIR_VEL_DIV;
    /* +0B8 */ float xB8_LW_FALL_ACCEL;
    /* +0BC */ float xBC_LW_TURN_STICK;
    /* +0C0 */ ReflectDesc_BE xC0_LW_REFLECT;
    /* +0E4 */ ReflectDesc_BE xE4_BAT_REFLECT;
} DISC_STRUCT ftNintenAttributes;

_Static_assert(sizeof(ftNintenAttributes) == 0x108, "the block PlNt.dat copies at code+0x9F0");

static inline ftNintenAttributes* ftNt_Attrs(Fighter* fp)
{
    return (ftNintenAttributes*) fp->dat_attrs;
}

typedef struct ftNinten_LwMotionVars {
    /* +2340 */ int turn_timer;
    /* +2344 */ int released;
    /* +2348 */ int gravity_delay;
    /* +234C */ int counter;
    /* +2350 */ int sfx_timer;
    /* +2354 */ int unused;
} ftNinten_LwMotionVars;

static inline ftNinten_LwMotionVars* ftNt_LwMV(Fighter* fp)
{
    _Static_assert(sizeof(ftNinten_LwMotionVars) <= sizeof(fp->mv), "Ninten motion vars overflow");
    return (ftNinten_LwMotionVars*) &fp->mv;
}

/* His bones the code names by index (the same two as Lucas's). */
#define ftNt_Part_Hand ftLc_Part_Hand /* parts[44]: the bat, the slingshot (fp->parts + 0x2C0) */

/* ---- ninten.c ---- */
/* code+0x3500 */ void ftNt_RemoveAllArticles(HSD_GObj* gobj);

/* ---- ninten_attacks4.c ---- */
/* code+0x0B50 */ void ftNt_AttackS4_Enter(HSD_GObj* gobj);
/* code+0x0C40 */ void ftNt_AttackS4_Anim(HSD_GObj* gobj);
/* code+0x0CE8 */ void ftNt_AttackS4_IASA(HSD_GObj* gobj);
/* code+0x4360 */ void ftNt_RemoveBat(HSD_GObj* gobj);

/* ---- ninten_specialn.c ---- */
/* code+0x0DA0 */ void ftNt_SpecialNStart_Anim(HSD_GObj* gobj);
/* code+0x1008 */ void ftNt_SpecialAirNStart_Anim(HSD_GObj* gobj);

/* ---- ninten_specials.c ---- */
/* code+0x052C */ void ftNt_SpecialS_Enter(HSD_GObj* gobj);
/* code+0x0550 */ void ftNt_SpecialAirS_Enter(HSD_GObj* gobj);

/* ---- ninten_items.c ---- */
/* code+0x33C4 */ Item_GObj* ftNt_SpawnBat(HSD_GObj* gobj, Vec3* pos, int part, float facing);
extern ItemLogicTable* const itNt_ArticleTables[ftNt_Article_Count];

/* ---- ninten_specialhi.c ---- */
void ftNt_SpecialHi_Enter(HSD_GObj*);
void ftNt_SpecialAirHi_Enter(HSD_GObj*);
void ftNt_SpecialHiStart_Anim(HSD_GObj*);
void ftNt_SpecialHiStart_Coll(HSD_GObj*);
void ftNt_SpecialHiHold_Anim(HSD_GObj*);
void ftNt_SpecialHiHold_IASA(HSD_GObj*);
void ftNt_NoCallback(HSD_GObj*);
void ftNt_SpecialHiHold_Coll(HSD_GObj*);
void ftNt_SpecialHiEnd_Phys(HSD_GObj*);
void ftNt_SpecialHiEnd_Coll(HSD_GObj*);
void ftNt_SpecialHi_Anim(HSD_GObj*);
void ftNt_SpecialHi_Phys(HSD_GObj*);
void ftNt_SpecialHi_Coll(HSD_GObj*);
void ftNt_SpecialAirHiStart_Anim(HSD_GObj*);
void ftNt_SpecialAirHiStart_Coll(HSD_GObj*);
void ftNt_SpecialAirHiHold_Anim(HSD_GObj*);
void ftNt_SpecialAirHiHold_IASA(HSD_GObj*);
void ftNt_SpecialAirHiHold_Coll(HSD_GObj*);
void ftNt_SpecialAirHiEnd_Anim(HSD_GObj*);
void ftNt_SpecialAirHiEnd_Phys(HSD_GObj*);
void ftNt_SpecialAirHiEnd_Coll(HSD_GObj*);
void ftNt_SpecialAirHi_Anim(HSD_GObj*);
void ftNt_SpecialAirHi_Phys(HSD_GObj*);
void ftNt_SpecialAirHi_Coll(HSD_GObj*);
void ftNt_SpecialHiBound_Anim(HSD_GObj*);
void ftNt_SpecialHiBound_Coll(HSD_GObj*);

/* ---- ninten_speciallw.c ---- */
void ftNt_SpecialLw_Enter(HSD_GObj*);
void ftNt_SpecialAirLw_Enter(HSD_GObj*);
void ftNt_SpecialLwStart_Anim(HSD_GObj*);
void ftNt_SpecialLwStart_IASA(HSD_GObj*);
void ftNt_SpecialLwStart_Coll(HSD_GObj*);
void ftNt_SpecialLwHold_Anim(HSD_GObj*);
void ftNt_SpecialLwHold_Phys(HSD_GObj*);
void ftNt_SpecialLwHold_Coll(HSD_GObj*);
void ftNt_SpecialLwHit_Anim(HSD_GObj*);
void ftNt_SpecialLwHit_Coll(HSD_GObj*);
void ftNt_SpecialLwEnd_Coll(HSD_GObj*);
void ftNt_SpecialLwTurn_Anim(HSD_GObj*);
void ftNt_SpecialLwTurn_Coll(HSD_GObj*);
void ftNt_SpecialAirLwStart_Phys(HSD_GObj*);
void ftNt_SpecialAirLwStart_Coll(HSD_GObj*);
void ftNt_SpecialAirLwHold_Phys(HSD_GObj*);
void ftNt_SpecialAirLwHold_Coll(HSD_GObj*);
void ftNt_SpecialAirLwHit_Coll(HSD_GObj*);
void ftNt_SpecialAirLwEnd_Coll(HSD_GObj*);
void ftNt_SpecialAirLwTurn_Coll(HSD_GObj*);

#endif
