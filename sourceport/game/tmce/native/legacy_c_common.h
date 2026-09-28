/* Training Mode CE, native build: what ten of UnclePunch's original events share among themselves
 * (ShieldDrop, AttackOnShield, Ledgetech, AmsahTech, ComboTraining, WaveshineSDI, SlideOff,
 * GrabMashOut, LedgetechCounter, EscapeSheik).
 *
 * The events come from run-source/tmce-src/ASM/training-mode/Custom Events/Custom Event Code -
 * Rewrite.asm (lines 2352-8600), rewritten by hand in C. The file's shared routines (lines
 * 8600-12371) are the lg_<AsmLabel> of legacy_common.h; they are called, not reimplemented, here.
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#ifndef LEGACY_C_COMMON_H
#define LEGACY_C_COMMON_H

#include "legacy_common.h"
#include "legacy.h"

/* ---------------------------------------------------------------------------------------------
 * Globals.s constants
 * --------------------------------------------------------------------------------------------- */
#define LGC_FOX_EXT 0x2
#define LGC_MARTH_EXT 0x9
#define LGC_SHEIK_EXT 0x13
#define LGC_FALCO_EXT 0x14
#define LGC_DK_INT 0x3
#define LGC_STAGE_POKEMON_STADIUM 0x3
#define LGC_STAGE_BATTLEFIELD 0x1F
#define LGC_STAGE_FINAL_DESTINATION 0x20
#define LGC_OSD_MISCELLANEOUS 15

/* the action state IDs these events name (Globals.s ASID_*), checked against MexTK's */
_Static_assert(ASID_WAIT == 0x0E && ASID_TURN == 0x12 && ASID_DASH == 0x14 && ASID_RUN == 0x15, "ASID");
_Static_assert(ASID_LANDING == 0x2A && ASID_DAMAGEHI1 == 0x4B && ASID_DAMAGEFLYTOP == 0x5A, "ASID");
_Static_assert(ASID_DAMAGEFLYROLL == 0x5B && ASID_DOWNBOUNDU == 0xB7 && ASID_DOWNWAITU == 0xB8, "ASID");
_Static_assert(ASID_DOWNBOUNDD == 0xBF && ASID_DOWNWAITD == 0xC0 && ASID_PASSIVE == 0xC7, "ASID");
_Static_assert(ASID_PASSIVESTANDF == 0xC8 && ASID_PASSIVESTANDB == 0xC9 && ASID_PASSIVEWALL == 0xCA, "ASID");
_Static_assert(ASID_PASSIVEWALLJUMP == 0xCB && ASID_THROWHI == 0xDD && ASID_CAPTUREPULLEDHI == 0xDF, "ASID");
_Static_assert(ASID_CAPTUREWAITHI == 0xE0 && ASID_CAPTUREPULLEDLW == 0xE2 && ASID_CAPTUREWAITLW == 0xE3, "ASID");
_Static_assert(ASID_CAPTURECUT == 0xE5 && ASID_CAPTUREJUMP == 0xE6 && ASID_CAPTUREFOOT == 0xE8, "ASID");
_Static_assert(ASID_THROWNF == 0xEF && ASID_THROWNLWWOMEN == 0xF3 && ASID_SHOULDEREDWAIT == 0x10A, "ASID");
_Static_assert(ASID_SHOULDEREDTURN == 0x10E && ASID_THROWNFF == 0x10F && ASID_THROWNFLW == 0x112, "ASID");
_Static_assert(ASID_CAPTUREKOOPA == 0x116 && ASID_CAPTUREWAITKOOPA == 0x118, "ASID");
_Static_assert(ASID_CAPTUREKOOPAAIR == 0x11B && ASID_CAPTUREWAITKOOPAAIR == 0x11D, "ASID");

/* ---------------------------------------------------------------------------------------------
 * Fighter fields the twin leaves out, by console offset through legacy_common.h's accessors
 * --------------------------------------------------------------------------------------------- */
#define LGC_FT_FREEZE_SET(fd) lg_FtSetFlag((fd), 0x2219, 0x04, 1)  /* flags.freeze */
#define LGC_FT_HITLAG(fd) lg_FtFlag((fd), 0x221A, 0x20)            /* flags.hitlag */
#define LGC_FT_HITLAG_SET(fd) lg_FtSetFlag((fd), 0x221A, 0x20, 1)
#define LGC_FT_FASTFALL(fd) lg_FtFlag((fd), 0x221A, 0x08)          /* flags.is_fastfall */
#define LGC_FT_HITSTUN(fd) lg_FtFlag((fd), 0x221C, 0x02)           /* flags.hitstun */
#define LGC_FT_NUDGE_DISABLE_SET(fd) lg_FtSetFlag((fd), 0x221D, 0x04, 1) /* flags.nudge_disable */
#define LGC_FT_DEAD(fd) lg_FtFlag((fd), 0x221F, 0x40)              /* flags.dead */

