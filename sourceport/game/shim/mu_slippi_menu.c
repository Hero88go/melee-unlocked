/* Native Slippi online menus: the core.
 *
 * Legacy gets the online menus from Slippi's codes in GALE01r2.ini (the Online/Menus, Online/Slippi
 * Online Scene and Online/Static folders). This file is their native form for the parts that are not
 * screen code: the state Slippi parks in r13 slots and code caves, the host-command wrappers and
 * codecs, the boot and main-menu entry, the online submenu, and the online major (GM_HANYU_CSS
 * turned into CSS / SSS / VS / Results / Splash). Each function names the code it stands for.
 * The public contract is run-source/s6-core-api.md.
 *
 * Nothing here runs unless the host set the Slippi-menus game option; the predicates answer 0
 * and the hook sites keep their original code.
 */
#include <string.h>
#include <dolphin/os.h>
#include <dolphin/pad.h>
#include <dolphin/vi.h>
#include <melee/gm/forward.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_1A36.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gm_1B03.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/gmresult.h>
#include <melee/gm/gmscdata.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/gmvsmelee.h>
#include <melee/gm/gmvsmode.h>
#include <melee/gm/types.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbdvd.h>
#include <melee/lb/types.h>
#include <melee/mn/inlines.h>
#include <melee/mn/mnmain.h>
#include <melee/mn/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/sislib.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbvector.h>
#include <mu_test_env.h>
#include <mu_native.h>
#include <mu_online_menu.h>
#include <mu_pick_preview.h>
#include <mu_lobby_wire.h>
#ifdef MELEE_DOUBLES
#include <mu_doubles_game.h>
#endif
#if MU_SQUAD
/* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
#include "mu_squad.h"
#endif
#include "mu_tourney.h"
#include "mu_online_pages.h"
#include <mu_ranked.h>
#include <mu_ranked_setup_online.h>

int mu_online_abi_command(unsigned int command, const unsigned char* payload, unsigned int size,
                          unsigned char* response, unsigned int capacity, unsigned int* response_size);
void mu_online_abi_log(const char* text);
int snprintf(char* buffer, __SIZE_TYPE__ size, const char* format, ...);
void mu_replay_apply_game_info(StartMeleeData* data, const unsigned char* info);
extern MenuKindData mn_803EB6B0[];

enum {
    CMD_GET_MATCH_STATE = 0xB3,
    CMD_FIND_OPPONENT = 0xB4,
    CMD_SET_MATCH_SELECTIONS = 0xB5,
    CMD_OPEN_LOGIN = 0xB6,
    CMD_UPDATE_APP = 0xB8,
    CMD_GET_ONLINE_STATUS = 0xB9,
    CMD_CLEANUP_CONNECTIONS = 0xBA,
    CMD_SEND_CHAT = 0xBB,
    CMD_GET_NEW_SEED = 0xBC,
    CMD_NAME_ENTRY_INDEX = 0xBE,
    CMD_YOUGAME_FRIENDS_MENU = 0xE6,
    CMD_YOUGAME_RANKED = 0xFA,   /* this fork's: flags (bit 0: Ranked offered), then the header text */
    CMD_YOUGAME_MENU = 0xEF,     /* upstream's online-menu query (upstream: 0xE7, this fork's Slippi link) */
    REPLY_CAPACITY = 4096,   /* the bridge refuses a smaller reply buffer */
    MSRB_SIZE = 1030,        /* Slippi 3.7.0 (Online.s MSRB_SIZE): ranked info and bot flags appended */
    MSRB_MIN_SIZE = 962,     /* 3.6.4's, what the peer-to-peer host answers: the rest reads as zero */
    OSB_SIZE = 42,
    FIGHTER_NUM = 33,        /* GetFighterNum: "no fighter" */
    ICON_ZELDA = 0x12,
    ICON_SHEIK = 0x13,
};

/* ---- flag ---- */

static int menus_flag = -1;

int mu_slippi_menus_enabled(void)
{
    if (menus_flag < 0) {
        menus_flag = (mu_game_options() & MU_OPTION_SLIPPI_MENUS) != 0;
    }
    return menus_flag;
}
/* A Doubles console session boots from before main, where the host's options are all zero (the
 * canonical menus, the same on all four), so the latch reads 0 there and kept the online menus off
 * after the session: its results came back to VS. Mode. Each main-menu entry outside a session
 * takes the host's menus back. This only ever turns them on, never off. */
static void menus_relatch(void)
{
    if (menus_flag == 0 && (mu_game_options() & MU_OPTION_SLIPPI_MENUS) != 0) {
        menus_flag = 1;
        OSReport("online menu: the host's online menus are back after a console session\n");
    }
}

/* ---- state (Slippi's r13 slots and cave bytes) ---- */

static MuSlippiMenuState slp;
static MuMatchState last_match;
static unsigned char user_name[32];
static unsigned char user_code[11];
static unsigned char reply[REPLY_CAPACITY];
static int installed;         /* the online major replaced GM_HANYU_CSS (main.asm) */
static int zelda_sheik;       /* between the online major's OnLoad and OnUnload */
static unsigned online_loads;  /* OnLoad count: the chat shim's Sheik selector keeps its icon until the next one */
static int boot_oneshot;      /* boot.asm's one-shot "first menu = online submenu" */
/* FA bit 5: the host wants the main menu first (a YouGame start for a local match, build 103). The
 * online submenu still opens first after an update's --resume online, in a P2P session and standalone. */
static int boot_main_menu;
static int ranked_supported;
static int ranked_offered;
static int friends_offered;   /* FA bit 1: YouGame Online > Friends is offered */
static int slippi_submenu;
static int agent_copy, local_peer, agent_lock_logged; /* FA bits 3/4; menu-entry log latch */
/* The YouGame byte at the last app-state fetch (EF). Bit 1: the split layout of the GitHub build, two
 * pages of the same eight rows: Ranked, Unranked, Friends (YouGame), Slippi; then Slippi Unranked,
 * Slippi Direct, Slippi Teams, Slippi Party. Bit 2: the YouGame rows are open (the launcher is signed
 * in to YouGame). A locked row is not drawn, so each page shows only its own rows. Never set inside
 * the YouGame app, whose rows are this fork's (FA, ranked_supported). */
static int yg_flags;
static int yg_pick;           /* the last row picked was a YouGame row (split layout) */
static int last_slot = -1;    /* the last row picked (split layout): the cursor goes back to it */
enum { YG_APP_RANKED = 1, YG_SPLIT = 2, YG_ROWS_OPEN = 4, YG_HEADER = 8, YG_OPEN_RANKED = 16, YG_OPEN_DIRECT = 32,
       YG_COMING_SOON = 64, YG_FRIENDS_FLOW = 128 };
/* Started for a YouGame mode (--online-mode): the boot menu confirms that row by itself, like the
 * P2P session's Direct (set at the boot entry, cleared by the menu's think before any match). */
static int auto_slot = -1;
/* A YouGame row picked while YouGame is not connected yet (YG_COMING_SOON): the host runs YouGame's
 * start hook (CMD_YOUGAME_MENU 0x20) and the menu confirms this row by itself once the rows open.
 * Menu-only, like auto_slot. -1: none. */
static int yg_wait_slot = -1;
/* The Friends row is waiting for YouGame's friend flow (the extension's app starting, EF 0x4B), not for the rows to
 * open; yg_friends_waited: that wait ended (flow there, or 20 s, or the app gone), so the row goes on as it is. */
static int yg_wait_friends, yg_friends_waited;
/* A 0x20 asked for YouGame on this menu (YouGame's own small window may be up over it, Windows): leaving
 * the menu tells the host (0x21, yougame_ask_withdraw) so the window goes with it. */
static int yg_asked;
static unsigned yg_wait_frames;
static unsigned yg_closed_frames;  /* frames on the Online menu with YouGame's rows still closed */
static int yg_open;           /* YG_OPEN_* from the first answer that carried it (the host answers it once) */
enum { SPLIT_SLIPPI = 3, SPLIT_FIRST_SLIPPI = 4 };
/* The split layout's rows as Slippi mode ids; rows 0-2 are YouGame's, row 3 opens Slippi's page. */
static const signed char split_mode[8] = { MU_SLP_MODE_RANKED, MU_SLP_MODE_UNRANKED, MU_SLP_MODE_DIRECT, -1,
                                           MU_SLP_MODE_UNRANKED, MU_SLP_MODE_DIRECT, MU_SLP_MODE_TEAMS,
                                           MU_SLP_MODE_PARTY };
static int split_page;        /* 0: Ranked, Unranked, Friends, Slippi; 1: Slippi's four */
static int split_layout(void) { return !ranked_supported && (yg_flags & YG_SPLIT) != 0; }
/* YouGame's page under Melee Unlocked's launcher with YouGame's rows open over the local channel to a
 * running YouGame app (YG_FRIENDS_FLOW): its Friends row opens YouGame's friend flow, as the app's
 * Friends row does (the host holds a Friends room and opens YouGame's friend picker and the Friends
 * lobby). Otherwise (the old launcher bridge) it stays a search by connect code. */
static int split_friends(void)
{
    return split_layout() && !slippi_submenu && (yg_flags & YG_FRIENDS_FLOW) != 0;
}
/* The cursor is on the row that opens YouGame's friend flow: the app's Friends row, or the launcher's
 * under split_friends(). */
static int on_friends_row(int sel)
{
    return mu_slippi_menu_option(sel) == MU_SLP_OPT_FRIENDS && (!split_layout() || split_friends());
}
/* The two pages (mu_online_pages.inc, included at the end of this file); slippi_submenu is the page. */
static int pages(void);
static int pages_start(void);
static void pages_install(void);
static void pages_flip(int to_start);
/* YouGame's sign-in arrived while Slippi's page was up (pages_frame): the turn to YouGame's page waits for the
 * menu's own think below. pages_frame runs from a drawing proc, and a page turn started there left the new page's
 * rows undrawn. */
static int pages_flip_pending;
static int pages_player_flipped;   /* the player pressed Z since the menu opened (no automatic page turn after) */
static void pages_unranked_step(int dir);
static int pick_slippi(void);
static int pages_unranked_value(void);
static void pages_texts_forget(void);
int mu_online_pages_heading(void);
static void pages_notice_show(const char* text);
static void pages_log_rows(void);
static void pages_frame(HSD_GObj* menu, int canvas, int idle);
static int unranked_val = -1;      /* Unranked's slider: 0 YouGame, 1 Both, 2 Slippi; -1 the page's own */
static int pages_refresh;          /* re-hover the row once: its label and description follow */
static int pages_notice_row = -1;
static const char* pages_notice;
static int pages_last_option = -1, pages_last_page;   /* the row last picked, as its option (a row moves when
                                                       * a page's order changes): the cursor comes back to it */
/* The page the online menu opens on: Slippi's own when YouGame's rows are closed and Slippi is signed
 * in (one press less for a player without YouGame), else the first page. */
static int split_start_page(void)
{
    if (yg_flags & YG_COMING_SOON) {
        return 0;   /* YouGame's rows, the same as when it is connected */
    }
    return !(yg_flags & YG_ROWS_OPEN) && slp.app_state == MU_SLP_APP_LOGGED_IN ? 1 : 0;
}
/* YouGame is not connected yet: its rows look and move exactly as when it is (same labels, never
 * greyed, no stickers). Picking one asks the host to start YouGame, then continues into that row. */
static int yougame_soon(void) { return split_layout() && (yg_flags & YG_COMING_SOON) != 0; }
static int cursor_stops(int sel)
{
    return mn_80229938(MENU_KIND_8, sel);
}
/* Direct's row: the P2P session menu confirms it by itself (Slippi Direct in the split layout). */
static int direct_row(void)
{
    return mu_online_menu_row(pages(), 1, MU_SLP_MODE_DIRECT);   /* Slippi Direct, on Slippi's page */
}
/* Agent copies never reach real Slippi: the split layout's rows mapped to this fork's options. */
static int split_agent_locked(int sel)
{
    if (pages()) return mu_online_agent_locked(agent_copy, 1, slippi_submenu, mu_slippi_menu_option(sel), local_peer);
    if (sel < 0 || sel >= 8) return agent_copy;
    return mu_online_agent_locked(agent_copy, 1, sel >= SPLIT_SLIPPI,
                                  sel == SPLIT_SLIPPI ? MU_SLP_OPT_SLIPPI : split_mode[sel], local_peer);
}
static int yougame_query(int pick, char* out, unsigned int cap);
/* YouGame's three rows on the first page: how far each has moved on screen (640x480 game pixels,
 * x then y) from where it sits with the menu camera untilted. Set every frame by the menu's think
 * (mu_slippi_menu_rows); 0 until the first frame of the page. */
static float soon_offsets[6];
static int soon_offsets_ok;
int mu_menu_camera_neutral_eye(float out[3]);
static int first_boot = 1;    /* OnMenuLoad's DOFST_IS_FIRST_BOOT */
/* A peer-to-peer session with menus (mu_online_p2p_menus): the first online submenu confirms Direct
 * by itself. Set in the boot menu entry and cleared by that menu's think a few frames later, both
 * before any match exists, so every rollback snapshot holds 0 and a load cannot arm it again. */
static int p2p_auto_direct;
/* Friends was just confirmed: its confirm cooldown must not read as "Friends no longer selected",
 * or the host stops the page picker it opened for that press (friends: cancelled, the invite still
 * goes out and the friend waits for a room nobody hosts). Menu-only, like p2p_auto_direct. */
static int friends_confirm_hold;
static int slippi_link_notice;
#if MU_SQUAD
/* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
 * Y on the Squad Strike row ("Play a friend") moved the cursor to Friends this frame: once the
 * Friends row is published below, the host opens the friend picker for it. Menu-only. */
static int squad_friend_invite;
#endif
#ifdef MELEE_DOUBLES
/* The Doubles home was open on the last online-menu frame (E6 value 13). Menu-only. */
static int doubles_home_seen;
#endif

MuSlippiMenuState* mu_slippi_state(void)
{
    return &slp;
}

/* ---- host commands (FN_EXITransferBuffer) ---- */

int mu_slippi_cmd(unsigned int cmd, const void* payload, unsigned int size, void* out,
                  unsigned int out_cap, unsigned int* out_size)
{
    unsigned int got = 0;
    int result = mu_online_abi_command(cmd, (const unsigned char*) payload, size, reply,
                                       REPLY_CAPACITY, &got);
    if (result != 0) {
        got = 0;
    }
    if (out != NULL) {
        unsigned int n = got < out_cap ? got : out_cap;
        memcpy(out, reply, n);
    }
    if (out_size != NULL) {
        *out_size = got;
    }
    return result;
}

static u32 be32(const u8* p) { return (u32) p[0] << 24 | (u32) p[1] << 16 | (u32) p[2] << 8 | p[3]; }
static void put32(u8* p, u32 v) { p[0] = (u8) (v >> 24); p[1] = (u8) (v >> 16); p[2] = (u8) (v >> 8); p[3] = (u8) v; }

/* UserDisplayFunctions FetchSlippiAppState: B9, 42 bytes {app state, name[31], code[10]}; the
 * app state goes to r13-0x505F. */
static int tournament_visible, tournament_back_ticks;
static char tournament_text[128];
static void tournament_query(unsigned char op)
{
    unsigned char reply[2 + 128] = { 0 };
    unsigned int got = 0;
    mu_slippi_cmd(MU_TOURNEY_COMMAND, &op, 1, reply, sizeof reply, &got);
    tournament_offered = got == sizeof reply && reply[0] != 0;
    tournament_visible = got == sizeof reply && reply[1] != 0;
    memcpy(tournament_text, reply + 2, 127); tournament_text[127] = 0;
}

