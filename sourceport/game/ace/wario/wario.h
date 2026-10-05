/* ACE 2.0.0's Wario, native (PlWr.dat, ftDataWario; m-ex internal 35, external 34).
 *
 * Wario ships as m-ex PowerPC in PlWr.dat: one "ftFunction" block built without symbols (0x21A0
 * bytes, 18 exports) and no "itFunction" block. He is a fighter of his own: every MxDt function
 * row of internal 35 is empty but the double jump (the common one) and the two entry table size,
 * and every state of his table but the last is the file's own code. Nothing of a retail fighter
 * is borrowed; what he calls is the common fighter code.
 *
 *   neutral  a punch that can be turned around during its first 7 frames
 *   side     a shoulder dash: the script raises command variables for the speed and the trail;
 *            a jump input leaves it into a leap, a hit bounces him back
 *   up       a rising spin steered with the stick, then the special fall
 *   down     a short rise, then a dive at a fixed speed until the floor
 *
 * Offsets in the comments (+0890) are offsets into the ftFunction block, as in
 * run-source/rel09-ace-native/wario/listing.txt. The file has no symbols: the function names are
 * this port's. "fp+" is a console offset of the Fighter. */
#ifndef MU_ACE_WARIO_H
#define MU_ACE_WARIO_H

#include <Runtime/platform.h>

#include <melee/ft/forward.h>
#include <sysdolphin/baselib/forward.h>

#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/types.h>
#include <melee/lb/types.h>

#include <stddef.h>

#include "../../akaneia/mu_ak_fighter.h"
#include "mu_disc.h"

/* ---- Action states (move_logic, +0094, numbered from ftCo_MS_Count) ---- */
typedef enum ftWario_MotionState {
    ftWr_MS_SpecialN = ftCo_MS_Count, // 341 (0x155)
    ftWr_MS_SpecialAirN,              // 342 (0x156)
    ftWr_MS_SpecialS,                 // 343 (0x157)
    ftWr_MS_SpecialSHit,              // 344 (0x158): the bounce after a hit on the ground
    ftWr_MS_SpecialAirS,              // 345 (0x159)
    ftWr_MS_SpecialSBound,            // 346 (0x15A): the bounce after a hit in the air
    ftWr_MS_SpecialSJump,             // 347 (0x15B): the leap out of the dash
    ftWr_MS_SpecialHi,                // 348 (0x15C)
    ftWr_MS_SpecialAirHi,             // 349 (0x15D)
    ftWr_MS_SpecialLw,                // 350 (0x15E)
    ftWr_MS_SpecialAirLw,             // 351 (0x15F)
    ftWr_MS_SpecialLwFall,            // 352 (0x160): the dive
    ftWr_MS_SpecialLwLanding,         // 353 (0x161)
    ftWr_MS_AppealS,                  // 354: the common taunt's callbacks on animation 239
    ftWr_MS_Count,
    ftWr_MS_SelfCount = ftWr_MS_Count - ftCo_MS_Count,
} ftWario_MotionState;

_Static_assert(ftWr_MS_SpecialN == 0x155 && ftWr_MS_SelfCount == 14,
               "the state numbers PlWr.dat's code passes to Fighter_ChangeMotionState");

