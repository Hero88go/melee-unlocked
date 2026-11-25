/* Native Slippi online character select screen (the online CSS, major 8 state 0).
 *
 * Native form of the Online/Menus/CSS codes of GALE01r2.ini that are not tied to mncharsel.c's
 * file-static state: SceneLoadCSS (the CSS data table and slpCSS.dat), FetchMatchInfo (B3 once
 * per cursor think), HandleInputsOnCSS (the lock-in / search / cancel machine and its host
 * commands), LoadCSSText (user display, status text, spinners, error lines, mode title),
 * SkipReturnToCssSound and the unselect / colour-change guards. The B5/B4 senders and the
 * r13-0x5018 callback body are shared with name entry and the SSS (mu_slippi_sss.c).
 * mncharsel.c calls in from #ifdef MU_NATIVE blocks; the Teams button, cursor and token colour
 * and CSP overrides live there because they touch its statics.
 *
 * Nothing here runs unless mu_slippi_menus_enabled() and the online CSS predicate hold.
 * Chat (SlippiCSS.dat) lives in mu_slippi_chat.c: it reads this table's MSRB and slpCSS and sets
 * the chat-window byte through the accessors at the end of this file.
 */
#include <string.h>
#include <dolphin/dvd.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_1A36.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gmvsmelee.h>
#include <melee/gm/types.h>
#include <melee/lb/lbarchive.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/mn/types.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/sislib.h>
#include <mu_native.h>
#include <mu_test_env.h>
#if MU_SQUAD
/* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
#include "mu_squad.h"
/* shim/mu_squad_css.c (included by mncharsel.c): the online door's coin pick and lineup. */
int gmSquad_CssPreview(u8* coin_ck, u8* coin_color, GmSquadMember out[GMSQUAD_SIZE]);
#endif
#include <mu_ranked.h>
#ifdef MELEE_DOUBLES
#include <mu_doubles_game.h>
#endif

extern char mnNameNew_CurrentNameText[];

enum {
    SB_RAND = -2,   /* stage behavior: random */
    SB_NOTSEL = -1, /* stage behavior: use the opponent's */
    DISCONNECT_HOLD_DELAY = 0x30,
    SPINNER_TRANSITION_FRAMES = 15,
    FRAME_MAX = 2 * SPINNER_TRANSITION_FRAMES,
};

/* slpCSS symbol (Online.s "slpCSS Symbol Structure"). */
typedef struct SlpCss {
    DISC_PTR(void) chat_select;
    DISC_PTR(void) chat_msg;
    DISC_PTR(HSD_MatAnimJoint) mode;
    DISC_PTR(void) connect_help;
} DISC_STRUCT SlpCss;

/* The CSS data table (CSSDT). A new, zeroed one per CSS scene load, as SceneLoadCSS allocates. */
static struct {
    MuMatchState msrb; /* CSSDT_MSRB_ADDR: only the per-think B3 writes it */
    SlpCss* slpcss;
    HSD_Text* text;
    u8 spinner[3];
    u16 frame_counter;
    u8 prev_lock_in;
    u8 prev_connected;
    u8 z_hold_timer;
    u8 chat_window_opened; /* set by the chat window (mu_slippi_chat.c) */
    u8 team_idx;
    u8 doubles_searched; /* the 4-peer Doubles search went out on this CSS visit */
    u8 doubles_connected; /* ... and its match room connected on this visit */
} dt;

/* mncharsel.c state the codes read through r13 (addresses of its statics). */
static CSSData** css_ptr;         /* -0x49F0 mnCharSel_804D6CB0 */
static u8* char_chosen;           /* -0x49A9 mnCharSel_804D6CF7 */
static s8* ctrl_port;             /* -0x49B0 mnCharSel_804D6CF0 */
static u8* scene_request;         /* -0x49AA mnCharSel_804D6CF6 */
static s8* name_entry_port;       /* -0x49A7 mnCharSel_804D6CF9 */
static HSD_JObj** single_menu;    /* -0x49E0 mnCharSel_804D6CC0 */

/* LoadCSSText's user display (FG_UserDisplay LDB). */
static HSD_Text* user_text;
static u8 user_display_mode;

static const GXColor color_white = { 0xFF, 0xFF, 0xFF, 0xFF };
static const GXColor color_gray = { 0x8E, 0x91, 0x96, 0xFF };
static const GXColor color_red = { 0xFF, 0x00, 0x00, 0xFF };
static const GXColor spinner_done_color = { 0x33, 0xFF, 0x2F, 0xFF };
static const GXColor spinner_wait_color = { 0x3C, 0xBC, 0xFF, 0xFF };

static const char spinner_str[2][3] = { { (char) 0x81, 0x7B, 0 }, { (char) 0x81, 0x7E, 0 } };
static const char spinner_done_str[3] = { (char) 0x81, 0x7C, 0 };

/* The Friends lobby's 4-door select runs in the same online major but is Melee's VS select
 * (mu_friends4_css.inc), never this 1-door one. */
int mu_lobby_css_friends4(void);   /* shim/mu_slippi_menu.c */

int mu_slippi_css_online(void)
{
    return mu_slippi_menus_enabled() && mu_slippi_on_online_css() && !mu_lobby_css();
}

/* YouGame Doubles on the 4-peer route: the home's match on this Teams character select. The room owns
 * the team (the toggle is locked), there is no code entry and no stage select: the search starts on
 * entry and every game locks in a random stage, which the room's seed turns into a doubles stage. */
static int doubles_teams(void)
{
#ifdef MELEE_DOUBLES
    return mu_slippi_state()->mode == MU_SLP_MODE_TEAMS && mu_doubles_teams_css();
#else
    return 0;
#endif
}

/* An arena room's free-for-all (host slippi_arena): the room's next game is a free-for-all on this
 * Teams character select. No team toggle (every fighter is their own team), and Start locks in a
 * random stage (the host picks the stage from the doubles list, the same on every fighter). Read
 * from the host's arena reply (arena_pairing below) on entry and every frame of the select. */
static int arena_ffa_css;
static u32 arena_pairing(int* test_room, int* in_room);

int mu_slippi_css_teams(void)
{
    return mu_slippi_css_online() && mu_slippi_state()->mode == MU_SLP_MODE_TEAMS && !arena_ffa_css;
}

int mu_slippi_css_arena_ffa(void)
{
    return arena_ffa_css;
}

int mu_slippi_css_local_ready(void)
{
    return dt.msrb.local_ready;
}

int mu_slippi_css_team_idx(void)
{
    return dt.team_idx;
}

/* InitTeamToggleButton: the data table and the injection's own byte (kept across CSS loads). */
void mu_slippi_css_set_team_idx(int team)
{
    if (doubles_teams()) {
        return;   /* the room's team; the toggle stays where the room put it */
    }
    dt.team_idx = (u8) team;
    mu_slippi_state()->team_idx = (u8) team;
}

/* 2 of every 3 bytes of the name-entry text: FMTB_OPP_CONNECT_CODE and the "--//--" template. */
static void code_sjis18(u8 out[18])
{
    const u8* src = (const u8*) mnNameNew_CurrentNameText;
    int i;
    for (i = 0; i < 9; i++) {
        out[i * 2] = src[i * 3];
        out[i * 2 + 1] = src[i * 3 + 1];
    }
}

/* ---- SceneLoadCSS (mnCharSel_Scene_OnEnter, after css_models) ---- */

void mu_slippi_css_enter(CSSData** css, u8* chosen, s8* port, u8* request, s8* name_port,
                         HSD_JObj** menu_root)
{
    MuSlippiMenuState* st = mu_slippi_state();
    css_ptr = css;
    char_chosen = chosen;
    ctrl_port = port;
    scene_request = request;
    name_entry_port = name_port;
    single_menu = menu_root;

    if (st->mode == MU_SLP_MODE_RANKED) {
        u8 screen = 3; /* E6: a new Ranked CSS visit, one mpRank read for this screen. */
        mu_slippi_cmd(0xE6, &screen, 1, NULL, 0, NULL);
    }

    /* FN_InitBuffers: nothing to allocate natively. CSS data table zeroed. */
    memset(&dt, 0, sizeof dt);
    /* The room's word on a free-for-all before the team toggle is set up. */
    { int t = 0, i = 0; arena_pairing(&t, &i); }

    /* The injection byte starts at 1 (red) and only ever holds 1-3. */
    if (st->team_idx == 0) {
        st->team_idx = 1;
    }
#ifdef MELEE_DOUBLES
    if (doubles_teams() && mu_doubles_teams_team() >= 0) {
        st->team_idx = (u8) (mu_doubles_teams_team() + 1);   /* red 1 = team 0, blue 2 = team 1 */
    }
#endif
    if (st->mode == MU_SLP_MODE_TEAMS) {
        dt.team_idx = st->team_idx;
    }

    /* SlippiCSS_gui.dat (Slippi 3.7.0's name for slpCSS.dat), symbol slpCSS. The host's
     * system-file layer serves it; a host that still serves only the old name gets that file. Without
     * either the mode title keeps the CSS's own texture instead of taking the game down. */
    {
        const char* name = DVDConvertPathToEntrynum("SlippiCSS_gui.dat") >= 0 ? "SlippiCSS_gui.dat"
                           : DVDConvertPathToEntrynum("slpCSS.dat") >= 0    ? "slpCSS.dat"
                                                                            : NULL;
        if (name != NULL) {
            HSD_Archive* ar = lbArchive_LoadArchive(name);
            dt.slpcss = ar != NULL ? HSD_ArchiveGetPublicAddress(ar, "slpCSS") : NULL;
        }
    }
    mu_slippi_sss_set_slpcss(dt.slpcss);   /* name entry's connect help */
}