/* A waiting view owns the game's menu input until it is left or a match starts. */
static int tournament_menu_think(HSD_GObj* gp, u32 buttons)
{
    int offered = tournament_offered, was_visible = tournament_visible, port, back = 0;
    tournament_query(MU_TOURNEY_MENU);
    if (offered != tournament_offered && ranked_supported && !slippi_submenu) {
        mu_mnmain_install_online_menu();
        mn_80229894(MENU_KIND_8, (u16) mu_slippi_first_unlocked(), 1);
        return 1;
    }
    if (!tournament_visible && was_visible) {
        slp.force_clear = 1;
        mn_80229894(MENU_KIND_8, (u16) mu_online_menu_row(ranked_supported, 0, MU_SLP_OPT_TOURNAMENT), 1);
        return 1;
    }
    if (!tournament_visible && tournament_back_ticks >= 0) return 0;
    for (port = 0; port < 4; ++port)
        if (gm_GetButtonsPressed((u8) port) & 0x0200) back = 1; /* mapped GameCube B */
    if (buttons & MenuInput_Back) back = 1;
    /* Finishing a leave consumes B until release, keeping the player on Online. */
    if (tournament_back_ticks < 0) {
        if (back) return 1;
        tournament_back_ticks = 0;
        return 0;
    }
    tournament_back_ticks = back ? tournament_back_ticks + 1 : 0;
    if (tournament_back_ticks >= 30) {
        tournament_query(MU_TOURNEY_LEAVE);
        tournament_back_ticks = -1;
        sfxBack();
        slp.force_clear = 1;
        mn_80229894(MENU_KIND_8, (u16) mu_online_menu_row(ranked_supported, 0, MU_SLP_OPT_TOURNAMENT), 1);
    } else mu_slippi_tournament_notice(gp, tournament_text);
    return 1;
}

int mu_slippi_fetch_app_state(void)
{
    u8 osb[OSB_SIZE];
    memset(osb, 0, sizeof osb);
    mu_slippi_cmd(CMD_GET_ONLINE_STATUS, NULL, 0, osb, sizeof osb, NULL);
    slp.app_state = osb[0];
    memcpy(user_name, osb + 1, 31);
    user_name[31] = 0;
    memcpy(user_code, osb + 32, 10);
    user_code[10] = 0;
    ranked_offered = mu_slippi_ranked_header(NULL, 0);
    yg_flags = yougame_query(-1, NULL, 0);
    yg_open |= yg_flags & (YG_OPEN_RANKED | YG_OPEN_DIRECT);
    tournament_query(MU_TOURNEY_MENU);
    return slp.app_state;
}

/* FA: flags, then six 128-byte presentation lines. Bit 0 offers the menu row; bit 2
 * supports YouGame Ranked text and random-stage play while the row stays hidden.
 * Bit 3 identifies an agent copy; bit 4 permits explicit local-peer test modes;
 * bit 5 boots on the main menu (boot_main_menu). */
int mu_slippi_ranked_line(unsigned int line, char* out, unsigned int cap)
{
    u8 r[1 + 6 * 128];
    unsigned int got = 0, n, offset = 1 + line * 128;
    memset(r, 0, sizeof r);
    if (out != NULL && cap > 0) out[0] = 0;
    if (line >= 6 || mu_slippi_cmd(CMD_YOUGAME_RANKED, NULL, 0, r, sizeof r, &got) != 0 || got < 1) {
        friends_offered = ranked_supported = 0;
        return 0;
    }
    agent_copy = (r[0] & 8) != 0;
    local_peer = (r[0] & 16) != 0;
    boot_main_menu = (r[0] & 32) != 0;
    friends_offered = (r[0] & 2) != 0;
    ranked_supported = (r[0] & 4) != 0;
    if (out == NULL) return (r[0] & 1) != 0; /* menu visibility stays independent */
    if (!ranked_supported || got <= offset || cap == 0) return 0;
    n = got - offset;
    if (n > 127) n = 127;
    if (n >= cap) n = cap - 1;
    memcpy(out, r + offset, n);
    out[n] = 0;
    return 1;
}

/* A CSS frame consumes all six lines from one bridge query. */
int mu_slippi_ranked_lines(char lines[6][128])
{
    u8 r[1 + 6 * 128];
    unsigned int got = 0, i, n, offset;
    memset(lines, 0, 6 * 128);
    memset(r, 0, sizeof r);
    if (mu_slippi_cmd(CMD_YOUGAME_RANKED, NULL, 0, r, sizeof r, &got) != 0 || got < 1) {
        friends_offered = ranked_supported = 0;
        return 0;
    }
    agent_copy = (r[0] & 8) != 0;
    local_peer = (r[0] & 16) != 0;
    boot_main_menu = (r[0] & 32) != 0;
    friends_offered = (r[0] & 2) != 0;
    ranked_supported = (r[0] & 4) != 0;
    if (!ranked_supported) return 0;
    for (i = 0; i < 6; i++) {
        offset = 1 + i * 128;
        if (got <= offset) break;
        n = got - offset;
        if (n > 127) n = 127;
        memcpy(lines[i], r + offset, n);
    }
    return 1;
}

int mu_slippi_ranked_header(char* out, unsigned int cap)
{
    return mu_slippi_ranked_line(0, out, cap);
}

/* EF: the host's YouGame byte (YG_*), then the Ranked header when YG_HEADER is set. `pick` 1 or 0
 * first tells the host the menu chose a YouGame or a Slippi row (split layout); -1 only asks. */
static int yougame_query(int pick, char* out, unsigned int cap)
{
    u8 r[64];
    u8 p = (u8) pick;
    unsigned int got = 0, n;
    memset(r, 0, sizeof r);
    if (out != NULL && cap > 0) {
        out[0] = 0;
    }
    if (mu_slippi_cmd(CMD_YOUGAME_MENU, pick >= 0 ? &p : NULL, pick >= 0 ? 1u : 0u, r, sizeof r, &got) != 0 || got < 1) {
        return 0;
    }
    if (got > sizeof r) {
        got = sizeof r;
    }
    if (out != NULL && cap > 0 && (r[0] & YG_HEADER) && got > 1) {
        n = got - 1 < cap - 1 ? got - 1 : cap - 1;
        memcpy(out, r + 1, n);
        out[n] = 0;
    }
    return r[0];
}

/* Leaving the Online menu (Back to 1-P Mode, or into a character select) after a YouGame row asked for
 * YouGame: the host closes YouGame's small window if it is still up from that ask (EF 0x21). Its
 * window is topmost and the game keeps reading the pads, so without this it stayed drawn over every
 * menu the player backed into. Choosing in the window (Play offline, Get YouGame) works as before. */
static void yougame_ask_withdraw(void)
{
    if (!yg_asked) {
        return;
    }
    yg_asked = 0;
    yg_wait_slot = -1;
    yougame_query(0x21, NULL, 0);
}

int mu_slippi_ranked_available(void) { return ranked_supported; }
int mu_slippi_yougame_pick(void) { return split_layout() && yg_pick; }
/* The service the mode was picked from: 0 Slippi, 1 YouGame, 2 Both (the Unranked slider). In the
 * YouGame app every row is YouGame's. */
