/* 20XX Hack Pack training options for CPUs, as native C (shim/mu_20xx_cpu.c).
 *
 * Each function is called from the decompiled statement the Hack Pack's hook address lands on and
 * answers "leave the game alone" (0) unless the player turned the matching option on. The options
 * are plain offline options (Game tab), carried to the game in option word 3 (host API 17,
 * MU_GAME_OPTION3_CPU_* in mu_host.h): never online, never in a TM-CE event, and in replay
 * playback only what the replay was recorded with. run-source/rel09-hackpack/ledger_M5.md lists
 * every mod, its address and the statement it maps to. */
#ifndef MU_20XX_CPU_H
#define MU_20XX_CPU_H

struct Fighter;
struct HSD_GObj;

/* Option word 3; values match MU_GAME_OPTION3_CPU_* in mu_host.h (asserted in shim/mu_entry.c). */
#define MU_CPU_TECH_MASK 0x00000007u    /* 0 off, 1 in place, 2 roll forward, 3 roll back, 4 miss, 5 random */
#define MU_CPU_TECH_SHIFT 0
#define MU_CPU_GETUP_MASK 0x00000038u   /* 0 off, 1 stand, 2 roll forward, 3 roll back, 4 attack, 5 random */
#define MU_CPU_GETUP_SHIFT 3
#define MU_CPU_DI_MASK 0x000000C0u      /* 0 off, 1 none, 2 random, 3 survival */
#define MU_CPU_DI_SHIFT 6
#define MU_CPU_SDI_MASK 0x00000700u     /* 0 off, 1 none, 2 random, 3 with the hit, 4 against it, 5 up, 6 down */
#define MU_CPU_SDI_SHIFT 8
#define MU_CPU_NO_TAUNT 0x00000800u
#define MU_CPU_LCANCEL 0x00001000u
#define MU_CPU_NO_RAPID_JAB 0x00002000u
#define MU_CPU_NO_TRANSFORM 0x00004000u
#define MU_CPU_ALL 0x00007FFFu

unsigned int mu_game_options3(void);      /* shim/mu_entry.c: the raw word from the host */
unsigned int mu_options3_offline(void);   /* shim/mu_te.c: the word when it is in effect, else 0 */

/* "20XX Teching Options", 80098948 (ftCo_80098928, after the tech test): 0 the game decides,
 * 1 tech roll forward, 2 tech roll back, 3 no roll. */
int mu_20xx_cpu_tech_roll(struct Fighter* fp);
/* "20XX Teching Options", 80098754 (ftCo_8009872C, after the hammer test): 0 the game decides,
 * 1 tech in place, 2 miss the tech. */
int mu_20xx_cpu_tech_place(struct Fighter* fp);
/* "20XX Teching Options", 80097DE0 (ftCo_DownBound_Anim, the bounce animation is over): 0 the game
 * decides, 1 handled (return), 2 lie down (enter DownWait). */
int mu_20xx_cpu_getup_bound(struct HSD_GObj* gobj);
/* "20XX Teching Options", 8009803C (ftCo_DownWait_IASA, first statement): 0 the game decides,
 * 1 handled (return). */
int mu_20xx_cpu_getup_wait(struct HSD_GObj* gobj);
/* "20XX Random DI Options", 8008E5CC (ftCo_8008E5A4, before the stick is read): may rewrite the
 * fighter's stick for the DI calculation. */
void mu_20xx_cpu_di(struct Fighter* fp);
/* "20XX SDI Options", 8008E510 (ftCo_Damage_OnEveryHitlag, after the allow_sdi test): may replace
 * the stick the smash DI test and shift use. */
void mu_20xx_cpu_sdi(struct Fighter* fp, float* stick_x, float* stick_y);
/* "L-Cancel Options", 8008D698 (ftCo_LandingAir_EnterWithLag, the L-cancel test): nonzero, the
 * landing is L-cancelled. */
int mu_20xx_cpu_lcancel(struct Fighter* fp);
/* "CPU - Disable Taunting", 800DE9D8 (ftCo_800DE9D8, entry): nonzero, no taunt. */
int mu_20xx_cpu_no_taunt(struct Fighter* fp);
/* "CPU - Disable Rapid Jabs for C. Falcon", 800D6B8C (fn_800D6B8C, entry): nonzero, return. */
int mu_20xx_cpu_no_rapid_jab(struct Fighter* fp);
/* "Sheik/Zelda CPU Disable Transformations", 800D68D8 (ftCo_800D68C0, entry) and 80096728
 * (ftCo_SpecialAir_CheckInput, before the down special call): nonzero, no down special. */
int mu_20xx_cpu_no_transform(struct Fighter* fp);

#endif