/* ---- FetchMatchInfo (mnCharSel_CursorThink, inside the no-scene-request branch) ---- */

/* An arena room (host slippi_arena) counts the new pairings it starts; 0 outside a room. */
int mu_online_abi_command(unsigned int command, const unsigned char* payload, unsigned int size,
                          unsigned char* response, unsigned int capacity, unsigned int* response_size);
#define CMD_ARENA_PAIRING 0xDD /* upstream 0xED: MU_PICK_PREVIEW_COMMAND in this build */
static u32 arena_pairing(int* test_room, int* in_room)
{
    static unsigned char reply[4096];   /* the host's minimum response capacity */
    unsigned int got = 0;
    *test_room = 0;
    *in_room = 0;
    arena_ffa_css = 0;
    if (mu_online_abi_command(CMD_ARENA_PAIRING, NULL, 0, reply, sizeof reply, &got) != 0 || got < 4) {
        return 0;
    }
    *test_room = got >= 5 && (reply[4] & 1) != 0;   /* the host locked in a random stage itself */
    *in_room = got >= 5 && (reply[4] & 2) != 0;     /* the player is in a room now (the count outlives it) */
    arena_ffa_css = got >= 5 && (reply[4] & 4) != 0; /* the room's next game is a free-for-all */
    return (u32) reply[0] << 24 | (u32) reply[1] << 16 | (u32) reply[2] << 8 | reply[3];
}

void mu_slippi_css_poll(void)
{
    int test_room, in_room;
    mu_slippi_load_match_state(NULL);
    dt.msrb = *mu_slippi_match_state();
    /* The room's word on a free-for-all, every frame (the toggle and Start read it). */
    arena_pairing(&test_room, &in_room);
}

#define CMD_CSS_FRAME 0xDA /* upstream 0xEC: MU_SQUAD_COMMAND in this build */
/* The menu camera (mn_8022BA1C, every menu frame): the eye before the C-stick turns it, then the
 * camera it turned to. The character select frame hands both to the host, which turns the easter
 * egg's faces with the picture. Reads only: the camera is not changed. */
static struct {
    int ok;                /* a camera after the turn this frame (and its eye before it) */
    float cam[13];         /* eye before, eye, interest, up, vertical field of view (degrees) */
} mu_menu_cam;

/* The menu camera's eye before the C-stick turned it this frame (mu_menu_camera above). */
int mu_menu_camera_neutral_eye(float out[3])
{
    out[0] = mu_menu_cam.cam[0];
    out[1] = mu_menu_cam.cam[1];
    out[2] = mu_menu_cam.cam[2];
    return out[0] != 0.0f || out[1] != 0.0f || out[2] != 0.0f;
}

void mu_menu_camera(HSD_CObj* cobj, int tilted)
{
    Vec3 v;
    if (cobj == NULL) {
        return;
    }
    if (!tilted) {
        HSD_CObjGetEyePosition(cobj, &v);
        mu_menu_cam.cam[0] = v.x;
        mu_menu_cam.cam[1] = v.y;
        mu_menu_cam.cam[2] = v.z;
        mu_menu_cam.ok = 0;
        return;
    }
    HSD_CObjGetEyePosition(cobj, &v);
    mu_menu_cam.cam[3] = v.x;
    mu_menu_cam.cam[4] = v.y;
    mu_menu_cam.cam[5] = v.z;
    HSD_CObjGetInterest(cobj, &v);
    mu_menu_cam.cam[6] = v.x;
    mu_menu_cam.cam[7] = v.y;
    mu_menu_cam.cam[8] = v.z;
    HSD_CObjGetUpVector(cobj, &v);   /* 0, 1, 0 when the camera has none */
    mu_menu_cam.cam[9] = v.x;
    mu_menu_cam.cam[10] = v.y;
    mu_menu_cam.cam[11] = v.z;
    mu_menu_cam.cam[12] = HSD_CObjGetFov(cobj);   /* 0: not a perspective camera */
    mu_menu_cam.ok = mu_menu_cam.cam[12] > 0.0f;
}

/* mnCharSel_Scene_OnFrame, every mode's character select, offline and online: tells the host a
 * character select is on screen (its easter egg lives only there, drawn by the host, never in a
 * match), with the menu camera when there is one, and plays the menu sound the host asks for. Local
 * only: nothing in the match changes. */
void mu_css_frame(void)
{
    static unsigned char reply[4096];   /* the host's minimum response capacity */
    unsigned int got = 0;
    const int with_camera = mu_menu_cam.ok;
    if (mu_online_abi_command(CMD_CSS_FRAME, with_camera ? (const unsigned char*) mu_menu_cam.cam : NULL,
                              with_camera ? (unsigned int) sizeof mu_menu_cam.cam : 0, reply, sizeof reply, &got) == 0 &&
        got >= 1 && reply[0] != 0) {
        lbAudioAx_80024030(reply[0]);
    }
}

#define CMD_ZELDA_AS_SHEIK 0xDE /* upstream 0xE6: CMD_YOUGAME_FRIENDS_MENU in this build */
/* Player_SetPlayerCharacter and the rogueMelee run: a human's Zelda starts as Sheik when the player
 * asked for it. The host answers 0 online and during replay playback. */
int mu_human_zelda_as_sheik(void)
{
    static unsigned char reply[4096];   /* the host's minimum response capacity */
    unsigned int got = 0;
    return mu_online_abi_command(CMD_ZELDA_AS_SHEIK, NULL, 0, reply, sizeof reply, &got) == 0 && got >= 1 && reply[0] != 0;
}

#define CMD_BUTTON_LABEL 0xDF
/* The key a keyboard player presses for a GameCube button (HSD_PAD_Z, HSD_PAD_B), for the prompts
 * below: the keyboard's Z key is the pad's A and E is its Z, so "Hold Z to disconnect" sent keyboard
 * players to the wrong key (11-18). The host answers from the live key bindings, only for a port the
 * keyboard feeds; 0 leaves `out` alone and the prompt keeps the GameCube name. */
int mu_host_button_label(int port, unsigned button_mask, char* out, int n)
{
    static unsigned char reply[4096];   /* the host's minimum response capacity */
    unsigned char payload[3];
    unsigned int got = 0;
    if (out == NULL || n < 2 || port < 0 || port > 3) return 0;
    payload[0] = (unsigned char) port;
    payload[1] = (unsigned char) (button_mask >> 8);
    payload[2] = (unsigned char) button_mask;
    if (mu_online_abi_command(CMD_BUTTON_LABEL, payload, sizeof payload, reply, sizeof reply, &got) != 0 ||
        got == 0 || got >= (unsigned int) n)
        return 0;
    memcpy(out, reply, got);
    out[got] = 0;
    return 1;
}

#define CMD_ARENA_HAND 0xDB /* upstream 0xEF: CMD_YOUGAME_MENU in this build */
/* mnCharSel_CursorThink, after the hand moved: the online CSS's own player's hand goes to the host
 * (x, y as the screen's units, 0.1 steps) when it moved, for an arena room's side panels: a hand
 * pushed against the left or right edge goes into that panel. The fifth byte is the select's mode
 * (MU_SLP_MODE_*): Ranked and Unranked show no rooms panel. */
void mu_slippi_css_hand(int port, float x, float y)
{
    static int last = 0x7FFFFFFF;
    static int last_mode = -1;
    static unsigned char reply[4096];
    unsigned char payload[5];
    int mode;
    unsigned int got = 0;
    int ix, iy, packed, moved, lobby_select;

    ix = (int) (x * 10.0f);
    iy = (int) (y * 10.0f);
    packed = (ix & 0xFFFF) << 16 | (iy & 0xFFFF);
    moved = 0;
    if (port >= 0 && port < 4) {
        /* Every hand on every character select, when it moved: the easter egg's face can be taken
         * hold of. The host only looks at it while the face is out. */
        static int last_any[4] = { 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF };
        if (packed != last_any[port]) {
            unsigned char any[5];
            moved = 1;
            last_any[port] = packed;
            any[0] = (unsigned char) port;
            any[1] = (unsigned char) (ix >> 8);
            any[2] = (unsigned char) ix;
            any[3] = (unsigned char) (iy >> 8);
            any[4] = (unsigned char) iy;
            /* The character select frame command with five bytes: a hand (the port, x, y). */
            mu_online_abi_command(CMD_CSS_FRAME, any, sizeof any, reply, sizeof reply, &got);
            got = 0;
        }
    }
    /* The local player's hand: the controlled port's, or the one that moves. A controller in port 2
     * drives the online select's cursor while the controlled port stays 0, so the check on the port
     * alone never reported that player's hand and no room panel ever showed for them (0.9.201). */
    lobby_select = mu_lobby_css_friends4();
    if (lobby_select) {
        if (!moved) return;   /* the Friends lobby's 4-door select: whichever hand moves */
    } else if (!mu_slippi_css_online() || ctrl_port == NULL || (port != *ctrl_port && !moved)) {
        return;
    }
    mode = lobby_select ? 0x40 : mu_slippi_state()->mode;   /* 0x40: slippi::arena::kCssFriendsLobby */
    if (packed == last && mode == last_mode) {   /* a new select's mode goes out even where the hand stayed */
        return;
    }
    last = packed;
    last_mode = mode;
    payload[0] = (unsigned char) (ix >> 8);
    payload[1] = (unsigned char) ix;
    payload[2] = (unsigned char) (iy >> 8);
    payload[3] = (unsigned char) iy;
    payload[4] = (unsigned char) mode;
    mu_online_abi_command(CMD_ARENA_HAND, payload, sizeof payload, reply, sizeof reply, &got);
}

