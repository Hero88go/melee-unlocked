/* Training Lab (M3): native training tools in Training mode, offline only.
 *
 * Savestates: D-pad Right keeps the whole game state, D-pad Left returns to it (as often as
 * wanted). The state is the rollback engine's snapshot (all of game memory and the game library's
 * data, minus sound driver and host-coupled state), taken and restored at the top of an engine
 * frame, the same point online rollback uses; the hardware timers keep their schedule across a
 * load exactly as they do for a rollback.
 *
 * Collision bubbles: the game's own debug drawing of every fighter's hitboxes and hurt capsules,
 * over the model or alone.
 *
 * Recording: D-pad Down keeps the moment and starts recording; while it records, your controller
 * plays the training dummy and your own character stands still. D-pad Down again stops. D-pad Up
 * returns to the kept moment and the dummy repeats what you recorded while you play again.
 *
 * Dummy behavior (when nothing is recorded or playing): it holds DI toward you, away from you or
 * a random side of the two for each hit, and once hitstun ends it can jump, shield or spot dodge
 * right away. This state is ordinary game state (kept and restored with savestates).
 *
 * Nothing here runs in an online match, a pending online match or replay playback. No Training
 * Mode mod code runs: these are C reimplementations of the tools. */
#include <dolphin/os.h>
#include <melee/ft/fighter.h>
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/forward.h>
#include <melee/gm/gmvs.h>
#include <melee/mp/mplib.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>

#include "mu_native.h"

int mu_online_active(void);
int mu_online_pending(void);
int mu_replay_abi_active(void);
int mu_online_abi_command(unsigned int command, const unsigned char* payload, unsigned int size,
                          unsigned char* response, unsigned int capacity, unsigned int* response_size);
void mu_alarms_hold(void);
void mu_alarms_release(void);

/* Host commands (source_host.cpp), native only. */
#define CMD_LAB_SAVE 0xF2
#define CMD_LAB_LOAD 0xF3
#define CMD_LAB_ADVANTAGE 0xF4   /* payload: s32 frames (BE), u8 1 hit / 2 shield */

static int lab_saved;            /* a state is kept for the current Training session */
enum { LAB_IDLE, LAB_RECORDING, LAB_PLAYING };
#define LAB_MAX_FRAMES (60 * 60)  /* one minute */
static MuLabInput lab_rec[LAB_MAX_FRAMES];
static int lab_rec_len;           /* frames recorded */
static int lab_mode;
static int lab_pos;               /* frame within the recording */
static int dummy_logs;             /* test log lines written (excluded: log only) */
static int lab_loop_pending;
/* Frame advantage: from the moment player 1 hits the dummy (or its shield), the first frame
 * each of them can act; the difference is player 1's advantage. */
static int adv_active;
static int adv_kind;
static int adv_count;
static int adv_attacker_at;
static int adv_defender_at;
static int adv_prev_hitlag;      /* playback ended with looping on: start it again next frame */
static unsigned int lab_test_fired;   /* MELEE_LAB_TEST presses already made (survives loads) */
static unsigned int lab_session; /* the scene the state belongs to */

/* Dummy behavior state: part of the game state, so savestates keep it. */
static int dummy_di_side;          /* +1 / -1: DI direction chosen for the current hit */
static int dummy_was_hit;          /* the dummy was in hitlag or hitstun last frame */
static int dummy_react_frames;     /* frames the reaction input is still held */
static unsigned int dummy_rng = 0x2545F491u;
static int dummy_tech_side;        /* armed tech: 0 none, else stick side (+2 = in place) */
static int dummy_tech_pressed;     /* the one shield press for this fall was made */

char* getenv(const char* name);

