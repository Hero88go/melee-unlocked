/* ACE 2.0.0's Fay, native (PlFy.dat, ftDataFay; m-ex internal 41, external 40, native kind
 * 0x30 = 48).
 *
 * Fay ships as m-ex PowerPC in PlFy.dat: one "ftFunction" block built without symbols (0x4900
 * bytes, 13 exports) and seven "itFunction" blocks (articles 0 to 6). MxDt names Fox's retail
 * function for the slots the file does not export (ondeath, the item and knockback events, the
 * attribute reload of slot 25, the common double jump), which the registry fills. Her special
 * attribute block is Fox's, word for word, for the 0xD4 bytes Fox's code reads (data.txt); her
 * own code never reads it. None of her 29 state rows is a retail row whole, but 25 retail Fox
 * callbacks sit in them (named by address in the rows, kept here by name).
 *
 *   neutral  two modes kept in fp+22F8 (A in the move swaps them, states 362, 363):
 *            mode 0: a held article (1) and shots of article 0 (4.5 a frame, 20 frames)
 *            mode 1: a held article (2) and shots of article 3 (9.0 a frame, 16 frames)
 *   side     a slash (article 5) at 36 degrees on the script's flag, and a held article (4);
 *            B held at frame 15 goes on to a second move (364 to 369) whose article 6 she
 *            steers up and down with the stick and that sends her into its end when it hits
 *   up       a charge (347, 348) that aims with the stick, then a launch (349) whose speed grows
 *            with the charge, and a helpless fall (351)
 *   down     a reflector (352 to 361) shaped like Fox's, with the reflect description of the
 *            file and a turn that tilts a bone by 45 degrees
 *
 * Her onframe also rewrites the callbacks of three common aerials (back, up, down air: a held
 * article 4 and a velocity kick) and of the special falls (normal drift), and a HUD object
 * (PlFyHUD.dat, "WpInd_scene_models") shows the neutral special's mode next to her damage.
 *
 * Offsets in the comments (+0D8C) are offsets into the ftFunction block, "it3 +58" into the
 * itFunction block of article 3, as in run-source/rel09-ace-native/fay/listing.txt. The file
 * has no symbols: the function names are this port's. "fp+" is a console offset of the Fighter.
 * FUNCTIONS.md in that folder lists every function of the eight blocks. */
#ifndef MU_ACE_FAY_H
#define MU_ACE_FAY_H

#include <Runtime/platform.h>

#include <melee/ft/forward.h>
#include <melee/it/forward.h>
#include <sysdolphin/baselib/forward.h>

#include <dolphin/mtx.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/types.h>
#include <melee/it/kinds/forward.h>
#include <melee/it/kinds/types.h>
#include <melee/it/types.h>

#include <stddef.h>

#include "../../akaneia/mu_ak_fighter.h"
#include "mu_disc.h"

/* ---- Action states (move_logic, +0138): 29 rows numbered from 341 ---- */
enum ftFy_MotionState {
    ftFy_MS_SpecialN = 341,       /* 0x155: neutral, mode 0, ground */
    ftFy_MS_SpecialN2,            /* 0x156: neutral, mode 1, ground */
    ftFy_MS_SpecialAirN,          /* 0x157: neutral, mode 0, air */
    ftFy_MS_SpecialAirN2,         /* 0x158: neutral, mode 1, air */
    ftFy_MS_SpecialS,             /* 0x159: side, ground */
    ftFy_MS_SpecialAirS,          /* 0x15A: side, air */
    ftFy_MS_SpecialHi,            /* 0x15B: up special charge, ground */
    ftFy_MS_SpecialAirHi,         /* 0x15C: up special charge, air */
    ftFy_MS_SpecialHiLaunch,      /* 0x15D: up special launch */
    ftFy_MS_SpecialHiUnused,      /* 0x15E: nothing of hers enters it */
    ftFy_MS_SpecialHiFall,        /* 0x15F: up special end (a helpless fall at its end) */
    ftFy_MS_SpecialLwStart,       /* 0x160: reflector start, ground */
    ftFy_MS_SpecialLwTurn,        /* 0x161: reflector after a turn, ground */
    ftFy_MS_SpecialLwHit,         /* 0x162: reflector hit, ground */
    ftFy_MS_SpecialLwEnd,         /* 0x163: reflector end, ground */
    ftFy_MS_SpecialLwLoop,        /* 0x164: reflector held, ground */
    ftFy_MS_SpecialAirLwStart,    /* 0x165: reflector start, air */
    ftFy_MS_SpecialAirLwTurn,     /* 0x166: reflector after a turn, air */
    ftFy_MS_SpecialAirLwHit,      /* 0x167: reflector hit, air */
    ftFy_MS_SpecialAirLwEnd,      /* 0x168: reflector end, air */
    ftFy_MS_SpecialAirLwLoop,     /* 0x169: reflector held, air */
    ftFy_MS_SpecialNSwap,         /* 0x16A: neutral mode swap, ground */
    ftFy_MS_SpecialAirNSwap,      /* 0x16B: neutral mode swap, air */
    ftFy_MS_SpecialS2,            /* 0x16C: second side move, ground */
    ftFy_MS_SpecialS2Hold,        /* 0x16D: second side move held, ground */
    ftFy_MS_SpecialS2End,         /* 0x16E: second side move end, ground */
    ftFy_MS_SpecialAirS2,         /* 0x16F: second side move, air */
    ftFy_MS_SpecialAirS2Hold,     /* 0x170: second side move held, air */
    ftFy_MS_SpecialAirS2End,      /* 0x171: second side move end, air */
    ftFy_MS_End
};