/* ---- HandleInputsOnCSS helpers ---- */

/* FN_TX_LOCK_IN and FN_TX_FIND_MATCH are shared with name entry and the SSS (mu_slippi_sss.c),
 * so every B5/B4 payload comes from one place. */
static void tx_lock_in(int sb)
{
    mu_slippi_sss_lock_in(sb);
}

/* FN_LOCK_IN_AND_SEARCH. A lock-in Squad Strike refused (fewer than four fighters, a lineup the
 * host rejected) starts no search: its reason stays on screen and the row stays editable. */
static void lock_in_and_search(int sb)
{
    if (mu_slippi_sss_lock_in(sb)) mu_slippi_sss_find_match();
}

/* FN_LOAD_CODE_ENTRY */
static void load_code_entry(void)
{
    MuSlippiMenuState* st = mu_slippi_state();
    st->code_entry = 1;
    st->callback = MU_SLP_CB_CODE_LOCK_IN;
    *name_entry_port = *ctrl_port;
    *scene_request = 4;
}

/* The stage the loser already picked: the asm reads the u16 at +0x1E of the VS data's match struct
 * (gmMainLib_804D3EE0 + 1424 + 8), in the console's byte order. */
static int picked_stage(void)
{
    const u8* b = (const u8*) &gmVsMelee_GetVsData()->start.rules;
    return (b[0x1E] << 8) | b[0x1F];
}

static int test_skip_code_entry;   /* tests only: MELEE_TEST_ARENA_PRESS_START below */

/* Shared by the hint and input path so the screen never advertises a dead Z. */
static int mu_slippi_practice_available(void)
{
#if defined(YOUGAME_RANKED_BRANCH) && YOUGAME_RANKED_BRANCH
    return mu_slippi_state()->mode == MU_SLP_MODE_RANKED && mu_slippi_ranked_available() &&
           dt.msrb.connection_state == MU_SLP_MM_IDLE && !mu_online_p2p_menus() &&
           !dt.chat_window_opened && char_chosen && *char_chosen && gm_801A4BB8() &&
           mu_ranked_practice_allowed();
#else
    return 0;
#endif
}

#if MU_SQUAD
/* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
 * START with fewer than four fighters. Between picks the coin is back in the hand, so the vanilla
 * CSS clears its ready flag and the START paths below stop before lock-in, which is where the
 * refusal is explained: explain it here instead. */
static void squad_start_refused(void)
{
    GmSquadMember lineup[GMSQUAD_SIZE];
    u8 ck, color;
    int count;
    if (!gmSquad_OnlineLive(mu_slippi_state()->mode)) return;
    count = gmSquad_CssPreview(&ck, &color, lineup);
    if (count == GMSQUAD_SIZE) return;
    gmSquad_OnlineNeedLineup(count);
    lbAudioAx_80024030(3);
}
#endif

/* HandleInputsOnCSS at fn_80262F44's `mnCharSel_804D6CF2 == 0 && START` test (only called when
 * mnCharSel_804D6CF2 == 0). Returns 1 when both players are ready: the caller continues into the
 * vanilla START path. */
/* Tap jump off and tilt stick (controls_lines, below): the host's line, and the answer command. */
#define CMD_CONTROLS 0xC9
enum { CTL_NONE, CTL_ASKED, CTL_ALLOWED_OWN, CTL_WAITING, CTL_AGREED, CTL_DEFAULTS };
static u8 ctl_line[8];
static int st_controls = -1;

static void controls_send(u8 action, u8 port)
{
    u8 q[2];
    unsigned int got = 0;
    q[0] = action;
    q[1] = port;
    if (mu_slippi_cmd(CMD_CONTROLS, q, 2, ctl_line, sizeof ctl_line, &got) != 0 || got != sizeof ctl_line) {
        memset(ctl_line, 0, sizeof ctl_line);
    }
}

