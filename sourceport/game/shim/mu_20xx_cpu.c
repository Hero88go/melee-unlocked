/* 20XX Hack Pack training options for CPUs, rewritten as native C (milestone 5 of
 * run-source/rel09-hackpack/PLAN.md; one row per mod in run-source/rel09-hackpack/ledger_M5.md).
 *
 * The Hack Pack hooks a handful of fighter functions and, for a fighter in a slot that is not a
 * human's, replaces the decision the game was about to make: tech or not and which way, what to do
 * from the floor, which way to hold the stick for DI and smash DI, whether an aerial landing is
 * L-cancelled, whether a taunt starts. Each function here is one of those hooks written by hand
 * against the native Fighter struct, called from the same statement, with the same tests in the
 * same order and the game's own random number calls where the original made them.
 *
 * The original reads its settings from its debug menu (percentages the player types in). Here each
 * option is a small choice in the Game tab and each choice sets those same values (the hp_*
 * variables, console address in the comment), so the code below is the original's and only the
 * numbers come from the option.
 *
 * Off by default. Offline only: never in an online match, never in a TM-CE event; in replay
 * playback the host passes the word the replay was recorded with, so playback follows the
 * recording. With every option off no function here changes anything or calls the random number
 * generator.
 *
 * State added: one byte per fighter, the frames a floored CPU still waits, kept where the original
 * keeps it: the top byte of the fighter object's gxlink_prios (console address object+0x20), which
 * only camera objects use. It is game memory, so snapshots and savestates carry it, and it starts
 * at zero with every new fighter object. */
#include <dolphin/os.h>
#include <melee/ft/fighter.h>
#include <melee/ft/forward.h>
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftCommon/ftCo_Down.h>
#include <melee/ft/kinds/ftCommon/ftCo_DownAttack.h>
#include <melee/ft/kinds/ftCommon/ftCo_DownStand.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/random.h>

#include "math_native.h"
#include "mu_20xx_cpu.h"
#include "mu_native.h"

int mu_online_active(void);
int mu_online_pending(void);
int mu_replay_abi_active(void);
char* getenv(const char* name);
unsigned long strtoul(const char* text, char** end, int base);
float sinf(float x);
float cosf(float x);

/* The Hack Pack's debug menu values these hooks read. set_values() fills them from the options. */
static int hp_di_mode;        /* 8040A19C: 0 the CPU's own DI, 1 none, 2 random, 3 survival */
static int hp_di_top_chance;  /* 8040AE98: launched straight up, percent that hold left or right */
static int hp_di_top_split;   /* 8040AE9C: of those, percent that hold toward the facing side */
static float hp_di_top_mag;   /* 8040AEA0: stick deflection for that hold */
static int hp_tech_on;        /* 8040AE28 */
static int hp_tech_forward;   /* 8040AE2C: percent, tech roll forward */
static int hp_tech_back;      /* 8040AE30: percent of the rest, tech roll back */
static int hp_tech_place;     /* 8040AE34: percent of the rest, tech in place; the rest miss */
static int hp_getup_on;       /* 8040AE38 */
static int hp_getup_act;      /* 8040AE3C: percent that act the moment the bounce ends */
static int hp_getup_wait;     /* 8040AE40: otherwise wait up to this many frames on the floor */
static int hp_getup_stand;    /* 8040AE44: percent, stand up */
static int hp_getup_forward;  /* 8040AE48: percent of the rest, roll forward */
static int hp_getup_back;     /* 8040AE4C: percent of the rest, roll back */
static int hp_getup_attack;   /* 8040AE50: percent of the rest, getup attack */
static int hp_sdi_mode;       /* 8040AE8C: 0 the CPU's own, 1 none, 2 random, 3 with the hit, 4 against, 5 up, 6 down */
static int hp_sdi_chance;     /* 8040AE90: percent of hitlag frames that smash DI */

/* The word in effect. MELEE_TEST_CPU_TRAIN=<hex> forces it for a hidden test run (same limits as
 * the option: not online, not in replay playback). */
static unsigned int cpu_word(void)
{
    static int forced = -1;
    static unsigned int forced_word;
    static unsigned int reported = 0xFFFFFFFFu;
    unsigned int word;
    if (forced < 0) {
        const char* v = getenv("MELEE_TEST_CPU_TRAIN");
        forced_word = v != NULL ? (unsigned int) strtoul(v, NULL, 16) & MU_CPU_ALL : 0;
        forced = forced_word != 0;
    }
    if (forced) {
        word = (mu_online_active() || mu_online_pending() || mu_replay_abi_active()) ? 0 : forced_word;
    } else {
        word = mu_options3_offline() & MU_CPU_ALL;
    }
    if (word != 0 && word != reported) {
        reported = word;
        OSReport("[20xx] cpu training options %08X in effect\n", word);
    }
    return word;
}