#define FTFY_MS_COUNT (ftFy_MS_End - ftFy_MS_SpecialN)
_Static_assert(FTFY_MS_COUNT == 29, "29 rows in PlFy.dat's move_logic");
_Static_assert(ftFy_MS_SpecialN == ftCo_MS_Count, "m-ex numbers a fighter's rows from 341");
_Static_assert(ftFy_MS_SpecialAirS2End == 0x171, "the last row is 0x171");

/* li r5, 0x5000 of the ground and air swaps: Ft_MF_SkipColAnim | Ft_MF_UpdateCmd. */
#define FTFY_MF_SWAP ((MotionFlags) 0x5000)

/* ---- Fighter variables (fp->u, console fp+222C). Her own words are fp+22DC to fp+22FC. The
 * slots before them are left to Fox's struct: MxDt's ondeath is Fox's retail routine, which
 * clears Fox's blaster pointer at the start of the union. Two of her words are pointers
 * (fp+22F0, fp+22FC); natively they are 8 bytes, so the offsets past fp+22EC are not the
 * console's, and every reader goes through the names. ---- */
typedef struct ftFy_FighterVars {
    /* fp+222C */ u8 fox_[0x22DC - 0x222C];
    /* fp+22DC */ float hi_angle;     ///< up special aim, radians (pi/2 at the start)
    /* fp+22E0 */ float x22E0;        ///< cleared at load, no reader
    /* fp+22E4 */ s32 lw_turn;        ///< reflector: frames of the 45 degree tilt left (4)
    /* fp+22E8 */ s32 hi_frames;      ///< up special: frames charged
    /* fp+22EC */ s32 s_lock;         ///< tested by the side special and onframe; nothing sets it
    /* fp+22F0 */ HSD_GObj* s2_shot;  ///< the second side move's article 6, NULL when gone
    /* fp+22F4 */ float x22F4;        ///< cleared at load, no reader
    /* fp+22F8 */ s32 n_mode;         ///< neutral special mode, 0 or 1 (the HUD's frame)
    /* fp+22FC */ HSD_GObj* hud;      ///< the HUD object made at load; no reader
} ftFy_FighterVars;

static inline ftFy_FighterVars* ftFy_FV(Fighter* fp)
{
    return (ftFy_FighterVars*) &fp->u;
}

/* ---- Articles (ftData->x48_items, in the order of her MxDt item lookup: 313 to 319). All
 * seven have code and [onload] registers all seven. ---- */
enum {
    ftFy_Article_Shot = 0,    /* item kind 313 on ACE 2.0.0: neutral mode 0's shot */
    ftFy_Article_Gun = 1,     /* 314: neutral mode 0's held article */
    ftFy_Article_Gun2 = 2,    /* 315: neutral mode 1's held article */
    ftFy_Article_Shot2 = 3,   /* 316: neutral mode 1's shot */
    ftFy_Article_Blade = 4,   /* 317: the held article of the side moves and the aerials */
    ftFy_Article_Slash = 5,   /* 318: the side special's slash */
    ftFy_Article_Wave = 6,    /* 319: the second side move's steered article */
    ftFy_Article_Count = 7,
};