int mu_slippi_css_inputs(unsigned int trigger)
{
    MuSlippiMenuState* st = mu_slippi_state();
    const MuMatchState* m = &dt.msrb;
    u8 prev, cur;

    mu_slippi_sss_report_pick(*char_chosen != 0);   /* an arena room's portraits */

    /* Tests (MELEE_TEST_ARENA_PRESS_START=1, an arena test room that does not lock its fighters in):
     * a connected fighter not locked in presses Start once a second, as a player locks in; a member
     * not connected (watching the room's match) presses Start every five seconds, as a waiting player
     * does. The connect-code entry that press opens is skipped (test_skip_code_entry). */
    {
        static int press = -1;
        static u32 tick;
        if (press < 0) {
            press = mu_test_getenv("MELEE_TEST_ARENA_PRESS_START") != NULL;
        }
        tick++;
        if (press && (st->mode == MU_SLP_MODE_DIRECT || st->mode == MU_SLP_MODE_TEAMS)) {
            if (m->connection_state == MU_SLP_MM_CONNECTION_SUCCESS && m->local_ready == 0 && tick % 60 == 0) {
                trigger |= HSD_PAD_START;
            } else if (m->connection_state == MU_SLP_MM_IDLE && tick % 300 == 0) {
                trigger |= HSD_PAD_START;
                test_skip_code_entry = 1;
                OSReport("arena test: Start pressed while not connected\n");
            }
        }
    }

    /* Sounds: lock-in 1 -> 0 "back"; ANY -> ERROR "error"; CONNECTED -> ANY "back". */
    prev = dt.prev_lock_in;
    cur = m->local_ready;
    dt.prev_lock_in = cur;
    if (prev == 1 && cur == 0) {
        lbAudioAx_80024030(0);
    } else {
        prev = dt.prev_connected;
        cur = m->connection_state;
        dt.prev_connected = cur;
        if (prev != MU_SLP_MM_ERROR && cur == MU_SLP_MM_ERROR) {
            lbAudioAx_80024030(3);
        } else if (prev == MU_SLP_MM_CONNECTION_SUCCESS && cur != MU_SLP_MM_CONNECTION_SUCCESS) {
            lbAudioAx_80024030(0);
        }
    }

    /* Y is unused in searching/error states. Read only the existing mapped pad edge. */
    if ((trigger & HSD_PAD_Y) && !(trigger & HSD_PAD_Z) &&
        (m->connection_state == MU_SLP_MM_MATCHMAKING || m->connection_state == MU_SLP_MM_ERROR)) {
        u8 action = 1;
        mu_slippi_cmd(0xE7, &action, 1, NULL, 0, NULL);
        return 0;
    }

    /* Tap jump off and tilt stick: X allows the opponent's options, Y declines them, or Y asks for the
     * default controls this match when they were allowed by this player's own (controls_lines). X and Y
     * do nothing else on a connected character select; Z with them stays Z's. */
    if (m->connection_state == MU_SLP_MM_CONNECTION_SUCCESS && (trigger & (HSD_PAD_X | HSD_PAD_Y)) &&
        !(trigger & HSD_PAD_Z)) {
        if (ctl_line[0] == CTL_ASKED) {
            const int allow = (trigger & HSD_PAD_X) != 0;
            controls_send(allow ? 1 : 2, ctl_line[1]);
            lbAudioAx_80024030(allow ? 1 : 0);
            return 0;
        }
        if (ctl_line[0] == CTL_ALLOWED_OWN && (trigger & HSD_PAD_Y)) {
            controls_send(3, ctl_line[1]);
            lbAudioAx_80024030(0);
            return 0;
        }
    }

    switch (m->connection_state) {
    case MU_SLP_MM_IDLE:
        if (doubles_teams()) {
            /* Join the match room once per visit, before the pick, so all four arrive in time. */
            if (!dt.doubles_searched && !dt.chat_window_opened && gm_801A4BB8() != 0) {
                dt.doubles_searched = 1;
                st->is_winner = MU_SLP_WINNER_NULL;
                st->chose_stage = 0;
                mu_slippi_sss_find_match();
            }
            return 0;
        }
        if (mu_online_p2p_menus()) {
            /* A peer-to-peer session has its opponent already: no search and no connect-code
             * entry. Its host never reports this state; a reply that could not be read does. */
            return 0;
        }
#if defined(YOUGAME_RANKED_BRANCH) && YOUGAME_RANKED_BRANCH
        /* Same lock-in path as search, with the CPU bridge installed first.
         * The host must acknowledge the local entry before any B5/B4 is sent. */
        if (mu_slippi_practice_available() && (trigger & HSD_PAD_Z)) {
            if (mu_ranked_practice_control(MU_YG_PRACTICE_START)) {
                st->is_winner = MU_SLP_WINNER_NULL;
                st->chose_stage = 0;
                lock_in_and_search(SB_RAND);
            }
            return 0;
        }
#endif
        if (dt.chat_window_opened || !(trigger & HSD_PAD_START) || gm_801A4BB8() == 0) {
            return 0;
        }
        st->is_winner = MU_SLP_WINNER_NULL;
        st->chose_stage = 0;
        if (*char_chosen == 0) {
#if MU_SQUAD
            squad_start_refused();
#endif
            return 0;
        }
#if defined(YOUGAME_RANKED_BRANCH) && YOUGAME_RANKED_BRANCH
        if (st->mode == MU_SLP_MODE_RANKED) {
            /* Returning from a practice reveal: restore the real backend before
             * the ordinary Ranked B5/B4, without changing its matchmaking path. */
            mu_ranked_practice_control(MU_YG_PRACTICE_STOP);
        }
#endif
        switch (st->mode) {
        case MU_SLP_MODE_RANKED:
        case MU_SLP_MODE_UNRANKED:
        case MU_SLP_MODE_PARTY:
            lock_in_and_search(SB_RAND);
            break;
        case MU_SLP_MODE_DIRECT:
        case MU_SLP_MODE_TEAMS:
            if (test_skip_code_entry) {
                test_skip_code_entry = 0;
                break;
            }
            load_code_entry();
            break;
        default:
            break;   /* the console stalls (b 0x0) */
        }
        return 0;

    case MU_SLP_MM_INITIALIZING:
    case MU_SLP_MM_MATCHMAKING:
    case MU_SLP_MM_OPPONENT_CONNECTING:
    case MU_SLP_MM_ERROR:
        /* An error also clears on A or B: the keyboard's Z key is the pad's A (E is Z), so a keyboard
         * player pressing the "Z" the screen names was stuck there until the game was killed (11-17). */
        if ((trigger & HSD_PAD_Z) ||
            (m->connection_state == MU_SLP_MM_ERROR && (trigger & (HSD_PAD_A | HSD_PAD_B)))) {
            mu_slippi_cleanup_connections();
        }
        /* A YouGame row while YouGame is on an older release: Start restarts in that release (the
         * host answers only when it made the offer). */
        if ((trigger & HSD_PAD_START) && mu_slippi_yougame_pick()) {
            mu_slippi_yougame_restart_older();
        }
        return 0;

    case MU_SLP_MM_CONNECTION_SUCCESS:
        break;

    default:
        return 0;
    }

    /* Connected: hold Z to disconnect. */
    if ((u32) gm_GetButtonsPressed((u8) *ctrl_port) & HSD_PAD_Z) {
        dt.z_hold_timer++;
        if (dt.z_hold_timer > DISCONNECT_HOLD_DELAY) {
            u8 reason = 1;   /* Same Slippi cleanup command, with the native CSS's Z reason. */
            dt.z_hold_timer = 0;
            mu_slippi_cmd(0xBA, &reason, 1, NULL, 0, NULL);
            return 0;
        }
    } else {
        dt.z_hold_timer = 0;
    }

    /* An arena room's new pairing: two friends the room put together have no previous winner,
     * so the first game's stage goes to player 1, as in a peer-to-peer session. A rematch of the
     * same pair keeps the usual winner and loser. */
    if ((st->mode == MU_SLP_MODE_DIRECT || st->mode == MU_SLP_MODE_TEAMS) &&
        m->connection_state == MU_SLP_MM_CONNECTION_SUCCESS) {
        static u32 arena_seen;
        int test_room = 0, in_room = 0;
        const u32 pairing = arena_pairing(&test_room, &in_room);
        /* Only a player still to lock in can be sent to pick: one who locked in with the search
         * (Direct's code entry) already has a stage in the match setup, and a loser's mark would
         * send them to a stage select whose lock-in starts a new search. */
        if (pairing != 0 && pairing != arena_seen) {
            arena_seen = pairing;
            if (m->local_ready == 0) {
                st->is_winner = m->local_index == 0 ? MU_SLP_WINNER_LOST : MU_SLP_WINNER_WON;
                st->chose_stage = 0;
            }
            OSReport("arena: new pairing %u, local index %d, %s the first stage\n", (unsigned) pairing, (int) m->local_index,
                     m->local_ready != 0 ? "already locked in for" : m->local_index == 0 ? "picks" : "waits for");
        }
        if (in_room && st->is_winner == MU_SLP_WINNER_NULL && m->local_ready == 0) {
            /* In a room nobody may be left with neither winner nor loser: Start would do nothing
             * (the console stalls). Decided as a new pairing is. */
            st->is_winner = m->local_index == 0 ? MU_SLP_WINNER_LOST : MU_SLP_WINNER_WON;
            st->chose_stage = 0;
            OSReport("arena: no winner or loser on record, local index %d %s the stage\n", (int) m->local_index,
                     m->local_index == 0 ? "picks" : "waits for");
        }
        {
            /* Tests: MELEE_TEST_ARENA_PRESS_START=1 in a real room (hidden runs through Slippi's server)
             * has nobody to pick a stage either. */
            static int press_env = -1;
            if (press_env < 0) {
                press_env = mu_test_getenv("MELEE_TEST_ARENA_PRESS_START") != NULL;
            }
            if ((test_room || (in_room && press_env)) && st->is_winner == MU_SLP_WINNER_LOST) {
                st->chose_stage = 1;   /* a test room: nobody is there to pick, the host's random stage stands */
            }
        }
    }

    /* A peer-to-peer session's first game: the stage is picked, never drawn. The player in slot 0
     * (the one who sent the request) picks it on the stage select screen, exactly as the loser of
     * a game picks the next one, and the other player locks in without a stage. Decided here, once
     * the two are connected and the slots are known; later games follow winner and loser. */
    if (mu_online_p2p_menus() && st->is_winner == MU_SLP_WINNER_NULL &&
        m->connection_state == MU_SLP_MM_CONNECTION_SUCCESS)
    {
        st->is_winner = m->local_index == 0 ? MU_SLP_WINNER_LOST : MU_SLP_WINNER_WON;
        st->chose_stage = 0;
    }

    if (m->local_ready == 0) {
        int advance = (trigger & HSD_PAD_START) != 0;
        if (!advance && (st->mode == MU_SLP_MODE_DIRECT || st->mode == MU_SLP_MODE_TEAMS) &&
            st->is_winner == MU_SLP_WINNER_LOST && st->chose_stage == 1)
        {
            advance = 1;
        }
#if MU_SQUAD
        if ((trigger & HSD_PAD_START) && *char_chosen == 0 && gm_801A4BB8() != 0) {
            squad_start_refused();
        }
#endif
        if (advance && *char_chosen != 0 && gm_801A4BB8() != 0 && (doubles_teams() || arena_ffa_css)) {
            tx_lock_in(SB_RAND);   /* every game: the room's seed picks the stage (a free-for-all: the host's doubles list) */
        } else if (advance && *char_chosen != 0 && gm_801A4BB8() != 0) {
            switch (st->mode) {
            case MU_SLP_MODE_UNRANKED:
            case MU_SLP_MODE_PARTY:
                tx_lock_in(SB_RAND);
                break;
            case MU_SLP_MODE_RANKED:
                /* YouGame's Ranked v1: the next game of the set locks in as Unranked does (a random
                 * legal stage). Without the host's Ranked the console stalls, as below. The
                 * launcher's YouGame Ranked row (split layout) is the host's Ranked too. */
                if (mu_slippi_ranked_available() || mu_slippi_yougame_pick()) {
                    tx_lock_in(SB_RAND);
                }
                break;
            case MU_SLP_MODE_DIRECT:
            case MU_SLP_MODE_TEAMS:
                if (st->is_winner == MU_SLP_WINNER_LOST) {
                    if (st->chose_stage != 0) {
                        tx_lock_in(picked_stage());
                    } else {
                        /* Teams bit for the doubles FoD rule, then leave for the SSS. */
                        CSSData* css = *css_ptr;
                        css->vs.start.rules.is_teams = st->mode == MU_SLP_MODE_TEAMS ? 1 : 0;
                        *scene_request = 1;
                        st->callback = MU_SLP_CB_SSS_LOCK_IN;
                        return 0;
                    }
                } else if (st->is_winner == MU_SLP_WINNER_WON) {
                    tx_lock_in(SB_NOTSEL);
                }
                /* any other value stalls on the console (b 0x0) */
                break;
            default:
                break;   /* Ranked: the console stalls (b 0x0) */
            }
        }
    }

    /* Both ready: start. Reads the table's MSRB, which the B5 above did not refresh. */
    return (m->local_ready & m->remote_ready) != 0;
}

/* PreventA/BPressCharUnselect: no unselect while chat is open or locked in. */
int mu_slippi_css_block_unselect(void)
{
    return mu_slippi_css_online() && (dt.chat_window_opened != 0 || dt.msrb.local_ready != 0);
}