int mu_slippi_pick_service(void) { return split_layout() ? yg_pick : ranked_supported ? 1 : 0; }
/* The service the found opponent came from: 0 not known, 1 Slippi, 2 YouGame (host, EF 0x49). */
int mu_slippi_match_service(void)
{
    u8 ask = 0x49, won = 0;
    unsigned int got = 0;
    if (mu_slippi_cmd(CMD_YOUGAME_MENU, &ask, 1, &won, 1, &got) != 0 || got < 1) return 0;
    return won;
}
void mu_slippi_yougame_restart_older(void) { yougame_query(3, NULL, 0); }
int mu_slippi_friends_available(void) { return friends_offered; }
/* FA presentation capability identifies YouGame even when an optional row is disabled. */
int mu_slippi_yougame_menu(void) { return ranked_supported; }
/* Heading word for the online menu: 1 = YouGame page, 2 = Slippi page, 0 = native heading. */
int mu_slippi_title_active(void)
{
    if (!mu_slippi_menus_enabled() || !pages()) return 0;
    return mu_online_pages_heading() ? 2 : 1;
}
void mu_slippi_log_online_rows(void)
{
    char line[160];
    if (pages()) { pages_log_rows(); return; }
    if (!ranked_supported || slippi_submenu) return;
#if MU_SQUAD && !defined(MU_NO_SLIPPI)
#define MU_ROWS_SQUAD ",Squad Strike"
#else
#define MU_ROWS_SQUAD ""
#endif
#ifdef MELEE_DOUBLES
    snprintf(line, sizeof line, "online menu: rows offered Ranked,Unranked,Friends,Doubles,%sSlippi%s tournament=%d\n",
#else
    snprintf(line, sizeof line, "online menu: rows offered Ranked,Unranked,Friends,%sSlippi%s tournament=%d\n",
#endif
             tournament_offered ? "Tournament," : "", MU_ROWS_SQUAD, tournament_offered ? 1 : 0);
#undef MU_ROWS_SQUAD
    /* Agent copies never show Slippi (mu_online_agent_locked); say so, so a walk doesn't read it as missing. */
    if (agent_copy) {
        size_t n = strlen(line);
        if (n > 0 && line[n - 1] == '\n') snprintf(line + n - 1, sizeof line - (n - 1), " (agent copy: Slippi row hidden)\n");
    }
    mu_online_abi_log(line);
}
int mu_slippi_menu_count(void) { return mu_online_menu_count(pages(), slippi_submenu); }
int mu_slippi_menu_option(int row) { return mu_online_menu_option(pages(), slippi_submenu, row); }
int mu_slippi_menu_label_kind(int row)
{
    int option;
    if (pages()) return mu_online_pages_label(row);
    option = mu_slippi_menu_option(row);
    if (!ranked_supported) return 0;
#ifdef MELEE_DOUBLES
    if (option == MU_SLP_OPT_DOUBLES) return 9;
#endif
#if MU_SQUAD
    /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
    if (option == MU_SLP_OPT_SQUAD) return 10;
#endif
    return option == MU_SLP_OPT_FRIENDS ? 1 : option == MU_SLP_OPT_SLIPPI ? 2 :
           option == MU_SLP_MODE_DIRECT ? 3 : option == MU_SLP_MODE_TEAMS ? 4 :
           option == MU_SLP_MODE_PARTY ? 5 : option == MU_SLP_MODE_RANKED ? 6 :
           option == MU_SLP_MODE_UNRANKED ? 7 : option == MU_SLP_OPT_TOURNAMENT ? 8 : 0;
}


int mu_slippi_app_state(void) { return slp.app_state; }
const char* mu_slippi_user_name(void) { return (const char*) user_name; }
const char* mu_slippi_user_code(void) { return (const char*) user_code; }

/* LoadMatchState: B3 into a Match State Response Buffer. */
static void parse_match_state(MuMatchState* m, const u8* r)
{
    int i;
    memcpy(m->raw, r, MSRB_SIZE);
    m->connection_state = r[0];
    m->local_ready = r[1];
    m->remote_ready = r[2];
    m->local_index = r[3];
    m->remote_index = r[4];
    m->rng_offset = be32(r + 5);
    m->delay_frames = r[9];
    m->user_chat_msg = r[10];
    m->opp_chat_msg = r[11];
    m->chat_player_index = r[12];
    m->user_rank = r[13];
    m->opp_rank = r[14];
    memcpy(m->local_name, r + 15, 31);
    for (i = 0; i < 4; i++) {
        memcpy(m->names[i], r + 46 + 31 * i, 31);
        memcpy(m->codes[i], r + 201 + 10 * i, 10);
        memcpy(m->uids[i], r + 241 + 29 * i, 29);
    }
    memcpy(m->opp_name, r + 170, 31);
    memcpy(m->error, r + 357, 241);
    memcpy(m->game_info, r + 598, 312);
    memcpy(m->match_id, r + 910, 51);
    m->alt_stage_mode = r[961];
}

/* ED read beside every B3: the opponents' live picks (display only, never match setup). A host
 * without the command answers nothing and every slot stays invalid. */
static void load_pick_previews(MuMatchState* m)
{
    u8 op = MU_PICK_PREVIEW_OP_READ;
    u8 r[MU_PICK_PREVIEW_REPLY];
    MuPickPreviewSlot slots[MU_PICK_PREVIEW_REMOTES];
    unsigned int got = 0;
    int i;
    memset(m->opp_picks, 0, sizeof m->opp_picks);
    if (mu_slippi_cmd(MU_PICK_PREVIEW_COMMAND, &op, 1, r, sizeof r, &got) != 0 ||
        !mu_pick_preview_reply_decode(r, got, slots)) {
        return;
    }
    for (i = 0; i < MU_PICK_PREVIEW_REMOTES; i++) {
        MuOpponentPick* o = &m->opp_picks[i];
        const MuPickPreview* p = &slots[i].pick;
        if (!slots[i].valid) {
            continue;
        }
        o->valid = 1;
        o->port = slots[i].port;
        o->char_id = p->char_id;
        o->color = p->color;
        o->team_id = p->team_id;
        o->squad_count = p->squad_count;
        memcpy(o->kinds, p->kinds, sizeof o->kinds);
        memcpy(o->colors, p->colors, sizeof o->colors);
        o->locked = p->locked;
    }
}

/* ED send: the host keeps it only while connected and sends it on change, rate limited. */
void mu_pick_preview_send(unsigned char char_id, unsigned char color, unsigned char team_id,
                          unsigned char squad_count, const unsigned char kinds[3],
                          const unsigned char colors[3], unsigned char locked)
{
    MuPickPreview p;
    u8 b[1 + MU_PICK_PREVIEW_WIRE];
    unsigned int i;
    mu_pick_preview_clear(&p);
    p.char_id = char_id;
    p.color = color;
    p.team_id = team_id;
    p.squad_count = squad_count <= MU_PICK_PREVIEW_SLOTS && kinds != NULL && colors != NULL ? squad_count : 0;
    for (i = 0; i < p.squad_count; i++) {
        p.kinds[i] = kinds[i];
        p.colors[i] = colors[i];
    }
    p.locked = locked != 0;
    b[0] = MU_PICK_PREVIEW_OP_SEND;
    if (mu_pick_preview_encode(&p, b + 1, sizeof b - 1) != MU_PICK_PREVIEW_WIRE) {
        return;
    }
    mu_slippi_cmd(MU_PICK_PREVIEW_COMMAND, b, sizeof b, NULL, 0, NULL);
}

int mu_slippi_load_match_state(MuMatchState* out)
{
    u8 msrb[MSRB_SIZE];
    unsigned int got = 0;
    int result;
    memset(msrb, 0, sizeof msrb);
    result = mu_slippi_cmd(CMD_GET_MATCH_STATE, NULL, 0, msrb, sizeof msrb, &got);
    parse_match_state(&last_match, msrb);
    load_pick_previews(&last_match);
    if (out != NULL && out != &last_match) {
        *out = last_match;
    }
    return result != 0 || got < MSRB_MIN_SIZE ? -1 : 0;
}

const MuMatchState* mu_slippi_match_state(void)
{
    return &last_match;
}

/* Find Match Transfer Buffer: mode, 18 Shift-JIS bytes. FN_TX_FIND_MATCH first resets the game
 * prep data's game and tiebreak counters. */
void mu_slippi_find_opponent(int mode, const unsigned char* code_sjis18)
{
    u8 b[19];
    mu_online_reset_game_index();
    memset(b, 0, sizeof b);
    b[0] = (u8) mode;
    if (code_sjis18 != NULL) {
        memcpy(b + 1, code_sjis18, 18);
    }
#if MU_SQUAD
    /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
    if (mode == MU_SLP_MODE_UNRANKED && gmSquad_OnlineChosen() &&
        (!gmSquad_OnlineBudget() || !gmSquad_OnlineSearchReady())) return;
#endif
    mu_slippi_cmd(CMD_FIND_OPPONENT, b, sizeof b, NULL, 0, NULL);
}

/* Player Selections Transfer Buffer. */
void mu_slippi_set_selections(const MuSlippiSelections* s)
{
    u8 b[9];
    b[0] = s->team_id;
    b[1] = s->char_id;
    b[2] = s->char_color;
    b[3] = s->char_opt;
    b[4] = (u8) (s->stage_id >> 8);
    b[5] = (u8) s->stage_id;
    b[6] = s->stage_opt;
    b[7] = s->online_mode;
    b[8] = s->alt_stage_mode;
    mu_slippi_cmd(CMD_SET_MATCH_SELECTIONS, b, sizeof b, NULL, 0, NULL);
}

void mu_slippi_cleanup_connections(void)
{
    mu_slippi_cmd(CMD_CLEANUP_CONNECTIONS, NULL, 0, NULL, 0, NULL);
}

unsigned int mu_slippi_new_seed(void)
{
    u8 b[4] = { 0, 0, 0, 0 };
    mu_slippi_cmd(CMD_GET_NEW_SEED, NULL, 0, b, sizeof b, NULL);
    return be32(b);
}

/* The bridge expects two payload bytes for BB. */
/* The Slippi Online mode picked on the menu (0-4; 2 = Direct): Melee Maker exchanges builds only in
 * Direct (maker/platform/online_sync.c). */
int mu_slippi_selected_mode(void)
{
    return slp.mode;
}

void mu_slippi_send_chat(int message_id)
{
    u8 b[2];
    b[0] = (u8) message_id;
    b[1] = 0;
    mu_slippi_cmd(CMD_SEND_CHAT, b, sizeof b, NULL, 0, NULL);
}

/* AutoComplete FetchSuggestion: 8x3 input, length, index u32, scroll, mode; the reply is found,
 * 8x3 suggestion, length, index u32. */
int mu_slippi_code_suggestion(const MuCodeSuggestionReq* in, MuCodeSuggestion* out)
{
    u8 b[31], r[30];
    int result;
    memcpy(b, in->input, 24);
    b[24] = in->length;
    put32(b + 25, in->index);
    b[29] = in->scroll;
    b[30] = in->mode;
    memset(r, 0, sizeof r);
    result = mu_slippi_cmd(CMD_NAME_ENTRY_INDEX, b, sizeof b, r, sizeof r, NULL);
    if (out != NULL) {
        out->found = r[0];
        memcpy(out->text, r + 1, 24);
        out->length = r[25];
        out->index = be32(r + 26);
    }
    return result;
}

/* ---- scene predicates ---- */

int mu_slippi_in_online_mode(void)
{
    return mu_slippi_menus_enabled() && installed && gm_GetCurrentGameMode() == GM_HANYU_CSS;
}

int mu_slippi_online_state(void)
{
    return mu_slippi_in_online_mode() ? gm_GetCurrentSceneIndex() : -1;
}

int mu_slippi_on_online_css(void) { return mu_slippi_online_state() == MU_SLP_STATE_CSS; }
int mu_slippi_on_online_sss(void) { return mu_slippi_online_state() == MU_SLP_STATE_SSS; }
int mu_slippi_in_online_game(void) { return mu_slippi_online_state() == MU_SLP_STATE_VS; }
int mu_slippi_on_online_results(void) { return mu_slippi_online_state() == MU_SLP_STATE_RESULTS; }
int mu_slippi_on_online_splash(void) { return mu_slippi_online_state() == MU_SLP_STATE_SPLASH; }
int mu_slippi_zelda_is_sheik(void)
{
#if MU_SQUAD
    /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
     * Squads keep the picker's exact Zelda/Sheik kinds, also once a friend room turns them on. */
    if (gmSquad_OnlineChosen() || gmSquad_LobbyChosen()) return 0;
#endif
    return mu_slippi_menus_enabled() && zelda_sheik;
}
unsigned mu_slippi_online_load_count(void) { return online_loads; }

/* IncreaseTextHeap: the online CSS gets a 0x4800-byte text heap instead of 0x2400. */
int mu_slippi_css_text_heap(void)
{
    return mu_slippi_on_online_css() ? 0x4800 : 0;
}

/* ---- helpers (Online/Static) ---- */

/* GetTeamCostumeIndex. */
unsigned char mu_slippi_team_costume(int team, int ckind)
{
    switch (team) {
    case 1:
        return gm_80169264((u8) ckind);
    case 3:
        return gm_80169290((u8) ckind);
    default:
        return gm_801692BC((u8) ckind);
    }
}

/* YouGame has four top-level rows or two focusable Slippi rows. The latter gate
 * confirmation on the app's Slippi sign-in. Standalone keeps
 * today's app-state locks: Unranked/Direct/Teams/Party, Log in, or Update. */
int mu_slippi_option_unlocked(int sel)
{
    if (split_layout() ? split_agent_locked(sel) :
        mu_online_agent_locked(agent_copy, pages(), slippi_submenu,
                               mu_slippi_menu_option(sel), local_peer)) return 0;
    /* Both pages: every row is drawn (a pick that needs a sign-in says so); YouGame Ranked in the app
     * only while the app offers it. */
    if (pages()) return sel >= 0 && sel < mu_slippi_menu_count() &&
        (slippi_submenu || sel != MU_YG_ROW_RANKED || ranked_offered || split_layout());
    if (split_layout()) {
        if (split_page == 0) {
            if (sel < SPLIT_SLIPPI) {
                /* YouGame: the launcher's account, or not connected yet (picking starts YouGame) */
                return (yg_flags & (YG_ROWS_OPEN | YG_COMING_SOON)) != 0;
            }
            return sel == SPLIT_SLIPPI;   /* Slippi: its page, or its log in / update */
        }
        return sel >= SPLIT_FIRST_SLIPPI && slp.app_state == MU_SLP_APP_LOGGED_IN;
    }
    if (sel == MU_SLP_OPT_FRIENDS || sel == MU_SLP_OPT_SLIPPI) return 0;
    switch (slp.app_state) {
    case MU_SLP_APP_LOGGED_IN:
        /* No Slippi Ranked in this build (owner 11-18): its row stays shut whatever the host offers. */
        return sel >= MU_SLP_MODE_UNRANKED && sel <= MU_SLP_MODE_PARTY;
    case MU_SLP_APP_UPDATE:
        return sel != MU_SLP_MODE_RANKED && sel != MU_SLP_MODE_UNRANKED &&
               sel != MU_SLP_MODE_DIRECT && sel != MU_SLP_MODE_TEAMS &&
               sel != MU_SLP_MODE_PARTY && sel != MU_SLP_OPT_LOGIN && sel != MU_SLP_OPT_LOGOUT;
    default:
        return sel != MU_SLP_MODE_RANKED && sel != MU_SLP_MODE_UNRANKED &&
               sel != MU_SLP_MODE_DIRECT && sel != MU_SLP_MODE_TEAMS &&
               sel != MU_SLP_MODE_PARTY && sel != MU_SLP_OPT_LOGOUT && sel != MU_SLP_OPT_UPDATE;
    }
}

/* YouGame starts on Ranked when offered, otherwise Unranked. Slippi starts on Direct. */
int mu_slippi_first_unlocked(void)
{
    int i;
    int count = mn_803EB6B0[MENU_KIND_8].selection_count;
    if (pages()) {
        /* The row last picked on this page, else its second row (owner 11-19: both pages open on
         * their second row), else its first open row. */
        if (pages_last_option >= 0 && pages_last_page == slippi_submenu) {
            const int last = mu_online_menu_row(pages(), slippi_submenu, pages_last_option);
            if (mu_slippi_menu_option(last) == pages_last_option && mn_80229938(MENU_KIND_8, last)) return last;
        }
        if (mu_slippi_menu_count() > 1 && mn_80229938(MENU_KIND_8, 1)) return 1;
        for (i = 0; i < mu_slippi_menu_count(); i++) if (mn_80229938(MENU_KIND_8, i)) return i;
        return 0;
    }
    /* The split layout: the first open row of the page shown. */
    for (i = split_layout() ? 0 : MU_SLP_MODE_UNRANKED; i < count; i++) {
        if (mn_80229938(MENU_KIND_8, i)) {
            break;
        }
    }
    return i < count ? i : 0;
}

/* ---- boot (boot.asm) and the main menu (Online/Menus/TitleMenu) ---- */

/* boot.asm: boot leaves straight to the main menu (no title, no opening movie), and the first
 * menu entry opens the online submenu. The Stadium transformation preloads are for the EXI
 * loader and have no native counterpart (disc reads are synchronous). */
unsigned char mu_slippi_boot_mode(unsigned char mode)
{
    if (!mu_slippi_menus_enabled()) {
        return mode;
    }
    boot_oneshot = 1;
    return GM_MENU;
}

/* OnMenuPrep, every main-menu entry: the online submenu table, the 1P description of row 2, and
 * the app state (InitBuffers + FetchSlippiAppState). */
void mu_slippi_menu_prep(void)
{
    menus_relatch();
    if (!mu_slippi_menus_enabled()) {
        return;
    }
    agent_lock_logged = 0;
#if MU_SQUAD
    /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
    {
        /* A Squad Strike search the player chose to keep while playing elsewhere: its lineup stays for
         * the way back to its select (the host answers 0x4A; an older host answers nothing). */
        u8 ask = 0x4A, kept = 0;
        unsigned int got = 0;
        if (mu_slippi_cmd(CMD_YOUGAME_MENU, &ask, 1, &kept, 1, &got) == 0 && got == 1 && kept)
            OSReport("squad: a kept Squad Strike search: the lineup stays\n");
        else
            gmSquad_OnlineChoose(false);
    }
#endif
    mu_slippi_fetch_app_state();
    slippi_submenu = pages() ? pages_start() : 0;
    if (pages()) pages_install();
    else mu_mnmain_install_online_menu();
}

static int friends_return;
static int doubles_home_return;   /* B out of the Doubles home: back on Online, Doubles highlighted */
/* The host's Friends lobby CSS ran since the last menu entry (MU_PR_CSS_PICK answered). */
static int friends_css_host;
void mu_slippi_note_friends_css(void) { friends_css_host = 1; }
/* The jump-table entries OnMenuPrep and boot.asm replace: previous mode 8 (OnReturnFromOnline)
 * and, once after boot, previous mode GM_BOOT (OnFirstMenuLoad). Returns 1 when it set the menu.
 * A pending Friends return opens the online menu on Friends here, before the menu is built:
 * mn_80229894 frees the running menu proc, so it can't run from the scene's OnEnter. */
int mu_slippi_menu_enter(int previous_mode, unsigned char* menu_kind, unsigned char* hovered)
{
    int from_friends_css = previous_mode == GM_VS && friends_css_host;
    /* A Friends room opened the online character select (Direct mode underneath). The host answers
     * once, so this asks on every entry and a stale answer never outlives one. */
    u8 friends_query = 14, friends_online = 0;
    unsigned int friends_got = 0;
    const int from_friends_online = mu_slippi_cmd(CMD_YOUGAME_FRIENDS_MENU, &friends_query, 1, &friends_online, 1,
                                                  &friends_got) == 0 && friends_got == 1 && friends_online != 0;
    agent_lock_logged = 0;
    friends_css_host = 0;
    if (!mu_slippi_menus_enabled()) {
        return 0;
    }
    if (previous_mode == GM_VS && tournament_visible) {
        slippi_submenu = 0;
        mu_mnmain_install_online_menu();
        *menu_kind = MENU_KIND_8;
        *hovered = (u8) mu_online_menu_row(pages(), 0, MU_SLP_OPT_TOURNAMENT);
        return 1;
    }
#ifdef MELEE_DOUBLES
    /* A Doubles session's end (its results, or B out of its character select) comes back to Online
     * with Doubles highlighted, where its home reopens. The host's Friends return arrives too late. */
    /* The 4-peer route's Teams character select gives the menu back the same way. */
    /* Only a menu that has the Doubles row takes the return; otherwise it waits for the next entry. */
    const int doubles_teams_back =
        previous_mode == GM_HANYU_CSS && pages() && mu_doubles_take_teams_return();
    if (doubles_teams_back) mu_content_mode(-1);   /* back from online: the offline view again */
    if (pages() && (mu_doubles_take_menu_return() || doubles_teams_back)) {
        friends_return = 0;
        slippi_submenu = 0;
        mu_mnmain_install_online_menu();
        *menu_kind = MENU_KIND_8;
        *hovered = (u8) mu_online_menu_row(pages(), 0, MU_SLP_OPT_DOUBLES);
        return 1;
    }
    /* B out of the lobby's Doubles home (lobby_css_decide): back on Online with Doubles highlighted, where
     * the home reopens, never the Direct-mode return (the Slippi submenu). */
    if (doubles_home_return) {
        doubles_home_return = 0;
        if (pages()) {
            friends_return = 0;
            slippi_submenu = 0;
            mu_mnmain_install_online_menu();
            *menu_kind = MENU_KIND_8;
            *hovered = (u8) mu_online_menu_row(pages(), 0, MU_SLP_OPT_DOUBLES);
            return 1;
        }
    }
#endif
    /* B out of the host's Friends CSS closes the lobby: back on Online, Friends highlighted. */
    if (friends_return || from_friends_css) {
        if (from_friends_css) OSReport("friends: left the lobby's character select; online menu on Friends\n");
        friends_return = 0;
        if (previous_mode == GM_BOOT) boot_oneshot = 0;
        slippi_submenu = 0;
        mu_mnmain_install_online_menu();
        *menu_kind = MENU_KIND_8;
        *hovered = (u8) mu_online_menu_row(pages(), 0, MU_SLP_OPT_FRIENDS);
        return 1;
    }
    if (previous_mode == GM_HANYU_CSS) {
        mu_content_mode(-1);   /* back from online: the offline view again */
        *menu_kind = MENU_KIND_8;
        if ((ranked_supported || (split_layout() && (yg_flags & YG_FRIENDS_FLOW))) && from_friends_online) {
            /* Friends runs in Direct mode: back on Online with Friends highlighted, not the Slippi
             * submenu (and, one B later, the hidden Slippi row). Under the launcher too, for a friend
             * room joined over the local channel (an invite accepted there). */
            OSReport("friends: left the friend room's character select; online menu on Friends\n");
            slippi_submenu = 0;
            mu_mnmain_install_online_menu();
            *hovered = (u8) mu_online_menu_row(1, 0, MU_SLP_OPT_FRIENDS);
        } else if (pages()) {
            /* Back from a match or its character select: YouGame's page (always the opening page, owner
             * 11-19), on the row last picked there, else its second row. */
            slippi_submenu = pages_start();
            pages_install();
            *hovered = (u8) mu_slippi_first_unlocked();
        } else {
            *hovered = mn_80229938(MENU_KIND_8, slp.mode) ? slp.mode : (u8) mu_slippi_first_unlocked();
        }
        return 1;
    }
    if (previous_mode == GM_BOOT && boot_oneshot) {
        boot_oneshot = 0;
        mu_slippi_fetch_app_state();
        /* Started from YouGame for a local match: the main menu, as the console boots (build 103).
         * A P2P session's players are already known, so it still goes straight to Online. */
        if (boot_main_menu && !mu_online_p2p_menus()) {
            OSReport("online menu: boot opens the main menu (no online mode asked)\n");
            return 0;
        }
        *menu_kind = MENU_KIND_8;
        *hovered = (u8) mu_slippi_first_unlocked();
        if (pages() && yg_open) {
            const int slot = (yg_open & YG_OPEN_DIRECT) ? 2 : 0;   /* YouGame Friends or YouGame Ranked */
            yg_open = 0;
            slippi_submenu = 0;
            pages_install();
            if (cursor_stops(slot)) {
                *hovered = (u8) slot;
                auto_slot = slot;
            }
        }
        if (mu_online_p2p_menus()) {
            mu_slippi_load_match_state(NULL);
            /* The session's players are already known: no choice of mode, straight to Direct's
             * character select. The row is only open when the host reports a signed-in state. */
            if (pages()) {
                slippi_submenu = 1;   /* Slippi Direct's page */
                pages_install();
            }
            if (mn_80229938(MENU_KIND_8, direct_row())) {
                *hovered = (u8) direct_row();
                p2p_auto_direct = 1;
            } else {
                OSReport("[p2p] menus: Direct is locked (app state %d); staying on the online menu\n",
                         slp.app_state);
            }
        }
        return 1;
    }
    return 0;
}

/* The host bridge runs outside every menu proc, so it never calls mn_80229894 (that frees the
 * running proc: NULL here, HSD_ASSERT gobjplink.c:109, the 50m host leave crash). From a match
 * scene the next menu entry opens Online on Friends; leaving the lobby's CSS already did. */
void mu_slippi_return_to_friends(void)
{
    if (gm_GetCurrentGameMode() == GM_MENU) {
        friends_return = 0;
        OSReport("friends: lobby closed while in the menu; menu left as it is\n");
        return;
    }
    friends_return = 1;
}
/* OnMenuLoad, end of mnMain_Scene_OnEnter: the "Melee" voice on the first boot, then BA on every
 * entry. The title-screen user text is intentionally off in this build. */
void mu_slippi_main_menu_loaded(void)
{
    u8 friends_menu = 0;
    friends_confirm_hold = 0;
    slippi_link_notice = 0;
    mu_slippi_cmd(CMD_YOUGAME_FRIENDS_MENU, &friends_menu, 1, NULL, 0, NULL);
    if (!mu_slippi_menus_enabled()) {
        friends_return = friends_css_host = doubles_home_return = 0; /* a pending Friends return never outlives a load without the online menu */
        return;
    }
    if (first_boot) {
        lbAudioAx_80026F2C(first_boot);
        lbAudioAx_8002702C(2, 8);
        lbAudioAx_80027168();
        lbAudioAx_80027648();
        lbAudioAx_800237A8(30005, 127, 64);
        first_boot = 0;
    }
    {
        /* Reason 3: this is the main menu's cleanup, so a search the player chose to keep while they
         * play goes on (the host decides; an older host reads it as the plain cleanup). */
        u8 menu = 3;
        mu_slippi_cmd(CMD_CLEANUP_CONNECTIONS, &menu, 1, NULL, 0, NULL);
    }
}

/* OnlineModeOptionSelected -> SwitchToOnlineMenu: open the online submenu from the 1P menu and
 * force the old menu to clear (AllowSwapToSameSubmenu). Runs from the 1P menu's think. */
void mu_slippi_switch_to_online_menu(void)
{
    agent_lock_logged = 0;
    if (pages()) {
        slippi_submenu = pages_start();
        pages_install();
    }
    mn_80229894(MENU_KIND_8, (u16) mu_slippi_first_unlocked(), 1);
    /* Slippi 3.7.0 (OnMenuPrep): without a think function the switch ends before the force. */
    if (mn_803EB6B0[MENU_KIND_8].think != NULL) {
        slp.force_clear = 1;
    }
}

/* AllowSwapToSameSubmenu: the menu clears when the kinds differ or the force flag is set; either
 * way the flag is consumed. */
int mu_slippi_menu_clear_check(int menus_differ)
{
    if (!mu_slippi_menus_enabled()) {
        return menus_differ;
    }
    if (menus_differ || slp.force_clear) {
        slp.force_clear = 0;
        return 1;
    }
    return 0;
}

/* E6 value 8 is a local grant query; rows stay focusable so a locked confirm can
 * explain the sign-in requirement without leaving this menu. Standalone never asks. */
static int upstream_slippi_available(void)
{
    u8 query = 8, available = 0;
    unsigned int got = 0;
    return mu_slippi_cmd(CMD_YOUGAME_FRIENDS_MENU, &query, 1, &available, 1, &got) == 0 &&
           got == 1 && available != 0;
}

static u8 confirm_port(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        if (gm_GetButtonsTriggered((u8) i) & PAD_CONFIRM) {
            return (u8) i;
        }
    }
    return 0;
}

