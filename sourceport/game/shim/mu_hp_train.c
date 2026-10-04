/* The 20XX Hack Pack's training displays, native: milestone 6 of run-source/rel09-hackpack/PLAN.md
 * section 3. One row per menu row and mod in run-source/rel09-hackpack/ledger_M6.md.
 *
 * Everything here is a display: a color over a fighter, the color of a hitbox bubble, which player
 * sees the L-cancel flash. None of it reads or advances the game's random generator, touches an
 * object list or changes a value the match is played from, so a match plays the same with any of it
 * on, and nothing is recorded in a replay. The choices are settings bytes (mu_hp.h,
 * MU_HP_SET_LCANCEL_OFF and up) read through mu_hp_setting(), which answers 0 unless the pack is
 * loaded, offline, in the normal game and outside replay playback; with 0 every function here
 * returns before it writes anything.
 *
 * Each function is the pack's injected code at its hook address, read from the 5.0.2 main.dol (the
 * branch at the site followed to its body), and is called from the decompiled statement that
 * address lands on, under MU_NATIVE. */
#include <dolphin/os.h>
#include <melee/ft/fighter.h>
#include <melee/ft/types.h>
#include <melee/lb/types.h>

#include "mu_hp.h"

/* ---- "L-Cancel Options", 8008D698: the flash is per player ---- */

int mu_hp_lcancel_flash_off(const struct Fighter* fp, int in_time)
{
    const int off = mu_hp_setting(MU_HP_SET_LCANCEL_OFF);
    static unsigned int said;
    int bit;
    if (off == 0 || fp->player_idx > 3) {
        return 0;
    }
    bit = fp->player_idx + (in_time ? 0 : 4);
    if (!((off >> bit) & 1)) {
        return 0;
    }
    /* One log line the first time each player's flash is held back, for a hidden run. */
    if (!(said & (1u << bit))) {
        said |= 1u << bit;
        OSReport("[20xx-hp] l-cancel flash off: player %d, %s\n", fp->player_idx + 1, in_time ? "in time" : "missed");
    }
    return 1;
}

/* ---- "Hitbox Color IDs", 80009F60 ---- */

/* The fighter hitbox being drawn (ft/ftdrawcommon.c), -1 outside that loop. The pack reads the
 * loop counter of the fighter's draw function there; what the same register holds while an item's
 * hitboxes are drawn is not known, so an item's bubbles keep the first color. */
static int hp_hit_id = -1;

void mu_hp_hitbox_id(int id)
{
    hp_hit_id = id;
}

void mu_hp_hitbox_color(unsigned char* rgba)
{
    static int changed;
    const int by_id = mu_hp_setting(MU_HP_SET_BUBBLE_FLAGS) & 1;
    const int alpha = mu_hp_setting(MU_HP_SET_HITBOX_ALPHA) ^ 0x80;
    if (!by_id && alpha == 0x80) {
        if (changed) {
            /* Switched off (or the match went online): the game's own color again. */
            rgba[0] = 0xFF;
            rgba[1] = 0x00;
            rgba[2] = 0x00;
            rgba[3] = 0x80;
            changed = 0;
        }
        return;
    }
    if (!changed) {
        OSReport("[20xx-hp] hitbox bubbles: id colors %d, alpha %d\n", by_id, alpha);
    }
    changed = 1;
    /* Id 0 red, 1 green, 2 magenta, any other orange; red for all with the option off. */
    rgba[0] = 0xFF;
    rgba[1] = 0x00;
    rgba[2] = 0x00;
    if (by_id) {
        switch (hp_hit_id) {
        case -1:
        case 0:
            break;
        case 1:
            rgba[0] = 0x00;
            rgba[1] = 0xFF;
            break;
        case 2:
            rgba[2] = 0xFF;
            break;
        default:
            rgba[1] = 0x80;
            break;
        }
    }
    /* The game draws a bubble whose alpha is 255 in its opaque pass and any other in the
     * translucent one (lbColl_80009F54), so 255 here moves it, as in the pack. */
    rgba[3] = (unsigned char) alpha;
}