/* PreventColorChange: never in Teams (the colour follows the team), else not while locked in. */
int mu_slippi_css_block_costume_change(void)
{
    if (!mu_slippi_css_online()) {
        return 0;
    }
    return mu_slippi_state()->mode == MU_SLP_MODE_TEAMS || dt.msrb.local_ready != 0;
}

/* SkipReturnToCssSound (mnCharSel_802640A0, before the voice of any CSS with match_type != 0,
 * whenever the menus flag is on, as on Legacy). Returns 1 to skip the voice. */
int mu_slippi_css_skip_return_sound(void)
{
    MuSlippiMenuState* st = mu_slippi_state();
    MuMatchState tmp;
    if (st->code_entry != 0) {
        /* Back from connect-code entry. The autocomplete buffers are name entry's own
         * (code_scratch); nothing to free natively. */
        st->code_entry = 0;
        return 1;
    }
    /* Back from the SSS locked in: "choose your character" would be wrong. Its own B3 buffer. */
    mu_slippi_load_match_state(&tmp);
    return tmp.local_ready != 0;
}

/* ---- LoadCSSText ---- */

/* FG_CreateSubtext, no outlines. */
static int create_subtext(HSD_Text* t, const GXColor* color, f32 scale, f32 x, f32 y)
{
    GXColor c = *color;
    int idx = HSD_SisLib_803A6B98(t, x, y, "");
    HSD_SisLib_803A7548(t, idx, scale, scale);
    HSD_SisLib_803A74F0(t, idx, &c);
    HSD_SisLib_803A70A0(t, idx, (char*) "");
    return idx;
}

/* The required-update answers: the server's ("Getting the latest version. ...") and the game's own
 * (yougame::update::kMessage, "Updating to the latest version. ..."). */
static int update_words(const char* s)
{
    return s != NULL && (strncmp(s, "Getting the latest version", 26) == 0 ||
                         strncmp(s, "Updating to the latest version", 30) == 0);
}

/* The host's Unranked note (MU_YG_SUB_UNRANKED_NOTE): the opponent's leave ended this side's session.
 * An older host answers nothing, so no note. */
static int unranked_opponent_left(void)
{
    MuYgU8 sub = MU_YG_SUB_UNRANKED_NOTE, left = 0;
    unsigned int got = 0;
    return mu_slippi_cmd(MU_YG_COMMAND, &sub, 1, &left, 1, &got) == 0 && got == 1 && left == 1;
}

static void set_text(int idx, const char* s)
{
    HSD_SisLib_803A70A0(dt.text, idx, (char*) "%s", s);
}

static void set_text_fmt(int idx, const char* fmt, const char* arg)
{
    HSD_SisLib_803A70A0(dt.text, idx, (char*) fmt, arg);
}

static HSD_Text* text_struct(f32 z, f32 scale)
{
    HSD_Text* t = HSD_SisLib_803A6754(0, 0);
    t->default_kerning = 1;
    t->default_alignment = 0;
    t->pos_z = z;
    t->font_size.x = scale;
    t->font_size.y = scale;
    return t;
}

/* FN_UserTextUpdate (one B9 per CSS load). The major is 8 here, so no submenu test. */
static void user_text_update(void)
{
    char buf[32];
    int i;
    mu_slippi_fetch_app_state();
    for (i = 0; i < 4; i++) {
        HSD_SisLib_803A70A0(user_text, i, (char*) "");
    }
    if (mu_slippi_app_state() != MU_SLP_APP_LOGGED_IN) {
        return;
    }
    HSD_SisLib_803A70A0(user_text, 0, (char*) "User");
    memcpy(buf, mu_slippi_user_name(), 31);
    buf[31] = 0;
    HSD_SisLib_803A70A0(user_text, 1, (char*) "%s", buf);
    if (user_display_mode == 2) {
        HSD_SisLib_803A70A0(user_text, 2, (char*) "Connect Code");
        memcpy(buf, mu_slippi_user_code(), 10);
        buf[10] = 0;
        HSD_SisLib_803A70A0(user_text, 3, (char*) "%s", buf);
    }
}

/* FN_InitUserDisplay at (-112, 20), z 0, scale 0.1. */
static void user_display_init(int mode)
{
    f32 x = -112.0f, y = 20.0f;
    static const f32 steps[3] = { 20.0f, 25.0f, 20.0f };
    int i;
    user_display_mode = (u8) mode;
    user_text = text_struct(0.0f, 0.1f);
    for (i = 0; i < 4; i++) {
        int is_label = (i & 1) == 0;
        int idx = HSD_SisLib_803A6B98(user_text, x, y, "");
        GXColor c = is_label ? color_gray : color_white;
        f32 size = is_label ? 0.4f : 0.5f;
        HSD_SisLib_803A7548(user_text, idx, size, size);
        HSD_SisLib_803A74F0(user_text, idx, &c);
        if (i < 3) {
            y += steps[i];
        }
    }
    user_text_update();
}

enum {
    ST_HEADER = 0,
    ST_LINE1 = 1,
    ST_SPIN1 = 2,
    ST_LINE3 = 5,
    ST_PRESS_Z = 7,
    ST_PRESS_D = 8,
    ST_PLAYING_LABEL = 9,
    ST_PLAYING_OPP = 10,
    ST_ERR1 = 11,
    ST_ERR4 = 14,
    ST_RANK_OPPONENT = 15,
};

/* RankInfo owns the standing, opponent and game-owned score below this title.
 * Keep the host query below: it starts the existing per-CSS YouGame mpRank read. */
/* The header names the service first, as the menu's rows do (owner, 11-18): "YouGame Unranked",
 * "Slippi Direct". A Both pick reads "Both" until an opponent is found, then the service that won. */
static const char* header_service(u8 conn)
{
    const int pick = mu_slippi_pick_service();
    if (pick == 2) {
        if (conn == MU_SLP_MM_OPPONENT_CONNECTING || conn == MU_SLP_MM_CONNECTION_SUCCESS) {
            const int won = mu_slippi_match_service();
            if (won == 1) return "Slippi";
            if (won == 2) return "YouGame";
        }
        return "Both";
    }
    return pick == 1 ? "YouGame" : "Slippi";
}

static void set_header(const char* service, const char* mode)
{
    HSD_SisLib_803A70A0(dt.text, ST_HEADER, (char*) "%s %s", service, mode);
}

static void ranked_header(const char* header)
{
    (void) header;
    HSD_SisLib_803A7548(dt.text, ST_HEADER, 0.5f, 0.5f);
    if (mu_ranked_practice()) set_text(ST_HEADER, "Practice - not rated");
    else set_header(header_service(dt.msrb.connection_state), "Ranked");
}

/* The live pick preview, every frame while connected (display only; the host sends it on change):
 * the fighter under this player's coin while it is down, the costume the portrait shows, the team,
 * the Squad Strike lineup as it stands and whether they locked in. The online CSS has one door, 0. */
static void pick_preview_frame(void)
{
    MuSlippiMenuState* st = mu_slippi_state();
    u8 port = gm_801677F0();   /* the port mu_slippi_sss_lock_in sends */
    u8 ck = 0xFF, color = 0, team = 0xFF, count = 0;
    u8 kinds[3] = { 0xFF, 0xFF, 0xFF }, colors[3] = { 0, 0, 0 };

    if (css_ptr == NULL || *css_ptr == NULL || port >= 4 ||
        dt.msrb.connection_state != MU_SLP_MM_CONNECTION_SUCCESS) {
        return;
    }
    mu_css_pick_preview(0, port, &ck, &color);
#if MU_SQUAD
    /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
    if (gmSquad_OnlineChosen()) {
        GmSquadMember lineup[GMSQUAD_SIZE];
        int i, n = gmSquad_CssPreview(&ck, &color, lineup);
        /* Kinds as the 0xE3 lineup carries them. The preview has three slots (its wire is every
         * mode's, unchanged): they carry the three members, and the fourth pick, always the coin
         * on the Squad Strike row, goes as the coin pick (ck), which opponent_picks_line shows. */
        for (i = 0; i < n && i < 3; i++) {
            kinds[i] = lineup[i].ckind;
            colors[i] = lineup[i].color;
        }
        count = (u8) i;
    }
#endif
#ifdef MU_AKANEIA_FIGHTERS
    if (ck != 0xFF) {
        const int ext = mu_ak_mex_external(ck);   /* the external id 0x82 sends */
        ck = ext >= 0 ? (u8) ext : 0xFF;
    }
#endif
    if (st->mode == MU_SLP_MODE_TEAMS && st->team_idx >= 1 && st->team_idx <= 3) {
        team = (u8) (st->team_idx - 1);
    }
    mu_pick_preview_send(ck, color, team, count, kinds, colors, dt.msrb.local_ready != 0);
}

/* The opponents' live picks, under "Playing:". Created last, so the other indexes stay put. */
static int st_opp_picks = -1;
int snprintf(char* buffer, __SIZE_TYPE__ size, const char* format, ...);