/* Item variables (ip->xDD4_itemVar, console item+DD4). One layout for the seven: the file
 * writes the owner at +DD4 for all, and +DFC, +E00 for article 5 and +E00 for article 6. The
 * console offsets of +DFC and +E00 do not hold natively (the owner pointer is 8 bytes). */
typedef struct itFy_ItemVars {
    /* +DD4 */ HSD_GObj* owner;
    /* +DD8 */ u8 xDD8[0xDFC - 0xDD8];
    /* +DFC */ HSD_GObj* owner2;  ///< article 5: the owner again, no reader
    /* +E00 */ float angle;       ///< article 5: its tilt; article 6: its aim, both radians
} itFy_ItemVars;

#define itFy_IV(ip) ((itFy_ItemVars*) &(ip)->xDD4_itemVar)

/* m-ex ids of her own effect file (EfFxData.dat, effect file 3, Fox's file as ACE changed it;
 * her MxDt entry names it). */
#define FTFY_EFFECT_REFLECT 5012 /* 0x1394: +3F8C, the reflector, on the bone of part 3 */
#define FTFY_EFFECT_REFLECT_HIT 5002 /* 0x138A: +470C, after a reflect */

/* +0D88: the CPU spoof kind. See fay.c and NOTES.md (Unproven). */
#define FTFY_CPU_SPOOF_KIND Ft_Kind_Fox

/* ---- fay.c ---- */
extern const MotionState ftFy_Init_MotionStateTable[FTFY_MS_COUNT];
/* +32DC */ void ftFy_TakeDamage_ClearShotFlag(HSD_GObj* gobj);

/* ---- fay_specialn.c ---- */
/* +04D8 */ void ftFy_SpecialN_Enter(HSD_GObj* gobj);
/* +05E4 */ void ftFy_SpecialAirN_Enter(HSD_GObj* gobj);
/* +0D8C */ void ftFy_SpecialN_Anim(HSD_GObj* gobj);
/* +0F50 */ void ftFy_SpecialN2_Anim(HSD_GObj* gobj);
/* +1114 */ void ftFy_SpecialAirN_Anim(HSD_GObj* gobj);
/* +12D4 */ void ftFy_SpecialAirN2_Anim(HSD_GObj* gobj);
/* +1498 */ void ftFy_SpecialAirN2_Phys(HSD_GObj* gobj);
/* +1530 */ void ftFy_SpecialAirN2_Coll(HSD_GObj* gobj);
/* +2E64 */ void ftFy_SpecialNSwap_Anim(HSD_GObj* gobj);
/* +2EB0 */ void ftFy_SpecialAirNSwap_Anim(HSD_GObj* gobj);

/* ---- fay_specials.c ---- */
/* +06F0 */ void ftFy_SpecialS_Enter(HSD_GObj* gobj);
/* +0764 */ void ftFy_SpecialAirS_Enter(HSD_GObj* gobj);
/* +157C */ void ftFy_SpecialS_Anim(HSD_GObj* gobj);
/* +16EC */ void ftFy_SpecialS_IASA(HSD_GObj* gobj);
/* +1768 */ void ftFy_SpecialS_Phys(HSD_GObj* gobj);
/* +17B8 */ void ftFy_SpecialAirS_Anim(HSD_GObj* gobj);
/* +1920 */ void ftFy_SpecialAirS_IASA(HSD_GObj* gobj);
/* +199C */ void ftFy_SpecialAirS_Phys(HSD_GObj* gobj);
/* +1A44 */ void ftFy_SpecialAirS_Coll(HSD_GObj* gobj);
/* +2EFC */ void ftFy_SpecialS2_Anim(HSD_GObj* gobj);
/* +3014 */ void ftFy_SpecialS2_Coll(HSD_GObj* gobj);
/* +3060 */ void ftFy_SpecialS2Hold_Anim(HSD_GObj* gobj);
/* +3094 */ void ftFy_SpecialS2End_Anim(HSD_GObj* gobj);
/* +30E0 */ void ftFy_SpecialAirS2_Anim(HSD_GObj* gobj);
/* +3200 */ void ftFy_SpecialAirS2_Phys(HSD_GObj* gobj);
/* +3210 */ void ftFy_SpecialAirS2_Coll(HSD_GObj* gobj);
/* +325C */ void ftFy_SpecialAirS2Hold_Anim(HSD_GObj* gobj);
/* +3290 */ void ftFy_SpecialAirS2End_Anim(HSD_GObj* gobj);