static int lab_on(void)
{
    const unsigned int options = mu_game_options();
    /* MELEE_LAB_ANY_MODE (tests only): the tools in any offline match, for scripted checks. */
    static int any_mode = -1;
    if (any_mode < 0) {
        any_mode = getenv("MELEE_LAB_ANY_MODE") != NULL;
    }
    return (options & MU_OPTION_LAB) != 0 && (gm_GetCurrentGameMode() == GM_TRAINING || any_mode) &&
           !mu_online_active() && !mu_online_pending() && !mu_replay_abi_active();
}

static int lab_command(unsigned int cmd)
{
    static unsigned char response[4096];   /* the host's minimum response capacity */
    unsigned int got = 0;
    return mu_online_abi_command(cmd, NULL, 0, response, sizeof response, &got);
}

static void lab_send_advantage(int frames, int kind)
{
    static unsigned char response[4096];
    unsigned char payload[5];
    unsigned int got = 0;
    payload[0] = (unsigned char) ((unsigned int) frames >> 24);
    payload[1] = (unsigned char) ((unsigned int) frames >> 16);
    payload[2] = (unsigned char) ((unsigned int) frames >> 8);
    payload[3] = (unsigned char) frames;
    payload[4] = (unsigned char) kind;
    mu_online_abi_command(CMD_LAB_ADVANTAGE, payload, sizeof payload, response, sizeof response, &got);
}

/* A fighter can act: its animation allows an interrupt, or it is in a free state (standing,
 * walking, dashing, running, crouching, falling, midair jump, shield held). */
static int lab_can_act(const Fighter* fp)
{
    const int m = (int) fp->motion_id;
    if (fp->dmg.x195c_hitlag_frames > 0.0f || fp->x221C_b6) {
        return 0;
    }
    if (fp->allow_interrupt) {
        return 1;
    }
    return (m >= ftCo_MS_Wait && m <= ftCo_MS_Run) || (m >= ftCo_MS_JumpAerialF && m <= ftCo_MS_FallAerialB) ||
           m == ftCo_MS_Squat || m == ftCo_MS_SquatWait || m == ftCo_MS_DamageFall || m == ftCo_MS_Guard;
}

/* Once per frame, from the dummy's input read (so both fighters are live): start a measurement
 * on a new hit, finish it when both can act. */
static void lab_advantage_frame(Fighter* fd)
{
    HSD_GObj* a = Player_GetEntity(0);
    Fighter* fa;
    int hitlag;
    if (a == NULL || a->user_data == NULL) {
        adv_active = 0;
        return;
    }
    fa = a->user_data;
    hitlag = fd->dmg.x195c_hitlag_frames > 0.0f;
    if (hitlag && !adv_prev_hitlag) {
        adv_active = 1;
        adv_kind = fd->motion_id >= ftCo_MS_GuardOn && fd->motion_id <= ftCo_MS_GuardReflect ? 2 : 1;
        adv_count = 0;
        adv_attacker_at = -1;
        adv_defender_at = -1;
    }
    adv_prev_hitlag = hitlag;
    if (!adv_active) {
        return;
    }
    adv_count++;
    if (fd->motion_id < ftCo_MS_Wait || fa->motion_id < ftCo_MS_Wait) {
        adv_active = 0;   /* a KO or respawn: no reading */
        return;
    }
    if (adv_attacker_at < 0 && fa->dmg.x195c_hitlag_frames <= 0.0f && lab_can_act(fa)) {
        adv_attacker_at = adv_count;
    }
    if (adv_defender_at < 0 && !hitlag && lab_can_act(fd)) {
        adv_defender_at = adv_count;
    }
    if (adv_attacker_at >= 0 && adv_defender_at >= 0) {
        adv_active = 0;
        lab_send_advantage(adv_defender_at - adv_attacker_at, adv_kind);
        if (dummy_logs < 40) {
            dummy_logs++;
            OSReport("[lab] frame advantage %+d on %s\n", adv_defender_at - adv_attacker_at,
                     adv_kind == 2 ? "shield" : "hit");
        }
    } else if (adv_count > 300) {
        adv_active = 0;   /* nobody could act for five seconds: no reading */
    }
}