static const char* const pick_names[26] = {
    "Falcon", "DK",     "Fox",   "Game & Watch", "Kirby", "Bowser", "Link",
    "Luigi",  "Mario",  "Marth", "Mewtwo",       "Ness",  "Peach",  "Pikachu",
    "Ice Climbers", "Jigglypuff", "Samus", "Yoshi", "Zelda", "Sheik", "Falco",
    "Young Link", "Dr. Mario", "Roy", "Pichu", "Ganondorf",
};

static const char* pick_name(u8 ck)
{
    return ck < 26 ? pick_names[ck] : "a fighter";
}

/* One line for every opponent whose game sends previews: who they are on, and whether they locked
 * in. An older game sends none, and the line stays empty. */
#if MU_SQUAD
/* Squad Strike: four places, the coin pick (the fourth, last pick) after the three slots. */
#define OPP_SQUAD_PLACES GMSQUAD_SIZE
#else
#define OPP_SQUAD_PLACES 3
#endif
static const char* opp_squad_place(const MuOpponentPick* o, int j)
{
    if (j == 3) return o->squad_count == 3 && o->char_id != 0xFF ? pick_name(o->char_id) : "-";
    return j < o->squad_count ? pick_name(o->kinds[j]) : "-";
}

static void opponent_picks_line(const MuMatchState* m, char* out, size_t size)
{
    size_t used = 0;
    int i, j, shown = 0, remotes = 0;
    out[0] = 0;
    for (i = 0; i < 3; i++) remotes += m->opp_picks[i].valid;
    for (i = 0; i < 3 && used + 1 < size; i++) {
        const MuOpponentPick* o = &m->opp_picks[i];
        char name[32];
        int n;
        if (!o->valid) continue;
        if (o->port < 4 && m->names[o->port][0]) memcpy(name, m->names[o->port], 31);
        else if (remotes == 1 && m->opp_name[0]) memcpy(name, m->opp_name, 31);
        else snprintf(name, sizeof name, "P%d", o->port + 1);
        name[31] = 0;
        n = snprintf(out + used, size - used, "%s%s: ", shown ? "   " : "", name);
        if (n < 0) break;
        used += (size_t) n < size - used ? (size_t) n : size - used - 1;
        if (o->squad_count > 0) {
            for (j = 0; j < OPP_SQUAD_PLACES && used + 1 < size; j++) {
                n = snprintf(out + used, size - used, "%s%s", j ? ", " : "", opp_squad_place(o, j));
                if (n < 0) break;
                used += (size_t) n < size - used ? (size_t) n : size - used - 1;
            }
        } else {
            n = snprintf(out + used, size - used, "%s", o->char_id == 0xFF ? "choosing" : pick_name(o->char_id));
            if (n > 0) used += (size_t) n < size - used ? (size_t) n : size - used - 1;
        }
        if (o->locked && used + 1 < size) {
            n = snprintf(out + used, size - used, " - locked in");
            if (n > 0) used += (size_t) n < size - used ? (size_t) n : size - used - 1;
        }
        shown++;
    }
}

/* Tap jump off and tilt stick (port/runtime/host/controls.h) on the online character select, as one more
 * step of Slippi's checklist: the host's line comes back from command 0xC9 every frame (state, the
 * opponent's port, seconds left, their options, this player's), and X / Y answer it. */

/* The font has no ASCII ? ( ): they are written as its Shift-JIS pairs (81 48, 81 69, 81 6A), as the + is.
 * "Tilt stick", "Tap jump off and Tilt stick", "Tap jump off, Tilt stick and Auto L-cancel" */
static int controls_names(unsigned bits, char* out, int size)
{
    const char* names[4];
    int k = 0, i;
    if (bits & MU_CONTROL_TAPJUMP_OFF) names[k++] = "Tap jump off";
    if (bits & MU_CONTROL_TILT_STICK) names[k++] = "Tilt stick";
    if (bits & MU_CONTROL_AUTO_LCANCEL) names[k++] = "Auto L-cancel";
    if (bits & MU_CONTROL_20XX103) names[k++] = "20XX Melee 1" "\x81\x44" "03";   /* full-width period: the font draws ASCII "." like a comma */
    out[0] = 0;
    for (i = 0; i < k; i++) {
        snprintf(out + strlen(out), size - (int) strlen(out), "%s%s",
                 i == 0 ? "" : i == k - 1 ? " and " : ", ", names[i]);
    }
    return k;
}

/* Overrides the checklist's third line (and its spinner) and the D-pad hint while there is something to
 * answer, and fills the controls line under them. Connected only; the error state shows its own lines. */
static void controls_lines(u8 conn, const char* opp_name, const char* x_key, const char* y_key)
{
    char names[64], line[96];
    int k;
    if (conn != MU_SLP_MM_CONNECTION_SUCCESS) {
        memset(ctl_line, 0, sizeof ctl_line);
        if (st_controls >= 0) set_text(st_controls, "");
        return;
    }
    controls_send(0, 0);
    if (st_controls < 0) return;
    k = controls_names(ctl_line[3], names, sizeof names);
    switch (ctl_line[0]) {
    case CTL_ASKED:
        snprintf(line, sizeof line, k == 1 ? "Allow their %s\x81\x48" : "Allow their options\x81\x48", names);
        set_text(ST_LINE3, line);
        dt.spinner[2] = 1;
        snprintf(line, sizeof line, "%s: %s", opp_name[0] ? opp_name : "Opponent", names);
        set_text(st_controls, line);
        if (ctl_line[2]) snprintf(line, sizeof line, "Press %s to allow, %s to decline \x81\x69%d\x81\x6A", x_key, y_key, ctl_line[2]);
        else snprintf(line, sizeof line, "Press %s to allow, %s to decline", x_key, y_key);
        set_text(ST_PRESS_D, line);
        break;
    case CTL_ALLOWED_OWN:
        snprintf(line, sizeof line, k == 1 ? "Their %s: allowed" : "Their options: allowed", names);
        set_text(ST_LINE3, line);
        dt.spinner[2] = 2;
        set_text(st_controls, k == 1 ? "You use it too" : "You use them too");
        if (ctl_line[2]) snprintf(line, sizeof line, "Press %s for default controls \x81\x69%d\x81\x6A", y_key, ctl_line[2]);
        else snprintf(line, sizeof line, "Press %s for default controls", y_key);
        set_text(ST_PRESS_D, line);
        break;
    case CTL_WAITING:
        set_text(st_controls, "Control options: waiting for an answer");
        break;
    case CTL_AGREED:
        snprintf(line, sizeof line, "Options on: %s", names);
        set_text(st_controls, line);
        break;
    case CTL_DEFAULTS:
        set_text(st_controls, "Default controls this match");
        break;
    default:
        set_text(st_controls, "");
        break;
    }
}