static int cpu_field(unsigned int mask, int shift)
{
    return (int) ((cpu_word() & mask) >> shift);
}

/* One log line the first time each decision is taken, so a log shows what a session used.
 * MELEE_CPU_TRAIN_LOG=1 (tests) logs every decision with the fighter's slot. Display only. */
enum {
    EV_TECH_FORWARD, EV_TECH_BACK, EV_TECH_PLACE, EV_TECH_MISS,
    EV_BOUND_STAND, EV_BOUND_FORWARD, EV_BOUND_BACK, EV_BOUND_ATTACK, EV_BOUND_WAIT,
    EV_WAIT_STAND, EV_WAIT_FORWARD, EV_WAIT_BACK, EV_WAIT_ATTACK,
    EV_DI_NONE, EV_DI_ANGLE, EV_DI_TOP, EV_SDI_NONE, EV_SDI,
    EV_LCANCEL, EV_NO_TAUNT, EV_NO_RAPID_JAB, EV_NO_TRANSFORM, EV_COUNT
};

static void cpu_note(int event, const Fighter* fp, int value)
{
    static const char* const names[EV_COUNT] = {
        "tech roll forward", "tech roll back", "tech in place", "tech missed",
        "bounce: stand", "bounce: roll forward", "bounce: roll back", "bounce: getup attack", "bounce: wait",
        "floor: stand", "floor: roll forward", "floor: roll back", "floor: getup attack",
        "DI none", "DI angle", "DI straight up hold", "SDI none", "SDI",
        "L-cancel", "taunt refused", "rapid jab refused", "down special refused",
    };
    static unsigned int seen;
    static int verbose = -1;
    if (verbose < 0) {
        verbose = getenv("MELEE_CPU_TRAIN_LOG") != NULL;
    }
    if (verbose || !(seen & (1u << event))) {
        seen |= 1u << event;
        OSReport("[20xx] cpu %d: %s (%d)\n", (int) fp->player_idx, (char*) names[event], value);
    }
}

/* Each choice as the Hack Pack's menu values. The percentages are tested one after another, each
 * with its own random number, exactly as the original does, so "random" uses 25 / 33 / 50 / 100 to
 * land near one in four each. */
static void set_tech(int mode)
{
    static const int table[6][3] = {
        {0, 0, 0}, {0, 0, 100}, {100, 0, 0}, {0, 100, 0}, {0, 0, 0}, {25, 33, 50},
    };
    if (mode > 5) {
        mode = 0;
    }
    hp_tech_on = mode != 0;
    hp_tech_forward = table[mode][0];
    hp_tech_back = table[mode][1];
    hp_tech_place = table[mode][2];
}

static void set_getup(int mode)
{
    static const int table[6][6] = {
        {0, 0, 0, 0, 0, 0},      {100, 0, 100, 0, 0, 0}, {100, 0, 0, 100, 0, 0},
        {100, 0, 0, 0, 100, 0},  {100, 0, 0, 0, 0, 100}, {50, 30, 25, 33, 50, 100},
    };
    if (mode > 5) {
        mode = 0;
    }
    hp_getup_on = mode != 0;
    hp_getup_act = table[mode][0];
    hp_getup_wait = table[mode][1];
    hp_getup_stand = table[mode][2];
    hp_getup_forward = table[mode][3];
    hp_getup_back = table[mode][4];
    hp_getup_attack = table[mode][5];
}

static void set_di(int mode)
{
    hp_di_mode = mode;
    hp_di_top_chance = 50;
    hp_di_top_split = 50;
    hp_di_top_mag = 1.0f;
}

static void set_sdi(int mode)
{
    hp_sdi_mode = mode > 6 ? 0 : mode;
    hp_sdi_chance = 50;
}

/* The slot is not a human's (the original reads the slot type at 80453088 + slot * 0xE90). */
static int cpu_slot(const Fighter* fp)
{
    return Player_GetPlayerSlotType(fp->player_idx) != Gm_PKind_Human;
}

static int roll100(void)
{
    return HSD_Randi(100) + 1;
}

static float deg_cos(int degrees)
{
    return cosf(0.017453292f * (float) degrees) * 1.0f;
}

