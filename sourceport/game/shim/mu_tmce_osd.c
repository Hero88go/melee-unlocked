/* Training Mode CE, native build: the on-screen displays and the per-fighter data they read.
 *
 * On the console these are PowerPC patches (ASM/training-mode/Onscreen Display, .../Misc and
 * ASM/m-ex/Custom Playerdata Variables). Natively each one is the C below, called from the decomp
 * statement its address lands on (mu_tmce_osd.h lists the sites; run-source/tmce-port/ledger_T2.md
 * maps every patch file). Hooks run only while TM-CE is active (never online, never in a replay
 * recorded without it), so vanilla play never reaches this file.
 *
 * The console patches keep their data at the end of the fighter (0x23F8, TM_* in m-ex/Header.s):
 * natively that is fp->mu_tm. Messages go through TM-CE's Message_Display (tmce/native/tmce_osd.c):
 * the text is formatted here and handed over whole.
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include <mu_native.h>
#include <mu_tmce_osd.h>

#include <melee/ft/fighter.h>
#include <melee/ft/forward.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/inlines.h>
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftYoshi/types.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/types.h>
#include <melee/it/inlines.h>
#include <melee/it/types.h>
#include <melee/lb/lbanim.h>
#include <melee/pl/pl_040D.h>
#include <melee/pl/player.h>
#include <melee/pl/types.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/random.h>

/* From the host's C runtime (see mu_shim.h): the game's own library has no vsnprintf. */
int vsnprintf(char* buffer, __SIZE_TYPE__ size, const char* format, __builtin_va_list args);

/* ---- the MexTK side (tmce/native/tmce_osd.c) ---- */
void* mu_tmce_osd_show(int kind, int queue, int color, const char* text);
void mu_tmce_osd_msg_color(void* msg_gobj, int subtext, unsigned rgba);
int mu_tmce_osd_rng(int setting);
u16* mu_tmce_osd_lc_rate(int ply);
u16* mu_tmce_osd_db_rate(int ply);
void mu_tmce_osd_legacy_combo_end(int ply);

/* OSD ids (TM-CE src/osds.h, Globals.s OSD.*) */
enum {
    OSD_Wavedash = 0,
    OSD_LCancel = 1,
    OSD_ActOoS = 3,
    OSD_Dashback = 5,
    OSD_FighterSpecificTech = 8,
    OSD_Powershield = 9,
    OSD_SDI = 10,
    OSD_LockoutTimers = 12,
    OSD_RollAirdodgeInterrupt = 13,
    OSD_BoostGrab = 14,
    OSD_Miscellaneous = 15,
    OSD_ActOoWait = 16,
    OSD_ActOoAirborne = 18,
    OSD_ActOoJumpSquat = 19,
    OSD_Fastfall = 20,
    OSD_ComboCounter = 22,
    OSD_GrabBreakout = 24,
    OSD_Ledge = 26,
    OSD_ActOoHitstun = 28,
    OSD_Null = -1,
    OSD_FighterSpecificTechAlt = 64,
};

/* message colors (TM-CE MSGCOLOR_*) and the console's color words */
enum { MSG_WHITE, MSG_GREEN, MSG_RED, MSG_YELLOW };
#define RGBA_GREEN 0x8dff6effu
#define RGBA_RED 0xffa2baffu
#define RGBA_YELLOW 0xfff000ffu
#define RGBA_WHITE 0xffffffffu

/* the motion states the patches name (Globals.s ASID_*) */
enum {
    ASID_RebirthWait = 0x0D,
    ASID_Wait = 0x0E,
    ASID_WalkSlow = 0x0F,
    ASID_WalkMiddle = 0x10,
    ASID_WalkFast = 0x11,
    ASID_Turn = 0x12,
    ASID_Dash = 0x14,
    ASID_KneeBend = 0x18,
    ASID_JumpF = 0x19,
    ASID_JumpB = 0x1A,
    ASID_JumpAerialF = 0x1B,
    ASID_JumpAerialB = 0x1C,
    ASID_Fall = 0x1D,
    ASID_Squat = 0x27,
    ASID_SquatWait = 0x28,
    ASID_Landing = 0x2A,
    ASID_LandingFallSpecial = 0x2B,
    ASID_AttackLw3 = 0x39,
    ASID_AttackLw4 = 0x40,
    ASID_AttackAirN = 0x41,
    ASID_AttackAirLw = 0x45,
    ASID_LandingAirN = 0x46,
    ASID_LandingAirLw = 0x4A,
    ASID_GuardOn = 0xB2,
    ASID_GuardOff = 0xB4,
    ASID_GuardSetOff = 0xB5,
    ASID_DownBoundU = 0xB7,
    ASID_PassiveStandB = 0xC9,
    ASID_ThrowF = 0xDB,
    ASID_ThrowLw = 0xDE,
    ASID_EscapeAir = 0xEC,
    ASID_Pass = 0xF4,
    ASID_MissFoot = 0xFB,
    ASID_CliffWait = 0xFD,
    ASID_BarrelCannonWait = 0x154,
};

/* internal fighter ids (Globals.s *.Int) */
enum {
    KIND_Fox = 0x01,
    KIND_Bowser = 0x05,
    KIND_Sheik = 0x07,
    KIND_Peach = 0x09,
    KIND_Nana = 0x0B,
    KIND_Yoshi = 0x0E,
    KIND_Falco = 0x16,
};

/* ---- helpers ---- */

static int osd_on(int id)
{
    return (mu_tmce_osd_enabled() >> id) & 1;
}

static void* osd_msg(int kind, int queue, int color, const char* fmt, ...)
{
    char text[128];
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    vsnprintf(text, sizeof text, fmt, args);
    __builtin_va_end(args);
    return mu_tmce_osd_show(kind, queue, color, text);
}

/* player.c's character table: whether a character's second fighter is a transformation */
extern struct {
    s8 internal_id;
    s8 extra_internal_id;
    s8 has_transformation;
} ftMapping_list[];

/* Check For ICs (0x80005510): a sub-fighter that is not a transformation, i.e. Nana */
static bool is_follower(Fighter* fp)
{
    if (!fp->is_sub_fighter) {
        return false;
    }
    return ftMapping_list[Player_GetPlayerCharacter(fp->player_idx)].has_transformation == 0;
}

/* the console's lhz of TM_PostHitstunFrameCount: the counter is a halfword stored in a word */
static int post_hitstun(Fighter* fp)
{
    return (u16) fp->mu_tm.post_hitstun_frames;
}

/* TM_PrevASStart + i*2 for i in 0..11: the six previous states, then their frame counts (the
 * console reads one past the states this way) */
static int tm_prev_half(Fighter* fp, int i)
{
    return i < 6 ? fp->mu_tm.state_prev[i] : fp->mu_tm.state_prev_frames[i - 6];
}

static u32 float_bits(float f)
{
    union {
        float f;
        u32 u;
    } v;
    v.f = f;
    return v.u;
}

static int success_rate(u16* rate)
{
    float r = (float) rate[0] / (float) rate[1];
    r = r * 100.0f;
    return (int) r;
}

