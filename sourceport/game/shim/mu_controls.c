/* Control options in the game (port/runtime/abi/mu_controls.h): tap jump off and tilt stick.
 *
 * The host hands over one byte per player slot as each match starts (gm/gmvs.c calls
 * mu_controls_match_start before the replay or the online match is set up) and the game keeps that
 * word for the whole match. The decisions themselves stay in the decomp's own functions
 * (ftCo_Jump.c, ftCo_JumpAerial.c, ftCo_AttackS4.c, ftCo_AttackHi4.c, ftCo_AttackLw4.c), each behind
 * one mu_control() test, so with the word at 0 every path is exactly the game's.
 *
 * A slot's bits apply only to a person's fighter: a human slot (Nana follows her Popo, so she shares
 * his), never a CPU, never the Pro AI seat a network match runs on this machine, and never inside a
 * TM-CE exercise, which owns its own rules. The word is constant within a match, so a rollback that
 * restores it restores the same value.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <dolphin/os.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/if/ifall.h>
#include <melee/lb/lbarchive.h>
#include <melee/sc/types.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/sislib.h>
#include <melee/ft/fighter.h>
#include <melee/ft/forward.h>
#include <melee/ft/types.h>
#include <melee/pl/player.h>
#include "mu_native.h"

int mu_online_active(void);
int mu_online_proai_port(void);   /* shim/mu_online.c: the Pro AI seat this machine plays, -1 none */
int mu_tmce_enabled(void);
unsigned int mu_control_flags(void);   /* shim/mu_entry.c: MuHostApi.control_flags, 0 from an older host */
unsigned char gm_GetCurrentGameMode(void);

int snprintf(char* buffer, __SIZE_TYPE__ size, const char* format, ...);

static unsigned int mu_controls_word;
/* Mid-match: the agreed bits a player has turned off with Start + D-pad (mu_controls_pad). Part of
 * the image, so a rollback snapshot carries it and a re-simulated frame takes the same decision. */
static unsigned char mu_controls_off[4];
static unsigned short mu_controls_msg_timer[4];   /* frames left of the "Tilt stick: off" line */
static unsigned char mu_controls_msg_bit[4];
static unsigned char mu_controls_shown[4];        /* what the line shows now: bit | 0x80 when on, 0 hidden */

/* Start + D-pad Down / Left / Right (Up is the taunt): tap jump off, tilt stick, automatic L-cancel. */
static const unsigned short toggle_pad[3] = { HSD_PAD_DPADDOWN, HSD_PAD_DPADLEFT, HSD_PAD_DPADRIGHT };
static const unsigned char toggle_bit[3] = { MU_CONTROL_TAPJUMP_OFF, MU_CONTROL_TILT_STICK, MU_CONTROL_AUTO_LCANCEL };
#define MU_CONTROL_TOGGLE_BITS (MU_CONTROL_TAPJUMP_OFF | MU_CONTROL_TILT_STICK | MU_CONTROL_AUTO_LCANCEL)

void mu_controls_match_start(void)
{
    unsigned int word = mu_control_flags();
    /* A TM-CE exercise plays by its own rules (the same test the 20XX TE options use, shim/mu_te.c). */
    if (mu_tmce_enabled() && gm_GetCurrentGameMode() == 0x2B /* GM_EVENT */) {
        word = 0;
    }
    mu_controls_word = word;
    for (int i = 0; i < 4; i++) {
        mu_controls_off[i] = 0;
        mu_controls_msg_timer[i] = 0;
        mu_controls_msg_bit[i] = 0;
        mu_controls_shown[i] = 0;
    }
    if (word != 0) {
        OSReport("controls: match starts with P1 %02X P2 %02X P3 %02X P4 %02X\n", mu_controls_slot(word, 0),
                 mu_controls_slot(word, 1), mu_controls_slot(word, 2), mu_controls_slot(word, 3));
    }
}

int mu_control(const struct Fighter* fp, unsigned int bit)
{
    int slot;
    if (mu_controls_word == 0 || fp == NULL) {
        return 0;
    }
    slot = fp->player_idx;
    if (slot < 0 || slot > 3 || !(mu_controls_slot(mu_controls_word, slot) & ~mu_controls_off[slot] & bit)) {
        return 0;
    }
    if (Player_GetPlayerSlotType(slot) != Gm_PKind_Human) {
        return 0;
    }
    if (mu_online_active() && slot == mu_online_proai_port()) {
        return 0;
    }
    return 1;
}