/* These are the cached, mapped controller values used by mn_80229624, not PADRead.
 * repeat2 includes the initial edge. Keep its full 64 bits (PAD_ANY_* is above bit 31)
 * and list every port contributing the chosen direction, since slot 4 ORs all ports. */
static void log_online_highlight(int row, u64 direction, u64 input[4][3])
{
    char ports[640] = "input none", line[1024];
    unsigned used = 0;
    int i;
    for (i = 0; i < 4; ++i) {
        if (input[i][2] & direction) {
            used += snprintf(ports + used, sizeof ports - used,
                "%sport %d held 0x%04llx edge 0x%04llx repeat 0x%04llx (%s)",
                used ? "; " : "", i + 1, (unsigned long long) input[i][0],
                (unsigned long long) input[i][1], (unsigned long long) input[i][2],
                input[i][1] & direction ? "press" : "repeat");
        }
    }
    /* OSReport has a 512-byte buffer; four contributing ports must retain the suffix too. */
    snprintf(line, sizeof line,
        "online menu: row %d (option %d) highlighted; frame %u, %s; yougame %d, submenu %d, ranked offered %d, friends offered %d, app state %d\n",
        row, mu_slippi_menu_option(row), VIGetRetraceCount(), ports,
        ranked_supported, slippi_submenu, ranked_offered, friends_offered, slp.app_state);
    mu_online_abi_log(line);
}

/* OnMenuPrep FN_OnlineSubmenuThink (ported from the regular-match think). The logout dialog is
 * unreachable: Log out is locked in every app state of this build. */