/* Any human's D-pad press this frame (the training dummy's port is ignored by the game anyway). */
static u32 lab_pressed(void)
{
    u32 pressed = 0;
    int port;
    /* Menus renew the copy status; a match renews the game status (lb_80019900) instead. */
    for (port = 0; port < 4; port++) {
        if (HSD_PadCopyStatus[port].err == 0) {
            pressed |= HSD_PadCopyStatus[port].trigger;
        }
        if (HSD_PadGameStatus[port].err == 0) {
            pressed |= HSD_PadGameStatus[port].trigger;
        }
    }
    return pressed;
}

static void lab_bubbles(void)
{
    const unsigned int options = mu_game_options();
    const u8 value = (options & MU_OPTION_LAB_BUBBLES) == 0 ? 1      /* model only (retail) */
                     : (options & MU_OPTION_LAB_BUBBLES_ONLY) ? 2  /* bubbles only */
                                                              : 3; /* bubbles over the model */
    HSD_GObj* gobj;
    for (gobj = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; gobj != NULL; gobj = gobj->next) {
        Fighter* fp = GET_FIGHTER(gobj);
        if (fp != NULL) {
            fp->x21FC_flag.byte = value;
        }
    }
}

/* 20XX TE "Collision bubbles": hitboxes and hurt capsules over the model in any offline match
 * (the lab's own setting wins in Training while the lab is on). Switching it off puts the model back. */
void mu_te_frame_begin(void)
{
    static int shown;
    const int want = mu_te2(MU_TE2_BUBBLES) && !lab_on();
    HSD_GObj* gobj;
    if (mu_te2(MU_TE2_COLOR_OVERLAYS)) {
        /* 20XX TE "Color overlays": green on every frame a fighter can act, so gaps between
         * actions (frame holes) show. The tint lasts two frames and is renewed while it applies;
         * an effect the game is already showing on that slot is left alone. */
        for (gobj = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; gobj != NULL; gobj = gobj->next) {
            Fighter* fp = GET_FIGHTER(gobj);
            ColorOverlay* co;
            if (fp == NULL || !lab_can_act(fp)) {
                continue;
            }
            co = &fp->x488;
            if (fp->x408.x28_colanim.i != 0 || co->x28_colanim.i != 0 || co->x8_ptr1 != NULL ||
                (co->x7C_color_enable && co->x34_color_green < 200.0F))
            {
                continue;
            }
            co->x4_pri = 2;
            co->x7C_color_enable = true;
            co->x30_color_red = 40.0F;
            co->x34_color_green = 255.0F;
            co->x38_color_blue = 40.0F;
            co->x3C_color_alpha = 110.0F;
            co->x40_colorblend_red = co->x44_colorblend_green = co->x48_colorblend_blue = 0.0F;
            co->x4C_colorblend_alpha = 0.0F;
        }
    }
    if (!want && !shown) {
        return;
    }
    for (gobj = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; gobj != NULL; gobj = gobj->next) {
        Fighter* fp = GET_FIGHTER(gobj);
        if (fp != NULL) {
            fp->x21FC_flag.byte = want ? 3 : 1;
        }
    }
    shown = want;
}

/* MELEE_LAB_TEST=frame,frame,... (tests only): key presses at those match frames, so a scripted run
 * can check the tools. A bare number is save (the first) or load (the rest); a prefix picks the key:
 * s save, l load, r record on/off, p play back. */