/* ---- Special attributes (ftData->ext_attr, 0x80 bytes, disc data; values of PlWr.dat) ---- */
typedef struct ftWr_DatAttrs {
    /* +00 */ u32 specialn_turn_frames;       ///< 7: the punch can turn before this frame
    /* +04 */ float specialn_turn_stick;      ///< 0.5: stick past this, against the facing
    /* +08 */ float x8;                       ///< 0.15, not read by his code
    /* +0C */ u32 specialn_phys_turn;         ///< 0: the physics callbacks' turn is off
    /* +10 */ u32 specialhi_turn_frames;      ///< 4
    /* +14 */ float specialhi_turn_stick;     ///< 0.1
    /* +18 */ float specialhi_drift;          ///< 0.9: x speed per unit of stick
    /* +1C */ float specialhi_fall_mobility;  ///< 1.12
    /* +20 */ u32 specialhi_landing_lag;      ///< 18
    /* +24 */ float specials_vel_x;           ///< 2.5: dash speed (command variable 2 at 1)
    /* +28 */ float specials_air_vel_x;       ///< 1.8
    /* +2C */ float x2C;                      ///< 0.3, not read
    /* +30 */ float specials_jump_vel_x;      ///< 2.06: the leap
    /* +34 */ float specials_jump_vel_y;      ///< 2.8
    /* +38 */ float specials_fall_terminal;   ///< 1.4: the dash carried off a floor
    /* +3C */ float specials_restart_frame;   ///< 13: where the dash animation resumes
    /* +40 */ float specials_fall_mobility;   ///< 1.3
    /* +44 */ u32 specials_landing_lag;       ///< 16 (halved when the air dash lands)
    /* +48 */ float speciallw_fall_vel_y;     ///< -5.2: the dive
    /* +4C */ float x4C;                      ///< 4, not read
    /* +50 */ float specialn_ground_turn_stick; ///< 0.1: the ground punch's animation callback
    /* +54 */ float specials_stop_decel;      ///< 0.3 (command variable 2 at 2)
    /* +58 */ float specialairs_gravity;      ///< 0
    /* +5C */ float specialairs_gravity_late; ///< 0.012 (command variable 1 set)
    /* +60 */ float specialairs_terminal;     ///< 0.6
    /* +64 */ float x64;                      ///< 0.04, not read
    /* +68 */ float specials_bound_vel_x;     ///< -0.3: the bounce after a hit, times facing
    /* +6C */ float specials_bound_vel_y;     ///< 2.5
    /* +70 */ float specialairs_bound_vel_x;  ///< -0.3
    /* +74 */ float specialairs_bound_vel_y;  ///< 2.5
    /* +78 */ float bound_drift;              ///< 0.08: x speed added per unit of stick
    /* +7C */ float bound_max_vel_x;          ///< 1.5
} DISC_STRUCT ftWr_DatAttrs;

_Static_assert(sizeof(ftWr_DatAttrs) == 0x80, "the size the load and the respawn event copy");

/* "ftColAnim" of PlWr.dat (a public symbol next to ftDataWario): one pointer to a table of color
 * animations in the layout lb_800144C8 reads. Entry 0 is played by the side special. */
typedef DISC_PTR(struct Fighter_804D653C_t) ftWr_ColAnimSlot;

/* ---- Fighter variables (fp->u, console fp+222C) ---- */
typedef struct ftWr_FighterVars {
    /* fp+222C */ s32 x222C;                ///< cleared at load, no reader
    /* fp+2230 */ ftWr_ColAnimSlot* colanim; ///< "ftColAnim" of PlWr.dat, NULL when not found
} ftWr_FighterVars;

/* ---- State variables (fp->mv, console fp+2340) ---- */
typedef struct ftWr_MotionVars {
    /* fp+2340 */ s32 x2340;    ///< not used
    /* fp+2344 */ s32 restart;  ///< set by the ground side special, cleared by the air one
} ftWr_MotionVars;

static inline ftWr_DatAttrs* ftWr_Attrs(Fighter* fp)
{
    return (ftWr_DatAttrs*) fp->dat_attrs;
}

static inline ftWr_FighterVars* ftWr_Vars(Fighter* fp)
{
    return (ftWr_FighterVars*) &fp->u;
}

static inline ftWr_MotionVars* ftWr_MVars(Fighter* fp)
{
    return (ftWr_MotionVars*) &fp->mv;
}

/* The state change flags of the two neutral special collision callbacks (0x0C4C5882). */
#define ftWr_MF_SpecialN_Coll                                                \
    (Ft_MF_KeepGfx | Ft_MF_SkipMatAnim | Ft_MF_SkipRumble | Ft_MF_SkipColAnim | \
     Ft_MF_UpdateCmd | Ft_MF_SkipItemVis | Ft_MF_Unk19 |                     \
     Ft_MF_SkipModelPartVis | Ft_MF_SkipModelFlags | Ft_MF_Unk27)

/* m-ex effect ids of his own effect file (EfWrData.dat, index 29). */
#define FTWR_EFFECT_DASH 5000 /* model effect 0, on the top joint */
#define FTWR_EFFECT_DIVE 6015 /* particle generator 15 (0x177F), on fp->parts[1] */

/* ---- wario.c ---- */
extern const MotionState ftWr_Init_MotionStateTable[ftWr_MS_SelfCount];

/* ---- wario_specialn.c ---- */
/* +0254 */ void ftWr_SpecialN_Enter(HSD_GObj* gobj);
/* +02B0 */ void ftWr_SpecialAirN_Enter(HSD_GObj* gobj);
/* +0890 */ void ftWr_SpecialN_Anim(HSD_GObj* gobj);
/* +09A8 */ void ftWr_SpecialN_Phys(HSD_GObj* gobj);
/* +0AB4 */ void ftWr_SpecialN_Coll(HSD_GObj* gobj);
/* +0B44 */ void ftWr_SpecialAirN_Anim(HSD_GObj* gobj);
/* +0C5C */ void ftWr_SpecialAirN_Phys(HSD_GObj* gobj);
/* +0D78 */ void ftWr_SpecialAirN_Coll(HSD_GObj* gobj);