/* ---- Additional Playerblock Variables ---- */

/* StatesAndTimers/ShiftStates (Fighter_ChangeMotionState+0x40, before the new motion is stored):
 * push the state being left and its frame count onto the history */
void mu_tmce_osd_change_motion(Fighter* fp)
{
    int i;
    for (i = 4; i >= 0; i--) {
        fp->mu_tm.state_prev[i + 1] = fp->mu_tm.state_prev[i];
    }
    fp->mu_tm.state_prev[0] = (u16) fp->motion_id;
    for (i = 4; i >= 0; i--) {
        fp->mu_tm.state_prev_frames[i + 1] = fp->mu_tm.state_prev_frames[i];
    }
    fp->mu_tm.state_prev_frames[0] = (u16) fp->mu_tm.state_frame;
    fp->mu_tm.state_frame = 0;
}

/* Fastfall Timer/Initialize Fastfall Variable (Fighter_ChangeMotionState+0x51c) */
void mu_tmce_osd_fastfall_reset(Fighter* fp)
{
    fp->mu_tm.can_fastfall_frames = 0;
}

/* StatesAndTimers/IncFrameCounts (Fighter_procAnim+0x818): outside hitlag, count frames in the
 * state and frames vulnerable since ledge intangibility, and record this frame's pad */
void mu_tmce_osd_anim_counts(Fighter* fp)
{
    int i;
    if (fp->x2219_b5) {
        return;
    }
    fp->mu_tm.state_frame++;
    if (fp->x1990 > 0 || (fp->mu_tm.vuln_frames == 0 && fp->x1988 == 2)) {
        fp->mu_tm.vuln_frames = 0;
    } else {
        fp->mu_tm.vuln_frames++;
    }
    for (i = 31; i > 0; i--) {
        fp->mu_tm.inputs[i] = fp->mu_tm.inputs[i - 1];
    }
    if (fp->pad_port < 4) {
        HSD_PadStatus* pad = &HSD_PadGameStatus[fp->pad_port];
        fp->mu_tm.inputs[0].buttons = (u16) pad->button;
        fp->mu_tm.inputs[0].stick_x = pad->stickX;
        fp->mu_tm.inputs[0].stick_y = pad->stickY;
        fp->mu_tm.inputs[0].cstick_x = pad->subStickX;
        fp->mu_tm.inputs[0].cstick_y = pad->subStickY;
        fp->mu_tm.inputs[0].ltrigger = pad->analogL;
        fp->mu_tm.inputs[0].rtrigger = pad->analogR;
    } else {
        __builtin_memset(&fp->mu_tm.inputs[0], 0, sizeof fp->mu_tm.inputs[0]);
    }
}

/* Act OOTumble/Increment Post Hitstun Count (Fighter_procAnim+0x800) */
void mu_tmce_osd_post_hitstun(Fighter* fp)
{
    if (fp->x221C_b6) {
        fp->mu_tm.post_hitstun_frames = 0;
    } else {
        fp->mu_tm.post_hitstun_frames = (u16) (fp->mu_tm.post_hitstun_frames + 1);
    }
}

/* Fastfall Timer/Increment Fastfall Variable (ftCommon_CheckFallFast+0x28: not fastfalling yet
 * and moving down) */
int mu_tmce_osd_ff_tick(Fighter* fp)
{
    fp->mu_tm.can_fastfall_frames++;
    return 1;
}

/* Frame Advantage/Save Pointer to Shield Owner On Hit (ftColl_80076CBC+0x1f0) */
void mu_tmce_osd_shield_hit(Fighter* attacker, Fighter* defender)
{
    attacker->mu_tm.fighter_hurt_shield = defender;
}

/* Previous Move Instance Hit By/Backup ... (pl_80038144+0xc8). The console writes to +0x2418,
 * which in its playerblock layout is TM_CanFastfallFrameCount, not TM_PreviousMoveInstanceHitBy
 * (+0x2414); kept as it runs there, so last_move_hurt stays 0 as on the console. */
void mu_tmce_osd_hit_backup(Fighter* victim)
{
    victim->mu_tm.can_fastfall_frames = victim->dmg.x18ec_instancehitby;
}

/* Better Combo Counter/Throw Hitboxes Update Variable in Playerblock (ftCo_800DDDE4+0x84) */
void mu_tmce_osd_throw_source(Fighter* victim, HSD_GObj* thrower)
{
    victim->dmg.x1868_source = thrower;
}

/* Better Combo Counter/Disregard Projectile Hitboxes w Same Move ID (ftColl_80078998+0x48):
 * true skips the combo stat update */
int mu_tmce_osd_same_projectile(Item* ip, HSD_GObj* victim)
{
    return GET_FIGHTER(victim)->dmg.x18ec_instancehitby == ip->xDA8_short;
}

/* Better Combo Counter/Interrupting Move with Same Move Gives a Unique ID (ft_800895E0+0x30) */
int mu_tmce_osd_same_move_again(Fighter* fp)
{
    return fp->motion_id == fp->mu_tm.state_prev[0];
}

/* SDI Display/Initialize Total SDI Inputs Variable (ftCo_8008EC90+0xa8c): a hit by a new move
 * (or by nothing, or by a stage element) restarts the SDI count */
void mu_tmce_osd_sdi_init(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_GObj* source = fp->dmg.x1868_source;
    if (source != NULL) {
        int instance;
        if (source->classifier == HSD_GOBJ_CLASS_FIGHTER) {
            instance = GET_FIGHTER(source)->x2074.x2088;
        } else if (source->classifier == HSD_GOBJ_CLASS_ITEM) {
            instance = GET_ITEM(source)->xDA8_short;
        } else {
            return;
        }
        if (instance != 0 && fp->mu_tm.last_move_hurt == instance) {
            return;
        }
    }
    fp->mu_tm.tm_union.sdi.successful_sdi_inputs = 0;
    fp->mu_tm.tm_union.sdi.total_sdi_inputs = 0;
}

/* Wavedash Display/Save Airdodge Y Angle (ftCo_80099A9C+0xd8, still in the jump state): the
 * airdodge angle and the hop the jump was (0 full, 1 short, 2 not from a jump) */
void mu_tmce_osd_airdodge_angle(Fighter* fp)
{
    fp->mu_tm.tm_union.wavedash.airdodge_angle = ftCommon_8007D9D4(fp);
    if (fp->motion_id == ASID_JumpF || fp->motion_id == ASID_JumpB) {
        fp->mu_tm.tm_union.wavedash.hop_type = fp->mv.co.common.x0;
    } else {
        fp->mu_tm.tm_union.wavedash.hop_type = 2;
    }
}

/* ---- Combo Counter ---- */

/* the combo goes on while in hitstun, missfoot, landing lag, missed tech / getups, grabs,
 * throws and special throws */