void mu_slippi_online_menu_think(HSD_GObj* gp)
{
    u32 buttons;
    u64 input[4][3], direction = 0;
    int port;
    MenuExitData* data;
    int auto_direct = 0;
    int selected_row, selected_option, steps;
    /* The app's Friends row, or the launcher's over the local channel (split_friends). */
    int friends_visible = (ranked_supported && !slippi_submenu) || split_friends();
    int split_friends_press = 0;   /* the launcher's Friends row was confirmed this frame (E6 17 below) */
    /* The Doubles row and its home are on YouGame's page in both layouts: the app's, and the launcher's
     * (the home's tickets then come through the launcher's bridge). */
    const int doubles_visible = pages() && !slippi_submenu;
    int online_visible = 1;
    /* The menu takes no input while its entry cooldown runs (mn_80229624 answers 0 and counts it
     * down), so the frame it reaches 0 is the first one a player's own press could confirm on. */
    int accepts_input = mn_804D6BC8.cooldown == 0;
    (void) gp;

    /* The getters only read controller_map; mn_80229624 reads the OR of this same snapshot. */
    for (port = 0; port < 4; ++port) {
        input[port][0] = gm_GetButtonsPressed((u8) port);
        input[port][1] = gm_GetButtonsTriggered((u8) port);
        input[port][2] = gm_801A36C0((u8) port);
    }
    buttons = mn_80229624(4);
    if (yg_wait_slot >= 0) {
        /* A YouGame row was picked before YouGame was connected: look again about once a second and
         * confirm that row by itself once the rows open. Moving off it or leaving the menu cancels. */
        if (mn_804A04F0.cur_menu != MENU_KIND_8 || slippi_submenu != 0 ||
            mn_804A04F0.hovered_selection != yg_wait_slot || ++yg_wait_frames > 60u * 600u) {   /* the sign-in code's 10 minutes */
            yg_wait_slot = -1;
            yg_wait_friends = 0;
        } else if (yg_wait_frames % 60u == 0) {
            int flags = yougame_query(-1, NULL, 0);
            if (yg_wait_friends) {
                if ((flags & YG_FRIENDS_FLOW) || yg_wait_frames >= 60u * 20u || yougame_query(0x4B, NULL, 0) != 1) {
                    yg_flags = flags;
                    auto_slot = yg_wait_slot;
                    yg_wait_slot = -1;
                    yg_wait_friends = 0;
                    yg_friends_waited = 1;
                    OSReport("online menu: Friends: %s; continuing into row %d\n", (flags & YG_FRIENDS_FLOW) ?
                             "the YouGame app is connected (friend flow)" : "no YouGame app channel (code search)", auto_slot);
                }
            } else if (flags & YG_ROWS_OPEN) {
                yg_flags = flags;
                auto_slot = yg_wait_slot;
                yg_wait_slot = -1;
                OSReport("online menu: YouGame is connected; continuing into row %d\n", auto_slot);
            }
        }
    } else if (mn_804A04F0.cur_menu == MENU_KIND_8 && split_layout() &&
               !(yg_flags & (YG_ROWS_OPEN | YG_COMING_SOON))) {
        /* The menu opened before YouGame knew the player (its first answer can come a second after the
         * launcher's restart): look again about once a second and open the rows as soon as it does,
         * rather than only on the next visit to this menu. */
        if (++yg_closed_frames % 60u == 0) {
            int flags = yougame_query(-1, NULL, 0);
            if (flags & YG_ROWS_OPEN) {
                yg_flags = flags;
                yg_open |= flags & (YG_OPEN_RANKED | YG_OPEN_DIRECT);
                OSReport("online menu: YouGame is connected; its rows open\n");
            }
        }
    }
    if (auto_slot >= 0 && accepts_input) {
        /* Once: confirm the YouGame row the launcher started this game for. */
        mn_804A04F0.hovered_selection = (u8) auto_slot;
        buttons = MenuInput_Confirm;
        auto_slot = -1;
    }
    /* Refuse even a stale/forced highlight before any confirm side effect or submenu change. */
    if ((buttons & MenuInput_Confirm) &&
        (split_layout() ? split_agent_locked(mn_804A04F0.hovered_selection) :
         mu_online_agent_locked(agent_copy, pages(), slippi_submenu, mu_slippi_menu_option(mn_804A04F0.hovered_selection), local_peer))) {
        if (!agent_lock_logged) {
            mu_online_abi_log("agent test: slippi locked");
            agent_lock_logged = 1;
        }
        return;
    }
    if (ranked_supported && tournament_menu_think(gp, buttons)) return;
    /* The refusal owns Y until an actual navigation/back/confirm leaves it. Cooldown and
     * focus changes never withdraw the context of an open app sheet. */
    if (slippi_link_notice) {
        if (buttons & (MenuInput_Back | MenuInput_Up | MenuInput_Down | MenuInput_Confirm)) {
            u8 leave = 4;
            slippi_link_notice = 0;
            mu_slippi_cmd(0xE7, &leave, 1, NULL, 0, NULL);
        } else {
            for (port = 0; port < 4; ++port) {
                if (accepts_input && (input[port][1] & 0x0800)) {
                    u8 press = 3; /* mapped GameCube Y, never a plain keyboard key */
                    mu_slippi_cmd(0xE7, &press, 1, NULL, 0, NULL);
                    break;
                }
            }
            mu_slippi_signin_notice(gp);
        }
    }
    if (p2p_auto_direct && accepts_input) {
        /* Once per run: confirm Direct as a press on this frame would, whatever the pads hold. */
        p2p_auto_direct = 0;
        if (mu_online_p2p_menus() && mn_804A04F0.cur_menu == MENU_KIND_8 &&
            mn_80229938(MENU_KIND_8, direct_row()))
        {
            auto_direct = 1;
            if (ranked_supported && mu_ranked_setup_cpu()) {
                slippi_submenu = 0;
                mu_mnmain_install_online_menu();
            }
            mn_804A04F0.hovered_selection = (u8) mu_online_menu_row(pages(), slippi_submenu,
                    ranked_supported && mu_ranked_setup_cpu() ? MU_SLP_MODE_RANKED : MU_SLP_MODE_DIRECT);
            buttons = MenuInput_Confirm;
            OSReport("[p2p] menus: entering the Direct character select (pad port %d)\n",
                     mu_online_p2p_port());
        }
    }
#ifdef MELEE_DOUBLES
    /* A join opens the Doubles home over this menu with no press here: put the cursor on Doubles
     * once per opening, so the highlighted row and its description match the home. */
    if (doubles_visible && mn_804A04F0.cur_menu == MENU_KIND_8) {
        u8 home_query = 13, home_open = 0;
        unsigned int got = 0;
        const int home = mu_slippi_cmd(CMD_YOUGAME_FRIENDS_MENU, &home_query, 1, &home_open, 1, &got) == 0 &&
                         got == 1 && home_open != 0;
        if (home && !doubles_home_seen)
            mn_804A04F0.hovered_selection = (u8) mu_online_menu_row(pages(), 0, MU_SLP_OPT_DOUBLES);
        doubles_home_seen = home;
    }
    /* The home found a match on the 4-peer route: each seat picks on its own Teams character select
     * (E6 value 14 answers once, with this seat's team from the room). */
    if (doubles_visible && mn_804A04F0.cur_menu == MENU_KIND_8) {
        u8 teams_query = 14, teams[2] = { 0, 0xFF };
        unsigned int teams_got = 0;
        if (mu_slippi_cmd(CMD_YOUGAME_FRIENDS_MENU, &teams_query, 1, teams, 2, &teams_got) == 0 &&
            teams_got == 2 && teams[0] == 1)
        {
            OSReport("doubles: opening the Teams character select (team %d)\n", teams[1] == 0xFF ? -1 : teams[1]);
#if MU_SQUAD
            /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
            gmSquad_OnlineChoose(0);
#endif
            mu_doubles_teams_enter(teams[1]);
            slp.mode = MU_SLP_MODE_TEAMS;
            slp.is_winner = MU_SLP_WINNER_NULL;
            slp.chose_stage = 0;
            slp.team_idx = (u8) (teams[1] == 1 ? 2 : 1);   /* the toggle's 1-based team; red 1, blue 2 */
            mu_content_mode(slp.mode);
            data = gm_GetCurrentSceneExitData();
            data->pending_mode = GM_HANYU_CSS;
            gm_801A4B60();
            return;
        }
    }
#endif
    /* Confirm may replace the menu and its row mapping below. Log the option
     * actually pressed, before entering or leaving the Slippi submenu. */
#if MU_SQUAD
    /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
     * "Play a friend": Y on the Squad Strike row (A searches online). Squad Strike goes with the
     * Friends row, which opens the friend picker as its own press does; the friend room then plays
     * Squad Strike, and the friend's game joins it. Mapped GameCube Y only, like the Slippi notice. */
    if (ranked_supported && friends_visible && accepts_input && mn_804A04F0.cur_menu == MENU_KIND_8 &&
        mu_slippi_menu_option(mn_804A04F0.hovered_selection) == MU_SLP_OPT_SQUAD &&
        !(buttons & (MenuInput_Confirm | MenuInput_Back | MenuInput_Up | MenuInput_Down)))
    {
        const int friends_row = mu_online_menu_row(ranked_supported, 0, MU_SLP_OPT_FRIENDS);
        for (port = 0; port < 4; ++port) {
            if (!(input[port][1] & 0x0800)) continue;
            if (!mn_80229938(MENU_KIND_8, friends_row)) {
                sfxBack();
                OSReport("squad: play a friend refused, Friends is locked\n");
                break;
            }
            gmSquad_OnlineChooseFriend(false);
            mn_804A04F0.hovered_selection = (u8) friends_row;
            mn_804D6BC8.cooldown = 5;
            friends_confirm_hold = 1;
            squad_friend_invite = 1;
            sfxForward();
            OSReport("squad: play a friend from the Squad Strike row (pad %d)\n", port + 1);
            break;
        }
    }
#endif
    if (pages_flip_pending) {
        pages_flip_pending = 0;
        if (pages() && mn_804A04F0.cur_menu == MENU_KIND_8 && slippi_submenu && !pages_player_flipped) {
            pages_flip(1);
            return;
        }
    }
    if (pages() && accepts_input && mn_804A04F0.cur_menu == MENU_KIND_8 &&
        !(buttons & (MenuInput_Confirm | MenuInput_Back | MenuInput_Up | MenuInput_Down))) {
        u64 pressed = 0;
        for (port = 0; port < 4; ++port) pressed |= input[port][1];
        if ((pressed & 0x0010) && !agent_copy) {   /* mapped GameCube Z: the other page */
            pages_player_flipped = 1;
            pages_flip(0);
            return;
        }
        /* D-pad or stick left / right (the menu's own merged input), or L / R (owner 11-18): Unranked's slider. */
        if (mu_slippi_menu_option(mn_804A04F0.hovered_selection) == MU_SLP_MODE_UNRANKED && !agent_copy) {
            const int left = (buttons & MenuInput_Left) || (pressed & 0x0040);     /* mapped GameCube L */
            const int right = (buttons & MenuInput_Right) || (pressed & 0x0020);   /* mapped GameCube R */
            if (left != right) pages_unranked_step(right ? 1 : -1);
        }
    }
    selected_row = mn_804A04F0.hovered_selection;
    selected_option = mu_slippi_menu_option(selected_row);
    mn_804A04F0.buttons = buttons;
    if ((buttons & MenuInput_Confirm) && ranked_supported && tournament_offered &&
        selected_option == MU_SLP_OPT_TOURNAMENT) {
        tournament_query(MU_TOURNEY_JOIN);
        friends_visible = 0;
        mn_804D6BC8.cooldown = 5;
        sfxForward();
        mu_slippi_tournament_notice(gp, tournament_text);
#ifdef MELEE_DOUBLES
    } else if ((buttons & MenuInput_Confirm) && ranked_supported && !slippi_submenu &&
               selected_option == MU_SLP_OPT_DOUBLES) {
        /* The Doubles row opens the 2-seat Doubles home's select (G2: the Friends lobby on its Doubles
         * profile, E6 value 16). The 4-seat md:1 console home (value 12) stays for md:1 invites. The app's
         * layout only: under the launcher (split layout) the row's own branch below sends the YouGame pick
         * first, and the host drops a value 16 that comes without it. friends_visible is not the test here:
         * it is also true under the launcher once the YouGame app's channel is up (split_friends). */
        u8 open_doubles = 16;
        mu_slippi_cmd(CMD_YOUGAME_FRIENDS_MENU, &open_doubles, 1, NULL, 0, NULL);
        mn_804D6BC8.cooldown = 5;
        sfxForward();
#endif
    } else if ((buttons & MenuInput_Confirm) && friends_visible &&
        on_friends_row(mn_804A04F0.hovered_selection))
    {
        /* A and START both confirm Friends. The host keeps the menu here until the
         * picker returns; still apply the ordinary confirm cooldown. */
        mn_804D6BC8.cooldown = 5;
        friends_confirm_hold = 1;
        yg_friends_waited = 0;
        if (split_layout()) {
            /* Over the local channel: a YouGame row (the host's launcher session), the cursor comes back
             * here, and the host is asked for the friend flow below, after this frame's publish. The app
             * opens it from the pad press instead; a confirm the menu made itself (auto_slot) has none. */
            yg_pick = 1;
            pages_last_option = MU_SLP_OPT_FRIENDS;
            pages_last_page = 0;
            yougame_query(yg_pick, NULL, 0);
            split_friends_press = 1;
            sfxForward();
            OSReport("online menu: Friends (YouGame) confirmed; asking for YouGame's friend flow\n");
        }
    } else if ((buttons & MenuInput_Confirm) && split_layout() && !pick_slippi() && !split_friends() &&
               selected_option == MU_SLP_OPT_FRIENDS && (yg_flags & YG_ROWS_OPEN) && !yg_friends_waited &&
               yougame_query(0x4B, NULL, 0) == 1) {
        /* Owner 11-21: Friends with "Launch with YouGame extension" opens YouGame's friend flow, not the connect-code
         * search, when the YouGame app is there. The launcher starts the app in the background; until its channel
         * is up, the row waits (at most 20 s), then goes on into the flow (or the code search if it never came). */
        yg_wait_slot = mn_804A04F0.hovered_selection;
        yg_wait_frames = 0;
        yg_wait_friends = 1;
        mn_804D6BC8.cooldown = 30;
        sfxForward();
        pages_notice_show("Starting YouGame Friends...");
        OSReport("online menu: row %d: Friends waits for the YouGame app's channel\n", yg_wait_slot);
    } else if ((buttons & MenuInput_Confirm) && ranked_supported &&
               mu_slippi_menu_option(mn_804A04F0.hovered_selection) == MU_SLP_OPT_SLIPPI) {
        slippi_submenu = 1;
        friends_visible = 0;
        mu_mnmain_install_online_menu();
        mn_804D6BC8.cooldown = 5;
        sfxForward();
        mn_80229894(MENU_KIND_8, 0, 1);
        slp.force_clear = 1;
    } else if ((buttons & MenuInput_Confirm) && ranked_supported && pick_slippi() &&
               !upstream_slippi_available()) {
        mn_804D6BC8.cooldown = 5;
        sfxBack();
        slippi_link_notice = 1;
        mu_slippi_signin_notice(gp);
    } else if ((buttons & MenuInput_Confirm) && split_layout() && pick_slippi() &&
               slp.app_state != MU_SLP_APP_LOGGED_IN) {
        /* Slippi's page before Slippi is signed in: its log in (or update), as its row opened before. */
        mn_804D6BC8.cooldown = 5;
        sfxForward();
        mu_slippi_cmd(slp.app_state == MU_SLP_APP_UPDATE ? CMD_UPDATE_APP : CMD_OPEN_LOGIN, NULL, 0, NULL, 0, NULL);
        OSReport("online menu: Slippi is not signed in; opening its %s\n", slp.app_state == MU_SLP_APP_UPDATE ? "update" : "log in");
    } else if ((buttons & MenuInput_Confirm) && split_layout() && !pick_slippi() && !yougame_soon() &&
               !(yg_flags & YG_ROWS_OPEN)) {
        /* The launcher is not signed in to YouGame: the host asks it to open its own Connect window (EF 0x22),
         * and the menu waits on the row as after 0x20, going on into it once the rows open. 0: no launcher to
         * ask (the row stays refused). */
        if (yougame_query(0x22, NULL, 0) == 1) {
            yg_wait_slot = mn_804A04F0.hovered_selection;
            yg_wait_frames = 0;
            mn_804D6BC8.cooldown = 30;
            sfxForward();
            pages_notice_show("Sign in to YouGame in the new window.");
            OSReport("online menu: row %d: YouGame is not signed in; opening the launcher's Connect\n", yg_wait_slot);
        } else {
            mn_804D6BC8.cooldown = 5;
            sfxBack();
            pages_notice_show("Connect YouGame in the launcher first.");
            OSReport("online menu: row %d refused: YouGame is not signed in\n", mn_804A04F0.hovered_selection);
        }
#ifdef MELEE_DOUBLES
    } else if ((buttons & MenuInput_Confirm) && split_layout() && !slippi_submenu && !yougame_soon() &&
               selected_option == MU_SLP_OPT_DOUBLES) {
        /* Under the launcher the Doubles row opens the same 2-seat Doubles home (E6 value 16): the host
         * holds the home through the launcher's bridge, named by YouGame's own record of this release
         * (launcher_yougame.cpp listed_build_id). The pick tells the host this is a YouGame row. */
        u8 open_doubles = 16;
        yg_pick = 1;
        pages_last_option = selected_option;
        pages_last_page = 0;
        yougame_query(yg_pick, NULL, 0);
        mu_slippi_cmd(CMD_YOUGAME_FRIENDS_MENU, &open_doubles, 1, NULL, 0, NULL);
        mn_804D6BC8.cooldown = 5;
        sfxForward();
        OSReport("online menu: row %d opens the Doubles home\n", mn_804A04F0.hovered_selection);
#endif
    } else if ((buttons & MenuInput_Confirm) && yougame_soon() && !pick_slippi()) {
        /* Ranked, Unranked or Friends while YouGame is not connected: the host runs YouGame's start
         * hook (YouGame's own small window when it is not on this computer); the menu stays on the row
         * and continues into it once YouGame is connected and the rows open. */
        yg_wait_slot = mn_804A04F0.hovered_selection;
        yg_wait_frames = 0;
        yg_asked = 1;
        yougame_query(0x20, NULL, 0);
        mn_804D6BC8.cooldown = 30;
        sfxForward();
        OSReport("online menu: row %d picked before YouGame was connected; starting YouGame\n", yg_wait_slot);
    } else if (buttons & MenuInput_Confirm) {
        yg_friends_waited = 0;   /* a Friends wait that ended is used by this confirm */
        mn_804D6BC8.cooldown = 5;
        mn_804A04F0.entering_menu = 1;
        friends_visible = 0;
        online_visible = 0;
        gm_801677E8(auto_direct ? (s8) mu_online_p2p_port() : (s8) confirm_port());
        {
        int sel = mn_804A04F0.hovered_selection;
        int act = mu_slippi_menu_option(sel);
        /* No Slippi Ranked in this build (owner 11-18): Ranked is YouGame's row only. Neither page lists
         * a Slippi Ranked row; a pick that would still send Ranked to Slippi is refused here. */
        if (act == MU_SLP_MODE_RANKED && (!pages() || pick_slippi())) {
            mn_804D6BC8.cooldown = 5;
            sfxBack();
            OSReport("online menu: row %d refused: Slippi Ranked is not in this build\n", sel);
            return;
        }
        /* YouGame Friends under the launcher: a code search on YouGame (Direct with the YouGame pick). */
        if (split_layout() && act == MU_SLP_OPT_FRIENDS) act = MU_SLP_MODE_DIRECT;
#if MU_SQUAD
        /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
        gmSquad_OnlineChoose(act == MU_SLP_OPT_SQUAD);
#endif
        switch (act) {
#if MU_SQUAD
        /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
        case MU_SLP_OPT_SQUAD:
#endif
        case MU_SLP_MODE_RANKED:
        case MU_SLP_MODE_UNRANKED:
        case MU_SLP_MODE_DIRECT:
        case MU_SLP_MODE_TEAMS:
        case MU_SLP_MODE_PARTY:
            slp.mode = (u8) act;
#if MU_SQUAD
            if (gmSquad_OnlineChosen()) slp.mode = MU_SLP_MODE_UNRANKED;
#endif
            if (pages()) {
                /* The host learns which service the next search uses before its character select:
                 * 1 YouGame, 0 Slippi, 2 Unranked on both (Unranked's slider, never Squad Strike). */
                const int both = act == MU_SLP_MODE_UNRANKED && pages_unranked_value() == 1;
                yg_pick = both ? 2 : pick_slippi() ? 0 : 1;
                pages_last_option = mu_slippi_menu_option(sel);
                pages_last_page = slippi_submenu;
                yougame_query(yg_pick, NULL, 0);
                OSReport("online menu: %s picked, pick byte %d\n",
                         both ? "Unranked on YouGame and Slippi" : pick_slippi() ? "a Slippi row" : "a YouGame row", yg_pick);
            }
            if (mu_online_p2p_menus()) {
                /* No game was played yet: neither winner nor loser. The character select gives the
                 * first game's stage pick to one player once the two are connected and their
                 * slots are known (mu_slippi_css.c). */
                slp.is_winner = MU_SLP_WINNER_NULL;
                slp.chose_stage = 0;
            }
            /* The files of the next scene follow the mode: retail online, the mod's in Direct when
             * the player uses it there (mu_content.c). Asked before the online major loads. */
            mu_content_mode(slp.mode);
            sfxForward();
            yougame_ask_withdraw();
            data = gm_GetCurrentSceneExitData();
            data->pending_mode = GM_HANYU_CSS;
            gm_801A4B60();
            break;
        case MU_SLP_OPT_LOGIN:
            sfxForward();
            mu_slippi_cmd(CMD_OPEN_LOGIN, NULL, 0, NULL, 0, NULL);
            break;
        case MU_SLP_OPT_UPDATE:
            sfxForward();
            mu_slippi_cmd(CMD_UPDATE_APP, NULL, 0, NULL, 0, NULL);
            break;
        }
        }
    } else if ((buttons & MenuInput_Back) && ranked_supported && slippi_submenu && !pages()) {
        slippi_submenu = 0;
        friends_visible = 1;
        mu_mnmain_install_online_menu();
        mn_804D6BC8.cooldown = 5;
        sfxBack();
        mn_80229894(MENU_KIND_8, mu_online_menu_row(ranked_supported, 0, MU_SLP_OPT_SLIPPI), 3);
        slp.force_clear = 1;
    } else if (buttons & MenuInput_Back && split_layout() && split_page == 1 && split_start_page() == 0 && !pages()) {
        /* Slippi's page: back to Ranked, Unranked, Friends, Slippi. */
        sfxBack();
        split_page = 0;
        mn_80229894(MENU_KIND_8, SPLIT_SLIPPI, 3);
        slp.force_clear = 1;
    } else if (buttons & MenuInput_Back) {
        friends_visible = 0;
        online_visible = 0;
        sfxBack();
        mn_804A04F0.entering_menu = 0;
        mn_80229894(MENU_KIND_1P, SEL_1P_2, 3);
        yougame_ask_withdraw();
    } else if (buttons & MenuInput_Up) {
        steps = mu_slippi_menu_count();
        direction = PAD_ANY_UP;
        sfxMove();
        /* A Coming soon row (split layout) is drawn, so it reads as unlocked, but never holds the cursor. */
        do {
            do {
                if (mn_804A04F0.hovered_selection == 0) {
                    mn_804A04F0.hovered_selection = mu_slippi_menu_count() - 1;
                } else {
                    mn_804A04F0.hovered_selection--;
                }
            } while (!mn_80229938(MENU_KIND_8, mn_804A04F0.hovered_selection) && --steps > 0);
        } while (!cursor_stops(mn_804A04F0.hovered_selection) && --steps > 0);
    } else if (buttons & MenuInput_Down) {
        steps = mu_slippi_menu_count();
        direction = PAD_ANY_DOWN;
        sfxMove();
        /* A Coming soon row (split layout) is drawn, so it reads as unlocked, but never holds the cursor. */
        do {
            do {
                if (mn_804A04F0.hovered_selection == mu_slippi_menu_count() - 1) {
                    mn_804A04F0.hovered_selection = 0;
                } else {
                    mn_804A04F0.hovered_selection++;
                }
            } while (!mn_80229938(MENU_KIND_8, mn_804A04F0.hovered_selection) && --steps > 0);
        } while (!cursor_stops(mn_804A04F0.hovered_selection) && --steps > 0);
    }
    /* One melee.log line per highlighted row and per select, so a menu walk is provable from the log. */
    {
        static int logged_row = -1, logged_submenu = -1;
        int row = mn_804A04F0.hovered_selection;
        if (!accepts_input) logged_row = -1;
        else if (row != logged_row || slippi_submenu != logged_submenu) {
            log_online_highlight(row, direction, input);
            logged_row = row;
            logged_submenu = slippi_submenu;
        }
        if (accepts_input && (buttons & MenuInput_Confirm))
            OSReport("online menu: row %d (option %d) selected\n", selected_row, selected_option);
    }
    /* Publish after Up/Down so the next PADRead sees the row now under the cursor. */
    {
        /* The entry cooldown still hides the row's selection (a press carried in from the main
         * menu must not open the picker); only the cooldown after a Friends confirm keeps it. */
        const int friends_hovered = on_friends_row(mn_804A04F0.hovered_selection);
        u8 friends_menu;
        /* Released on the first frame that takes input with no cooldown pending (not the confirm
         * frame itself, which sets the cooldown above), so there is no frame without selection. */
        if (!friends_visible || !friends_hovered || (accepts_input && mn_804D6BC8.cooldown == 0))
            friends_confirm_hold = 0;
        friends_menu = !friends_visible ? 0 :
            (accepts_input || friends_confirm_hold) && friends_hovered ? 2 : 1;
        /* Track Online even with the Friends row hidden; bit 2 is status-area visibility. */
        if (online_visible) friends_menu |= 4;
        mu_slippi_cmd(CMD_YOUGAME_FRIENDS_MENU, &friends_menu, 1, NULL, 0, NULL);
        if (split_friends_press) {
            /* After the publish, so the host already sees Friends selected (slippi_online.cpp, E6 17). */
            u8 press = 17;
            mu_slippi_cmd(CMD_YOUGAME_FRIENDS_MENU, &press, 1, NULL, 0, NULL);
        }
#if MU_SQUAD
        /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
        if (gmSquad_OnlineFriend() && (!friends_visible || !friends_hovered)) {
            /* Off the Friends row: Squad Strike no longer goes with the next friend room. */
            gmSquad_OnlineChoose(false);
            squad_friend_invite = 0;
            OSReport("squad: friend room squad mode off (left the Friends row)\n");
        } else if (gmSquad_OnlineFriend()) {
            if (squad_friend_invite) {
                u8 invite[2] = { MU_SQUAD_OP_FRIEND, MU_SQUAD_FRIEND_INVITE }, opening = 0;
                unsigned int got = 0;
                squad_friend_invite = 0;
                if (mu_slippi_cmd(MU_SQUAD_COMMAND, invite, sizeof invite, &opening, 1, &got) || got != 1 || !opening)
                    OSReport("squad: play a friend, the host did not open the friend picker\n");
            } else {
                /* The picker closed without a room (or never opened): Squad Strike goes with it. */
                u8 intent[2] = { MU_SQUAD_OP_FRIEND, MU_SQUAD_FRIEND_INTENT }, on = 0;
                unsigned int got = 0;
                if (mu_slippi_cmd(MU_SQUAD_COMMAND, intent, sizeof intent, &on, 1, &got) || got != 1 || !on) {
                    gmSquad_OnlineChoose(false);
                    OSReport("squad: friend picker closed, Squad Strike off\n");
                }
            }
            /* Re-applied each frame, as the Slippi sign-in line is: the row's own text would come back. */
            if (gmSquad_OnlineFriend()) mu_slippi_tournament_notice(gp, "Invite a friend to Squad Strike.");
        }
#endif
    }
}

