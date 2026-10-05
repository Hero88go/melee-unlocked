/* ACE 2.0.0's Blastoise, native (PlBl.dat, ftDataBlastoise; m-ex internal 54, external 53).
 *
 * Blastoise ships as m-ex PowerPC in PlBl.dat: an "ftFunction" block built with symbols (0x1DC0
 * bytes, 22 exports) and two "itFunction" blocks built without. MxDt names Bowser's retail
 * function for every slot he has (all 22 non-empty rows are Bowser's, internal 5), but the file
 * exports its own function for all of them except the frame event, so in practice only four
 * things are Bowser's: the frame event (slot 23, the registry default), the physics of state 344,
 * the interrupt callback of state 346 and the whole of state 349.
 *
 * His nine states are his own: Hydro Pump (neutral special, one shot of article 0 with recoil),
 * a ground and air dash (side special), a steerable shell spin (up special) and Bubble (down
 * special, article 1, repeatable while B is held). His special attributes are his own block of
 * 0x58 bytes, not Bowser's.
 *
 * Offsets in the comments (+09C8) are offsets into the ftFunction block, as in
 * run-source/rel09-ace-native/blastoise/listing.txt; names in quotes are the file's own symbols.
 * "fp+" and "ip+" are console offsets of the Fighter and the Item. */
#ifndef MU_ACE_BLASTOISE_H
#define MU_ACE_BLASTOISE_H

#include <Runtime/platform.h>

#include <melee/ft/forward.h>
#include <melee/it/forward.h>
#include <sysdolphin/baselib/forward.h>

#include <dolphin/mtx.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftKoopa/forward.h>
#include <melee/ft/types.h>
#include <melee/it/kinds/forward.h>
#include <melee/it/kinds/types.h>
#include <melee/it/types.h>

#include <stddef.h>

#include "../../akaneia/mu_ak_fighter.h"
#include "mu_disc.h"

/* ---- Action states (move_logic, +00C4, numbered from ftCo_MS_Count) ---- */
typedef enum ftBlastoise_MotionState {
    ftBl_MS_SpecialN = ftCo_MS_Count, // 341
    ftBl_MS_SpecialAirN,              // 342
    ftBl_MS_SpecialS,                 // 343
    ftBl_MS_SpecialAirS,              // 344
    ftBl_MS_SpecialHi,                // 345
    ftBl_MS_SpecialAirHi,             // 346
    ftBl_MS_SpecialLw,                // 347
    ftBl_MS_SpecialAirLw,             // 348
    ftBl_MS_SpecialLwLanding,         // 349, Bowser's callbacks; nothing of his enters it
    ftBl_MS_Count,
    ftBl_MS_SelfCount = ftBl_MS_Count - ftCo_MS_Count,
} ftBlastoise_MotionState;

/* Animation ids of the states (his own animation table, from 295 as every fighter's). */
typedef enum ftBl_Submotion {
    ftBl_SM_SpecialN = ftCo_SM_Count, // 295
    ftBl_SM_SpecialAirN,
    ftBl_SM_SpecialS,
    ftBl_SM_SpecialAirS,
    ftBl_SM_SpecialHi,
    ftBl_SM_SpecialAirHi,
    ftBl_SM_SpecialLw,
    ftBl_SM_SpecialAirLw,
    ftBl_SM_SpecialLwLanding,         // 303
} ftBl_Submotion;