static bool combo_continues(Fighter* fp)
{
    int s = fp->motion_id;
    if (fp->x221C_b6 || s == ASID_MissFoot) {
        return true;
    }
    if (s == ASID_Landing && fp->cur_anim_frame < fp->co_attrs.normal_landing_lag) {
        return true;
    }
    return (s >= 0xB7 && s <= 0xC9) || (s >= 0xDF && s <= 0xE8) || (s >= 0xEF && s <= 0xF3) ||
           (s >= 0x10A && s <= 0x130);
}

/* Combo Counter - On Combo Increment: HitstunMonitor, run each frame by the victim */
static void hitstun_monitor(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_GObj* source = fp->dmg.x1868_source;
    if (ftLib_80086960(source)) {
        if (combo_continues(fp)) {
            return;
        }
        mu_tmce_osd_legacy_combo_end(GET_FIGHTER(source)->player_idx);
    }
    fp->mu_tm.cb_anim = NULL;
}

/* Better Combo Counter/Custom Function Blrl + Keep Combo Counter Going For Missed Techs+Getups
 * (Fighter_procAnim+0x828). The game's own end check is skipped (Skip Original Combo End Check). */
void mu_tmce_osd_anim_end(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->mu_tm.cb_anim != NULL) {
        ((void (*)(HSD_GObj*)) fp->mu_tm.cb_anim)(gobj);
    }
    if (!combo_continues(fp)) {
        pl_800411C4(fp->player_idx, fp->is_sub_fighter);
    }
}

/* Better Combo Counter/Pummels and Multi-Hits Increment Total Damage But Not Combo Counter
 * (pl_80040ED4+0x30): the move id the combo count sees; 76 is one the game never counts */
int mu_tmce_osd_combo_move(int slot, int victim_slot, int move)
{
    HSD_GObj* attacker_gobj = Player_GetEntity(slot);
    HSD_GObj* victim_gobj = Player_GetEntity(victim_slot);
    Fighter* attacker;
    Fighter* victim;
    if (attacker_gobj == NULL || victim_gobj == NULL) {
        return move;
    }
    attacker = GET_FIGHTER(attacker_gobj);
    victim = GET_FIGHTER(victim_gobj);
    if (move == 0x34) {
        return 76;
    }
    if (victim->dmg.x18ec_instancehitby == attacker->x2074.x2088) {
        if (move < 0x35 || move > 0x38) {
            return 76;
        }
    } else if (move == 0x3F || move == 0x3E) {
        return move;
    }
    return attacker->x1A6A != 0 ? 76 : move;
}

/* the console passes r13 as the message kind here (`mr r3, 13`): a kind no other OSD uses */
#define COMBO_MSG_KIND ((int) 0x804DB6A0)

/* Combo Counter - On Combo Increment (pl_80040ED4+0xd0) */
void mu_tmce_osd_combo_increment(int slot, int victim_slot, int arg3)
{
    struct pl_x5EC_t* victim_stats = Player_GetUnk6A8Ptr(victim_slot);
    int count = (u16) victim_stats->x10[arg3 == 1 ? 0 : slot].x4;
    HSD_GObj* gobj;
    Fighter* fp;
    if (!osd_on(OSD_ComboCounter)) {
        return;
    }
    gobj = Player_GetEntity(slot);
    if (gobj == NULL) {
        return;
    }
    fp = GET_FIGHTER(gobj);
    if (is_follower(fp) || count < 2) {
        return;
    }
    /* the console asks pl_80041300 with r3 still 0 from the follower check: port 1's damage */
    osd_msg(COMBO_MSG_KIND, fp->player_idx, MSG_WHITE, "Combo Count\n%d Hits / %d%%", count, pl_80041300(0));
    if (ftLib_80086960(fp->x2094)) {
        GET_FIGHTER(fp->x2094)->mu_tm.cb_anim = (void*) hitstun_monitor;
    }
}

/* ---- Generic Per Frame (Fighter_procInput+0xae4) ---- */

/* Act OoWait/Act OOWait- Standalone (0x8000551c) */
static void act_oo_wait(Fighter* fp)
{
    const char* text;
    int frames;
    int before;
    int i;
    int j;
    if (is_follower(fp)) {
        return;
    }
    for (i = 0;; i++) {
        int s = fp->mu_tm.state_prev[i];
        if (s == ASID_Wait) {
            text = "Act OoWait\nFrame: %d";
            frames = 1;
            break;
        }
        if (s == ASID_Landing) {
            text = "Act OoAutocancel\nFrame: %d";
            frames = -3;
            break;
        }
        if (i + 1 >= 6) {
            return;
        }
    }
    before = tm_prev_half(fp, i + 1);
    for (j = i; j >= 0; j--) {
        int s = fp->mu_tm.state_prev[j];
        if (s == ASID_Turn) {
            frames--;
        }
        if (s == ASID_Wait) {
            frames--;
        }
        frames += fp->mu_tm.state_prev_frames[j];
    }
    if (frames > 13) {
        return;
    }
    if (!((before >= ASID_LandingAirN && before <= ASID_LandingAirLw) ||
          (before >= ASID_ThrowF && before <= ASID_ThrowLw) ||
          (before >= ASID_DownBoundU && before <= ASID_PassiveStandB) || before == ASID_LandingFallSpecial ||
          (before >= 341 && before <= 346 && (fp->kind == KIND_Fox || fp->kind == KIND_Falco)) ||
          (before >= 341 && before <= 348 && fp->kind == KIND_Sheik) ||
          (before >= 344 && before <= 348 && fp->kind == KIND_Peach) ||
          (before >= ASID_AttackAirN && before <= ASID_AttackAirLw)))
    {
        return;
    }
    mu_tmce_osd_msg_color(osd_msg(5, fp->player_idx, MSG_WHITE, text, frames), 1,
                          frames == 1 ? RGBA_GREEN : RGBA_RED);
}

/* CheckForJumpCancel: a tap jump in its window, or X/Y */
static bool jump_input(Fighter* fp)
{
    if (fp->input.lstick[0].y >= p_ftCommonData->tap_jump_threshold &&
        fp->active_timer.lstick.y < p_ftCommonData->tap_jump_window)
    {
        return true;
    }
    return (fp->input.pressed_buttons & (HSD_PAD_X | HSD_PAD_Y)) != 0;
}

/* frames the jump button (X/Y or stick up past `threshold`) was held, from the input history;
 * -1 when the press is older than the history */
static int jump_held_frames(Fighter* fp, int threshold)
{
    int i;
    for (i = 0; i < 32; i++) {
        if ((fp->mu_tm.inputs[i].buttons & 0xC00) || fp->mu_tm.inputs[i].stick_y > threshold) {
            return fp->x685 - i + 1;
        }
    }
    return -1;
}