/* ---- the online major (Slippi Online Scene/main.asm) ----
 * GM_HANYU_CSS (unused in retail) becomes the online mode: states 0 CSS, 1 SSS, 2 VS, 3 Results,
 * 4 Splash. Slippi writes the raw next-state byte (id + 1); gm_SetNextGameModeStateId(id) is the
 * same write. State 5 reserves GameSetup callbacks for track A. Ranked stays hidden. */

static CSSData online_css_data;   /* Slippi reuses the event CSS buffer; the choice is saved in vs.unk_530 */

/* SplashSceneInit: the negotiated match into the VS mode data (AdjustNullCharID is a no-op). */
void mu_slippi_splash_init(void)
{
    mu_slippi_load_match_state(NULL);
    mu_replay_apply_game_info(&gmVsMelee_GetVsData()->start, last_match.game_info);
#if MU_SQUAD
    /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
    if (mu_lobby_css()) {
        unsigned char query = MU_SQUAD_OP_LOBBY_MODE, squad = 0;
        mu_slippi_cmd(MU_SQUAD_COMMAND, &query, 1, &squad, 1, NULL);
        gmSquad_LobbyChoose(squad != 0);
    }
    if (gmSquad_OnlineChosen() || gmSquad_LobbyChosen()) gmSquad_OnlineSetup(&gmVsMelee_GetVsData()->start);
#endif
}

#if MU_SQUAD
/* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
 * The opponent left between lock-in and the first frame: the host has cancelled the match and
 * says why. Back to the CSS, which restores the lineup, instead of a splash or a game against
 * nobody. */
static int squad_opponent_gone(const char* where)
{
    if (!gmSquad_OnlineLive(slp.mode) && !gmSquad_LobbyChosen()) return 0;
    if (!mu_slippi_load_match_state(NULL) &&
        last_match.connection_state == MU_SLP_MM_CONNECTION_SUCCESS) return 0;
    OSReport("squad: opponent gone at the %s (connection %u), back to the CSS\n", where,
             (unsigned) last_match.connection_state);
    return 1;
}
#endif

/* ---- the 4-player Friends lobby's select (port/runtime/abi/mu_lobby_wire.h) ----
 * The Friends row or an accepted lobby invite armed the lobby before this major loaded. Its select is
 * Melee's VS 4-door one (mu_friends4_css.inc draws the other doors from the view); the host's Start
 * goes through the stage select; once every link is up and everyone locked in, the splash and the
 * match. After it, the same select again with its picks: the room parks, the next ENTER adopts it. */
static struct {
    int major;            /* this online major runs the lobby's select */
    int view_ok;
    MuLobbyView view;     /* this frame's (mu_lobby_read) */
    VsModeData vs;        /* the select's own VS data: picks kept between matches, offline VS untouched */
    int vs_ready;
    int exit;             /* MU_LOBBY_EXIT_*: where the select left to */
    int watching;         /* a guest on the host's stage select: 1 watching, 2 on its way back */
    int played;           /* the select left for a match: the next visit parks the room first */
    int curtain;          /* the stage is picked: the select is only passed through, hidden (mu_lobby_curtain) */
    int curtain_frames, curtain_started;
    unsigned char curtain_refusals;   /* view.refusal_seq when it went up: a new refusal lifts it at once */
    unsigned char arm;    /* the arming this select opened on (view.arm): another one is never left */
    int partner_source, partner_b_held;   /* Doubles: the controller that plays Player 2's door, its B */
    int partner_want;     /* Doubles: the partner door's tab, MU_LOBBY_WANT_* (this machine's own; FIND each home) */
    int cancel_held;      /* Doubles: B was down on the last pad read while the home searched */
    int cancel_mask;      /* Doubles: the B that cancelled the search is kept from the select until let go */
    GameRules rules;      /* the player's own rules, back when the lobby closes */
} lobby;

static int lobby_view_op(unsigned char op, MuLobbyView* out)
{
    unsigned char buf[MU_LOBBY_VIEW_SIZE];
    unsigned int got = 0;
    if (mu_slippi_cmd(MU_LOBBY_COMMAND, &op, 1, buf, sizeof buf, &got) != 0) return 0;
    return mu_lobby_view_decode(buf, got, out);
}
static int lobby_read_into(MuLobbyView* out) { return lobby_view_op(MU_LOBBY_OP_READ, out); }

int mu_lobby_css(void)
{
    return lobby.major && mu_slippi_menus_enabled() && mu_slippi_on_online_css();
}

const MuLobbyView* mu_lobby_read(void)
{
    if (!lobby.major) return NULL;
    if (lobby_read_into(&lobby.view)) lobby.view_ok = 1;
    return lobby.view_ok ? &lobby.view : NULL;
}

int mu_lobby_send(const unsigned char* request, unsigned int size)
{
    unsigned char ok = 0;
    unsigned int got = 0;
    if (!lobby.major || !mu_lobby_request_ok(request, size)) return 0;
    if (mu_slippi_cmd(MU_LOBBY_COMMAND, request, size, &ok, 1, &got) != 0) return 0;
    return got == 1 && ok == 1;
}

void mu_lobby_css_exit(int where) { lobby.exit = where; }

/* The links come up and everyone locks in on the select, so the game passes through it between the
 * stage select and the splash: hidden, with no pads. It shows again if the room calls the match off
 * or never starts it, with the reason, and after 10 s rather than leave a black screen. */
#define LOBBY_CURTAIN_GRACE 90    /* frames the room has to answer the host's Start */
#define LOBBY_CURTAIN_MAX 600

int mu_lobby_curtain(void) { return lobby.major && lobby.curtain; }

void mu_lobby_curtain_frame(const MuLobbyView* v)
{
    int lift;
    if (!lobby.curtain) return;
    ++lobby.curtain_frames;
    if (v && (v->flags & MU_LOBBY_F_STARTED)) {
        lobby.curtain_started = 1;
        lift = 0;
    } else {
        lift = lobby.curtain_started || lobby.curtain_frames > LOBBY_CURTAIN_GRACE ||
               (v && v->refusal_seq != lobby.curtain_refusals);
    }
    if (lift || lobby.curtain_frames > LOBBY_CURTAIN_MAX) {
        OSReport("friends lobby: the match did not start after %d frames, the select shows\n", lobby.curtain_frames);
        lobby.curtain = 0;
    }
}

/* Friends: the host went to the stage select, so this guest's select follows it there. */
void mu_lobby_watch_stage(void)
{
    lobby.exit = MU_LOBBY_EXIT_SSS;
    lobby.watching = 1;
}

/* A guest only watches the stage select: no pad reaches it. It ends when the host's does or the lobby
 * closes or a setup starts (even if the frame closing it was lost), back to the select, where a started
 * setup goes on to the match just as the host's does.
 * gm_801A4B60 is the scene-loop flag only. Returns 1 when these pads were taken. */
int mu_lobby_watch_pads(PADStatus* status)
{
    const MuLobbyView* v;
    if (!lobby.major || !lobby.watching || !mu_slippi_menus_enabled() || !mu_slippi_on_online_sss()) return 0;
    memset(status, 0, sizeof(PADStatus) * 4);
    v = mu_lobby_read();
    if (lobby.watching == 1 && (!v || !(v->mode & MU_LOBBY_M_STAGE_SELECT) || !(v->flags & MU_LOBBY_F_ARMED) ||
                                v->block_hint == MU_LOBBY_HINT_CLOSED || (v->flags & MU_LOBBY_F_STARTED))) {
        OSReport("friends lobby: the stage select ended, back to the select\n");
        /* The host's pick started the match: the guests hear the pick as Melee plays it (sfxForward, the
         * host's own stage select sound), since their stage select takes no press of its own. */
        if (v && (v->flags & MU_LOBBY_F_STARTED)) sfxForward();
        lobby.watching = 2;
    }
    if (lobby.watching == 2) gm_801A4B60();   /* every frame: the scene loop may reset the flag while it loads */
    return 1;
}

/* Only this player's pad, on its own seat's door; the other doors are the lobby's. The other people's
 * seats get still, connected pads (their cursors show, idle); open and CPU seats get none, so their
 * cursors hide as in Melee and the host's HMN/CPU switch goes open -> CPU -> open (an empty port can't
 * be HMN). Before the seat is known, the pad stays on its own port. While the Doubles home searches the
 * select is locked: this pad goes still too, and a B press cancels the search (Rooms refuses it with
 * match_active once the match is placed; the hint says so). That B stays off the select until it is let go,
 * so the press that cancelled the search never also takes back this player's pick. */
void mu_lobby_pads(PADStatus* status)
{
    PADStatus mine, partner;
    int src, seat, i;
    if (!mu_lobby_css()) return;
    src = gm_801677F0() & 3;
    seat = lobby.view_ok && lobby.view.my_seat < MU_LOBBY_SEATS ? lobby.view.my_seat : src;
    mine = status[src];
    memset(&partner, 0, sizeof partner);
    partner.err = PAD_ERR_NO_CONTROLLER;
    if (lobby.view_ok && mu_lobby_local_owned(&lobby.view)) {
        /* Player 2's door (the partner tab on P2 claimed it: mu_friends4_partner_cycle) is played by the first
         * other connected controller, taken as it is plugged in; until one is, the door stays a person's with a
         * still pad, as if a 2nd controller were plugged in. */
        if (lobby.partner_source < 0 || lobby.partner_source == src || status[lobby.partner_source].err != 0) {
            for (i = 0; i < 4; ++i)
                if (i != src && status[i].err == 0) break;
            if (i < 4 && i != lobby.partner_source) {
                OSReport("doubles: Player 2's door is controller %d's\n", i + 1);
                lobby.partner_source = i;
                lobby.partner_b_held = (status[i].button & PAD_BUTTON_B) != 0;   /* a B already down never leaves */
            }
        }
        partner.err = 0;
        if (lobby.partner_source >= 0 && lobby.partner_source != src && status[lobby.partner_source].err == 0) {
            const int b = (status[lobby.partner_source].button & PAD_BUTTON_B) != 0;
            partner = status[lobby.partner_source];
            partner.button &= (u16) ~PAD_BUTTON_START; /* only the main controller starts a search */
            if (!(lobby.view.seats[1].flags & MU_LOBBY_S_PICKED) && b) {
                if (!lobby.partner_b_held) {
                    const unsigned char release[2] = { MU_LOBBY_OP_LOCAL_SEAT, 0 };
                    if (mu_lobby_send(release, sizeof release)) lobby.partner_want = MU_LOBBY_WANT_FIND;
                }
                partner.button &= (u16) ~PAD_BUTTON_B; /* never B out of the home's main door */
            }
            lobby.partner_b_held = b;
        }
    }
    for (i = 0; i < 4; ++i) {
        const int person = lobby.view_ok && (lobby.view.seats[i].kind == MU_LOBBY_KIND_HUMAN || lobby.view.seats[i].kind == MU_LOBBY_KIND_LOCAL);
        memset(&status[i], 0, sizeof status[i]);
        status[i].err = person ? 0 : PAD_ERR_NO_CONTROLLER;
    }
    if (lobby.view_ok && (lobby.view.mode & MU_LOBBY_M_SEARCHING)) {
        const int b = (mine.button & PAD_BUTTON_B) != 0;
        if (b && !lobby.cancel_held) {
            unsigned char cancel = MU_LOBBY_OP_CANCEL;
            OSReport("doubles: B, cancel the search%s\n", mu_lobby_send(&cancel, 1) ? "" : " (not sent)");
            lobby.cancel_mask = 1;
        }
        lobby.cancel_held = b;
        memset(&mine, 0, sizeof mine);
    } else if (lobby.view_ok && (lobby.view.mode & MU_LOBBY_M_DOUBLES) && (lobby.view.flags & MU_LOBBY_F_HOST) &&
               (lobby.view.flags & MU_LOBBY_F_INVITING)) {
        /* Doubles: the host's B takes the invite back (the friend list closes) and the door stays on Invite. */
        const int b = (mine.button & PAD_BUTTON_B) != 0;
        if (b && !lobby.cancel_held) {
            unsigned char cancel = MU_LOBBY_OP_CANCEL;
            OSReport("doubles: B, cancel the invite%s\n", mu_lobby_send(&cancel, 1) ? "" : " (not sent)");
            lobby.cancel_mask = 1;
        }
        lobby.cancel_held = b;
        if (lobby.cancel_mask) mine.button &= (u16) ~PAD_BUTTON_B;
    } else {
        lobby.cancel_held = (mine.button & PAD_BUTTON_B) != 0;   /* a B held into the search never cancels it */
        if (!lobby.cancel_held) lobby.cancel_mask = 0;
        if (lobby.cancel_mask) mine.button &= (u16) ~PAD_BUTTON_B;   /* the cancel's B, still down */
    }
    if (lobby.curtain) memset(&mine, 0, sizeof mine);   /* nothing to act on while it is hidden */
    if (lobby.view_ok && mu_lobby_local_owned(&lobby.view)) status[1] = partner;
    status[seat] = mine;
    status[seat].err = 0;
}

/* The physical controller that claimed door 2 stays fixed through this home's matches. */
int mu_lobby_partner_source(void)
{
    return lobby.major && lobby.view_ok && (lobby.view.mode & MU_LOBBY_M_LOCAL_OWNER) &&
           lobby.view.seats[1].kind == MU_LOBBY_KIND_LOCAL ? lobby.partner_source : -1;
}

/* Doubles: the partner door's tab (mu_friends4_css.inc). Kept on this machine through the home's matches; the
 * room only ever sees what it fills the seat with. */
int mu_lobby_partner_want(void)
{
    return lobby.partner_want;
}
void mu_lobby_set_partner_want(int want)
{
    if (want < MU_LOBBY_WANT_FIND || want > MU_LOBBY_WANT_CPU) want = MU_LOBBY_WANT_FIND;
    lobby.partner_want = want;
}

static void lobby_open(void)
{
    MuLobbyView v;
    memset(&lobby, 0, sizeof lobby);
    if (!lobby_read_into(&v) || !(v.flags & MU_LOBBY_F_ARMED)) return;
#if MU_SQUAD
    gmSquad_OnlineChoose(false); /* the lobby carries its own mode and canonical setup */
    gmSquad_LobbyChoose(false);
#endif
    lobby.partner_source = -1;
    lobby.major = 1;
    lobby.view = v;
    lobby.view_ok = 1;
    lobby.arm = v.arm;
    lobby.rules = *gmMainLib_GetGameRules();
    OSReport("friends lobby: the %s select (%s)\n", (v.mode & MU_LOBBY_M_DOUBLES) ? "Doubles 2-door" : "4-door",
             (v.flags & MU_LOBBY_F_GUEST) ? "guest" : "host");
}

/* Every way out of the major (B, L+R+A+START, an error) leaves the lobby and closes its links, but only
 * the lobby this select opened on: an invite accepted here (a 1v1 room, or another lobby) already let it
 * go or armed a new one (view.arm changed), and a LEAVE now would close that new connection. */