/* ---- fay_specialhi.c ---- */
/* +07D8 */ void ftFy_SpecialHi_Enter(HSD_GObj* gobj);
/* +08FC */ void ftFy_SpecialAirHi_Enter(HSD_GObj* gobj);
/* +1AF8 */ void ftFy_SpecialHi_Anim(HSD_GObj* gobj);
/* +1DAC */ void ftFy_SpecialAirHi_Phys(HSD_GObj* gobj);
/* +1DE8 */ void ftFy_SpecialAirHi_Coll(HSD_GObj* gobj);
/* +1E70 */ void ftFy_SpecialHiLaunch_Anim(HSD_GObj* gobj);
/* +1F00 */ void ftFy_SpecialHi_Nothing(HSD_GObj* gobj);
/* +1F04 */ void ftFy_SpecialHiLaunch_Phys(HSD_GObj* gobj);
/* +1FB0 */ void ftFy_SpecialHiUnused_Anim(HSD_GObj* gobj);
/* +2020 */ void ftFy_SpecialHiUnused_Coll(HSD_GObj* gobj);
/* +2024 */ void ftFy_SpecialHiFall_Anim(HSD_GObj* gobj);
/* +2090 */ void ftFy_SpecialHiFall_Phys(HSD_GObj* gobj);
/* +2128 */ void ftFy_SpecialHiFall_Coll(HSD_GObj* gobj);

/* ---- fay_speciallw.c ---- */
/* +0A20 */ void ftFy_SpecialLw_Enter(HSD_GObj* gobj);
/* +0A70 */ void ftFy_SpecialAirLw_Enter(HSD_GObj* gobj);
/* +21E8 */ void ftFy_SpecialLwStart_Anim(HSD_GObj* gobj);
/* +2294 */ void ftFy_SpecialLwStart_IASA(HSD_GObj* gobj);
/* +22F0 */ void ftFy_SpecialLwTurn_Anim(HSD_GObj* gobj);
/* +2444 */ void ftFy_SpecialLw_JumpIASA(HSD_GObj* gobj);
/* +2490 */ void ftFy_SpecialLwHit_Anim(HSD_GObj* gobj);
/* +25B4 */ void ftFy_SpecialLwEnd_Anim(HSD_GObj* gobj);
/* +2600 */ void ftFy_SpecialLwLoop_Anim(HSD_GObj* gobj);
/* +2800 */ void ftFy_SpecialAirLw_Coll(HSD_GObj* gobj);
/* +284C */ void ftFy_SpecialAirLwStart_Anim(HSD_GObj* gobj);
/* +28F8 */ void ftFy_SpecialAirLwTurn_Anim(HSD_GObj* gobj);
/* +2A58 */ void ftFy_SpecialAirLw_JumpIASA(HSD_GObj* gobj);
/* +2AA4 */ void ftFy_SpecialAirLw_Phys(HSD_GObj* gobj);
/* +2AF4 */ void ftFy_SpecialAirLwHit_Anim(HSD_GObj* gobj);
/* +2C18 */ void ftFy_SpecialAirLwEnd_Anim(HSD_GObj* gobj);
/* +2C64 */ void ftFy_SpecialAirLwLoop_Anim(HSD_GObj* gobj);

/* ---- fay_items.c ---- */
/* +39B8 */ void itFy_Held_Spawn(HSD_GObj* owner_gobj, Vec3* pos, ItemKind kind, s32 msid,
                                 float facing);
/* +3AE8 */ void itFy_Shot_Spawn(HSD_GObj* owner_gobj, Vec3* pos, Vec3* vel, ItemKind kind,
                                 s32 lifetime, float facing);
/* +3D1C */ void itFy_Slash_Spawn(HSD_GObj* owner_gobj, Vec3* pos, Vec3* vel, s32 lifetime,
                                  float facing, float angle);
/* +3E74 */ void itFy_Blade_Spawn(HSD_GObj* owner_gobj, Vec3* pos, s32 msid, float facing);
/* +4244 */ void itFy_Wave_Spawn(HSD_GObj* owner_gobj, Vec3* pos, s32 lifetime, float facing);
extern const ItemLogicTable itFy_Articles[ftFy_Article_Count];

#endif