static void fighter_specific_tech(Fighter* fp)
{
    int ply = fp->player_idx;
    int s = fp->motion_id;
    if (fp->kind == KIND_Fox || fp->kind == KIND_Falco) {
        switch (s) {
        case 0x15B:
        case 0x15E: /* side B start */
            if ((fp->input.pressed_buttons & HSD_PAD_B) && fp->x590 != NULL) {
                int early = (int) fp->x590->frames - fp->mu_tm.state_frame - 1;
                osd_msg(OSD_FighterSpecificTech, ply, MSG_RED, "Shorten Press\n%df Early", early);
            }
            return;
        case 0x15C:
        case 0x15F: /* side B */
            if (fp->input.pressed_buttons & HSD_PAD_B) {
                osd_msg(OSD_FighterSpecificTech, ply, MSG_GREEN, "Shorten Press\nFrame %d/4",
                        (int) fp->cur_anim_frame + 1);
            }
            return;
        case 0x15D:
        case 0x160: /* side B end */
            if (fp->input.pressed_buttons & HSD_PAD_B) {
                osd_msg(OSD_FighterSpecificTech, ply, MSG_RED, "Shorten Press\nLate");
            }
            return;
        case 0x16D: { /* air shine startup, right after a jump from the ground: JC shine */
            int held;
            void* msg;
            int full;
            int prev = fp->mu_tm.state_prev[0];
            if (prev != ASID_JumpF && prev != ASID_JumpB) {
                return;
            }
            held = jump_held_frames(fp, 44);
            if (held == -1) {
                return;
            }
            full = !((float) held < fp->co_attrs.jump_startup_time);
            msg = osd_msg(OSD_FighterSpecificTechAlt, ply, MSG_WHITE, "JC Shine\nFrame %d\n%s: %df",
                          fp->mu_tm.state_prev_frames[0], full ? "Full Hop" : "Short Hop", held);
            mu_tmce_osd_msg_color(msg, 1, fp->mu_tm.state_prev_frames[0] <= 2 ? RGBA_GREEN : RGBA_RED);
            mu_tmce_osd_msg_color(msg, 2, full ? RGBA_RED : RGBA_GREEN);
            return;
        }
        case 0x16E: /* air shine loop: only with a jump left */
            if (fp->x1968_jumpsUsed >= fp->co_attrs.max_jumps) {
                return;
            }
            /* fallthrough */
        case 0x169: /* ground shine loop */
            if (jump_input(fp)) {
                osd_msg(OSD_FighterSpecificTech, ply, fp->mu_tm.state_frame == 1 ? MSG_GREEN : MSG_RED,
                        "Act OoShine\nFrame %d", (u16) fp->mu_tm.state_frame);
            }
            return;
        }
        return;
    }
    if (fp->kind == KIND_Yoshi && s == 0x159 && jump_input(fp)) {
        osd_msg(OSD_FighterSpecificTech, ply, MSG_WHITE, "Jump OoParry\nFrame %d", (u16) fp->mu_tm.state_frame);
    }
}

/* Peach's instant double jump (DJL): frames since the previous jump press, per player */
static u8 djl_since[8];
static u8 djl_prev_timer[8];

static void peach_djl(Fighter* fp)
{
    int ply = fp->player_idx & 7;
    u8 timer = fp->x685;
    u8 since = djl_since[ply];
    u8 prev = djl_prev_timer[ply];
    djl_prev_timer[ply] = timer;
    /* the jump timer holds 0 while jump is held: a press is the frame it becomes 0 */
    if (timer != 0 || prev == 0) {
        if (since != 255) {
            djl_since[ply] = since + 1;
        }
        return;
    }
    djl_since[ply] = 0;
    if (since >= 10) {
        return;
    }
    osd_msg(OSD_FighterSpecificTech, fp->player_idx, since + 1 == 5 ? MSG_GREEN : MSG_RED,
            "Insta Double Jump\nFrame %d", since + 1);
}

void mu_tmce_osd_input(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (osd_on(OSD_ActOoWait)) {
        int i;
        for (i = 0; i < 6; i++) {
            int s = fp->mu_tm.state_prev[i];
            if (s == ASID_Wait || s == ASID_Landing) {
                int cur = fp->motion_id;
                /* not when crouch, shield, walk or turn was buffered */
                if (cur != ASID_Wait && cur != ASID_WalkSlow && cur != ASID_WalkMiddle && cur != ASID_WalkFast &&
                    cur != ASID_Squat && cur != ASID_GuardOn && cur != ASID_Turn && cur != ASID_SquatWait)
                {
                    act_oo_wait(fp);
                }
                break;
            }
            /* walking or turning in between: look past it */
            if (s != ASID_WalkSlow && s != ASID_WalkMiddle && s != ASID_WalkFast && s != ASID_Turn &&
                s != ASID_Squat)
            {
                break;
            }
        }
    }

    if (osd_on(OSD_FighterSpecificTech)) {
        fighter_specific_tech(fp);
    }

    if (osd_on(OSD_LockoutTimers) && fp->ground_or_air != GA_Air && fp->motion_id != ASID_AttackLw3 &&
        fp->motion_id != ASID_AttackLw4 && fp->input.lstick[0].y < 0.0f)
    {
        int t = fp->active_sticky.lstick.y;
        if (t < 25) { /* red in lockout (under 4), green after, gone from 25 */
            osd_msg(OSD_LockoutTimers, fp->player_idx, t < 4 ? MSG_RED : MSG_GREEN, "DTilt Lockout\nFrame %d",
                    t + 1);
        }
    }

    if (osd_on(OSD_FighterSpecificTech) && fp->kind == KIND_Peach) {
        peach_djl(fp);
    }
}

/* ---- single-site displays ---- */

/* Act Oo Float (ftPe_8011BE80+0x3c: A or an aerial input while floating) */
void mu_tmce_osd_act_oo_float(Fighter* fp)
{
    int frame = (u16) fp->mu_tm.state_frame;
    if (!osd_on(OSD_FighterSpecificTech) || is_follower(fp) || frame > 15) {
        return;
    }
    osd_msg(OSD_FighterSpecificTech, fp->player_idx, frame == 1 ? MSG_GREEN : MSG_RED, "Act OoFloat\nFrame %d",
            frame);
}

/* Airdodge Item Throw (ftCo_EscapeAir_IASA+0x58: an item throw or Z-drop out of airdodge) */
void mu_tmce_osd_airdodge_item(Fighter* fp, int is_throw)
{
    if (!osd_on(OSD_RollAirdodgeInterrupt) || is_follower(fp)) {
        return;
    }
    if (is_throw) {
        osd_msg(OSD_RollAirdodgeInterrupt, fp->player_idx, MSG_WHITE, "Airdodge Item Throw\nFrame %d",
                fp->mu_tm.state_prev_frames[0]);
    } else {
        osd_msg(OSD_RollAirdodgeInterrupt, fp->player_idx, MSG_WHITE, "Airdodge Z-Drop\nFrame %d",
                (u16) fp->mu_tm.state_frame);
    }
}

/* Boost Grab (ftCo_800D8AE0+0x78: a grab out of a dash attack) */
void mu_tmce_osd_boost_grab(Fighter* fp)
{
    if (!osd_on(OSD_BoostGrab) || is_follower(fp)) {
        return;
    }
    osd_msg(OSD_BoostGrab, fp->player_idx, MSG_WHITE, "Boost Grab\nFrame %d", (u16) fp->mu_tm.state_frame);
}