static float deg_sin(int degrees)
{
    return sinf(0.017453292f * (float) degrees) * 1.0f;
}

/* -------------------------------------------------------------------------------------------
 * "20XX Teching Options", 80098948: ftCo_80098928, the tech roll check on landing in tumble.
 * ------------------------------------------------------------------------------------------- */
int mu_20xx_cpu_tech_roll(Fighter* fp)
{
    const int mode = cpu_field(MU_CPU_TECH_MASK, MU_CPU_TECH_SHIFT);
    if (mode == 0) {
        return 0;
    }
    set_tech(mode);
    if (fp->kind == Ft_Kind_Sandbag || fp->kind == Ft_Kind_Nana) {
        return 0;
    }
    if (!hp_tech_on || !cpu_slot(fp)) {
        return 0;
    }
    if (roll100() <= hp_tech_forward) {
        cpu_note(EV_TECH_FORWARD, fp, mode);
        return 1;   /* 80098998: ftCo_MS_PassiveStandF */
    }
    if (roll100() <= hp_tech_back) {
        cpu_note(EV_TECH_BACK, fp, mode);
        return 2;   /* 800989A0: ftCo_MS_PassiveStandB */
    }
    return 3;       /* 800989B8: no roll */
}

/* "20XX Teching Options", 80098754: ftCo_8009872C, the tech in place check that follows. */
int mu_20xx_cpu_tech_place(Fighter* fp)
{
    const int mode = cpu_field(MU_CPU_TECH_MASK, MU_CPU_TECH_SHIFT);
    if (mode == 0) {
        return 0;
    }
    set_tech(mode);
    if (fp->kind == Ft_Kind_Sandbag || fp->kind == Ft_Kind_Nana) {
        return 0;
    }
    if (!hp_tech_on || !cpu_slot(fp)) {
        return 0;
    }
    if (roll100() <= hp_tech_place) {
        cpu_note(EV_TECH_PLACE, fp, mode);
        return 1;   /* 800987A4: tech in place */
    }
    cpu_note(EV_TECH_MISS, fp, mode);
    return 2;       /* 800987B4: missed */
}

/* The floor wait, the top byte of the fighter object's gxlink_prios (see the file comment). */
static int wait_get(const HSD_GObj* gobj)
{
    return (int) (gobj->gxlink_prios >> 56) & 0xFF;
}

static void wait_set(HSD_GObj* gobj, int frames)
{
    gobj->gxlink_prios = (gobj->gxlink_prios & 0x00FFFFFFFFFFFFFFull) | ((u64) (frames & 0xFF) << 56);
}

/* "20XX Teching Options", 80097DE0: ftCo_DownBound_Anim once the bounce animation is over. */
int mu_20xx_cpu_getup_bound(HSD_GObj* gobj)
{
    Fighter* fp = gobj->user_data;
    const int mode = cpu_field(MU_CPU_GETUP_MASK, MU_CPU_GETUP_SHIFT);
    if (mode == 0) {
        return 0;
    }
    set_getup(mode);
    if (fp->kind == Ft_Kind_Nana || fp->kind == Ft_Kind_Sandbag) {
        return 0;
    }
    if (!hp_getup_on || !cpu_slot(fp)) {
        return 0;
    }
    if (roll100() <= hp_getup_act) {
        if (roll100() <= hp_getup_stand) {
            /* The stand check looks for the lying state, not the bounce. */
            if (fp->motion_id == ftCo_MS_DownBoundU) {
                fp->motion_id = ftCo_MS_DownWaitU;
            }
            fp->input.pressed_buttons = 0x80000000u;   /* HSD_PAD_LR */
            cpu_note(EV_BOUND_STAND, fp, ftCo_800980BC(gobj));
            return 1;
        }
        if (roll100() <= hp_getup_forward) {
            fp->input.lstick[0].x = fp->facing_dir;
            cpu_note(EV_BOUND_FORWARD, fp, ftCo_Down_CheckInput(gobj));
            return 1;
        }
        if (roll100() <= hp_getup_back) {
            fp->input.lstick[0].x = -fp->facing_dir;
            cpu_note(EV_BOUND_BACK, fp, ftCo_Down_CheckInput(gobj));
            return 1;
        }
        if (roll100() <= hp_getup_attack) {
            fp->x67D = 0;
            cpu_note(EV_BOUND_ATTACK, fp, ftCo_80098400(gobj));
            return 1;
        }
    }
    wait_set(gobj, HSD_Randi(hp_getup_wait));
    cpu_note(EV_BOUND_WAIT, fp, wait_get(gobj));
    return 2;   /* 80097E00: ftCo_80097E8C, lie down */
}