static void lobby_close(void)
{
    unsigned char op = MU_LOBBY_OP_LEAVE, ok = 0;
    MuLobbyView now;
    if (!lobby.major) return;
    /* PEEK, not READ: a READ after a pre-match drop takes the parked room back, which a LEAVE ends. */
    if (lobby_view_op(MU_LOBBY_OP_PEEK, &now) && (now.flags & MU_LOBBY_F_ARMED) && now.arm == lobby.arm)
        mu_slippi_cmd(MU_LOBBY_COMMAND, &op, 1, &ok, 1, NULL);
    else
        OSReport("friends lobby: another match took over; no leave\n");
    *gmMainLib_GetGameRules() = lobby.rules;
    OSReport("friends lobby: left the select\n");
    memset(&lobby, 0, sizeof lobby);
#if MU_SQUAD
    gmSquad_LobbyChoose(false);
#endif
}

static void lobby_css_prep(GameModeState* state)
{
    unsigned char op = MU_LOBBY_OP_ENTER, ok = 0;
    if (!lobby.vs_ready) {
        int i;
        lobby.vs = *gmVsMelee_GetVsData();
        for (i = 0; i < 4; ++i) {
            lobby.vs.start.players[i].ckind = ChKind_None;
            lobby.vs.start.players[i].slot_type = Gm_PKind_NA;
        }
        lobby.vs.start.rules.is_teams = 0;
        lobby.vs_ready = 1;
    }
    if (lobby.played) {
        /* Back from a match: the room parks with its links (cleanup 2 = the next lobby match). */
        unsigned char next = 2;
        mu_slippi_cmd(CMD_CLEANUP_CONNECTIONS, &next, 1, NULL, 0, NULL);
        lobby.played = 0;
    }
    lobby.exit = MU_LOBBY_EXIT_NONE;
    lobby.watching = 0;
    lobby.curtain_frames = lobby.curtain_started = 0;
    {
        /* This build's costume counts (m-ex ones included): every machine's match clamps colours to them. */
        unsigned char costumes[1 + MU_LOBBY_CHARS];
        unsigned ch;
        costumes[0] = MU_LOBBY_OP_COSTUMES;
        for (ch = 0; ch < MU_LOBBY_CHARS; ++ch) {
            unsigned n = gm_GetNumCostumesForCKind((u8) ch);
            costumes[1 + ch] = (unsigned char) (n < 1 ? 1 : n > MU_LOBBY_COLOR_MAX + 1 ? MU_LOBBY_COLOR_MAX + 1 : n);
        }
        if (!mu_lobby_send(costumes, sizeof costumes)) OSReport("friends lobby: costume counts not sent\n");
    }
    mu_slippi_cmd(MU_LOBBY_COMMAND, &op, 1, &ok, 1, NULL);
    gmVsMelee_EnterCss(state, &lobby.vs, VS_MELEE);
#if MU_SQUAD
    if (!(lobby.view.mode & MU_LOBBY_M_DOUBLES)) gmSquad_LobbyAdmission();
#endif
}

/* 1 when the lobby owns this decide. */
static int lobby_css_decide(GameModeState* state)
{
    CSSData* css = gm_GetGameModeStateExitData(state);
    if (!lobby.major) return 0;
    OSReport("friends lobby: select exit request %u, to %d\n", (unsigned) css->pending_scene_change, lobby.exit);
    if (css->pending_scene_change == 2) {
        /* B back (also from "The lobby closed"): Online with its row highlighted (Friends, or Doubles for
         * the Doubles home). Without this the menu entry falls to the Direct-mode return, the Slippi
         * submenu, one A from connect codes. */
        if (lobby.view.mode & MU_LOBBY_M_DOUBLES) doubles_home_return = 1;
        else friends_return = 1;
        gm_ChangeGameModeAfterCurrentScene(GM_MENU);   /* the major's unload leaves the lobby */
        return 1;
    }
    gmVsMelee_ExitCss(state, &lobby.vs);
    if (lobby.exit == MU_LOBBY_EXIT_SSS) {
        gm_SetNextGameModeStateId(MU_SLP_STATE_SSS);
    } else if (lobby.exit == MU_LOBBY_EXIT_SPLASH) {
        lobby.played = 1;
        lobby.curtain = 0;
        mu_slippi_splash_init();
        if (last_match.connection_state != MU_SLP_MM_CONNECTION_SUCCESS) {
            gm_SetNextGameModeStateId(MU_SLP_STATE_CSS);
        } else
#if MU_SQUAD
        if (gmSquad_LobbyChosen() && !gmSquad_OnlineReady()) {
            gm_SetNextGameModeStateId(MU_SLP_STATE_CSS);
        } else
#endif
        gm_SetNextGameModeStateId(MU_SLP_STATE_SPLASH);
    } else {
        gm_SetNextGameModeStateId(MU_SLP_STATE_CSS);
    }
    lobby.exit = MU_LOBBY_EXIT_NONE;
    return 1;
}

/* The host's stage (random is 0: the room picks), sent with Start; back to the select either way. */
static int lobby_sss_decide(GameModeState* state)
{
    SSSData* sss = gm_GetGameModeStateExitData(state);
    if (!lobby.major) return 0;
    if (sss->start_game && !lobby.watching) {
        unsigned char start[2];
        unsigned stage = sss->vs.start.rules.stkind;
        start[0] = MU_LOBBY_OP_START;
        start[1] = (unsigned char) (mu_lobby_stage_ok(stage) && stage != 0 ? stage : 0);
        OSReport("friends lobby: start, stage %u\n", (unsigned) start[1]);
        (void) mu_lobby_read();   /* the select's last snapshot predates the stage select */
        lobby.curtain_refusals = lobby.view.refusal_seq;
        if (mu_lobby_send(start, sizeof start)) lobby.curtain = 1;
        else OSReport("friends lobby: start not sent\n");
    } else if (!lobby.watching) {
        const unsigned char closed[2] = { MU_LOBBY_OP_STAGE_SELECT, 0 };   /* the host backed out: the guests too */
        if (!mu_lobby_send(closed, sizeof closed)) OSReport("friends lobby: stage select close not sent\n");
    } else if (lobby.view_ok && (lobby.view.flags & MU_LOBBY_F_STARTED)) {
        lobby.curtain_refusals = lobby.view.refusal_seq;
        lobby.curtain = 1;   /* a guest: the host's Start ended the stage select */
    }
    gm_SetNextGameModeStateId(MU_SLP_STATE_CSS);
    return 1;
}

/* MajorSceneLoad. */
static void online_on_load(void)
{
    lobby_open();
    mu_ranked_set_reset();
    gmMainLib_804D3EE0->vs.unk_530.x6 = gm_801677F0();   /* the 1P port for the event CSS slot */
    zelda_sheik = 1;                                       /* the Zelda icon picks Sheik */
#if MU_SQUAD
    /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
    if (gmSquad_OnlineChosen()) zelda_sheik = 0; /* keep the picker's exact Zelda/Sheik kinds */
#endif
    online_loads++;
}

/* MajorSceneUnload. */
static void online_on_unload(void)
{
    lobby_close();
    mu_ranked_practice_control(MU_YG_PRACTICE_STOP);
    mu_ranked_update_reset(); // leaving the online major abandons any unfinished set
    mu_ranked_set_reset();
    zelda_sheik = 0;
}

/* CSSScenePrep: event-style CSS with the saved choice, then clear the preload cache. */
static void css_prep(GameModeState* state)
{
    struct EventData* ev = &gmMainLib_804D3EE0->vs.unk_530;
    CSSData* css = gm_GetGameModeStateEnterData(state);
    if (lobby.major) {
        lobby_css_prep(state);
        return;
    }
    gm_801B06B0(css, 0xE, ev->x2, 0, ev->x3, ev->nametag, 0, ev->x6);
    lbDvd_SetupVsPreloadCache();
#if MU_SQUAD
    /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
    gmSquad_OnlineRoom(slp.mode); /* a choice this mode or friend room doesn't carry is dropped */
    if (gmSquad_OnlineChosen()) gmSquad_OnlineCss();
#endif
}

/* CSSSceneDecide (runs the event CSS decide first to save the choice). */
static void css_decide(GameModeState* state)
{
    struct EventData* ev = &gmMainLib_804D3EE0->vs.unk_530;
    CSSData* css = gm_GetGameModeStateExitData(state);

    if (lobby_css_decide(state)) return;
    /* Record the guest's exit request before the main-menu load sends BA.
     * A cleanup line after returning to the menu does not explain the CSS exit. */
    OSReport("slippi CSS: exit mode %u request %u connection %u\n",
             (unsigned) slp.mode, (unsigned) css->pending_scene_change,
             (unsigned) last_match.connection_state);
    /* gmevent.c onExitCss */
    if (css->pending_scene_change == 2) {
        gm_ChangeGameModeAfterCurrentScene(GM_MENU);
        return;
    }
    gm_801B0730(css, &ev->x2, NULL, &ev->x3, &ev->nametag, NULL);
    ev->x8 = -1;
    ev->x9 = -1;
    ev->xA = -1;

    switch (slp.mode) {
    case MU_SLP_MODE_RANKED:
        /* asm Online/Slippi Online Scene/main.asm:420-447. */
        mu_ranked_set_reset();
        gm_SetNextGameModeStateId(MU_SLP_STATE_GAME_SETUP);
        return;
    case MU_SLP_MODE_DIRECT:
    case MU_SLP_MODE_TEAMS:
        /* The first match and the last winner go to the splash; the loser picks the stage unless
         * it already did. Any other value stalls on the console. */
        OSReport("online css: is_winner %d, chose_stage %d\n", (int) slp.is_winner, (int) slp.chose_stage);
        if (slp.is_winner == MU_SLP_WINNER_LOST && slp.chose_stage == 0) {
            gm_SetNextGameModeStateId(MU_SLP_STATE_SSS);
            return;
        }
        break;
    default:
        /* Unranked and Party. */
        break;
    }
#if MU_SQUAD
    if (squad_opponent_gone("CSS exit")) {
        gm_SetNextGameModeStateId(MU_SLP_STATE_CSS);
        return;
    }
#endif
    mu_slippi_splash_init();
    gm_SetNextGameModeStateId(MU_SLP_STATE_SPLASH);
}

/* SSSScenePrep: the vanilla VS-mode SSS prep. */
static void sss_prep(GameModeState* state)
{
    if (lobby.major) {
        gmVsMelee_EnterSss(state, &lobby.vs);   /* the lobby select's own picks and Teams */
        return;
    }
    gmVsMelee_EnterSss(state, gmVsMelee_GetVsData());
}

/* SSSSceneDecide. */
static void sss_decide(GameModeState* state)
{
    SSSData* sss = gm_GetGameModeStateExitData(state);
    if (lobby_sss_decide(state)) return;
    if (!sss->start_game) {
        gm_SetNextGameModeStateId(MU_SLP_STATE_CSS);
        return;
    }
    slp.chose_stage = 1;
    mu_slippi_load_match_state(NULL);
    if (last_match.local_ready == last_match.remote_ready) {
        mu_slippi_splash_init();
        gm_SetNextGameModeStateId(MU_SLP_STATE_SPLASH);
    } else {
        gm_SetNextGameModeStateId(MU_SLP_STATE_CSS);
    }
}

/* VS ScenePrep (vanilla 801b1588), then InitOnlinePlay arms the scene as an online match. */
static void vs_prep(GameModeState* state)
{
    gmVsMelee_EnterVs(state, gmVsMelee_GetVsData(), NULL, NULL);
    if (!mu_ranked_setup_cpu()) mu_online_arm(slp.mode, gm_801677F0());
    else if (mu_ranked_set()->tiebreak_num && mu_ranked_set()->last_end_mode != 7) {
        gmVsMelee_StartData.rules.time_limit = 180;
        gmVsMelee_StartData.players[0].stocks = gmVsMelee_StartData.players[1].stocks = 1;
    }
}

/* CheckIfWonLastGame. */
static int won_last_game(int slot)
{
    MatchEnd* me = &gmVsMelee_VsExitInfo.match_end;
    struct MatchPlayerData* p = &me->player_standings[slot];

    if (me->outcome == 0) {
        return 0;
    }
    if (gm_GetStartMeleeRules()->is_teams != me->is_teams) {
        return 0;
    }
    if (p->pkind == Gm_PKind_NA) {
        return 0;
    }
    if (me->outcome == 7) {   /* LRA-Start: byte 0 is the player who pressed it */
        int lras = (int) (me->x0 >> 24);
        if (me->is_teams == 1) {
            if (lras >= GM_MAX_PLAYERS) {
                return 1;
            }
            return me->player_standings[lras].team != p->team;
        }
        return lras != slot;
    }
    if (me->is_teams == 1) {
        return fn_801654A0(me) == p->team;
    }
    return p->is_big_loser == 0;
}

/* VSSceneDecide (runs the vanilla VS decide first). */
static void vs_decide(GameModeState* state)
{
    MatchExitInfo* mei;
    MatchEnd* me = &gmVsMelee_VsExitInfo.match_end;
    int next = MU_SLP_STATE_CSS;
    int local;
    ssize_t i;

    /* gmvsmode.c onExitVs */
    gmVsMelee_ExitVs(state, gmVsMode_State_Results, gmVsMode_State_SuddenDeath);
    mei = gm_GetGameModeStateExitData(state);
    for (i = 0; i < GM_MAX_PLAYERS; i++) {
        if (mei->match_end.player_standings[i].pkind != Gm_PKind_NA) {
            gm_80162A98(mei->match_end.player_standings[i].x20);
            gm_RecordSelfDestructs(mei->match_end.player_standings[i].self_destructs);
            gm_80162A4C(mei->match_end.player_standings[i].x44);
        }
    }

    mu_slippi_load_match_state(NULL);
    local = last_match.local_index & 3;
    slp.game_local_index = (u8) local;   /* StaticData: read by the Party results screen */

    if (slp.mode == MU_SLP_MODE_PARTY) {
        /* Someone who chose to leave (Slippi 3.7.0) goes straight back to the CSS. */
        if (!mu_online_is_leaving()) {
            next = MU_SLP_STATE_RESULTS;
        }
        mu_slippi_cleanup_connections();   /* always disconnect after Party */
    }
    if (slp.mode == MU_SLP_MODE_RANKED) {
        int result;
        /* CPU hook plays an ordinary local VS game; online results are captured
         * by report_game while LGL/stocks/percent still exist. */
        if (mu_ranked_setup_cpu()) {
            int w0 = won_last_game(0), w1 = won_last_game(1);
            mu_ranked_setup_game_result(w0 != w1 ? (w0 ? 0 : 1) : -1, me->outcome, 0);
        }
        /* The stage the game ran, from its own VS data (the same game_info + 0xE): a failed
         * post-game read zeroes last_match, and the stage enters the set's DSR history. */
        result = mu_ranked_setup_finish_game(
            last_match.connection_state == MU_SLP_MM_CONNECTION_SUCCESS,
            gmVsMelee_GetVsData()->start.rules.stkind);
        /* asm main.asm:617-741: only conclusive games score; a tie reuses cur_game. */
        if (result == MU_SETUP_NEXT_GAME || result == MU_SETUP_TIEBREAK) {
            next = MU_SLP_STATE_GAME_SETUP;
        } else {
            u8 end_mode = result == MU_SETUP_SET_OVER ? 0 : 1;
            if (result == MU_SETUP_SET_OVER && mu_ranked_practice()) {
                const MuRankedSet* set = mu_ranked_set();
                mu_ranked_practice_control(set->score[0] > set->score[1] ?
                                          MU_YG_PRACTICE_WIN : MU_YG_PRACTICE_LOSS);
            }
            mu_slippi_cmd(0xC2, &end_mode, 1, NULL, 0, NULL);
            mu_slippi_cleanup_connections();
        }
    }
    gm_SetNextGameModeStateId((u8) next);

    slp.is_winner = (s8) won_last_game(local);
    if (slp.mode == MU_SLP_MODE_TEAMS) {
        /* Port 0 always picks the stage. */
        slp.is_winner = local != 0 ? 1 : 0;
    } else {
        int winners = 0;
        for (i = 0; i < 4; i++) {
            winners += won_last_game((int) i) != 0;
        }
        if (winners != 1) {
            slp.is_winner = MU_SLP_WINNER_LOST;   /* a draw: both pick */
        }
    }

    /* The gold winner text: fake who LRA-started (byte 0) and player 1's loser flag. Not for
     * Party (its results screen breaks); forced off in Teams and Ranked. */
    if (slp.mode != MU_SLP_MODE_PARTY) {
        u32 who = won_last_game(local) ? 1 : 0;
        me->x0 = (me->x0 & 0x00FFFFFFu) | (who << 24);
        me->player_standings[0].is_big_loser = who ? 0 : 1;
        if (slp.mode == MU_SLP_MODE_TEAMS || slp.mode == MU_SLP_MODE_RANKED) {
            me->outcome = 0;
        }
    }

    slp.chose_stage = 0;
    /* A new seed, so both games do not random the same character after a match. */
    *HSD_RandSeedPtr = mu_slippi_new_seed();
}