/* Bowser Fortress NIL (ftCo_80096D28, special fall landing): landing on the first special fall
 * frame after an aerial Whirling Fortress */
void mu_tmce_osd_fortress_nil(Fighter* fp)
{
    int frames;
    if (!osd_on(OSD_FighterSpecificTech) || is_follower(fp) || fp->kind != KIND_Bowser ||
        fp->mu_tm.state_prev[0] != 360)
    {
        return;
    }
    frames = (int) fp->cur_anim_frame + 1;
    if (frames == 1) {
        osd_msg(OSD_FighterSpecificTech, fp->player_idx, MSG_GREEN, "Successful NIL\nSp Fall Frames: %d", frames);
    } else {
        osd_msg(OSD_FighterSpecificTech, fp->player_idx, MSG_RED, "Missed NIL:\nSp Fall Frames: %d", frames);
    }
}

/* Glide Toss (ftCo_8009563C+0x84: an item throw out of a roll) */
void mu_tmce_osd_glide_toss(Fighter* fp)
{
    if (!osd_on(OSD_RollAirdodgeInterrupt) || is_follower(fp)) {
        return;
    }
    osd_msg(OSD_RollAirdodgeInterrupt, fp->player_idx, MSG_WHITE, "Glide Toss\nFrame %d",
            (u16) fp->mu_tm.state_frame);
}

/* Grab Breakout (fn_800DA054+0x28, each frame of a grab, with the grabbed fighter): the mash
 * rate since the grab started. A run that is not one frame after the last is a new grab. */
void mu_tmce_osd_grab_breakout(Fighter* victim)
{
    static float initial_timer;
    static float frames;
    static int last_frame;
    int now;
    float timer;
    if (!osd_on(OSD_GrabBreakout) || is_follower(victim)) {
        return;
    }
    now = (int) gmVs_GetSceneController()->state.frame_count;
    if (last_frame + 1 != now) {
        initial_timer = victim->grab_timer;
        frames = 0.0f;
    }
    last_frame = now;
    frames += 1.0f;
    timer = victim->grab_timer;
    osd_msg(OSD_GrabBreakout, victim->player_idx, MSG_WHITE, "Grab Breakout\nGrab Timer: %.0f\nMash Rate: %.1f/f",
            (double) timer, (double) ((initial_timer - timer) / frames));
}

/* Jump Cancel (ftCo_KneeBend_IASA, every exit): an action out of jumpsquat */
void mu_tmce_osd_kneebend_exit(MuTmceExit* e)
{
    Fighter* fp;
    int frames;
    if (e->msid < 0 || !mu_tmce_active()) {
        return;
    }
    fp = GET_FIGHTER(e->gobj);
    if (fp->motion_id == ASID_KneeBend || !osd_on(OSD_ActOoJumpSquat) || is_follower(fp)) {
        return;
    }
    frames = fp->mu_tm.state_prev_frames[0];
    osd_msg(OSD_ActOoJumpSquat, fp->player_idx, frames == 1 ? MSG_GREEN : MSG_RED, "Jump Cancel\nFrame %d", frames);
}

/* L Cancel Display NEW (ftCo_LandingAir_EnterWithMsidLag, at its end) */
void mu_tmce_osd_lcancel(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    u16* rate;
    int press;
    int color;
    void* msg;
    if (!osd_on(OSD_LCancel) || is_follower(fp)) {
        return;
    }
    rate = mu_tmce_osd_lc_rate(fp->player_idx);
    press = fp->x67F;
    if (press < 7) {
        color = MSG_GREEN;
        rate[0]++;
        rate[1]++;
    } else {
        color = MSG_RED;
        rate[1]++;
    }
    if (press <= 40) {
        msg = osd_msg(1, fp->player_idx, color, "L-Cancel %d%%\nFrame %d/7", success_rate(rate), press + 1);
    } else {
        msg = osd_msg(1, fp->player_idx, color, "L-Cancel %d%%\nNo Press", success_rate(rate));
    }
    mu_tmce_osd_msg_color(msg, 0, RGBA_WHITE);
}

/* Powershield (ftColl_80077688, a projectile hitting a shield): why it was not a powershield.
 * The console also stores 0xFFA20000 through its caller's r29 on the "reflect ended" path, a stray
 * write left from extracting the code from a disassembly; not reproduced. */
void mu_tmce_osd_powershield(Fighter* fp)
{
    int s = fp->motion_id;
    void* msg;
    if (!osd_on(OSD_Powershield) || is_follower(fp)) {
        return;
    }
    if (s == 178 || s == 341) {
        msg = osd_msg(OSD_Powershield, fp->player_idx, MSG_RED, "Missed Powershield\nNo Hard Press");
    } else if (s == 182 || s == 345) {
        if (fp->reflecting) {
            msg = osd_msg(OSD_Powershield, fp->player_idx, MSG_RED, "Missed Powershield\nEarly: %df",
                          (int) fp->mv.co.guard.x0 + 1);
        } else {
            msg = osd_msg(OSD_Powershield, fp->player_idx, MSG_RED, "Missed Powershield\nReflect Ended: %df",
                          (int) fp->mv.co.guard.x0 - 1);
        }
    } else {
        return;
    }
    mu_tmce_osd_msg_color(msg, 0, RGBA_WHITE);
}

/* Yoshi Egg Toss (fn_8012E110+0xf8, right after ftYs_SpecialS_8012DF8C): the throw's angle (the
 * one that function aimed with, in degrees, mirrored when facing left) and its charge */
void mu_tmce_osd_egg_toss(Fighter* fp)
{
    struct ftYs_DatAttrs* da = fp->dat_attrs;
    float mag;
    float angle;
    if (!osd_on(OSD_FighterSpecificTech)) {
        return;
    }
    mag = ABS(fp->input.lstick[0].x) / da->xEC;
    if (mag > 1.0f) {
        mag = 1.0f;
    }
    mag *= da->xF0;
    if (mag < da->xF4) {
        mag = 0.0f;
    }
    mag *= getStickDirX(fp);
    if (fp->facing_dir == +1.0f) {
        angle = da->specialhi_base_angle - mag;
    } else {
        angle = M_PI - da->specialhi_base_angle - mag;
    }
    if (!(fp->facing_dir > 0.0f)) {
        angle = 3.1415926f - angle;
    }
    osd_msg(OSD_FighterSpecificTech, fp->player_idx, MSG_WHITE, "Egg Toss\nAngle: %.0f\nStrength: %.0f",
            (double) (angle / 0.0174533f), (double) (float) fp->mv.ys.specialhi.x4);
}

/* ---- Act OOS Frame Display ---- */

/* Count During ShieldWait (ftCo_Guard_Anim) and Count During GuardOff (ftCo_GuardOff_Anim) */
void mu_tmce_osd_shield_count(Fighter* fp)
{
    u16 frames = (u16) fp->mu_tm.shield_frame;
    if (frames != 0xFF) {
        fp->mu_tm.shield_frame = (s16) (frames + 1);
    }
}