/* "20XX Teching Options", 8009803C: ftCo_DownWait_IASA, every frame on the floor. */
int mu_20xx_cpu_getup_wait(HSD_GObj* gobj)
{
    Fighter* fp = gobj->user_data;
    const int mode = cpu_field(MU_CPU_GETUP_MASK, MU_CPU_GETUP_SHIFT);
    int wait;
    if (mode == 0) {
        return 0;
    }
    set_getup(mode);
    if (fp->kind == Ft_Kind_Nana || fp->kind == Ft_Kind_Sandbag) {
        return 0;
    }
    if (!hp_getup_on || !cpu_slot(fp)) {
        return 0;
    }
    wait = wait_get(gobj);
    if (wait > 0) {
        wait_set(gobj, wait - 1);
        return 1;
    }
    if (roll100() <= hp_getup_stand) {
        fp->input.pressed_buttons = 0x80000000u;   /* HSD_PAD_LR */
        cpu_note(EV_WAIT_STAND, fp, ftCo_800980BC(gobj));
        return 1;
    }
    if (roll100() <= hp_getup_forward) {
        fp->input.lstick[0].x = fp->facing_dir;
        cpu_note(EV_WAIT_FORWARD, fp, ftCo_Down_CheckInput(gobj));
        return 1;
    }
    if (roll100() <= hp_getup_back) {
        fp->input.lstick[0].x = -fp->facing_dir;
        cpu_note(EV_WAIT_BACK, fp, ftCo_Down_CheckInput(gobj));
        return 1;
    }
    if (roll100() <= hp_getup_attack) {
        fp->input.pressed_buttons = 0x200;   /* HSD_PAD_B */
        cpu_note(EV_WAIT_ATTACK, fp, ftCo_800984D4(gobj));
        return 1;
    }
    return 1;   /* nothing this frame; the original also skips the game's own checks */
}

/* -------------------------------------------------------------------------------------------
 * "20XX Random DI Options", 8008E5CC: ftCo_8008E5A4, the DI calculation at the end of hitlag
 * and at a throw's release.
 * ------------------------------------------------------------------------------------------- */
void mu_20xx_cpu_di(Fighter* fp)
{
    const int mode = cpu_field(MU_CPU_DI_MASK, MU_CPU_DI_SHIFT);
    int angle;
    if (mode == 0) {
        return;
    }
    set_di(mode);
    if (fp->kind == Ft_Kind_Nana || fp->kind == Ft_Kind_Sandbag) {
        return;
    }
    if (!cpu_slot(fp) || fp->cpu.kind == 5) {
        return;
    }
    if (hp_di_mode == 0) {
        return;
    }
    if (hp_di_mode == 2) {
        angle = HSD_Randi(360);
    } else if (hp_di_mode == 3) {
        /* Survival: the stick at a right angle to the launch, on the upper side. The launch angle
         * is the attack's own (361 is the default angle, taken as 45) and the hit's direction says
         * which way it is mirrored. */
        u32 dir_bits;
        int from_left;   /* the original tests the float's top byte for 0xBF: direction is -1 */
        const int seed_byte = (int) (*HSD_RandSeedPtr >> 8) & 0xFF;   /* console byte 804D5F92 */
        int kb = fp->dmg.x1848_kb_angle;
        __builtin_memcpy(&dir_bits, &fp->dmg.facing_dir_1, sizeof dir_bits);
        from_left = (dir_bits >> 24) == 0xBF;
        if (kb == 361) {
            kb = 45;
        }
        if (kb == 0 || kb == 180) {
            angle = 90;
        } else if (kb == 90 || kb == 270) {
            angle = seed_byte < 0x80 ? 0 : 180;
        } else if (kb < 90) {
            angle = from_left ? 90 + kb : 90 - kb;
        } else if (kb < 270) {
            angle = from_left ? kb - 90 : 270 - kb;
        } else {
            angle = from_left ? 90 + kb : 450 - kb;
        }
    } else {
        fp->input.lstick[0].x = 0.0f;
        fp->input.lstick[0].y = 0.0f;
        cpu_note(EV_DI_NONE, fp, mode);
        return;
    }
    if (fp->motion_id == ftCo_MS_DamageFlyTop) {
        /* Launched straight up: some of the time hold fully left or right instead. */
        if (roll100() <= hp_di_top_chance) {
            const int roll = roll100();
            float x = fp->facing_dir * hp_di_top_mag;
            if (!(hp_di_top_split > roll)) {
                x = -x;
            }
            fp->input.lstick[0].x = x;
            fp->input.lstick[0].y = 0.0f;
            cpu_note(EV_DI_TOP, fp, x < 0.0f ? -1 : 1);
            return;
        }
    }
    fp->input.lstick[0].x = deg_cos(angle);
    fp->input.lstick[0].y = deg_sin(angle);
    cpu_note(EV_DI_ANGLE, fp, angle);
}