static u32 lab_test_presses(void)
{
    static int parsed;
    static int frames[16];
    static u32 keys[16];
    static int count;
    const int now = (int) gm_GetFrameCount();
    int i;
    if (!parsed) {
        const char* spec = getenv("MELEE_LAB_TEST");
        parsed = 1;
        while (spec != NULL && *spec && count < 16) {
            int v = 0;
            u32 key = count == 0 ? HSD_PAD_DPADRIGHT : HSD_PAD_DPADLEFT;
            switch (*spec) {
            case 's': key = HSD_PAD_DPADRIGHT; spec++; break;
            case 'l': key = HSD_PAD_DPADLEFT; spec++; break;
            case 'r': key = HSD_PAD_DPADDOWN; spec++; break;
            case 'p': key = HSD_PAD_DPADUP; spec++; break;
            }
            while (*spec >= '0' && *spec <= '9') {
                v = v * 10 + (*spec++ - '0');
            }
            keys[count] = key;
            frames[count++] = v;
            if (*spec == ',') {
                spec++;
            } else {
                break;
            }
        }
    }
    for (i = 0; i < count; i++) {
        if (frames[i] == now && !(lab_test_fired & (1u << i))) {
            lab_test_fired |= 1u << i;
            return keys[i];
        }
    }
    return 0;
}

/* Top of an engine frame (gmscene.c, next to the online frame hook). */
void mu_lab_frame_begin(void)
{
    u32 pressed;
    if (!lab_on()) {
        return;
    }
    if (lab_session != (unsigned int) gm_GetCurrentSceneIndex() + 1u) {
        lab_session = (unsigned int) gm_GetCurrentSceneIndex() + 1u;
        lab_saved = 0;   /* a new Training session starts without a state */
    }
    pressed = lab_pressed() | lab_test_presses();
    if (pressed & (HSD_PAD_DPADDOWN | HSD_PAD_DPADUP | HSD_PAD_DPADLEFT | HSD_PAD_DPADRIGHT)) {
        lab_loop_pending = 0;   /* any lab key ends a loop */
    }
    if (lab_loop_pending) {
        lab_loop_pending = 0;
        if (lab_saved && lab_rec_len > 0 && (mu_game_options() & MU_OPTION_LAB_LOOP)) {
            mu_alarms_hold();
            lab_command(CMD_LAB_LOAD);
            mu_alarms_release();
            lab_mode = LAB_PLAYING;
            lab_pos = 0;
        }
    }
    if (pressed & HSD_PAD_DPADDOWN) {
        if (lab_mode == LAB_RECORDING) {
            lab_mode = LAB_IDLE;
            OSReport("[lab] recording stopped, %d frames\n", lab_rec_len);
        } else if (lab_command(CMD_LAB_SAVE) == 0) {
            lab_saved = 1;
            lab_mode = LAB_RECORDING;
            lab_rec_len = 0;
            lab_pos = 0;
            OSReport("[lab] recording\n");
        }
    } else if ((pressed & HSD_PAD_DPADUP) && lab_saved && lab_rec_len > 0) {
        mu_alarms_hold();
        lab_command(CMD_LAB_LOAD);
        mu_alarms_release();
        lab_mode = LAB_PLAYING;
        lab_pos = 0;
        OSReport("[lab] playing back %d frames\n", lab_rec_len);
    } else if (pressed & HSD_PAD_DPADRIGHT) {
        if (lab_command(CMD_LAB_SAVE) == 0) {
            lab_saved = 1;
            OSReport("[lab] state saved\n");
        }
    } else if ((pressed & HSD_PAD_DPADLEFT) && lab_saved) {
        mu_alarms_hold();   /* timers are hardware: they keep their schedule through a load */
        lab_command(CMD_LAB_LOAD);
        mu_alarms_release();
        lab_mode = LAB_IDLE;   /* a plain return stops any recording or playback */
        OSReport("[lab] state loaded\n");
    }
    lab_bubbles();
}

/* Fighter input (Fighter_procInput): the recording drives the dummy (player 2); while recording,
 * player 1's controller is what the dummy does and player 1 stands still. */
/* The dummy's own reactions while the lab is idle: DI during hitlag and hitstun, then one
 * action on the first frames it can act. Returns 1 when it sets the dummy's input. */