/* Init Frame Count (ftCo_GuardSetOff_Anim, shieldstun over) */
void mu_tmce_osd_shield_count_reset(Fighter* fp)
{
    fp->mu_tm.shield_frame = 0;
}

/* Display OOS Frame Count Standalone (0x80005508), from GuardWait / GuardOff - Display OOS Frame
 * Count (ftCo_Guard_IASA, ftCo_GuardOff_IASA: every exit) */
void mu_tmce_osd_guard_iasa_exit(MuTmceExit* e)
{
    Fighter* fp;
    u16 frames;
    if (e->msid < 0 || !mu_tmce_active()) {
        return;
    }
    fp = GET_FIGHTER(e->gobj);
    /* an interrupt: one of the checks moved the fighter to another state */
    if (fp->motion_id == e->msid || !osd_on(OSD_ActOoS) || is_follower(fp) || fp->motion_id == ASID_GuardOff) {
        return;
    }
    if (fp->mu_tm.state_prev[1] != ASID_GuardSetOff && fp->mu_tm.state_prev[2] != ASID_GuardSetOff) {
        return;
    }
    frames = (u16) fp->mu_tm.shield_frame;
    osd_msg(3, fp->player_idx, frames > 0 ? MSG_WHITE : MSG_GREEN, "Act OoShield\nFrame %d", frames + 1);
}

/* ---- Act OOTumble ---- */

/* Static Function - Display Frames Spent Post Hitstun (0x80005504) */
int mu_tmce_osd_oo_hitstun(int interrupted, HSD_GObj* gobj)
{
    Fighter* fp;
    int frames;
    if (interrupted != 1 || !mu_tmce_active()) {
        return interrupted;
    }
    fp = GET_FIGHTER(gobj);
    if (!osd_on(OSD_ActOoHitstun) || is_follower(fp)) {
        return interrupted;
    }
    frames = post_hitstun(fp);
    osd_msg(5, fp->player_idx, frames > 0 ? MSG_WHITE : MSG_GREEN, "Act OoHitstun\nFrame %d", frames + 1);
    return interrupted;
}

/* DamageFall - Check For Interrupt (ftCo_DamageFall_IASA, every exit): an interrupt, or tumble
 * ending in Fall */
void mu_tmce_osd_damagefall_exit(MuTmceExit* e)
{
    if (e->msid < 0 || !mu_tmce_active()) {
        return;
    }
    mu_tmce_osd_oo_hitstun(GET_FIGHTER(e->gobj)->motion_id != e->msid, e->gobj);
}

/* ---- Act OoAirborne ---- */

static const struct {
    const char* text;
    int kind;
} oo_airborne[] = {
    [MU_TMCE_OO_SPECIAL_JUMP] = { "Special OoJump\nFrame %d", OSD_ActOoAirborne },
    [MU_TMCE_OO_AERIAL_JUMP] = { "Aerial OoJump\nFrame %d", OSD_ActOoAirborne },
    /* its own kind so it shows along with Aerial OoJump */
    [MU_TMCE_OO_DBLJUMP_JUMP] = { "DblJump OoJump\nFrame %d", OSD_Miscellaneous },
    [MU_TMCE_OO_SPECIAL_DBLJUMP] = { "Special OoDblJump\nFrame %d", OSD_ActOoAirborne },
    [MU_TMCE_OO_AERIAL_DBLJUMP] = { "Aerial OoDblJump\nFrame %d", OSD_ActOoAirborne },
    [MU_TMCE_OO_SPECIAL_DROP] = { "Special OoDrop\nFrame %d", OSD_ActOoAirborne },
    [MU_TMCE_OO_AERIAL_DROP] = { "Aerial OoDrop\nFrame %d", OSD_ActOoAirborne },
    [MU_TMCE_OO_SPECIAL_FALL] = { "Special OoFall\nFrame %d", OSD_ActOoAirborne },
    [MU_TMCE_OO_AERIAL_FALL] = { "Aerial OoFall\nFrame %d", OSD_ActOoAirborne },
};

/* Display Act OoAirborne Frame Count - Static Function (0x8000550c), from the Special / Aerial
 * checks of the jump, double jump, drop and fall states */
int mu_tmce_osd_oo_airborne(int interrupted, HSD_GObj* gobj, int which)
{
    Fighter* fp;
    int prev;
    int frames;
    if (interrupted == 0 || !mu_tmce_active()) {
        return interrupted;
    }
    fp = GET_FIGHTER(gobj);
    if (!osd_on(OSD_ActOoAirborne) || is_follower(fp)) {
        return interrupted;
    }
    prev = fp->mu_tm.state_prev[0];
    frames = fp->mu_tm.state_prev_frames[0];
    if (prev == ASID_Fall) {
        int before = fp->mu_tm.state_prev[1];
        if (frames > 10 || before == ASID_JumpF || before == ASID_JumpB ||
            (before >= ASID_AttackAirN && before <= ASID_AttackAirLw) || before > ASID_BarrelCannonWait)
        {
            return interrupted;
        }
    } else if (prev != ASID_Pass) {
        /* out of a jump: only up to a few frames past the jump's peak */
        float v = fp->co_attrs.jump_v_initial_velocity;
        if (prev == ASID_JumpF || prev == ASID_JumpB) {
        } else if (prev == ASID_JumpAerialF || prev == ASID_JumpAerialB) {
            v = v * fp->co_attrs.air_jump_v_multiplier;
        } else if (fp->co_attrs.max_jumps > 2 && prev >= 0x155 && prev <= 0x159) {
            v = v * fp->co_attrs.air_jump_v_multiplier; /* the multiple jumps of Kirby, Jigglypuff */
        } else {
            return interrupted;
        }
        if (frames > (int) (v / fp->co_attrs.gravity) + 5) {
            return interrupted;
        }
    }
    osd_msg(oo_airborne[which].kind, fp->player_idx, frames == 1 ? MSG_GREEN : MSG_RED, oo_airborne[which].text,
            frames);
    return interrupted;
}

/* ---- Fastfall Timer ---- */

/* Display FF Timing (ftCommon_CheckFallFast+0x54, a fastfall) */
void mu_tmce_osd_ff_display(Fighter* fp)
{
    void* msg;
    if (!osd_on(OSD_Fastfall) || is_follower(fp) || fp->mu_tm.state_prev[0] == ASID_RebirthWait) {
        return;
    }
    msg = osd_msg(7, fp->player_idx, MSG_WHITE, "Fastfall\nFrame %d", fp->mu_tm.can_fastfall_frames);
    if (fp->mu_tm.can_fastfall_frames == 1) {
        mu_tmce_osd_msg_color(msg, 1, RGBA_GREEN);
    }
}

/* ---- Dashback ---- */