/* ---- Special attributes (ftData->ext_attr, 0x58 bytes, disc data; values of PlBl.dat) ---- */
typedef struct ftBl_DatAttrs {
    /* +00 */ float specialn_item_vel_x;     ///< 3: Hydro Pump speed
    /* +04 */ float specialn_item_offset_x;  ///< 3.2, from bone 34, times facing
    /* +08 */ float specialn_item_offset_y;  ///< 1.1
    /* +0C */ float specialairn_landing_lag; ///< 16
    /* +10 */ float specials_vel_x;          ///< 1.15: ground speed from frame 10
    /* +14 */ float specials_vel_x_end;      ///< 0.25: ground speed from frame 30
    /* +18 */ float specialairs_landing_lag; ///< 25
    /* +1C */ float specialairhi_landing_lag; ///< 10
    /* +20 */ float x20;                     ///< 0, not read by his code
    /* +24 */ float specialairhi_vel_y;      ///< 3: rise during the first 3 frames
    /* +28 */ float specialhi_vel_r;         ///< 0.7: frames 6 to 30, stick right
    /* +2C */ float specialhi_vel_l;         ///< -0.7
    /* +30 */ float specialhi_vel_r_full;    ///< 1.5: stick past 0.75
    /* +34 */ float specialhi_vel_l_full;    ///< -1.5
    /* +38 */ float specialhi_end_vel_r;     ///< 0.3: frames 31 to 50, stick right
    /* +3C */ float specialhi_end_vel_l;     ///< -0.3
    /* +40 */ float specialhi_end_vel_r_full; ///< 0.7
    /* +44 */ float specialhi_end_vel_l_full; ///< -0.7
    /* +48 */ float speciallw_item_vel_x;    ///< 0.2: Bubble speed
    /* +4C */ float speciallw_item_offset_x; ///< 3.2, from bone 61, times facing
    /* +50 */ float speciallw_item_offset_y; ///< 1.1
    /* +54 */ float x54;                     ///< 16, not read by his code
} DISC_STRUCT ftBl_DatAttrs;

_Static_assert(sizeof(ftBl_DatAttrs) == 0x58, "the size [ResetAttributes] copies");

/* Bowser's frame event (the m-ex default of slot 23, ftKp_SpecialLw_80134D78) runs on him and
 * reads this block as Bowser's: outside states 341 to 346 it adds +08 to fp+222C up to +10 and
 * +0C to fp+2230 up to +18. Nothing of Blastoise reads the two words back. */

/* ---- Fighter variables (fp->u, console fp+222C) ---- */
typedef struct ftBl_FighterVars {
    /* fp+222C */ float x222C; ///< written by Bowser's frame event only
    /* fp+2230 */ s32 x2230;   ///< cleared by [OnRespawn]; Bowser's frame event adds to it
    /* fp+2234 */ s32 x2234;   ///< cleared by [OnRespawn], no reader
} ftBl_FighterVars;

static inline ftBl_DatAttrs* ftBl_Attrs(Fighter* fp)
{
    return (ftBl_DatAttrs*) fp->dat_attrs;
}

static inline ftBl_FighterVars* ftBl_Vars(Fighter* fp)
{
    return (ftBl_FighterVars*) &fp->u;
}

/* ---- Articles (ftData->x48_items, in the order of his MxDt item lookup) ---- */
enum {
    ftBl_Article_HydroPump = 0, /* item kind 344 on ACE 2.0.0 */
    ftBl_Article_Bubble,        /* item kind 345 */
    ftBl_Article_Count,
};

/* Both articles' special attributes (article x4, disc data): the code reads the first word only.
 * Hydro Pump: 28, 20, 1.9, 2.9, 1.6, 1.6. Bubble: 120, 20, 1.9, 2.9, 1.6, 1.6. */
typedef struct itBl_Attrs {
    /* +00 */ float lifetime;
    /* +04 */ float x4;
    /* +08 */ float x8;
    /* +0C */ float xC;
    /* +10 */ float x10;
    /* +14 */ float x14;
} DISC_STRUCT itBl_Attrs;

/* The model bones the two shots leave from (fp->parts index: 0x220 / 0x10 and 0x3D0 / 0x10). */
#define FTBL_SPECIALN_BONE 34
#define FTBL_SPECIALLW_BONE 61

/* The kind a CPU Blastoise poses as while the game's CPU code runs: the second argument of
 * MexCPU_InitSpoofData in [OnLoad] (+0058, li r4, 0x1F), written to fp->kind as is. The CPU code
 * compares kinds with the retail numbers (m-ex maps the moved special fighters back for that
 * step), so 0x1F is the retail Giga Bowser, as for ACE's own Giga Bowser. */