static int dummy_behavior(Fighter* fp, MuLabInput* out)
{
    const unsigned int options = mu_game_options();
    const int di = (int) ((options & MU_OPTION_LAB_DI_MASK) >> MU_OPTION_LAB_DI_SHIFT);
    const int react = (int) ((options & MU_OPTION_LAB_REACT_MASK) >> MU_OPTION_LAB_REACT_SHIFT);
    const int tech = (int) ((options & MU_OPTION_LAB_TECH_MASK) >> MU_OPTION_LAB_TECH_SHIFT);
    const int hit = fp->dmg.x195c_hitlag_frames > 0.0f || fp->x221C_b6;
    const int tumbling = fp->ground_or_air == GA_Air &&
                         (hit || fp->motion_id == ftCo_MS_DamageFall ||
                          (fp->motion_id >= ftCo_MS_DamageHi1 && fp->motion_id <= ftCo_MS_DamageFlyRoll));
    if (di == 0 && react == 0 && tech == 0) {
        dummy_was_hit = hit;
        return 0;
    }
    if (hit && !dummy_was_hit) {   /* a new hit: pick this hit's DI side, forget any armed tech */
        HSD_GObj* p1 = Player_GetEntity(0);
        float toward = 1.0f;
        if (p1 != NULL && p1->user_data != NULL) {
            toward = ((Fighter*) p1->user_data)->cur_pos.x >= fp->cur_pos.x ? 1.0f : -1.0f;
        }
        dummy_rng = dummy_rng * 1103515245u + 12345u;
        switch (di) {
        case 1: dummy_di_side = (int) toward; break;
        case 2: dummy_di_side = (int) -toward; break;
        default: dummy_di_side = (dummy_rng >> 16) & 1 ? (int) toward : (int) -toward; break;
        }
        dummy_react_frames = 0;
        dummy_tech_side = 0;
        dummy_tech_pressed = 0;
        if (dummy_logs < 8) {
            dummy_logs++;
            OSReport("[lab] dummy hit at frame %d: DI %s (%s you), action %d\n", gm_GetFrameCount(),
                     dummy_di_side > 0 ? "right" : "left", dummy_di_side == (int) toward ? "toward" : "away from",
                     fp->motion_id);
        }
    }
    if (!hit && dummy_was_hit && react != 0) {
        /* hitstun just ended: act now (a jump is a short press, shield and dodge hold longer) */
        dummy_react_frames = react == 1 ? 3 : 10;
        if (dummy_logs < 16) {
            dummy_logs++;
            OSReport("[lab] dummy hitstun ended at frame %d (%s): reaction %d\n", gm_GetFrameCount(),
                     fp->ground_or_air == GA_Ground ? "ground" : "air", react);
        }
    }
    dummy_was_hit = hit;
    if (fp->dmg.x195c_hitlag_frames > 0.0f && di != 0) {   /* DI is read as hitlag ends */
        __builtin_memset(out, 0, sizeof *out);
        out->lx = (float) dummy_di_side;
        return 1;
    }
    if (!tumbling) {
        dummy_tech_side = 0;
        dummy_tech_pressed = 0;
    } else if (tech != 0) {
        /* Tech: one shield press a few frames before the dummy meets the floor (the game techs a
         * landing within 20 frames of a press, with no other press in the 40 before it), then the
         * stick held for the tech direction until it lands. The floor is found with the game's
         * own floor query along the dummy's current motion. */
        if (!dummy_tech_pressed) {
            const float vx = fp->self_vel.x + fp->x8c_kb_vel.x;
            const float vy = fp->self_vel.y + fp->x8c_kb_vel.y;
            /* from the bottom of the collision box, which is what meets the floor */
            const float ax = fp->coll_data.cur_pos.x + fp->coll_data.ecb.bottom.x;
            const float ay = fp->coll_data.cur_pos.y + fp->coll_data.ecb.bottom.y + 1.0f;
            Vec3 floor_pos, normal;
            int line = -1;
            u32 flags = 0;
            if (vy < 0.0f && mpCheckFloor(ax, ay, ax + vx * 6.0f, ay + vy * 6.0f - 2.0f, 0.0f, &floor_pos,
                                          &line, &flags, &normal, -1, -1, -1, NULL, NULL))
            {
                HSD_GObj* p1 = Player_GetEntity(0);
                int toward = 1;
                int choice = tech;
                if (p1 != NULL && p1->user_data != NULL) {
                    toward = ((Fighter*) p1->user_data)->cur_pos.x >= fp->cur_pos.x ? 1 : -1;
                }
                if (choice == 4) {
                    dummy_rng = dummy_rng * 1103515245u + 12345u;
                    choice = 1 + (int) ((dummy_rng >> 16) % 3u);
                }
                dummy_tech_side = choice == 1 ? 2 : choice == 2 ? toward : -toward;
                dummy_tech_pressed = 1;
                dummy_react_frames = 0;
                if (dummy_logs < 24) {
                    dummy_logs++;
                    OSReport("[lab] dummy tech press at frame %d: %s\n", gm_GetFrameCount(),
                             choice == 1 ? "in place" : choice == 2 ? "toward you" : "away from you");
                }
                __builtin_memset(out, 0, sizeof *out);
                out->buttons = HSD_PAD_R;
                out->r = 1.0f;
                out->lx = dummy_tech_side == 2 ? 0.0f : (float) dummy_tech_side;
                return 1;
            }
        } else if (dummy_tech_side != 0) {
            __builtin_memset(out, 0, sizeof *out);
            out->lx = dummy_tech_side == 2 ? 0.0f : (float) dummy_tech_side;
            return 1;
        }
    }
    if (hit && di != 0) {
        __builtin_memset(out, 0, sizeof *out);
        out->lx = (float) dummy_di_side;
        return 1;
    }
    if (dummy_react_frames > 0) {
        dummy_react_frames--;
        __builtin_memset(out, 0, sizeof *out);
        switch (react) {
        case 1: out->buttons = HSD_PAD_X; break;
        case 2: out->buttons = HSD_PAD_R; out->r = 1.0f; break;
        default:   /* spot dodge on the ground, air dodge in place in the air */
            out->buttons = HSD_PAD_R;
            out->r = 1.0f;
            out->ly = fp->ground_or_air == GA_Ground ? -1.0f : 0.0f;
            break;
        }
        return 1;
    }
    return 0;
}