/* Dashback Display - Static Function (0x80005518) */
static void dashback_display(Fighter* fp, bool dashback)
{
    u16* rate;
    const char* text;
    unsigned color;
    void* msg;
    if (!osd_on(OSD_Dashback) || is_follower(fp)) {
        return;
    }
    rate = mu_tmce_osd_db_rate(fp->player_idx);
    if (dashback) {
        rate[0]++;
        rate[1]++;
        /* the turn's frames_to_turn is 0 after a smash turn: vanilla */
        if (fp->mv.co.common.x10 == 0) {
            text = "Vanilla Dashback\nSuccess Rate: %d%%";
            color = RGBA_GREEN;
        } else {
            text = "UCF Dashback\nSuccess Rate: %d%%";
            color = RGBA_YELLOW;
        }
    } else {
        rate[1]++;
        text = "Failed Dashback\nSuccess Rate: %d%%";
        color = RGBA_RED;
    }
    msg = osd_msg(4, fp->player_idx, MSG_WHITE, text, success_rate(rate));
    mu_tmce_osd_msg_color(msg, 0, color);
}

/* Run Out of Tilt Turn (ftCo_Turn_IASA+0x190, a dash out of a turn): a dashback when the turn's
 * animation frame is 2 (the high half of the float, as the console reads it) */
void mu_tmce_osd_dashback(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    int prev = fp->mu_tm.state_prev[0];
    if (prev == ASID_Dash || prev == ASID_SquatWait) {
        return;
    }
    dashback_display(fp, (float_bits(fp->cur_anim_frame) >> 16) == 0x4000);
}

/* ---- Ledge Codes ---- */

/* Frames Spent in CliffWait (ftCo_8009AAFC+0x7c, dropping from the ledge) */
void mu_tmce_osd_cliffwait_frames(Fighter* fp)
{
    int frames = (u16) fp->mu_tm.state_frame;
    if (!osd_on(OSD_Ledge) || is_follower(fp) || frames > 20) {
        return;
    }
    frames -= 1;
    osd_msg(13, fp->player_idx, frames == 1 ? MSG_GREEN : MSG_RED, "Frames in\nCliffwait: %d", frames);
}

/* Standalaone - GALINT Display (0x80005514): ledge intangibility left on landing */
static void galint_display(HSD_GObj* gobj, bool aerial_interrupt)
{
    Fighter* fp = GET_FIGHTER(gobj);
    int galint;
    if (!osd_on(OSD_Ledge) || is_follower(fp) || fp->mu_tm.vuln_frames > 20) {
        return;
    }
    galint = fp->x1990;
    if (aerial_interrupt) {
        galint -= (int) fp->co_attrs.normal_landing_lag;
    }
    if (galint > 0) {
        osd_msg(19, fp->player_idx, MSG_GREEN, "GALINT\nFrames: %d", galint);
    } else {
        osd_msg(19, fp->player_idx, MSG_RED, "GALINT\nFrames: %d", -(int) fp->mu_tm.vuln_frames);
    }
}

/* GALINT Aerail Interrupt (ftCo_LandingAir_EnterWithLag+0xf8, a plain landing) */
void mu_tmce_osd_galint_aerial(HSD_GObj* gobj)
{
    if (GET_FIGHTER(gobj)->mu_tm.state_prev[2] == ASID_CliffWait) {
        galint_display(gobj, true);
    }
}

/* GALINT Laser Land (ftCo_AirCatchHit_Coll+0xc8, landing straight into Wait) */
void mu_tmce_osd_galint_laser(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    int i;
    for (i = 0; i <= 5; i++) {
        if (fp->mu_tm.state_prev[i] == ASID_CliffWait) {
            galint_display(gobj, false);
            return;
        }
    }
}

/* GALINT Ledgedash (ftCo_Landing_Anim+0x20, landing lag over): airdodge from the ledge */
void mu_tmce_osd_galint_ledgedash(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->mu_tm.state_prev[0] == ASID_EscapeAir && fp->mu_tm.state_prev[3] == ASID_CliffWait) {
        galint_display(gobj, false);
    }
}

/* GALINT No Impact Land (ft_80082B1C+0x30, landing straight into Wait) */
void mu_tmce_osd_galint_noimpact(HSD_GObj* gobj)
{
    if (GET_FIGHTER(gobj)->mu_tm.state_prev[1] == ASID_CliffWait) {
        galint_display(gobj, false);
    }
}

/* Ledgestall Display (ftCliffCommon_80081370, catching the ledge) */
void mu_tmce_osd_ledgestall(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    int vulnerable = fp->mu_tm.vuln_frames - 1;
    if (!osd_on(OSD_Ledge) || is_follower(fp) || vulnerable > 15) {
        return;
    }
    if (vulnerable > 0) {
        osd_msg(19, fp->player_idx, MSG_RED, "Ledgestall\nVulnerable: %d", vulnerable);
    } else {
        osd_msg(19, fp->player_idx, MSG_GREEN, "Ledgestall\nPerfect", vulnerable);
    }
}

/* ---- SDI Display ---- */

/* SDI Display Main (ftCo_Damage_OnEveryHitlag+0x3c, each hitlag frame that allows SDI) */
int mu_tmce_osd_sdi(Fighter* fp)
{
    float x;
    float y;
    float len;
    float min;
    if (!osd_on(OSD_SDI) || is_follower(fp)) {
        return 1;
    }
    x = fp->input.lstick[0].x * fp->input.lstick[0].x;
    y = fp->input.lstick[0].y * fp->input.lstick[0].y;
    len = x + y;
    min = p_ftCommonData->sdi_min_stick_mag * p_ftCommonData->sdi_min_stick_mag;
    if (len >= min && (fp->active_timer.lstick.x < p_ftCommonData->sdi_stick_window ||
                       fp->active_timer.lstick.y < p_ftCommonData->sdi_stick_window))
    {
        fp->mu_tm.tm_union.sdi.successful_sdi_inputs++;
    }
    fp->mu_tm.tm_union.sdi.total_sdi_inputs++;
    /* shown on the last hitlag frame */
    if (float_bits(fp->dmg.x195c_hitlag_frames) == 0x3F800000) {
        osd_msg(OSD_Null, fp->player_idx, fp->mu_tm.tm_union.sdi.successful_sdi_inputs ? MSG_GREEN : MSG_RED,
                "SDI Inputs\n%d/%d", fp->mu_tm.tm_union.sdi.successful_sdi_inputs,
                fp->mu_tm.tm_union.sdi.total_sdi_inputs);
    }
    return 1;
}

/* ---- Wavedash Display ---- */

/* the airdodge angle in degrees, one decimal kept (truncated), folded to 0..90 */
static float wavedash_angle(float radians)
{
    float degrees = radians / 0.0174532924f;
    float angle = (float) (int) (degrees * 10.0f);
    angle = angle / 10.0f;
    if (angle >= 0.0f) {
        if (angle > 90.0f) {
            angle = 180.0f + -angle;
        }
    } else if (angle > -90.0f) {
        angle = -angle;
    } else {
        angle = 180.0f + angle;
    }
    return angle;
}