/* ---- wario_specials.c ---- */
/* +030C */ void ftWr_SpecialS_Enter(HSD_GObj* gobj);
/* +039C */ void ftWr_SpecialAirS_Enter(HSD_GObj* gobj);
/* +0E28 */ void ftWr_SpecialS_Anim(HSD_GObj* gobj);
/* +0FFC */ void ftWr_SpecialS_IASA(HSD_GObj* gobj);
/* +10CC */ void ftWr_SpecialS_Phys(HSD_GObj* gobj);
/* +11B0 */ void ftWr_SpecialS_Coll(HSD_GObj* gobj);
/* +128C */ void ftWr_SpecialSHit_Anim(HSD_GObj* gobj);
/* +12E4 */ void ftWr_SpecialSHit_Phys(HSD_GObj* gobj);
/* +12E8 */ void ftWr_SpecialSHit_Coll(HSD_GObj* gobj);
/* +1334 */ void ftWr_SpecialAirS_Anim(HSD_GObj* gobj);
/* +1438 */ void ftWr_SpecialAirS_Phys(HSD_GObj* gobj);
/* +14D0 */ void ftWr_SpecialAirS_Coll(HSD_GObj* gobj);
/* +1638 */ void ftWr_SpecialSBound_Anim(HSD_GObj* gobj);
/* +16D8 */ void ftWr_SpecialSBound_Phys(HSD_GObj* gobj);
/* +173C */ void ftWr_SpecialSBound_Coll(HSD_GObj* gobj);
/* +1788 */ void ftWr_SpecialSJump_Anim(HSD_GObj* gobj);
/* +1868 */ void ftWr_SpecialSJump_IASA(HSD_GObj* gobj);
/* +18D0 */ void ftWr_SpecialSJump_Phys(HSD_GObj* gobj);
/* +18EC */ void ftWr_SpecialSJump_Coll(HSD_GObj* gobj);

/* ---- wario_specialhi.c ---- */
/* +042C */ void ftWr_SpecialHi_Enter(HSD_GObj* gobj);
/* +047C */ void ftWr_SpecialAirHi_Enter(HSD_GObj* gobj);
/* +19B0 */ void ftWr_SpecialHi_Anim(HSD_GObj* gobj);
/* +1B10 */ void ftWr_SpecialHi_IASA(HSD_GObj* gobj);
/* +1B14 */ void ftWr_SpecialHi_Phys(HSD_GObj* gobj);
/* +1B30 */ void ftWr_SpecialHi_Coll(HSD_GObj* gobj);
/* +1B78 */ void ftWr_SpecialAirHi_Anim(HSD_GObj* gobj);
/* +1B7C */ void ftWr_SpecialAirHi_IASA(HSD_GObj* gobj);
/* +1B80 */ void ftWr_SpecialAirHi_Phys(HSD_GObj* gobj);
/* +1B9C */ void ftWr_SpecialAirHi_Coll(HSD_GObj* gobj);

/* ---- wario_speciallw.c ---- */
/* +04E4 */ void ftWr_SpecialLw_Enter(HSD_GObj* gobj);
/* +0548 */ void ftWr_SpecialAirLw_Enter(HSD_GObj* gobj);
/* +1BE4 */ void ftWr_SpecialLw_Anim(HSD_GObj* gobj);
/* +1C50 */ void ftWr_SpecialLw_Phys(HSD_GObj* gobj);
/* +1C94 */ void ftWr_SpecialLw_Coll(HSD_GObj* gobj);
/* +1D40 */ void ftWr_SpecialAirLw_Anim(HSD_GObj* gobj);
/* +1DAC */ void ftWr_SpecialAirLw_Phys(HSD_GObj* gobj);
/* +1DCC */ void ftWr_SpecialAirLw_Coll(HSD_GObj* gobj);
/* +1E50 */ void ftWr_SpecialLwFall_Anim(HSD_GObj* gobj);
/* +1E80 */ void ftWr_SpecialLwFall_Phys(HSD_GObj* gobj);
/* +1E94 */ void ftWr_SpecialLwFall_Coll(HSD_GObj* gobj);
/* +1EA4 */ void ftWr_SpecialLwLanding_Anim(HSD_GObj* gobj);
/* +1EF0 */ void ftWr_SpecialLwLanding_Phys(HSD_GObj* gobj);

#endif