static void text_think(HSD_GObj* gobj)
{
    MuSlippiMenuState* st = mu_slippi_state();
    MuMatchState* m = &dt.msrb;
    char opp_code[19] = "--//--//--//--//00";
    char opp_name[32];
    char link_line[128] = { 0 };
    int updating;
    char rank_lines[6][128] = { { 0 } };
    /* The keys the prompts name: the bound key for a keyboard player, else the GameCube button. */
    char z_key[16] = "Z", b_key[16] = "B", start_key[16] = "START", x_key[16] = "X", y_key[16] = "Y", prompt[64];
    const char* s;
    int idx, i;
    u8 conn = m->connection_state;
    /* A peer-to-peer session with menus: other words for the same screen (no code, no chat). */
    int p2p = mu_online_p2p_menus();
    /* ... and its opponent is not connected yet. The host reports 3 here; 0 is the table before
     * its first B3, where "enter code" would be just as wrong. */
    int p2p_connecting = p2p && conn < MU_SLP_MM_CONNECTION_SUCCESS;
    /* Doubles on the Teams client joins the home's match room by itself: there is no code to enter.
     * Until it connects the line says so; once it dropped back to idle the room is gone, and the
     * home brings the game back to it (holding B leaves sooner). */
    int doubles_waiting = doubles_teams() && conn < MU_SLP_MM_CONNECTION_SUCCESS;
    int doubles_closed = doubles_waiting && dt.doubles_connected && conn == MU_SLP_MM_IDLE;

    (void) gobj;
    pick_preview_frame();
    if (ctrl_port != NULL) {
        mu_host_button_label(*ctrl_port, HSD_PAD_Z, z_key, sizeof z_key);
        mu_host_button_label(*ctrl_port, HSD_PAD_B, b_key, sizeof b_key);
        mu_host_button_label(*ctrl_port, HSD_PAD_START, start_key, sizeof start_key);
        mu_host_button_label(*ctrl_port, HSD_PAD_X, x_key, sizeof x_key);
        mu_host_button_label(*ctrl_port, HSD_PAD_Y, y_key, sizeof y_key);
    }
    if (doubles_teams() && conn == MU_SLP_MM_CONNECTION_SUCCESS) {
        dt.doubles_connected = 1;
    }
#if MU_SQUAD
    /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
    /* A friend room: the friend picked Squad Strike, so this side's CSS turns squads on too. */
    gmSquad_OnlineFriendJoin();
    if (gmSquad_OnlineChosen()) {
        GmSquadMember lineup[GMSQUAD_SIZE];
        u8 ck, color;
        gmSquad_OnlinePickCount(gmSquad_CssPreview(&ck, &color, lineup));
    }
#endif
    if (conn == MU_SLP_MM_MATCHMAKING || conn == MU_SLP_MM_ERROR) {
        u8 query = 0;
        unsigned int got = 0;
        if (mu_slippi_cmd(0xE7, &query, 1, (u8*) link_line, sizeof link_line, &got) != 0 || got != sizeof link_line)
            link_line[0] = 0;
        link_line[sizeof link_line - 1] = 0;
    }
    /* A required update (the app is already fetching it) is not an error: it installs when the player
     * leaves the online screen (yougame::update::SafePoint), so say that instead of "Error". */
    updating = conn == MU_SLP_MM_ERROR && (update_words(link_line) || update_words((const char*) m->error));
    if (st->mode == MU_SLP_MODE_RANKED) mu_slippi_ranked_lines(rank_lines);
    code_sjis18((u8*) opp_code);

    /* Mode title: slpCSS mode matanim on SingleMenu child 36, MELEE (0) or TEAM MATCH (16). */
    if (dt.slpcss != NULL && single_menu != NULL && *single_menu != NULL) {
        HSD_JObj* j = NULL;
        lb_80011E24(*single_menu, &j, 36, -1);
        HSD_JObjRemoveAnim(j);
        HSD_JObjAddAnim(j, NULL, DP(dt.slpcss->mode), NULL);
        HSD_JObjReqAnim(j, st->mode == MU_SLP_MODE_TEAMS ? 16.0f : 0.0f);
        HSD_JObjAnim(j);
    }

    /* Header */
    if (conn > MU_SLP_MM_CONNECTION_SUCCESS) {
        if (st->mode == MU_SLP_MODE_RANKED) {
            HSD_SisLib_803A7548(dt.text, ST_HEADER, 0.5f, 0.5f);   /* the Ranked header may have shrunk */
        }
        set_text(ST_HEADER, updating ? "Update ready" : "Error");
    } else {
        switch (st->mode) {
        case MU_SLP_MODE_UNRANKED:
#if MU_SQUAD
            /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
            if (gmSquad_OnlineChosen()) { set_header("YouGame", "Squad Strike"); break; }
#endif
            set_header(header_service(conn), "Unranked"); break;
        case MU_SLP_MODE_DIRECT:
#if MU_SQUAD
            /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
            if (gmSquad_OnlineFriend()) { set_header("YouGame", "Squad Strike"); break; }
#endif
            if (p2p) {
                set_text_fmt(ST_HEADER, "%s Mode", "P2P");
            } else if (mu_slippi_pick_service() == 1) {
                set_header("YouGame", "Friends");
            } else {
                set_header("Slippi", "Direct");
            }
            break;
        case MU_SLP_MODE_RANKED: ranked_header(rank_lines[0]); break;
        case MU_SLP_MODE_TEAMS:
            if (mu_slippi_pick_service() == 1) set_header("YouGame", "Doubles");
            else set_header("Slippi", "Teams");
            break;
        case MU_SLP_MODE_PARTY: set_header(header_service(conn), "Party"); break;
        default: set_text(ST_HEADER, "Error"); break;
        }
    }

#if MU_SQUAD
    /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
    if (gmSquad_OnlineChosen()) {
        set_text(ST_RANK_OPPONENT, "Squad Strike by Chris x9x9x9x9x9. Idea by Tyler");
        HSD_SisLib_803A7548(dt.text, ST_RANK_OPPONENT, 0.28f, 0.28f);
    }
#endif
    /* Hand the old R1 rows to RankInfo and the authoritative result reveal. */
    /* The rank panel, also under the launcher's YouGame Ranked row (the host answers its rank reads there too). */
    if (st->mode == MU_SLP_MODE_RANKED && (mu_slippi_ranked_available() || mu_slippi_yougame_pick())) {
        for (i = 0; i < 5; i++) {
            set_text(ST_RANK_OPPONENT + i, "");
        }
    }

    /* Playing label, opponent name, D-pad hint */
    memcpy(opp_name, m->opp_name, 31);
    opp_name[31] = 0;
    set_text(ST_PLAYING_LABEL, conn == MU_SLP_MM_CONNECTION_SUCCESS ? "Playing:" :
                             conn == MU_SLP_MM_OPPONENT_CONNECTING ? "Found:" : "");
    set_text(ST_PLAYING_OPP, conn == MU_SLP_MM_CONNECTION_SUCCESS ||
                           conn == MU_SLP_MM_OPPONENT_CONNECTING ? opp_name : "");
    if (conn == MU_SLP_MM_CONNECTION_SUCCESS && !p2p && !mu_ranked_practice()) {
        /* A keyboard has no D-pad: name its four chat keys (up, left, right, down) instead. */
        char du[16], dl[16], dr[16], dd[16], chat[80];
        if (ctrl_port != NULL && mu_host_button_label(*ctrl_port, HSD_PAD_DPADUP, du, sizeof du) &&
            mu_host_button_label(*ctrl_port, HSD_PAD_DPADLEFT, dl, sizeof dl) &&
            mu_host_button_label(*ctrl_port, HSD_PAD_DPADRIGHT, dr, sizeof dr) &&
            mu_host_button_label(*ctrl_port, HSD_PAD_DPADDOWN, dd, sizeof dd)) {
            snprintf(chat, sizeof chat, "Chat: %s %s %s %s", du, dl, dr, dd);
            set_text(ST_PRESS_D, chat);
        } else {
            set_text(ST_PRESS_D, "Use D-Pad to Chat");
        }
    } else {
        set_text(ST_PRESS_D, "");
    }
    if (st_opp_picks >= 0) {
        char picks[160];
        if (conn == MU_SLP_MM_CONNECTION_SUCCESS) opponent_picks_line(m, picks, sizeof picks);
        else picks[0] = 0;
        set_text(st_opp_picks, picks);
    }
#if defined(YOUGAME_RANKED_BRANCH) && YOUGAME_RANKED_BRANCH
    if (mu_slippi_practice_available()) {
        snprintf(prompt, sizeof prompt, "%s: Practice vs CPU", z_key);
        set_text(ST_PRESS_D, prompt);
    }
#endif
    /* Searching in Ranked or Unranked (owner 11-24): holding L and R goes to Training and keeps the search
     * (native_practice kept_go_training); say so where the chat line stands once connected. */
    if (conn == MU_SLP_MM_MATCHMAKING && !mu_ranked_practice() &&
        (st->mode == MU_SLP_MODE_RANKED || st->mode == MU_SLP_MODE_UNRANKED)) {
        char lk[16], rk[16];
        if (ctrl_port != NULL && mu_host_button_label(*ctrl_port, HSD_PAD_L, lk, sizeof lk) &&
            mu_host_button_label(*ctrl_port, HSD_PAD_R, rk, sizeof rk)) {
            snprintf(prompt, sizeof prompt, "Hold %s\x81\x7B%s to warm up", lk, rk);   /* the font's plus: SIS drops an ASCII '+' */
        } else {
            strcpy(prompt, "Hold L\x81\x7BR to warm up");
        }
        set_text(ST_PRESS_D, prompt);
    }

    /* Press / hold Z (the keyboard player's key for it) */
    prompt[0] = 0;
    if (conn == MU_SLP_MM_CONNECTION_SUCCESS) {
        snprintf(prompt, sizeof prompt, "Hold %s to disconnect", z_key);
    } else if (updating) {
        snprintf(prompt, sizeof prompt, "Press %s, then hold %s to update", z_key, b_key);
    } else if (conn == MU_SLP_MM_ERROR) {
        snprintf(prompt, sizeof prompt, "Press %s to clear error", z_key);
    } else if (conn > MU_SLP_MM_IDLE) {
        snprintf(prompt, sizeof prompt, "Press %s to cancel", z_key);
    } else if (conn == MU_SLP_MM_IDLE && st->mode == MU_SLP_MODE_UNRANKED && unranked_opponent_left()) {
        /* Unranked: the opponent left mid-game or between games. Said where "Hold Z to disconnect"
         * stood, until the next search (the host clears it on Start). */
        strcpy(prompt, "Opponent left");
    }
    set_text(ST_PRESS_Z, prompt);

    /* Clear lines, spinners and error lines */
    for (idx = ST_LINE1; idx <= ST_LINE3; idx += 2) {
        set_text(idx, "");
    }
    dt.spinner[0] = dt.spinner[1] = dt.spinner[2] = 0;
    for (idx = ST_ERR1; idx <= ST_ERR4; idx++) {
        set_text(idx, "");
    }

    if (conn == MU_SLP_MM_ERROR
#if MU_SQUAD
        /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
        || gmSquad_OnlineError()[0] /* empty unless chosen, but for "Waiting for your friend's game..." */
#endif
    ) {
        /* Wrap the error at 30 letters on the last space (Shift-JIS pairs count as one). The line
         * index state carries over between lines exactly as the asm's registers do. */
        u8 buf[241 + 4];
        int sub = ST_ERR1, line_idx = 0, line_len = 0, last_space = 0, start = 0;
        u8 c;
        memset(buf, 0, sizeof buf);
#if MU_SQUAD
        if (gmSquad_OnlineError()[0])
            strncpy((char*) buf, gmSquad_OnlineError(), 241);
        else
#endif
        if (updating)
            strncpy((char*) buf, "A new version is ready. It installs when you leave this screen, then brings you back to Online.", 241);
        else if (link_line[0]) memcpy(buf, link_line, sizeof link_line);
        else memcpy(buf, m->error, 241);
        for (;;) {
            int set = 0;
            int pos = start + line_idx;
            c = pos < (int) sizeof buf ? buf[pos] : 0;
            if (c & 0x80) {
                line_idx += 2;
                line_len++;
            } else {
                if (c == 0x20) {
                    last_space = line_idx;
                }
                line_idx++;
                line_len++;
            }
            if (c == 0) {
                set = 1;
            } else if (line_len > 30) {
                if (start + last_space < (int) sizeof buf) {
                    buf[start + last_space] = 0;
                }
                set = 1;
            }
            if (set) {
                set_text(sub, start < (int) sizeof buf ? (const char*) &buf[start] : "");
                sub++;
                start += last_space + 1;
                line_idx = 0;
                line_len = 0;
            }
            if (c == 0 || start + line_idx >= 241 || sub > ST_ERR4) {
                break;
            }
        }
    } else {
        idx = ST_LINE1;
        if (*char_chosen == 0) {
            set_text(idx, "Select your character");
            dt.spinner[0] = 1;
            if (p2p_connecting) {
                /* The fighter can be chosen meanwhile; the second line says what is awaited. */
                set_text_fmt(idx + 2, "Connecting to %s", "opponent");
                dt.spinner[1] = 1;
            }
            goto spinners;
        }
        set_text(idx, "Character selected");
        idx += 2;
        dt.spinner[0] = 2;

        if (p2p_connecting) {
            /* In place of "Press START to enter code": START does nothing until connected. */
            set_text_fmt(idx, "Connecting to %s", "opponent");
            dt.spinner[1] = 1;
            goto spinners;
        }
        if (doubles_waiting && m->local_ready == 0) {
            snprintf(prompt, sizeof prompt, "Match closed. Hold %s to exit", b_key);
            set_text(idx, doubles_closed ? prompt : "Connecting to players");
            dt.spinner[1] = doubles_closed ? 0 : 1;
            goto spinners;
        }
        if (m->local_ready == 0) {
            const char* action;
            if ((st->mode == MU_SLP_MODE_DIRECT || st->mode == MU_SLP_MODE_TEAMS) &&
                conn == MU_SLP_MM_CONNECTION_SUCCESS && st->is_winner == MU_SLP_WINNER_LOST &&
                st->chose_stage == 0)
            {
                action = "select stage";
            } else if (conn == MU_SLP_MM_CONNECTION_SUCCESS) {
                action = "lock in";
            } else if (st->mode == MU_SLP_MODE_DIRECT || st->mode == MU_SLP_MODE_TEAMS) {
                action = "enter code";
            } else {
                action = "search";
            }
            snprintf(prompt, sizeof prompt, "Press %s to %s", start_key, action);
            set_text(idx, prompt);
            dt.spinner[1] = 1;
            goto spinners;
        }
        set_text(idx, "Locked in");
        idx += 2;
        dt.spinner[1] = 2;

        {
            /* p2p: "opponent" too, there is no typed code to show. */
            int generic = st->mode == MU_SLP_MODE_UNRANKED || st->mode == MU_SLP_MODE_RANKED ||
                          st->mode == MU_SLP_MODE_PARTY || p2p;
            if (conn == MU_SLP_MM_CONNECTION_SUCCESS) {
                set_text_fmt(idx, "Waiting on %s", opp_name[0] ? opp_name : "opponent");
            } else if (conn == MU_SLP_MM_OPPONENT_CONNECTING) {
                set_text_fmt(idx, "Connecting to %s", opp_name[0] ? opp_name :
                             generic ? "opponent" : opp_code);
            } else {
                if (link_line[0]) set_text(idx, link_line);
                else set_text_fmt(idx, "Searching for %s", generic ? "opponent" : opp_code);
            }
        }
        dt.spinner[2] = 1;
    }

spinners:
    controls_lines(conn, opp_name, x_key, y_key);
    for (i = 0, idx = ST_SPIN1; i < 3; i++, idx += 2) {
        if (dt.spinner[i] == 1) {
            GXColor c = spinner_wait_color;
            s = spinner_str[(dt.frame_counter / SPINNER_TRANSITION_FRAMES) & 1];
            set_text(idx, s);
            HSD_SisLib_803A74F0(dt.text, idx, &c);
        } else if (dt.spinner[i] == 2) {
            GXColor c = spinner_done_color;
            set_text(idx, spinner_done_str);
            HSD_SisLib_803A74F0(dt.text, idx, &c);
        } else {
            set_text(idx, "");
        }
    }

    dt.frame_counter++;
    if (dt.frame_counter >= FRAME_MAX) {
        dt.frame_counter = 0;
    }
}