/* Wavedash Timing Display (ftCo_80099D70, airdodge landing): within 20 frames of the airdodge */
void mu_tmce_osd_wavedash(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float angle;
    int jump_frames;
    int hop;
    int held = -1;
    void* msg;
    if (!osd_on(OSD_Wavedash) || is_follower(fp) || (u16) fp->mu_tm.state_frame > 20) {
        return;
    }
    angle = wavedash_angle(fp->mu_tm.tm_union.wavedash.airdodge_angle);
    jump_frames = fp->mu_tm.state_prev_frames[0];
    hop = (int) fp->mu_tm.tm_union.wavedash.hop_type;
    if (hop <= 1) {
        held = jump_held_frames(fp, 24);
    }
    if (held != -1) {
        msg = osd_msg(0, fp->player_idx, MSG_WHITE, "Wavedash Frame: %d\nAngle: %2.1f\n%s: %df", jump_frames,
                      (double) angle, hop == 1 ? "Short Hop" : "Full Hop", held);
        if (hop != 0) {
            mu_tmce_osd_msg_color(msg, 2, RGBA_GREEN);
        }
    } else {
        msg = osd_msg(0, fp->player_idx, MSG_WHITE, "Wavedash Frame: %d\nAngle: %2.1f", jump_frames, (double) angle);
    }
    if (jump_frames == 1) {
        mu_tmce_osd_msg_color(msg, 0, RGBA_GREEN);
    }
    if (angle < 16.8f) {
        return;
    }
    if (angle < 23.7f) {
        mu_tmce_osd_msg_color(msg, 1, RGBA_GREEN);
    } else if (!(angle > 30.5f)) {
        mu_tmce_osd_msg_color(msg, 1, RGBA_YELLOW);
    }
}

/* ---- exits ---- */

int mu_tmce_osd_msid(HSD_GObj* gobj)
{
    return mu_tmce_active() ? GET_FIGHTER(gobj)->motion_id : -1;
}

/* ---- Misc: Character Randomness (the lab's picks; 0 keeps the game's roll) ---- */

enum {
    RNG_PeachItem = 0,
    RNG_PeachFSmash = 1,
    RNG_LuigiMisfire = 2,
    RNG_GnWHammer = 3,
    RNG_NanaThrow = 4,
};

/* GnW Hammer (ftGw_SpecialS_GetRandomInt+0xcc): the judgement number stored for the move (the
 * function still returns its own roll) */
int mu_tmce_osd_gnw_hammer(int roll)
{
    int pick = mu_tmce_osd_rng(RNG_GnWHammer);
    return pick == 0 ? roll : pick - 1;
}

/* Luigi Misfire (ftLg_SpecialS_SetVars+0x84): 0 = misfire; the roll is skipped on a pick */
int mu_tmce_osd_luigi_misfire(int chance)
{
    int pick = mu_tmce_osd_rng(RNG_LuigiMisfire);
    if (pick < 1) {
        return HSD_Randi(chance);
    }
    return pick == 1 ? 0 : 1;
}

/* Nana Throw (ftCo_800B683C+0x7b8): the CPU's throw roll, [0,.25) up, [.25,.5) down, [.5,.75)
 * forward, [.75,1) back; pick 5 is forward or down on the roll */
float mu_tmce_osd_nana_throw(Fighter* fp, float roll)
{
    int pick;
    if (fp->kind != KIND_Nana) {
        return roll;
    }
    pick = mu_tmce_osd_rng(RNG_NanaThrow);
    switch (pick) {
    case 0:
        return roll;
    case 1:
        return 0.5f;
    case 2:
        return 0.75f;
    case 3:
        return 0.0f;
    case 4:
        return 0.25f;
    case 5:
        return roll >= 0.5f ? 0.5f : 0.25f;
    default:
        return 0.0f; /* as the console: the roll is zeroed before the picks are tested */
    }
}

/* Peach FSmash (ftPe_AttackS4_Enter+0x34): the club / pan / racket roll; a pick also forgets the
 * last one so the reroll loop takes it */
int mu_tmce_osd_peach_fsmash(Fighter* fp, int count)
{
    int pick = mu_tmce_osd_rng(RNG_PeachFSmash);
    if (pick == 0) {
        return HSD_Randi(count);
    }
    fp->u.pe.attacks4_motion_id = -1;
    return pick - 1;
}

/* Peach Item (spawnVeg+0x8c): 1-4 a turnip (its face: Peach Turnip), 5 Mr. Saturn, 6 Bob-omb,
 * 7 beam sword */
int mu_tmce_osd_peach_item(int kind)
{
    int pick = mu_tmce_osd_rng(RNG_PeachItem);
    switch (pick) {
    case 0:
        return kind;
    case 5:
        return 0x07;
    case 6:
        return 0x06;
    case 7:
        return 0x0C;
    default:
        return 0x63;
    }
}

/* Peach Turnip (it_802BD32C+0xc0): the face roll; a pick skips the roll */
int mu_tmce_osd_turnip(int sum)
{
    switch (mu_tmce_osd_rng(RNG_PeachItem)) {
    case 0:
        return HSD_Randi(sum);
    case 1:
        return 0;
    case 2:
        return 52;
    case 3:
        return 56;
    default:
        return 57;
    }
}

/* ---- Misc: Frame Advance Rewrite, Better VS Mode Frame Counter, LRAS ---- */

/* Frame Advance Rewrite (gm_AnyControllerPressedZ): Z advances one frame when pressed, then
 * every frame once held for 30; the press is taken off the pad so it does not reach the game */
int mu_tmce_osd_frame_advance(void)
{
    static int held;
    int i;
    for (i = 0; i < 4; i++) {
        HSD_PadStatus* pad = &HSD_PadMasterStatus[i];
        if (pad->button & HSD_PAD_Z) {
            held++;
            if (held == 1 || held >= 30) {
                pad->button &= ~HSD_PAD_Z;
                pad->trigger &= ~HSD_PAD_Z;
                return 1;
            }
            return 0;
        }
    }
    held = 0;
    mu_gm_engine_state()->unk_10.x2 &= ~1;
    return 0;
}

int mu_tmce_game_frame_counter;

/* Better VS Mode Frame Counter/Init Counter (fn_8016E730+0x14) */
void mu_tmce_osd_frame_counter_init(void)
{
    mu_tmce_game_frame_counter = 0;
}

/* Better VS Mode Frame Counter/Inc Counter (fn_8016CFE0+0x330) */
void mu_tmce_osd_frame_counter_inc(void)
{
    mu_tmce_game_frame_counter++;
}

/* Allow light press in LRAS (fn_8016CFE0+0x218): Start and A held with L and R each pressed
 * past the trigger threshold or clicked, on any pad (the console loops 6 entries of 0x44 from the
 * master pads: the last two are the copied status of ports 1 and 2, the same pads) */
int mu_tmce_osd_lras_light_press(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        HSD_PadStatus* pad = &HSD_PadMasterStatus[i];
        u32 held = pad->button;
        if (!(held & HSD_PAD_START) || !(held & HSD_PAD_A)) {
            continue;
        }
        if (pad->analogL < 43 && !(held & HSD_PAD_L)) {
            continue;
        }
        if (pad->analogR < 43 && !(held & HSD_PAD_R)) {
            continue;
        }
        return 1;
    }
    return 0;
}
