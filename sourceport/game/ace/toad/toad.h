/* ACE 2.0.0's Toad, native (PlTd.dat, ftDataToad; m-ex internal 57, external 56).
 *
 * Toad ships as m-ex PowerPC in PlTd.dat: one "ftFunction" block built without symbols (0x2030
 * bytes, 13 exports) and one "itFunction" block (article 0). MxDt names Mario's retail function
 * for every other slot he has (24 rows equal Mario's, internal 0), so the item and knockback
 * events, the attribute reload and the demo hooks are Mario's. His eight special states are the
 * file's own code:
 *
 *   neutral  Mario's neutral special with his own projectile (article 0) and effect 5000
 *   side     Mario's DOWN special (the tornado) rewritten: no B mashing, his own numbers, the
 *            model leans with the floor by a Z rotation
 *   up       Mario's up special with his own attribute block
 *   down     a short pose that raises his meter by one level (0 to 3) for 1200 frames
 *
 * The meter: a HUD object of its own ("Relax_scene_models" of Meters.dat) created at load. Its
 * per-frame routine counts the level down and rewrites seven of his common attributes from the
 * level (faster, lighter, less traction). The values at load are kept in a block of the file's
 * own data (+08A0), shared by every Toad, as here.
 *
 * Offsets in the comments (+0B1C) are offsets into the ftFunction block, "item +EC" into the
 * itFunction block, as in run-source/rel09-ace-native/toad/listing.txt. The file has no symbols:
 * the function names are this port's. "fp+" is a console offset of the Fighter. */
#ifndef MU_ACE_TOAD_H
#define MU_ACE_TOAD_H

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

#include <stddef.h>

#include "../../akaneia/mu_ak_fighter.h"
#include "mu_disc.h"

/* ---- Action states (move_logic, +0144): Mario's ten rows by number, animation, flags and move
 * id (rows 0 and 1 are empty in both), so Mario's names are used as they are. ---- */
_Static_assert(ftMr_MS_SpecialN == 0x157 && ftMr_MS_SpecialAirLw == 0x15E,
               "the state numbers PlTd.dat's code passes to Fighter_ChangeMotionState");
_Static_assert(ftMr_MS_SelfCount == 10, "10 rows in PlTd.dat's move_logic");

/* ---- Special attributes (ftData->ext_attr; the load copies 0x70 bytes; values of PlTd.dat).
 * Not Mario's layout. The code reads +00 to +34. ---- */
typedef struct ftTd_DatAttrs {
    /* +00 */ float specials_momentum_x;      ///< 1.125
    /* +04 */ float specials_air_momentum_x;  ///< 1.5: x speed limit after leaving the floor
    /* +08 */ float specials_momentum_x_mul;  ///< 1.3
    /* +0C */ float specials_air_momentum_x_mul; ///< 0.175
    /* +10 */ float specials_tap_y_vel;       ///< 0.13
    /* +14 */ float specials_tap_grav;        ///< 0.09
    /* +18 */ float specialhi_fall_mobility;  ///< 0.5
    /* +1C */ float specialhi_landing_lag;    ///< 44
    /* +20 */ float specialhi_reverse_stick;  ///< 0.25
    /* +24 */ float specialhi_momentum_stick; ///< 0.7
    /* +28 */ float specialhi_angle_diff;     ///< 22 (degrees)
    /* +2C */ float specialairhi_vel_x_mul;   ///< 1.2
    /* +30 */ float specialairhi_grav;        ///< 0.5533
    /* +34 */ float specialairhi_vel_mul;     ///< 1.9
    /* +38 */ float x38[9];                   ///< 0.6, 0.33, 1.7, 0.1, 0.28, 1.2, 0.19, 0.4, 1.66
    /* +5C */ s32 x5C[3];                     ///< 3, 33, 12
    /* +68 */ float x68[2];                   ///< 0, 1
} DISC_STRUCT ftTd_DatAttrs;

_Static_assert(sizeof(ftTd_DatAttrs) == 0x70, "the size [onload] copies");

static inline ftTd_DatAttrs* ftTd_Attrs(Fighter* fp)
{
    return (ftTd_DatAttrs*) fp->dat_attrs;
}

/* ---- Fighter variables (console fp+222C on).
 * Mario's retail ondeath (called by Toad's own, and Mario's field order) writes fp+222C to
 * fp+2240, so the words they share are reached through Mario's struct:
 *   fp+2234  fp->u.mr.x2234_tornadoCharge: the air side special gave its lift already
 * Toad's own words start at fp+2240, which is the last member of Mario's struct: */
typedef struct ftTd_MeterVars {
    /* fp+2240 */ s32 level;       ///< 0 to 3
    /* fp+2244 */ s32 timer;       ///< frames until the level drops, 1200 at each raise
    /* fp+2248 */ HSD_GObj* meter; ///< the HUD object, NULL when it could not be made
} ftTd_MeterVars;

static inline ftTd_MeterVars* ftTd_Meter(Fighter* fp)
{
    return (ftTd_MeterVars*) &fp->u.mr.x2240;
}