/* The agreed bits a slot may turn off and on again: never one the match did not agree on. */
static unsigned toggleable(int slot)
{
    if (slot < 0 || slot > 3) return 0;
    if (Player_GetPlayerSlotType(slot) != Gm_PKind_Human) return 0;
    if (mu_online_active() && slot == mu_online_proai_port()) return 0;
    return mu_controls_slot(mu_controls_word, slot) & MU_CONTROL_TOGGLE_BITS;
}

/* Fighter_procInput, after the pad (or the replay's recorded input) is in held_buttons: Start + a
 * D-pad direction, the moment the pair is first held together (either one pressed second), flips
 * that option for this player. The inputs are the ones every machine and the replay share. */
void mu_controls_pad(struct Fighter* fp)
{
    unsigned now, before, bits;
    int slot, i;
    /* Training offline: a choice changed in the settings applies at once, not from the next match (a
     * player turned tap jump off and tilt stick on inside Training and saw nothing change, 11-25).
     * Nothing else is recorded or played against anyone there; every other mode keeps its word. */
    if (!mu_online_active() && gm_GetCurrentGameMode() == 0x1C /* GM_TRAINING */) {
        const unsigned int live = mu_control_flags();
        if (live != mu_controls_word) {
            mu_controls_word = live;
            OSReport("controls: training now P1 %02X P2 %02X P3 %02X P4 %02X\n", mu_controls_slot(live, 0),
                     mu_controls_slot(live, 1), mu_controls_slot(live, 2), mu_controls_slot(live, 3));
        }
    }
    if (mu_controls_word == 0 || fp == NULL || ftCo_IsCpuControlled(fp)) return;
    slot = fp->player_idx;
    bits = toggleable(slot);
    if (bits == 0) return;
    now = fp->input.held_buttons[0];
    before = fp->input.held_buttons[1];
    if (!(now & HSD_PAD_START)) return;
    for (i = 0; i < 3; i++) {
        if (!(bits & toggle_bit[i]) || !(now & toggle_pad[i])) continue;
        if ((before & HSD_PAD_START) && (before & toggle_pad[i])) continue;
        mu_controls_off[slot] ^= toggle_bit[i];
        mu_controls_msg_bit[slot] = toggle_bit[i];
        mu_controls_msg_timer[slot] = 150;
        OSReport("controls: P%d turns %s %s\n", slot + 1,
                 toggle_bit[i] == MU_CONTROL_TAPJUMP_OFF ? "tap jump off" :
                 toggle_bit[i] == MU_CONTROL_TILT_STICK ? "tilt stick" : "auto L-cancel",
                 (mu_controls_off[slot] & toggle_bit[i]) ? "off" : "on");
    }
}

/* What the host's automatic L-cancel asks: this slot's agreed press, turned off mid-match. */
int mu_controls_lcancel_off(int slot)
{
    return slot >= 0 && slot < 4 && (mu_controls_off[slot] & MU_CONTROL_AUTO_LCANCEL) != 0;
}

/* gmvs.c pause checks: Start pressed with one of the three D-pad directions held is the toggle, not
 * a pause, for a player who has that option this match. Any other Start pauses as always. */
int mu_controls_pause_combo(int pad)
{
    HSD_PadStatus* st;
    int slot, i;
    unsigned bits;
    if (mu_controls_word == 0 || pad < 0 || pad >= 4) return 0;
    st = &HSD_PadCopyStatus[(u8) pad];
    for (slot = 0; slot < 4; slot++) {
        if (Player_GetPlayerSlotType(slot) == Gm_PKind_NA || Player_GetPadPort(slot) != pad) continue;
        bits = toggleable(slot);
        for (i = 0; i < 3; i++)
            if ((bits & toggle_bit[i]) && (st->button & toggle_pad[i])) return 1;
    }
    return 0;
}