/* Stopgap: two console offsets legacy_common.h's accessors do not list yet ("an offset they do not
 * know reports and reads 0"). Each is the native place of the decomp member holding the console
 * field, from the game DLL's DWARF (decomp_info.json, 2026-09-28): replace with lg_FtPtr(fd, 0x988) and
 * lg_FtU32(fd, 0xE14) once mu_tmce_legacy_ft_* know them. */
/* hitbox[0].victims[0].data, console 0x988: decomp x914[0].victims_1[0] (native +0xB30) */
#define LGC_FT_HITBOX0_VICTIM0(fd) (*(void **) ((u8 *) (fd) + 0xB30))
/* throw_hitbox[0].angle, console 0xE14: decomp xDF4[0].kb_angle (native +0x12D8) */
#define LGC_FT_THROW_HITBOX0_ANGLE(fd) (*(int *) ((u8 *) (fd) + 0x12D8))

/* ---------------------------------------------------------------------------------------------
 * Game functions MexTK has no name for (the ASM's branchl): the decomp functions
 * --------------------------------------------------------------------------------------------- */
/* fn_800D62C4 and ftCo_800939B4 are file-private in the decomp (ftCo_SquatWait.c, ftCo_Guard.c):
 * natively they are reached through MU_NATIVE wrappers named as the other mu_tmce_fn_* ones */
void lgc_AS_SquatWait(GOBJ *fighter) __asm__("mu_tmce_fn_800D62C4");     /* 0x800D62C4 */
void lgc_AS_Guard(GOBJ *fighter) __asm__("mu_tmce_fn_ftCo_800939B4");    /* 0x800939B4 */
void lgc_AS_Catch(GOBJ *fighter, int state_id) __asm__("ftCo_800D8C54"); /* 0x800D8C54 */
void lgc_AS_GrabOpponent(GOBJ *fighter) __asm__("fn_800D9CE8");          /* 0x800D9CE8 */
void lgc_AS_Grabbed(GOBJ *fighter, GOBJ *grabber) __asm__("fn_800DAADC"); /* 0x800DAADC */
void lgc_AS_CatchWait(GOBJ *fighter) __asm__("fn_800DA1D8");             /* 0x800DA1D8 */
float lgc_CPU_JoystickXAxis_Convert(FighterData *fighter) __asm__("ftCo_GetCpuLStickX"); /* 0x800A17E4 */
float lgc_CPU_JoystickYAxis_Convert(FighterData *fighter) __asm__("ftCo_GetCpuLStickY"); /* 0x800A1874 */
void lgc_RemoveHitbox(GOBJ *fighter, int hit_idx) __asm__("ftColl_8007AFC8"); /* 0x8007AFC8 */
int lgc_GetComboCount(int slot) __asm__("pl_8004134C");                  /* 0x8004134C */

/* PowerPC fabs: clears the sign bit (MexTK's fabs keeps the sign of -0 and NaN) */
#define LGC_FABS(x) __builtin_fabsf(x)

/* Rtoc floats of the retail DOL the ASM reads (lfs -0xXXXX(rtoc), rtoc 0x804DF9E0) */
#define LGC_RTOC_0F 0.0f                 /* -0x76B0, -0x7414, -0x750C, -0x68E0, -0x37B4 */
#define LGC_RTOC_1F 1.0f                 /* -0x7418, -0x73D0, -0x68DC */
#define LGC_RTOC_DEG_TO_RAD 0.017453292f /* -0x7510: 0x3C8EFA35 */
#define LGC_RTOC_40F 40.0f               /* -0x37B0 */

/* The static player block's damage shown on the HUD (console 0x80453080 + 0xE90 * slot + 0x60) */
#define LGC_SET_SHOWN_PERCENT(slot, percent) mu_tmce_legacy_static_set((slot), 0x60, (percent))

/* ---------------------------------------------------------------------------------------------
 * Routines one event keeps that another of these events also calls
 * --------------------------------------------------------------------------------------------- */
/* ComboTrainingDecideStickAngle_ConvertAngle (legacy_combotraining.c): an angle in degrees as the
 * stick's X and Y (-127..127, each 0 under 36) */
void lgc_ConvertAngle(int angle, int *stick_x, int *stick_y);
/* Ledgetech_InitializePositions (legacy_ledgetech.c): r3 = the ledge (0 left, 1 right); P2 at the
 * ledge, P1 on the respawn platform in front of it */
void lgc_Ledgetech_InitializePositions(LgPlayers *pl, int ledge_side);

#endif