/* Results: the vanilla VS-mode results prep and decide (back to CSS, state 0). */
static void results_prep(GameModeState* state)
{
    gmVsMelee_EnterResults(state);
}

static void results_decide(GameModeState* state)
{
    gmVsMelee_ExitResults(state, gmVsMelee_GetVsData(), MU_SLP_STATE_CSS);
    if (!gm_WasMatchCanceled(gmVsMelee_ResultsEnterData.match_end.outcome)) {
        gm_801623A4(&gmVsMelee_ResultsEnterData.match_end);
    }
}

/* The Classic splash's enter data, as bytes (gmclassic.c gmClassicIntroData: two s32, then u8s
 * at the console offsets). */
typedef struct MuSplashData {
    s32 x00;   /* pvp type (Slippi writes the low byte of the big-endian word) */
    s32 x04;   /* event mode */
    u8 b[0x18];
} MuSplashData;
/* Classic's intro buffer is the first part of gmClassic_rt natively (gm/gmclassic.c). */
extern unsigned char gmClassic_rt[];
#define gmClassicIntroDataBuffer (*(MuSplashData*) gmClassic_rt)

/* SplashScenePrep. */
static void splash_prep(GameModeState* state)
{
    static const u8 layout[16] = { 0x01, 0x78, 0x01, 0x01, 0x01, 0xFF, 0x21, 0x21,
                                   0xFF, 0x21, 0x21, 0xEE, 0x00, 0x00, 0xEE, 0x00 };
    StartMeleeData* start = &gmVsMelee_GetVsData()->start;
    MuSplashData* sp = &gmClassicIntroDataBuffer;
    u8* d = (u8*) sp;    /* console offsets from the start of the struct */
    PreloadedGameModeState* cache;
    const u8* gi;
    int i, is_teams, local, local_team, left = 0, right = 0;
    u64 mask = 0;
    (void) state;

    mu_slippi_load_match_state(NULL);
    gi = last_match.game_info;

    if (slp.mode == MU_SLP_MODE_TEAMS) {
        for (i = 0; i < 4; i++) {
            start->players[i].color =
                mu_slippi_team_costume(start->players[i].team + 1, start->players[i].ckind);
        }
    }

    memcpy(d + 8, layout, sizeof layout);
    sp->x04 = 0;
    sp->x00 = 0;
    is_teams = gi[8];
    d[0x0A] = 2;
    d[0x0E] = 1;
    d[0x0F] = 1;
    d[0x11] = 1;
    d[0x12] = 1;
    d[0x14] = 1;
    d[0x15] = 1;
    d[0x17] = 1;
    sp->x00 = is_teams;   /* the announcer says "Team ..." */

    local = last_match.local_index;
    local_team = gi[0x60 + 0x24 * (local & 3) + 9];
    for (i = 0; i < 4; i++) {
        PlayerInitData* p = &start->players[i];
        int go_left;
        if (p->slot_type >= 3) {
            continue;
        }
        go_left = i == local || (is_teams && p->team == local_team);
        /* The splash poses each fighter with its Classic intro motion, and the intro archive
         * (IrAls.dat) has those for the retail characters only: a mod's added fighter (ACE,
         * Akaneia) had none and the splash stopped the game as it built the model. Its place on
         * the splash stays empty (0x21, as an unused place); the name call and the match keep it. */
        if (MU_AK_CKIND(p->ckind)) {
            OSReport("[ak] online splash: character kind %d has no intro motion; its model is left out\n",
                     (int) p->ckind);
        }
        if (go_left) {
            d[0x0D + left] = MU_AK_CKIND(p->ckind) ? 0x21 : (u8) p->ckind;
            d[0x13 + left] = p->color;
            left++;
        } else {
            d[0x10 + right] = MU_AK_CKIND(p->ckind) ? 0x21 : (u8) p->ckind;
            d[0x16 + right] = p->color;
            right++;
        }
    }
    d[0x0B] = (u8) left;
    d[0x0C] = (u8) right;
    /* A side with no player keeps the layout's 0xFF in its first place, and a player whose
     * character is unset reads as -1: the splash built a fighter of kind -1 for it and stopped the
     * game (a player's 0.9.56 report, 136,000 frames into an online session). Anything that is not a
     * character is an empty place (0x21), which the splash skips. */
    for (i = 0; i < 3; i++) {
        if (d[0x0D + i] > 0x21 || d[0x10 + i] > 0x21) {
            OSReport("online splash: place %d holds no character (%d, %d); left empty\n", i,
                     (int) d[0x0D + i], (int) d[0x10 + i]);
        }
        if (d[0x0D + i] > 0x21) d[0x0D + i] = 0x21;
        if (d[0x10 + i] > 0x21) d[0x10 + i] = 0x21;
    }

    /* Preload the fighters (p3/p4 only when the negotiated slot is used) and the stage. */
    cache = lbDvd_GetPreloadCacheScene();
    for (i = 0; i < 4; i++) {
        if (i >= 2 && gi[0x60 + 0x24 * i + 1] >= 3) {
            continue;
        }
        cache->game_cache.entries[i].char_id = start->players[i].ckind;
        cache->game_cache.entries[i].color = start->players[i].color;
    }
    cache->game_cache.stkind = start->rules.stkind;
    lbDvd_80018254();
    lbDvd_80018C2C(199);
    lbDvd_80017700(4);

    /* The fighters' and the stage's sound banks. */
    lbAudioAx_80026F2C(0x1C);
    for (i = 0; i < 6; i++) {
        if (start->players[i].ckind != FIGHTER_NUM) {
            mask |= lbAudioAx_80026E84((CharacterKind) start->players[i].ckind);
        }
    }
#if MU_SQUAD
    /* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026) */
    if (gmSquad_OnlineReady()) mask |= gmSquad_SoundMask();
#endif
    mask |= lbAudioAx_80026EBC((StKind) start->rules.stkind);
    lbAudioAx_8002702C(4, mask);
    lbAudioAx_80027168();
}

/* SplashSceneDecide: on to VS. */
static void splash_decide(GameModeState* state)
{
    (void) state;
#if MU_SQUAD
    if (squad_opponent_gone("splash")) {
        gm_SetNextGameModeStateId(MU_SLP_STATE_CSS);
        return;
    }
#endif
    gm_SetNextGameModeStateId(MU_SLP_STATE_VS);
}

extern UNK_T gmClassic_804D68D0;

/* asm main.asm:1360-1434: invalidate character preloads except on replay. */
static void setup_prep(GameModeState* state)
{
    (void)state;
    if (!mu_ranked_set()->tiebreak_num) lbDvd_SetupVsPreloadCache();
}
static void setup_decide(GameModeState* state)
{
    (void)state;
    if (mu_slippi_load_match_state(NULL) ||
        last_match.connection_state != MU_SLP_MM_CONNECTION_SUCCESS ||
        mu_ranked_setup_model()->status != MU_SETUP_COMPLETE) {
        u8 end_mode = 1;
        mu_online_abi_log("ranked setup: left before both seats finished it");
        mu_slippi_cmd(0xC2, &end_mode, 1, NULL, 0, NULL);
        gm_SetNextGameModeStateId(MU_SLP_STATE_CSS);
        return;
    }
    mu_slippi_splash_init();
    gm_SetNextGameModeStateId(mu_ranked_set()->tiebreak_num ? MU_SLP_STATE_VS : MU_SLP_STATE_SPLASH);
}

static GameModeState online_states[] = {
    { MU_SLP_STATE_CSS, lbDvdPreload_3, 0, css_prep, css_decide,
      { GS_CSS, &online_css_data, &online_css_data } },
    { MU_SLP_STATE_SSS, lbDvdPreload_3, 0, sss_prep, sss_decide,
      { GS_SSS, &gmVsMelee_SssData, &gmVsMelee_SssData } },
    { MU_SLP_STATE_VS, lbDvdPreload_3, 0, vs_prep, vs_decide,
      { GS_VS, &gmVsMelee_StartData, &gmVsMelee_VsExitInfo } },
    { MU_SLP_STATE_RESULTS, lbDvdPreload_3, 0, results_prep, results_decide,
      { GS_RESULTS, &gmVsMelee_ResultsEnterData, NULL } },
    { MU_SLP_STATE_SPLASH, lbDvdPreload_3, 0, splash_prep, splash_decide,
      { GS_INTRO_EASY, &gmClassicIntroDataBuffer, &gmClassic_804D68D0 } },
    /* Ranked game prep: rules/transport, then A2 screen callbacks. */
    { MU_SLP_STATE_GAME_SETUP, lbDvdPreload_3, 0, setup_prep, setup_decide,
      { GS_SLIPPI_GAME_SETUP, NULL, NULL } },
    { GM_GAMEMODESTATE_TERMINATE },
};

/* main.asm, once at boot: the online major, and the online bytes cleared. The '#' key of the name
 * entry belongs to the name-entry owner (see s6-core-api.md). */
void mu_slippi_scene_install(void)
{
    GameMode* mode;
    if (!mu_slippi_menus_enabled()) {
        return;
    }
    slp.code_entry = 0;
    slp.pause = 0;
    for (mode = gm_GetAllGameModes(); mode->kind != GM_COUNT; mode++) {
        if (mode->kind == GM_HANYU_CSS) {
            mode->preloaded = true;
            mode->on_load = online_on_load;
            mode->on_unload = online_on_unload;
            mode->states = online_states;
            installed = 1;
            break;
        }
    }
}


/* ---- Coming soon, in the menu's own text ----
 * YouGame's three rows while YouGame is not open yet: "Coming soon" written beside each greyed name
 * with the menu's description text (its font, canvas and camera), so it is part of the game's
 * picture like the rest of the menu: shader presets and the CRT look apply to it, and settings and
 * dialogs draw over it. Shown only while the first page is idle on screen; freed when the menu
 * moves, and forgotten (never freed twice) when a new menu scene starts. */
static HSD_Text* soon_text;

typedef struct SoonLayout { float x, y, dx, dy, size; } SoonLayout;
static SoonLayout soon_layout(void)
{
    /* The first row's text, then the step to each next row (the rows step left as they go down),
     * in the description canvas's units; MELEE_TEST_SOON overrides them for measuring. */
    SoonLayout l = { 4.6f, -4.5f, -1.5f, 3.0f, 0.028f };   /* measured on captures (0.9.210) */
    const char* v = mu_test_getenv("MELEE_TEST_SOON");
    if (v != NULL) {
        float f[5];
        int n = 0;
        while (n < 5 && *v) {   /* [-]digits[.digits], separated by spaces or commas */
            float sign = 1.0f, val = 0.0f, scale = 0.0f;
            int digits = 0;
            if (*v == '-') { sign = -1.0f; v++; }
            for (; (*v >= '0' && *v <= '9') || *v == '.'; v++) {
                if (*v == '.') { scale = 1.0f; continue; }
                val = val * 10.0f + (float) (*v - '0');
                if (scale > 0.0f) scale *= 10.0f;
                digits++;
            }
            if (!digits) break;
            f[n++] = sign * (scale > 0.0f ? val / scale : val);
            while (*v == ' ' || *v == ',') v++;
        }
        if (n == 5) { l.x = f[0]; l.y = f[1]; l.dx = f[2]; l.dy = f[3]; l.size = f[4]; }
    }
    return l;
}

void mu_slippi_menu_soon(int canvas, int idle)
{
    int show, i;
    SoonLayout l;
    if (canvas < 0) {
        soon_text = NULL;
        pages_texts_forget();
        return;
    }
    if (mu_slippi_online_pages()) pages_frame(HSD_GObj_CurrentInvokedProcGObj, canvas, idle);
    /* Not shown yet: the owner keeps the sticker until a native look is approved. MELEE_TEST_SOON shows it. */
    show = idle && mu_test_getenv("MELEE_TEST_SOON") != NULL && yougame_soon() && split_page == 0 && mn_804A04F0.cur_menu == MENU_KIND_8;
    if (!show) {
        if (soon_text != NULL) {
            HSD_SisLib_803A5CC4(soon_text);
            soon_text = NULL;
        }
        return;
    }
    if (soon_text != NULL) {
        return;
    }
    l = soon_layout();
    soon_text = HSD_SisLib_803A6754(0, canvas);
    if (soon_text == NULL) {
        return;
    }
    soon_text->pos_x = l.x;
    soon_text->pos_y = l.y;
    soon_text->pos_z = 17.0f;   /* the description's depth: in front of the rows */
    soon_text->font_size.x = l.size;
    soon_text->font_size.y = l.size;
    soon_text->default_kerning = 1;
    soon_text->default_alignment = 0;
    for (i = 0; i < 3; i++) {
        /* A dark copy one step down and right, then the text: the menu labels' outline, simply. */
        static GXColor shadow = { 0, 0, 0, 200 };
        static GXColor ink = { 236, 200, 110, 255 };   /* the rows' gold rim */
        const float x = (l.dx * (float) i) / l.size, y = (l.dy * (float) i) / l.size;
        int id = HSD_SisLib_803A6B98(soon_text, x + 2.0f, y + 2.0f, "Coming soon");
        HSD_SisLib_803A74F0(soon_text, id, &shadow);
        id = HSD_SisLib_803A6B98(soon_text, x, y, "Coming soon");
        HSD_SisLib_803A74F0(soon_text, id, &ink);
    }
}

/* The Coming soon stickers are drawn by the host over the picture, at each YouGame row's place with
 * the camera untilted. The C-stick turns the menu camera and the rows move with it, so each row's
 * centre is put on screen twice, through the turned camera and through the untilted one, and the
 * difference moves its sticker. Reads only: the camera's eye is put back as it was. */
void mu_slippi_menu_rows(struct HSD_JObj** rows, int count, void* cobj_ptr)
{
    HSD_CObj* cobj = (HSD_CObj*) cobj_ptr;
    float eye[3];
    Vec3 turned, neutral;
    int i;
    soon_offsets_ok = 0;
    if (!yougame_soon() || split_page != 0 || cobj == NULL || count < 3 || !mu_menu_camera_neutral_eye(eye)) {
        return;
    }
    neutral.x = eye[0];
    neutral.y = eye[1];
    neutral.z = eye[2];
    HSD_CObjGetEyePosition(cobj, &turned);
    for (i = 0; i < 3; i++) {
        Vec3 world, a, b;
        if (rows[i] == NULL) {
            return;
        }
        lb_8000B1CC(rows[i], NULL, &world);
        lbVector_WorldToScreen(cobj, &world, &a, 0);
        HSD_CObjSetEyePosition(cobj, &neutral);
        lbVector_WorldToScreen(cobj, &world, &b, 0);
        HSD_CObjSetEyePosition(cobj, &turned);
        soon_offsets[2 * i] = a.x - b.x;
        soon_offsets[2 * i + 1] = a.y - b.y;
        {
            static int logged;
            if (!logged && mu_test_getenv("MELEE_TEST_SOON_LOG") != NULL) {
                logged = 1;
                OSReport("[soon] row 0 on screen %.1f, %.1f (untilted %.1f, %.1f)\n", a.x, a.y, b.x, b.y);
            }
        }
    }
    soon_offsets_ok = 1;
}
#include "mu_online_pages.inc"