/* LoadCSSText's init (mnCharSel_802640A0, after the SingleMenu joint loads). The portrait BG word
 * (mnCharSel_804D50D8) is reset by the caller. */
void mu_slippi_css_text_init(void)
{
    HSD_GObj* gobj;
    void* ud;
    HSD_Text* t;
    int i;
    int credit = 0;

    /* Mode 2 adds the "Connect Code" rows; a peer-to-peer session has no code to show. */
    user_display_init(mu_slippi_state()->mode == MU_SLP_MODE_DIRECT && !mu_online_p2p_menus() ? 2 : 1);

    gobj = GObj_Create(4, 5, 0x80);
    ud = HSD_MemAlloc(4);
    GObj_InitUserData(gobj, 4, HSD_Free, ud);
    HSD_GObj_SetupProc(gobj, text_think, 4);

    t = text_struct(0.0f, 0.1f);
    dt.text = t;

    create_subtext(t, &color_white, 0.5f, 70.0f, 23.0f);                  /* header */
    {
        static const f32 line_y[3] = { 52.0f, 75.0f, 98.0f };
        for (i = 0; i < 3; i++) {
            create_subtext(t, &color_white, 0.4f, 90.0f, line_y[i]);      /* line */
            create_subtext(t, &color_white, 0.45f, 70.0f, line_y[i]);     /* spinner */
        }
    }
    create_subtext(t, &color_gray, 0.4f, 70.0f, 132.5f);                  /* press Z */
    create_subtext(t, &color_gray, 0.4f, 70.0f, 152.5f);                  /* use D-pad */
    /* "Playing:" and the opponent sit at the top of the bar, so their picks fit under them above
     * the portraits (at -226 the picks line was drawn behind the top row). */
    create_subtext(t, &color_gray, 0.5f, -130.0f, -256.0f);               /* Playing: */
    create_subtext(t, &color_white, 0.5f, -50.0f, -256.0f);               /* opponent */
    {
        static const f32 err_y[4] = { 52.0f, 70.0f, 88.0f, 106.0f };
        for (i = 0; i < 4; i++) {
            create_subtext(t, &color_red, 0.4f, 70.0f, err_y[i]);
        }
    }
#if MU_SQUAD
    /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
    /* Direct too: a friend room may turn squads on after this screen opened (gmSquad_OnlineFriendJoin). */
    if (gmSquad_OnlineChosen() || mu_slippi_state()->mode == MU_SLP_MODE_DIRECT) {
        create_subtext(t, &color_gray, 0.28f, 70.0f, 121.0f);   /* the credit: left-aligned with the header, under the status lines */
        credit = 1;
    }
#endif
    if (mu_slippi_state()->mode == MU_SLP_MODE_RANKED && (mu_slippi_ranked_available() || mu_slippi_yougame_pick())) {
        for (i = 0; i < 5; i++) {
            create_subtext(t, &color_white, 0.32f, 70.0f, 178.0f + (float) i * 18.0f);
        }
    }
    st_opp_picks = create_subtext(t, &color_white, 0.35f, -130.0f, -240.0f);  /* their picks */
    /* Tap jump off and tilt stick: what the match plays, or who asks (controls_lines). */
    /* In the gap under the status lines, above "Hold Z": the panel's border runs under the D-pad line. */
    st_controls = create_subtext(t, &color_gray, 0.35f, 70.0f, credit ? 110.0f : 116.0f);
}

/* ---- chat (mu_slippi_chat.c): the CSS data table as SlippiCSS.dat reads it ---- */

const MuMatchState* mu_slippi_css_msrb(void)
{
    return &dt.msrb;
}

void* mu_slippi_css_slpcss(void)
{
    return dt.slpcss;
}

void mu_slippi_css_set_chat_open(int open)
{
    dt.chat_window_opened = (u8) (open != 0);
}

/* mnCharSel_804D6CF6: nonzero while the CSS leaves or hands over to name entry. */
int mu_slippi_css_scene_request(void)
{
    return scene_request != NULL ? *scene_request : 0;
}

/* mnCharSel_804D6CF0 read as a byte, as the module does. */
int mu_slippi_css_port(void)
{
    return ctrl_port != NULL ? (u8) *ctrl_port : 0;
}