/* ---- the line over the player's damage: "Tilt stick: off", 2.5 s, in the game's own font. */
static HSD_Text* ctl_text;
static int ctl_sub[4] = { -1, -1, -1, -1 };

static void ctl_think(HSD_GObj* gobj)
{
    int slot;
    char line[40];
    (void) gobj;
    if (ctl_text == NULL) return;
    for (slot = 0; slot < 4; slot++) {
        unsigned want = 0;
        if (ctl_sub[slot] < 0) continue;
        if (mu_controls_msg_timer[slot] != 0) {
            mu_controls_msg_timer[slot]--;
            want = mu_controls_msg_bit[slot] | ((mu_controls_off[slot] & mu_controls_msg_bit[slot]) ? 0 : 0x80);
        }
        if (want == mu_controls_shown[slot]) continue;
        mu_controls_shown[slot] = (unsigned char) want;
        if (want == 0) {
            HSD_SisLib_803A70A0(ctl_text, ctl_sub[slot], (char*) "");
            continue;
        }
        snprintf(line, sizeof line, "%s: %s",
                 (want & 0x7F) == MU_CONTROL_TAPJUMP_OFF ? "Tap jump off" :
                 (want & 0x7F) == MU_CONTROL_TILT_STICK ? "Tilt stick" : "Auto L-cancel",
                 (want & 0x80) ? "on" : "off");
        HSD_SisLib_803A70A0(ctl_text, ctl_sub[slot], (char*) "%s", line);
    }
}

static void ctl_cobj_cb(HSD_GObj* gobj, intptr_t pass)
{
    HSD_GObj_803910D8(gobj, pass);
}

/* ifStatus_802F665C, the match HUD's setup: only when some player has a toggleable option. */
void mu_controls_hud_init(void)
{
    SceneDesc* sd = NULL;
    HSD_GObj* cam;
    HSD_GObj* proc;
    HSD_CObj* cobj;
    int canvas, slot, any = 0;
    ctl_text = NULL;
    for (slot = 0; slot < 4; slot++) {
        ctl_sub[slot] = -1;
        if (toggleable(slot)) any = 1;
    }
    if (!any) return;
    lbArchive_LoadSections(*ifAll_GetArchive(), (void**) &sd, "ScInfDmg_scene_data", NULL);
    if (sd == NULL) return;
    cobj = HSD_CObjLoadDesc(DP(DP(sd->cameras)[0].desc));
    cam = GObj_Create(19, 20, 0);
    HSD_GObjObject_80390A70(cam, HSD_GObj_CameraKind, cobj);
    GObj_SetupGXLinkMax(cam, ctl_cobj_cb, 8);
    cam->gxlink_prios = 1ULL << 13;
    canvas = HSD_SisLib_803A611C(2, cam, 9, 13, 0, 13, 80, 8);
    ctl_text = HSD_SisLib_803A6754(2, canvas);
    ctl_text->default_kerning = 1;
    ctl_text->default_alignment = 1;
    ctl_text->pos_z = 0.0f;
    ctl_text->font_size.x = 0.1f;
    ctl_text->font_size.y = 0.1f;
    for (slot = 0; slot < 4; slot++) {
        float x;
        if (!toggleable(slot)) continue;
        x = ifAll_GetPlayerHUDPosition(slot)->x;
        ctl_sub[slot] = HSD_SisLib_803A6B98(ctl_text, x * 10.0f + 20.0f, 86.0f, "");   /* above the stock icon */
        HSD_SisLib_803A7548(ctl_text, ctl_sub[slot], 0.4f, 0.4f);
    }
    proc = GObj_Create(14, 15, 0);
    HSD_GObj_SetupProc(proc, ctl_think, 0);
}

/* 20XX Melee 1.03's ruleset (shim/mu_20xx103.c): on for the whole match when any slot carries the
 * bit. Offline that is this PC's choice for everyone; online the word only has it when every player
 * agreed. A TM-CE exercise has no word (mu_controls_match_start). */
int mu_20xx103_on(void)
{
    int slot;
    if (mu_controls_word == 0) return 0;
    for (slot = 0; slot < 4; slot++)
        if (mu_controls_slot(mu_controls_word, slot) & MU_CONTROL_20XX103) return 1;
    return 0;
}