/* ---- "20XX Color Overlays", 800BF550 ---- */

/* The pack's color table (8032DCAC, "Color Overlays Table"), by the menu's color value. Entry 0 is
 * "none" and is never drawn. */
static const unsigned char hp_colors[23][3] = {
    {0x00, 0x00, 0x00},   /* none */
    {0x00, 0x00, 0x00},   /* black */
    {0x80, 0x80, 0x80},   /* gray */
    {0xFF, 0xFF, 0xFF},   /* white */
    {0xFF, 0x00, 0x00},   /* red */
    {0xFA, 0x80, 0x72},   /* salmon */
    {0xFF, 0x80, 0x00},   /* orange */
    {0xF0, 0xE6, 0x8C},   /* khaki */
    {0xFF, 0xD7, 0x00},   /* gold */
    {0xFF, 0xFF, 0x00},   /* yellow */
    {0xC0, 0xFF, 0x3E},   /* olivedrab */
    {0x00, 0xFF, 0x00},   /* green */
    {0x22, 0x8B, 0x22},   /* forest green */
    {0x00, 0xFF, 0xFF},   /* cyan */
    {0x7F, 0xFF, 0xD4},   /* aqua marine */
    {0x46, 0x82, 0xB4},   /* steel blue */
    {0x00, 0x00, 0xFF},   /* blue */
    {0x80, 0x00, 0x80},   /* purple */
    {0xFF, 0x00, 0xFF},   /* magenta */
    {0xFF, 0x69, 0xB4},   /* hot pink */
    {0xFF, 0xC0, 0xCB},   /* pink */
    {0xD2, 0xB4, 0x8C},   /* tan */
    {0x96, 0x4B, 0x00},   /* brown */
};

/* The wavedash stick window, the pack's defaults (803FA4C4 X-MAX, 803FA4C8 X-MIN, 803FA2EC Y-MAX,
 * 803FA2E8 Y-MIN). Its four menu rows are not wired, so the window is fixed at these. */
#define HP_WD_X_MAX 0.95F
#define HP_WD_X_MIN 0.95F
#define HP_WD_Y_MAX (-0.2875F)
#define HP_WD_Y_MIN (-0.2875F)

/* The overlay's flag byte as the pack reads it (fp + 0x504). The game uses its top bits (0x80 the
 * color is on); the pack keeps its own state in the two low bits, which the game never reads and
 * its own reset (lb_80014498) leaves alone:
 *   0x80  a color for as long as its condition holds (hitlag, hitstun)
 *   0x82  a color for this frame only: dropped at the next draw unless the condition still holds
 *   0x83  a color that stays until the game resets the overlay (the next action that does) */
static int hp_ov_flags(const ColorOverlay* co)
{
    return co->x7C_color_enable << 7 | co->x7C_flag2 << 6 | co->x7C_light_enable << 5 | co->x7C_flag4 << 4 |
           co->x7C_flag5 << 3 | co->x7C_flag6 << 2 | co->x7C_flag7 << 1 | co->x7C_flag8;
}

static void hp_ov_set_flags(ColorOverlay* co, int flags)
{
    co->x7C_color_enable = (flags & 0x80) != 0;
    co->x7C_flag2 = (flags & 0x40) != 0;
    co->x7C_light_enable = (flags & 0x20) != 0;
    co->x7C_flag4 = (flags & 0x10) != 0;
    co->x7C_flag5 = (flags & 0x08) != 0;
    co->x7C_flag6 = (flags & 0x04) != 0;
    co->x7C_flag7 = (flags & 0x02) != 0;
    co->x7C_flag8 = (flags & 0x01) != 0;
}

static int hp_ov_color(int which)
{
    const int color = mu_hp_setting(MU_HP_SET_OVERLAY_COLOR + (unsigned int) which);
    return color >= 1 && color <= 22 ? color : 0;
}