/* 20XX TE "CPU smart DI": every CPU DIs at random (toward, away or not at all), uses survival DI
 * (up and toward the stage) against strong hits, and techs at random (in place, either way, or
 * not). State per player; it is ordinary game state, and its random numbers are its own. */
typedef struct SmartDi {
    int was_hit, side_x, side_y, tech_side, tech_pressed;
} SmartDi;
static SmartDi smart_di[6];
static unsigned int smart_rng = 0x6A09E667u;

static unsigned int smart_rand(unsigned int n)
{
    smart_rng = smart_rng * 1103515245u + 12345u;
    return (smart_rng >> 16) % n;
}

static int cpu_smart_di(Fighter* fp, MuLabInput* out)
{
    SmartDi* s = &smart_di[fp->player_idx < 6 ? fp->player_idx : 0];
    const int hitlag = fp->dmg.x195c_hitlag_frames > 0.0f;
    const int hit = hitlag || fp->x221C_b6;
    const int tumbling = fp->ground_or_air == GA_Air &&
                         (hit || fp->motion_id == ftCo_MS_DamageFall ||
                          (fp->motion_id >= ftCo_MS_DamageHi1 && fp->motion_id <= ftCo_MS_DamageFlyRoll));
    if (hit && !s->was_hit) {
        /* a new hit: survival DI for a strong one (long hitlag), otherwise a random side */
        const int toward_center = fp->cur_pos.x > 0.0f ? -1 : 1;
        if (fp->dmg.x195c_hitlag_frames >= 12.0f) {
            s->side_x = toward_center;
            s->side_y = 1;
        } else {
            const unsigned int pick = smart_rand(3);
            s->side_x = pick == 0 ? 0 : pick == 1 ? 1 : -1;
            s->side_y = 0;
        }
        s->tech_side = 0;
        s->tech_pressed = 0;
    }
    s->was_hit = hit;
    if (hitlag) {
        __builtin_memset(out, 0, sizeof *out);
        out->lx = s->side_y ? 0.7f * (float) s->side_x : (float) s->side_x;
        out->ly = s->side_y ? 0.7f : 0.0f;
        return 1;
    }
    if (!tumbling) {
        s->tech_side = 0;
        s->tech_pressed = 0;
        return 0;
    }
    if (!s->tech_pressed) {
        const float vx = fp->self_vel.x + fp->x8c_kb_vel.x;
        const float vy = fp->self_vel.y + fp->x8c_kb_vel.y;
        const float ax = fp->coll_data.cur_pos.x + fp->coll_data.ecb.bottom.x;
        const float ay = fp->coll_data.cur_pos.y + fp->coll_data.ecb.bottom.y + 1.0f;
        Vec3 floor_pos, normal;
        int line = -1;
        u32 flags = 0;
        if (vy < 0.0f && mpCheckFloor(ax, ay, ax + vx * 6.0f, ay + vy * 6.0f - 2.0f, 0.0f, &floor_pos, &line,
                                      &flags, &normal, -1, -1, -1, NULL, NULL))
        {
            const unsigned int pick = smart_rand(4);   /* in place, left, right, no tech */
            s->tech_pressed = 1;
            if (pick == 3) {
                s->tech_side = 0;
                return 0;
            }
            s->tech_side = pick == 0 ? 2 : pick == 1 ? -1 : 1;
            __builtin_memset(out, 0, sizeof *out);
            out->buttons = HSD_PAD_R;
            out->r = 1.0f;
            out->lx = s->tech_side == 2 ? 0.0f : (float) s->tech_side;
            return 1;
        }
    } else if (s->tech_side != 0) {
        __builtin_memset(out, 0, sizeof *out);
        out->lx = s->tech_side == 2 ? 0.0f : (float) s->tech_side;
        return 1;
    }
    return 0;
}