/* ---- State variables of the side special (fp->mv, console fp+2340) ---- */
typedef struct ftTd_SpecialSVars {
    /* fp+2340 */ float decay;    ///< grows more negative by 0.83 a frame once cmd_vars[0] is set
    /* fp+2344 */ s32 x2344;      ///< 5 at entry, no reader
    /* fp+2348 */ s32 x2348;      ///< not used
    /* fp+234C */ float on_floor; ///< 1.0 while the floor test holds, else 0.0; no reader
} ftTd_SpecialSVars;

static inline ftTd_SpecialSVars* ftTd_SVars(Fighter* fp)
{
    return (ftTd_SpecialSVars*) &fp->mv;
}

/* ---- Articles (ftData->x48_items, in the order of his MxDt item lookup: 354, 355, 356) ---- */
enum {
    ftTd_Article_Shot = 0,  /* item kind 354 on ACE 2.0.0: the neutral special's projectile */
    ftTd_Article_Count = 1, /* articles with code; [onload] registers only article 0 */
};

/* m-ex effect ids of his own effect file (EfTdData.dat, index 55). */
#define FTTD_EFFECT_SPECIALN 5000 /* on the hand bone, with his facing */
#define FTTD_EFFECT_SPECIALS 5001 /* on his root joint */

/* ---- toad.c ---- */
extern const MotionState ftTd_Init_MotionStateTable[ftMr_MS_SelfCount];

/* ---- toad_meter.c ---- */
/* +075C */ HSD_GObj* ftTd_Meter_Create(HSD_GObj* fighter_gobj);
/* +1DBC */ void ftTd_Meter_ApplyLevel(HSD_GObj* fighter_gobj);
void ftTd_Meter_SaveBase(Fighter* fp);

/* ---- toad_specialn.c ---- */
/* +0284 */ void ftTd_SpecialN_Enter(HSD_GObj* gobj);
/* +02FC */ void ftTd_SpecialAirN_Enter(HSD_GObj* gobj);
/* +08EC */ void ftTd_SpecialN_Anim(HSD_GObj* gobj);
/* +0938 */ void ftTd_SpecialN_IASA(HSD_GObj* gobj);
/* +093C */ void ftTd_SpecialN_Phys(HSD_GObj* gobj);
/* +0940 */ void ftTd_SpecialN_Coll(HSD_GObj* gobj);
/* +09D0 */ void ftTd_SpecialAirN_Anim(HSD_GObj* gobj);
/* +0A20 */ void ftTd_SpecialAirN_Phys(HSD_GObj* gobj);
/* +0A24 */ void ftTd_SpecialAirN_Coll(HSD_GObj* gobj);

/* ---- toad_specials.c ---- */
/* +0374 */ void ftTd_SpecialS_Enter(HSD_GObj* gobj);
/* +0480 */ void ftTd_SpecialAirS_Enter(HSD_GObj* gobj);
/* +0AB4 */ void ftTd_SpecialS_Anim(HSD_GObj* gobj);
/* +0B1C */ void ftTd_SpecialS_Phys(HSD_GObj* gobj);
/* +0C8C */ void ftTd_SpecialS_Coll(HSD_GObj* gobj);
/* +0E10 */ void ftTd_SpecialAirS_Anim(HSD_GObj* gobj);
/* +0E98 */ void ftTd_SpecialAirS_Phys(HSD_GObj* gobj);
/* +0F70 */ void ftTd_SpecialAirS_Coll(HSD_GObj* gobj);

/* ---- toad_specialhi.c ---- */
/* +05A0 */ void ftTd_SpecialHi_Enter(HSD_GObj* gobj);
/* +0608 */ void ftTd_SpecialAirHi_Enter(HSD_GObj* gobj);
/* +10F0 */ void ftTd_SpecialHi_Anim(HSD_GObj* gobj);
/* +1164 */ void ftTd_SpecialHi_IASA(HSD_GObj* gobj);
/* +12A4 */ void ftTd_SpecialHi_Coll(HSD_GObj* gobj);
/* +1374 */ void ftTd_SpecialAirHi_Phys(HSD_GObj* gobj);

/* ---- toad_speciallw.c ---- */
/* +0688 */ void ftTd_SpecialLw_Enter(HSD_GObj* gobj);
/* +06F0 */ void ftTd_SpecialAirLw_Enter(HSD_GObj* gobj);
/* +1464 */ void ftTd_SpecialLw_Anim(HSD_GObj* gobj);
/* +14F4 */ void ftTd_SpecialLw_Phys(HSD_GObj* gobj);
/* +14F8 */ void ftTd_SpecialLw_Coll(HSD_GObj* gobj);
/* +1544 */ void ftTd_SpecialAirLw_Anim(HSD_GObj* gobj);
/* +1594 */ void ftTd_SpecialAirLw_Phys(HSD_GObj* gobj);
/* +1598 */ void ftTd_SpecialAirLw_Coll(HSD_GObj* gobj);

/* ---- toad_items.c ---- */
/* +1AE8 */ void itTd_Shot_Spawn(HSD_GObj* owner_gobj, Vec3* pos, ItemKind kind,
                                 float facing_dir);
extern const ItemLogicTable itTd_Articles[ftTd_Article_Count];

#endif