/* One log line the first time each overlay shows, so a hidden run can tell they work. */
static void hp_ov_report(int which, const Fighter* fp, int color)
{
    static const char* const names[MU_HP_OV_COUNT] = {
        "hitlag", "hitstun", "auto-cancel", "IASA", "missed smash turn", "Nana desynced", "wavedash timing",
        "wavedash window", "wavedash timing and window",
    };
    static unsigned int seen;
    if (!(seen & (1u << which))) {
        seen |= 1u << which;
        OSReport("[20xx-hp] overlay %s: player %d, color %d, action %d\n", names[which], fp->player_idx + 1, color,
                 (int) fp->motion_id);
    }
}

void mu_hp_overlay(struct Fighter* fp)
{
    const int players = mu_hp_setting(MU_HP_SET_OVERLAY_PLAYERS);
    ColorOverlay* co;
    unsigned char* note;
    int action, alpha, flags, mode, color, pick, prev;
    int which = 0;

    if (players == 0 || fp->player_idx > 3 || !(players & (1 << fp->player_idx))) {
        return;
    }
    co = &fp->x488;
    /* The three unused bytes after the flag byte, where the pack keeps its notes too (fp + 0x505,
     * 0x506): note[1] the color of a 0x83 overlay, note[2] the action at the last draw, note[3]
     * the action before that one. They are inside the fighter, so a savestate carries them. */
    note = (unsigned char*) (&co->x78_light_rot_yz + 1);
    action = (int) fp->motion_id;
    alpha = mu_hp_setting(MU_HP_SET_OVERLAY_ALPHA) ^ 0xFF;
    flags = hp_ov_flags(co);
    /* The pack stores the action at the end of every draw and compares the next draw's against
     * it. This port can draw one game frame more than once, so the action before the current one
     * is kept apart; the stored one would already be the current action on the second draw. */
    if (note[2] != (unsigned char) action) {
        note[3] = note[2];
        note[2] = (unsigned char) action;
    }
    prev = note[3];

    if (flags == 0x82) {
        flags = 0;
        hp_ov_set_flags(co, 0);
    }
    if (flags == 0x83) {
        mode = 0x83;
        color = note[1];
        goto apply;
    }

    /* Hitlag (the one hit) and hitstun. The last frame of either shows at half the alpha. */
    if (fp->x221C_b6) {
        const int defender = fp->x221A_b0 || fp->x221A_b1 || fp->allow_sdi || fp->x221A_b3 || fp->fall_fast ||
                             fp->x221A_b5 || fp->x221A_b6 || fp->x221A_b7;
        const float left = defender ? fp->dmg.x195c_hitlag_frames : *(const float*) &fp->mv;
        which = defender ? MU_HP_OV_HITLAG : MU_HP_OV_HITSTUN;
        pick = hp_ov_color(which);
        if (pick != 0) {
            color = pick;
            if (left == 1.0F) {
                alpha /= 2;
                mode = 0x82;
            } else {
                mode = 0x80;
            }
            goto apply;
        }
    }

    /* IASA: a landing past its lag (Landing, 42), or a jab (44 to 46), a ground attack or an
     * aerial (50 to 69) whose interrupt flag is up. */
    which = MU_HP_OV_IASA;
    pick = hp_ov_color(which);
    if (pick != 0) {
        int on = 0;
        if (action == 42) {
            on = !(fp->cur_anim_frame < fp->co_attrs.normal_landing_lag);
        } else if ((action >= 44 && action < 47) || (action >= 50 && action < 70)) {
            on = fp->allow_interrupt;
        }
        if (on) {
            color = pick;
            mode = 0x82;
            goto apply;
        }
    }

    /* Auto-cancel: an aerial (65 to 69; Mr. Game & Watch, fighter kind 24, also 347 to 349) whose
     * landing would not go into its landing lag (the first script variable is 0). */
    which = MU_HP_OV_AUTOCANCEL;
    pick = hp_ov_color(which);
    if (pick != 0 &&
        (((int) fp->kind == 24 && action >= 347 && action <= 349) || (action >= 65 && action <= 69)) &&
        fp->cmd_vars[0] == 0)
    {
        color = pick;
        mode = 0x82;
        goto apply;
    }

    /* Missed smash turn: frame 2 of Turn (18) with the slow turn still counting. */
    which = MU_HP_OV_SMASH_TURN;
    pick = hp_ov_color(which);
    if (pick != 0 && action == 18 && fp->cur_anim_frame >= 2.0F && fp->cur_anim_frame < 2.0078125F &&
        fp->mv.co.turn.x8 != 0.0F && fp->mv.co.turn.frames_to_turn != 0.0F)
    {
        note[1] = (unsigned char) pick;
        color = pick;
        mode = 0x83;
        goto apply;
    }

    /* Nana whose CPU flag (cpu + 0xFA, lowest bit) is down. */
    which = MU_HP_OV_NANA;
    pick = hp_ov_color(which);
    if (pick != 0 && fp->is_sub_fighter && !fp->cpu.xFA_b7) {
        color = pick;
        mode = 0x82;
        goto apply;
    }

    /* Wavedash: the first frame of the special landing (43) when it did not follow the helpless
     * fall (35). Timing color when the timer at fp + 0x680 is 0; window color when the stick is
     * inside the window on either side; the third color when both hold and it is set. */
    if (action == 43 && prev != 35 && fp->cur_anim_frame == 0.0F) {
        const float x = fp->input.lstick[0].x, y = fp->input.lstick[0].y;
        int in_window = 0;
        pick = 255;
        which = MU_HP_OV_WD_TIMING;
        if (fp->x680 == 0) {
            pick = hp_ov_color(MU_HP_OV_WD_TIMING);
        }
        if (!(y > HP_WD_Y_MAX) && !(y < HP_WD_Y_MIN)) {
            if (!(x > HP_WD_X_MAX) && !(x < HP_WD_X_MIN)) {
                in_window = 1;
            } else if (!(x > -HP_WD_X_MIN) && !(x < -HP_WD_X_MAX)) {
                in_window = 1;
            }
        }
        if (in_window) {
            if (pick == 255) {
                pick = hp_ov_color(MU_HP_OV_WD_WINDOW);
                which = MU_HP_OV_WD_WINDOW;
            } else if (hp_ov_color(MU_HP_OV_WD_BOTH) != 0) {
                pick = hp_ov_color(MU_HP_OV_WD_BOTH);
                which = MU_HP_OV_WD_BOTH;
            }
        }
        if (pick != 255 && pick != 0) {
            note[1] = (unsigned char) pick;
            color = pick;
            mode = 0x83;
            goto apply;
        }
    }
    /* The pack's 22 "action state overlay" rows follow here in its code; they are not wired, and
     * at the pack's default (no action chosen) they do nothing. */
    /* Nothing to show (80195FA4): once the game has switched the color off, the pack's own state
     * goes too. Without this a 0x83 whose top bit the game cleared would come back as the pack's
     * color the next time the game turns its own color on. The pack zeroes the whole byte; only
     * its two bits are dropped here, the game's other flags stay the game's. */
    if (!co->x7C_color_enable && (co->x7C_flag7 || co->x7C_flag8)) {
        co->x7C_flag7 = 0;
        co->x7C_flag8 = 0;
    }
    return;

apply:
    if (color < 1 || color > 22) {
        return;
    }
    if (mode != 0x83 || flags != 0x83) {
        hp_ov_report(which, fp, color);
    }
    hp_ov_set_flags(co, mode);
    co->x2C_hex.r = hp_colors[color][0];
    co->x2C_hex.g = hp_colors[color][1];
    co->x2C_hex.b = hp_colors[color][2];
    co->x2C_hex.a = (u8) alpha;
    /* As the pack: the fighter's other overlay slot gives way (its color animation id, fp + 0x430). */
    fp->x408.x28_colanim.ptr = NULL;
}