int mu_lab_input(void* fighter, MuLabInput* out)
{
    Fighter* fp = fighter;
    const int dummy = fp->player_idx == 1 && !fp->is_sub_fighter;
    if (!lab_on()) {
        /* MELEE_TE_FAKE_ATTACK (tests only): player 1 walks to player 2 and forward smashes it once
         * a second in any offline match, so CPU smart DI can be checked without a controller. */
        static int te_fake = -1;
        if (te_fake < 0) {
            te_fake = getenv("MELEE_TE_FAKE_ATTACK") != NULL;
        }
        if (te_fake && fp->player_idx == 0 && !fp->is_sub_fighter && !mu_online_active()) {
            HSD_GObj* d = Player_GetEntity(1);
            const int t = gm_GetFrameCount() % 60;
            float dx = 0.0f;
            if (d != NULL && d->user_data != NULL) {
                dx = ((Fighter*) d->user_data)->cur_pos.x - fp->cur_pos.x;
            }
            __builtin_memset(out, 0, sizeof *out);
            if (dx > 18.0f || dx < -18.0f) {
                out->lx = dx > 0.0f ? 0.6f : -0.6f;
            } else if (t < 3) {
                out->cx = dx >= 0.0f ? 1.0f : -1.0f;
            }
            return 1;
        }
        if (ftCo_IsCpuControlled(fp) && mu_te2(MU_TE2_CPU_SMART_DI)) {
            return cpu_smart_di(fp, out);
        }
        return 0;
    }
    if (dummy) {
        lab_advantage_frame(fp);
    }
    if (lab_mode == LAB_IDLE) {
        static int fake_attack = -1;
        if (fake_attack < 0) {
            fake_attack = getenv("MELEE_LAB_FAKE_ATTACK") != NULL;
        }
        if (fake_attack && fp->player_idx == 0 && !fp->is_sub_fighter) {
            /* MELEE_LAB_FAKE_ATTACK (tests only): player 1 walks to the dummy and forward smashes
             * it once a second, so dummy behavior can be checked without a controller. */
            HSD_GObj* d = Player_GetEntity(1);
            const int t = gm_GetFrameCount() % 60;
            float dx = 0.0f;
            if (d != NULL && d->user_data != NULL) {
                dx = ((Fighter*) d->user_data)->cur_pos.x - fp->cur_pos.x;
            }
            __builtin_memset(out, 0, sizeof *out);
            if (dx > 18.0f || dx < -18.0f) {
                out->lx = dx > 0.0f ? 0.6f : -0.6f;
            } else if (t < 3) {
                out->cx = dx >= 0.0f ? 1.0f : -1.0f;
            }
            return 1;
        }
        return dummy ? dummy_behavior(fp, out) : 0;
    }
    if (lab_mode == LAB_RECORDING) {
        if (fp->player_idx == 0 && !fp->is_sub_fighter) {
            __builtin_memset(out, 0, sizeof *out);
            return 1;
        }
        if (dummy) {
            const HSD_PadStatus* pad = &HSD_PadGameStatus[Player_GetPadPort(0)];
            out->lx = pad->nml_stickX;
            out->ly = pad->nml_stickY;
            out->cx = pad->nml_subStickX;
            out->cy = pad->nml_subStickY;
            out->l = pad->nml_analogL;
            out->r = pad->nml_analogR;
            out->buttons = pad->button;
            {
                /* MELEE_LAB_FAKE_INPUT (tests only): a fixed pattern instead of the controller:
                 * walk right and left in half-second turns, jumping every second and a half. */
                static int fake = -1;
                if (fake < 0) {
                    fake = getenv("MELEE_LAB_FAKE_INPUT") != NULL;
                }
                if (fake) {
                    __builtin_memset(out, 0, sizeof *out);
                    out->lx = (lab_pos % 60) < 30 ? 0.8f : -0.8f;
                    out->buttons = (lab_pos % 90) < 3 ? HSD_PAD_X : 0;
                }
            }
            lab_rec[lab_pos++] = *out;
            lab_rec_len = lab_pos;
            if (lab_pos >= LAB_MAX_FRAMES) {
                lab_mode = LAB_IDLE;
                OSReport("[lab] recording full, %d frames\n", lab_rec_len);
            }
            return 1;
        }
        return 0;
    }
    if (dummy) {   /* playing back */
        *out = lab_rec[lab_pos++];
        if (lab_pos >= lab_rec_len) {
            lab_mode = LAB_IDLE;   /* the dummy goes back to its own behavior */
            lab_loop_pending = (mu_game_options() & MU_OPTION_LAB_LOOP) != 0;
            if (lab_loop_pending && dummy_logs < 32) {
                dummy_logs++;
                OSReport("[lab] playback looped at frame %d\n", gm_GetFrameCount());
            }
        }
        return 1;
    }
    return 0;
}

/* A Training session ends (scene exit): the kept state belongs to it. */
void mu_lab_session_end(void)
{
    lab_saved = 0;
    lab_session = 0;
}

/* Snapshot exclusions: the lab's own bookkeeping must survive the loads it asks for. */
MU_EXCLUSIONS(lab,
              MU_EXCLUDE(lab_saved),
              MU_EXCLUDE(lab_session),
              MU_EXCLUDE(lab_test_fired),
              MU_EXCLUDE(lab_rec),
              MU_EXCLUDE(lab_rec_len),
              MU_EXCLUDE(lab_mode),
              MU_EXCLUDE(lab_pos),
              MU_EXCLUDE(dummy_logs),
              MU_EXCLUDE(lab_loop_pending))