/* -------------------------------------------------------------------------------------------
 * "20XX SDI Options", 8008E510: ftCo_Damage_OnEveryHitlag, each frame of hitlag that allows
 * smash DI. The original replaces the two stick values the test and the shift use (registers),
 * not the fighter's stick, and clears the stick's "fresh" timer so the test passes.
 * ------------------------------------------------------------------------------------------- */
void mu_20xx_cpu_sdi(Fighter* fp, float* stick_x, float* stick_y)
{
    const int mode = cpu_field(MU_CPU_SDI_MASK, MU_CPU_SDI_SHIFT);
    if (mode == 0) {
        return;
    }
    set_sdi(mode);
    if (!cpu_slot(fp) || fp->cpu.kind == 5) {
        return;
    }
    if (hp_sdi_mode == 0) {
        return;
    }
    if (hp_sdi_mode == 1 || roll100() > hp_sdi_chance) {
        *stick_x = 0.0f;
        *stick_y = 0.0f;
        cpu_note(EV_SDI_NONE, fp, mode);
        return;
    }
    fp->active_timer.lstick.x = 0;
    cpu_note(EV_SDI, fp, mode);
    switch (hp_sdi_mode) {
    case 2: {
        const int angle = HSD_Randi(360);
        *stick_x = deg_cos(angle);
        *stick_y = deg_sin(angle);
        break;
    }
    case 3:
        *stick_x = fp->dmg.facing_dir_1;
        *stick_y = 0.0f;
        break;
    case 4:
        *stick_x = -fp->dmg.facing_dir_1;
        *stick_y = 0.0f;
        break;
    case 5:
        *stick_x = 0.0f;
        *stick_y = 1.0f;
        break;
    default:
        *stick_x = 0.0f;
        *stick_y = -1.0f;
        break;
    }
}

/* "L-Cancel Options", 8008D698 (the CPU part: the press counts as made on the first frame). The
 * mod's flash and its counters for the results screen are separate features (see the ledger). */
int mu_20xx_cpu_lcancel(Fighter* fp)
{
    if (!(cpu_word() & MU_CPU_LCANCEL)) {
        return 0;
    }
    if (cpu_slot(fp) && fp->cpu.kind != 5) {
        cpu_note(EV_LCANCEL, fp, fp->x67F);
        return 1;
    }
    return 0;
}

/* "CPU - Disable Taunting", 800DE9D8. */
int mu_20xx_cpu_no_taunt(Fighter* fp)
{
    if (!(cpu_word() & MU_CPU_NO_TAUNT)) {
        return 0;
    }
    if (cpu_slot(fp) && fp->cpu.kind != 5) {
        if (fp->input.pressed_buttons & HSD_PAD_DPADUP) {
            cpu_note(EV_NO_TAUNT, fp, 0);
        }
        return 1;
    }
    return 0;
}

/* "CPU - Disable Rapid Jabs for C. Falcon", 800D6B8C. */
int mu_20xx_cpu_no_rapid_jab(Fighter* fp)
{
    if (!(cpu_word() & MU_CPU_NO_RAPID_JAB)) {
        return 0;
    }
    if (fp->cpu.kind != 5 && Player_GetPlayerSlotType(fp->player_idx) == Gm_PKind_Cpu &&
        fp->kind == Ft_Kind_Captain)
    {
        cpu_note(EV_NO_RAPID_JAB, fp, 0);
        return 1;
    }
    return 0;
}

/* "Sheik/Zelda CPU Disable Transformations", 800D68D8 and 80096728. */
int mu_20xx_cpu_no_transform(Fighter* fp)
{
    if (!(cpu_word() & MU_CPU_NO_TRANSFORM)) {
        return 0;
    }
    if (cpu_slot(fp) && fp->cpu.kind != 5 && (fp->kind == Ft_Kind_Zelda || fp->kind == Ft_Kind_Seak)) {
        cpu_note(EV_NO_TRANSFORM, fp, (int) fp->kind);
        return 1;
    }
    return 0;
}