#define ftBl_CpuSpoofKind Ft_Kind_GKoops

/* ---- blastoise.c ---- */
extern const MotionState ftBl_Init_MotionStateTable[ftBl_MS_SelfCount];

/* ---- blastoise_specials.c ---- */
/* +01E4 */ void ftBl_SpecialN_Enter(HSD_GObj* gobj);
/* +0208 */ void ftBl_SpecialAirN_Enter(HSD_GObj* gobj);
/* +022C */ void ftBl_SpecialS_Enter(HSD_GObj* gobj);
/* +0250 */ void ftBl_SpecialAirS_Enter(HSD_GObj* gobj);
/* +0274 */ void ftBl_SpecialHi_Enter(HSD_GObj* gobj);
/* +0298 */ void ftBl_SpecialAirHi_Enter(HSD_GObj* gobj);
/* +02BC */ void ftBl_SpecialLw_Enter(HSD_GObj* gobj);
/* +02E0 */ void ftBl_SpecialAirLw_Enter(HSD_GObj* gobj);

/* +065C */ void ftBl_SpecialN_Anim(HSD_GObj* gobj);
/* +069C */ void ftBl_SpecialN_Phys(HSD_GObj* gobj);
/* +06BC */ void ftBl_SpecialN_Coll(HSD_GObj* gobj);
/* +06FC */ void ftBl_SpecialAirN_Anim(HSD_GObj* gobj);
/* +073C */ void ftBl_SpecialAirN_Phys(HSD_GObj* gobj);
/* +075C */ void ftBl_SpecialAirN_Coll(HSD_GObj* gobj);
/* +07B8 */ void ftBl_SpecialS_Anim(HSD_GObj* gobj);
/* +07F8 */ void ftBl_SpecialS_Phys(HSD_GObj* gobj);
/* +0878 */ void ftBl_SpecialS_Coll(HSD_GObj* gobj);
/* +08E8 */ void ftBl_SpecialAirS_Anim(HSD_GObj* gobj);
/* +0928 */ void ftBl_SpecialAirS_Coll(HSD_GObj* gobj);
/* +0984 */ void ftBl_SpecialHi_Anim(HSD_GObj* gobj);
/* +09C4 */ void ftBl_SpecialHi_IASA(HSD_GObj* gobj);
/* +09C8 */ void ftBl_SpecialHi_Phys(HSD_GObj* gobj);
/* +0CB0 */ void ftBl_SpecialHi_Coll(HSD_GObj* gobj);
/* +0D20 */ void ftBl_SpecialAirHi_Anim(HSD_GObj* gobj);
/* +0D78 */ void ftBl_SpecialAirHi_Phys(HSD_GObj* gobj);
/* +10EC */ void ftBl_SpecialAirHi_Coll(HSD_GObj* gobj);
/* +1114 */ void ftBl_SpecialLw_Anim(HSD_GObj* gobj);
/* +1154 */ void ftBl_SpecialLw_IASA(HSD_GObj* gobj);
/* +11EC */ void ftBl_SpecialLw_Phys(HSD_GObj* gobj);
/* +120C */ void ftBl_SpecialLw_Coll(HSD_GObj* gobj);
/* +124C */ void ftBl_SpecialAirLw_Anim(HSD_GObj* gobj);
/* +128C */ void ftBl_SpecialAirLw_IASA(HSD_GObj* gobj);
/* +1324 */ void ftBl_SpecialAirLw_Phys(HSD_GObj* gobj);
/* +1344 */ void ftBl_SpecialAirLw_Coll(HSD_GObj* gobj);

/* ---- blastoise_items.c ---- */
/* +1BEC */ void itBl_HydroPump_Spawn(HSD_GObj* owner_gobj, Vec3* pos, Vec3* vel,
                                      float facing_dir);
/* +1AC4 */ void itBl_Bubble_Spawn(HSD_GObj* owner_gobj, Vec3* pos, Vec3* vel,
                                   float facing_dir);
extern const ItemLogicTable itBl_Articles[ftBl_Article_Count];

#endif
