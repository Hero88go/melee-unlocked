"""Execute the production menu input/entry functions with offline scene stubs."""
from pathlib import Path
import importlib.util
import subprocess
import shutil
import os
import shlex
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

def compiler_command():
    # Windows compilers lack setenv/unsetenv, which several fixtures use.
    compat = ['-include', str(Path(__file__).resolve().parent / 'posix_fixture_compat.h')] if os.name == 'nt' else []
    if os.environ.get('CXX'):
        return shlex.split(os.environ['CXX']) + compat
    for name in ('c++', 'g++', 'clang++'):
        if path := shutil.which(name):
            return [path] + compat
    raise RuntimeError('an existing C++ compiler is required (CXX, c++, g++, clang++)')
spec = importlib.util.spec_from_file_location('fixture', ROOT / 'tools/native_major_fixture.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)

STUBS = r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>
using u8 = unsigned char; using s8 = signed char; using u16 = unsigned short; using u32 = unsigned;
using u64 = unsigned long long;
static constexpr u64 PAD_ANY_UP = 1ULL << 36, PAD_ANY_DOWN = 1ULL << 37;
enum { MU_SLP_MODE_RANKED, MU_SLP_MODE_UNRANKED, MU_SLP_MODE_DIRECT, MU_SLP_MODE_TEAMS, MU_SLP_MODE_PARTY,
       MU_SLP_OPT_LOGIN, MU_SLP_OPT_LOGOUT, MU_SLP_OPT_UPDATE, MU_SLP_OPT_FRIENDS, MU_SLP_OPT_SLIPPI, MU_SLP_OPT_TOURNAMENT };
#include "mu_online_menu.h"
enum { MENU_KIND_8 = 8, MENU_KIND_1P = 1, SEL_1P_2 = 2, GM_HANYU_CSS = 8, GM_BOOT = 0, GM_VS = 2,
       MU_TOURNEY_COMMAND = 0xeb, MU_TOURNEY_MENU = 4, MU_TOURNEY_JOIN = 5, MU_TOURNEY_LEAVE = 6,
       MU_SLP_APP_LOGGED_IN = 1, MU_SLP_APP_UPDATE = 2, MU_SLP_WINNER_NULL = -1,
       MenuInput_Confirm = 1, MenuInput_Back = 2, MenuInput_Up = 4, MenuInput_Down = 8,
       MenuInput_Left = 16, MenuInput_Right = 32,   /* the Unranked row's slider (0.9.990) */
       CMD_YOUGAME_FRIENDS_MENU = 0xe6, CMD_OPEN_LOGIN = 0xb6, CMD_UPDATE_APP = 0xb8, PAD_CONFIRM = 0x1100 };
struct HSD_GObj {}; struct MenuExitData { int pending_mode; } exit_data;
struct { int app_state, mode, force_clear, is_winner, chose_stage; } slp;
struct { int cooldown; } mn_804D6BC8;
struct { int hovered_selection, cur_menu, buttons, entering_menu; } mn_804A04F0;
struct AnimLoopSettings { int tag; };
static AnimLoopSettings mn_803EB57C[] = {{0},{1},{2}}, mn_803EB5A0[] = {{3},{4},{5}}, mn_803EB5C4[] = {{6},{7}};
static AnimLoopSettings mn_803EB444[] = {{100},{101},{102},{103},{104}};   // the 1P menu's previews
struct { int selection_count; AnimLoopSettings* anim_loop; int start_frame; u16* description_indices; void(*think)(HSD_GObj*); } mn_803EB6B0[9];
static u16 mn_803EB66C[3];
void mu_slippi_online_menu_think(HSD_GObj*);
static int agent_copy, local_peer, agent_lock_logged;
static int ranked_supported, ranked_offered, friends_offered, slippi_submenu, boot_oneshot, boot_main_menu, p2p_auto_direct, friends_confirm_hold, slippi_link_notice, friends_return, friends_css_host;
static int pending_buttons, entered, last_mode = -1, published, installs, switched_menu, switched_row;
static int p2p, cpu, granted = 1, grant_queries, notices, grant_error, grant_short;
static int link_requests, link_leaves;
static int tournament_visible, tournament_back_ticks, queue_available, queue_visible, joins, leaves, queue_notices;
static char tournament_text[128];
void mu_slippi_tournament_notice(HSD_GObj*, const char*) { ++queue_notices; }
void mu_slippi_signin_notice(HSD_GObj*) { ++notices; }
int mu_slippi_menu_count(); int mu_slippi_menu_option(int); int mu_slippi_option_unlocked(int);
int mu_slippi_first_unlocked();
int mn_80229938(int, int row) { return mu_slippi_option_unlocked(row); }
int mu_slippi_menus_enabled() { return 1; }
void mu_content_mode(int mode) { last_mode = mode; }
int mu_slippi_fetch_app_state() { return slp.app_state; }
void mu_mnmain_install_online_menu();
void mu_slippi_log_online_rows();
int mu_online_p2p_menus() { return p2p; } int mu_online_p2p_port() { return 0; }
int mu_ranked_setup_cpu() { return cpu; }
void mu_slippi_load_match_state(void*) {}
static int logged_selection_row = -1, logged_selection_option = -1;
static int logged_highlight_row = -1, logged_highlight_option = -1;
static std::vector<std::string> highlight_lines;
static std::vector<std::string> row_lines;
void mu_online_abi_log(const char* line) {
    if (std::strstr(line, "online menu: rows offered ")) row_lines.emplace_back(line);
    if (std::strstr(line, " highlighted;")) {
        std::sscanf(line, "online menu: row %d (option %d) highlighted", &logged_highlight_row, &logged_highlight_option);
        highlight_lines.emplace_back(line);
    }
}
void OSReport(const char* format, ...) {
    if (std::strcmp(format, "online menu: row %d (option %d) selected\n") == 0) {
        va_list args; va_start(args, format);
        logged_selection_row = va_arg(args, int); logged_selection_option = va_arg(args, int);
        va_end(args);
    }
}
u32 mn_80229624(int) { return pending_buttons; }
static u64 cached_input[4][3];
u64 gm_GetButtonsPressed(u8 port) { return cached_input[port][0]; }
u64 gm_GetButtonsTriggered(u8 port) { return cached_input[port][1]; }
u64 gm_801A36C0(u8 port) { return cached_input[port][2]; }
static unsigned sim_frame = 1934;
unsigned VIGetRetraceCount() { return sim_frame; }
void gm_801677E8(s8) {} void sfxForward() {} void sfxBack() {} void sfxMove() {}
MenuExitData* gm_GetCurrentSceneExitData() { return &exit_data; }
void gm_801A4B60() { ++entered; }
void mn_80229894(int menu, int row, int) {
    switched_menu = menu; switched_row = row;
    mn_804A04F0.cur_menu = menu; mn_804A04F0.hovered_selection = row;
}
static std::vector<int> yg_menu_picks;
static int friends_css_answer, friends_css_queries;   // the host's one-shot "a Friends room opened the CSS"
int mu_slippi_cmd(unsigned cmd, const void* p, unsigned, void* out, unsigned, unsigned* got) {
    if (cmd == CMD_YOUGAME_FRIENDS_MENU && *(const u8*)p == 14) {
        ++friends_css_queries; *(u8*)out = (u8)friends_css_answer; friends_css_answer = 0; *got = 1; return 0;
    }
    if (cmd == MU_TOURNEY_COMMAND) {
        if (*(const u8*)p == MU_TOURNEY_JOIN) { ++joins; queue_visible = 1; }
        if (*(const u8*)p == MU_TOURNEY_LEAVE) { ++leaves; queue_visible = 0; }
        std::memset(out, 0, 130); ((u8*)out)[0] = queue_available; ((u8*)out)[1] = queue_visible;
        *got = 130; return 0;
    }
    if (cmd == CMD_YOUGAME_FRIENDS_MENU && *(const u8*)p == 8) {
        ++grant_queries; *(u8*)out = (u8)granted; *got = grant_short ? 0 : 1; return grant_error;
    }
    if (cmd == 0xE7) {
        if (*(const u8*)p == 3) ++link_requests;
        if (*(const u8*)p == 4) ++link_leaves;
    }
    if (cmd == CMD_YOUGAME_FRIENDS_MENU) published = *(const u8*)p;
    if (cmd == 0xEF && p) yg_menu_picks.push_back(*(const u8*)p);   // CMD_YOUGAME_MENU's one-byte picks
    return 0;
}
static int checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::abort(); } } while (0)
'''
# Melee Unlocked 1.0 (upstream 0.9.97): the split layout's production state and helpers. Inert in
# these fixtures: the host stub answers no YouGame byte (EF), so yg_flags stays 0 and every row,
# cursor and lock below is the app's.
_SHIM = (ROOT / 'sourceport/game/shim/mu_slippi_menu.c').read_text()
STUBS = (STUBS.replace('CMD_UPDATE_APP = 0xb8,', 'CMD_UPDATE_APP = 0xb8, CMD_YOUGAME_MENU = 0xef,', 1) +
         _SHIM[_SHIM.index('static int yg_flags;'):_SHIM.index('static int yougame_query(')] +
         fixture.extract(_SHIM, 'yougame_query'))
CASES = r'''
static void press(int buttons) { pending_buttons = buttons; mn_804D6BC8.cooldown = 0; mu_slippi_online_menu_think(nullptr); }
int main() {
    ranked_supported = ranked_offered = friends_offered = 1; slp.app_state = MU_SLP_APP_LOGGED_IN;
    mu_mnmain_install_online_menu(); mn_804A04F0.cur_menu = MENU_KIND_8;
    CHECK(row_lines == std::vector<std::string>{"online menu: rows offered Ranked,Unranked,Friends,Slippi tournament=0\n"});
    row_lines.clear(); press(0); press(0); CHECK(row_lines.empty());
    mu_mnmain_install_online_menu(); CHECK(row_lines.size() == 1);
    CHECK(mu_slippi_first_unlocked() == 0 && mu_slippi_menu_count() == 4);
    const int preview[] = {0,1,2,3};
    const int descriptions[] = {0x645,0x646,0x647,0x64B};
    for (int i = 0; i < 4; ++i) {
        CHECK(mn_803EB6B0[8].anim_loop[i].tag == preview[i]);
        CHECK(mn_803EB6B0[8].description_indices[i] == descriptions[i]);
    }
    CHECK(mn_803EB6B0[8].start_frame == 140);
    // Build 103: 1P > Online Play (retail's hidden row 2, a blank preview) shows the character select's,
    // as Ranked does; the other 1P rows keep their own.
    CHECK(mn_803EB444[SEL_1P_2].tag == mn_803EB57C[0].tag);
    for (int i : {0, 1, 3, 4}) CHECK(mn_803EB444[i].tag == 100 + i);
    const int order[] = {0,1,8,9};
    for (int i = 0; i < 4; ++i) { CHECK(mu_slippi_menu_option(i) == order[i]); CHECK(mu_slippi_option_unlocked(i)); }
    CHECK(!mu_slippi_option_unlocked(4) && !mu_slippi_option_unlocked(-1));
    CHECK(mu_slippi_menu_label_kind(0) == 6 && mu_slippi_menu_label_kind(1) == 7);
    CHECK(mu_slippi_menu_label_kind(2) == 1 && mu_slippi_menu_label_kind(3) == 2);
    press(MenuInput_Up); CHECK(mn_804A04F0.hovered_selection == 3 && published == 5);
    press(MenuInput_Down); CHECK(mn_804A04F0.hovered_selection == 0 && published == 5);
    for (int i = 1; i <= 4; ++i) { press(MenuInput_Down); CHECK(mn_804A04F0.hovered_selection == i % 4); CHECK(published == (i == 2 ? 6 : 5)); }
    mn_804A04F0.hovered_selection = 2; press(MenuInput_Confirm);
    CHECK(entered == 0 && published == 6 && mn_804D6BC8.cooldown == 5);
    // Friends' own confirm cooldown keeps the row selected, so the host keeps the picker that press
    // opened (11-01, build 30: it published 5, "friends: cancelled", the friend waited alone).
    for (int i = 0; i < 4; ++i) { pending_buttons = 0; mu_slippi_online_menu_think(nullptr); CHECK(published == 6 && friends_confirm_hold); }
    pending_buttons = 0; mn_804D6BC8.cooldown = 0; mu_slippi_online_menu_think(nullptr);
    CHECK(published == 6 && !friends_confirm_hold);
    // The entry cooldown alone still hides the selection: a press carried in must not open the picker.
    pending_buttons = 0; mn_804D6BC8.cooldown = 5; mu_slippi_online_menu_think(nullptr); CHECK(published == 5);
    mn_804A04F0.hovered_selection = 3; press(MenuInput_Confirm);
    CHECK(slippi_submenu == 1 && mu_slippi_menu_count() == 3 && switched_row == 0 && slp.force_clear);
    CHECK(entered == 0 && published == 4);
    CHECK(logged_selection_row == 3 && logged_selection_option == MU_SLP_OPT_SLIPPI);
    CHECK(mn_803EB6B0[8].anim_loop[0].tag == 2 && mn_803EB6B0[8].anim_loop[1].tag == 3);
    CHECK(mn_803EB6B0[8].description_indices[0] == 0x647 && mn_803EB6B0[8].description_indices[1] == 0x64B);
    CHECK(mu_slippi_menu_option(0) == 2 && mu_slippi_menu_option(1) == 3 && mu_slippi_menu_option(2) == 4);
    CHECK(mn_803EB6B0[8].selection_count == 3 && !mu_slippi_option_unlocked(3));
    CHECK(mn_803EB6B0[8].anim_loop[2].tag == 4 && mn_803EB6B0[8].description_indices[2] == 0x64C);
    CHECK(mu_slippi_menu_label_kind(0) == 3);
    CHECK(mu_slippi_menu_label_kind(1) == 4);
    CHECK(mu_slippi_menu_label_kind(2) == 5 && mu_slippi_menu_label_kind(3) == 0);
    press(MenuInput_Up); CHECK(mn_804A04F0.hovered_selection == 2 && published == 4);
    press(MenuInput_Down); CHECK(mn_804A04F0.hovered_selection == 0);
    for (int row = 0; row < 3; ++row) {
        CHECK(mn_804A04F0.hovered_selection == row && published == 4);
        CHECK(logged_highlight_row == row && logged_highlight_option == row + 2);
        press(MenuInput_Down);
    }
    CHECK(mn_804A04F0.hovered_selection == 0);
    press(MenuInput_Back); CHECK(!slippi_submenu && switched_menu == 8 && switched_row == 3 && published == 5);
    // Absent, short, and failed local grant replies all stay in the submenu.
    // Both A and START use Confirm; neither reaches CSS or a search command.
    for (int failure = 0; failure < 3; ++failure) {
        granted = failure != 0; grant_error = failure == 1; grant_short = failure == 2;
        mn_804A04F0.hovered_selection = 3; press(MenuInput_Confirm);
        for (int row = 0; row < 3; ++row) {
            mn_804A04F0.hovered_selection = row;
            int before = entered, before_notice = notices, before_queries = grant_queries;
            int old_mode = slp.mode, old_content = last_mode;
            press(MenuInput_Confirm);
            CHECK(entered == before && notices == before_notice + 1 && grant_queries == before_queries + 1);
            CHECK(slippi_submenu && mu_slippi_menu_count() == 3 && mu_slippi_option_unlocked(row));
            CHECK(slp.mode == old_mode && last_mode == old_content && published == 4);
            CHECK(mn_804D6BC8.cooldown == 5);
            CHECK(slippi_link_notice);
            const int before_link = link_requests;
            pending_buttons = 0; cached_input[0][1] = 0x0800;
            mu_slippi_online_menu_think(nullptr); // cooldown never launches or withdraws the sheet
            CHECK(slippi_link_notice && link_requests == before_link && entered == before);
            mn_804D6BC8.cooldown = 0;
            mu_slippi_online_menu_think(nullptr);
            CHECK(link_requests == before_link + 1 && entered == before && slp.mode == old_mode);
            for (u64 button : {0x0010ULL, 0x0400ULL}) { // Z and X are free too
                cached_input[0][1] = button; mu_slippi_online_menu_think(nullptr);
                CHECK(link_requests == before_link + 1 && entered == before);
            }
            cached_input[0][1] = 0;
        }
        const int leaves = link_leaves;
        press(MenuInput_Up); CHECK(link_leaves == leaves + 1 && !slippi_link_notice);
        CHECK(mn_804A04F0.hovered_selection == 1);
 CHECK(mn_804A04F0.hovered_selection == 1);
        press(MenuInput_Back); CHECK(!slippi_submenu && switched_row == 3);
    }
    granted = 1; grant_error = grant_short = 0;
    // Enter all three upstream modes, then return from CSS to their actual submenu row.
    for (int row = 0; row < 3; ++row) {
        mn_804A04F0.hovered_selection = 3; press(MenuInput_Confirm);
        mn_804A04F0.hovered_selection = row; int before = entered; press(MenuInput_Confirm);
        CHECK(entered == before + 1 && slp.mode == row + 2 && last_mode == row + 2 && published == 0);
        CHECK(logged_selection_row == row && logged_selection_option == row + 2);
        u8 menu, hovered; CHECK(mu_slippi_menu_enter(GM_HANYU_CSS, &menu, &hovered));
        CHECK(menu == 8 && hovered == row && slippi_submenu);
        press(MenuInput_Back);
    }
    for (int row : {0, 1}) {
        mn_804A04F0.hovered_selection = row; press(MenuInput_Confirm);
        CHECK(slp.mode == (row == 0 ? 0 : 1) && published == 0);
    }
    slippi_submenu = 0; press(MenuInput_Back); CHECK(switched_menu == 1 && switched_row == 2 && published == 0);
    // A new Online entry starts on Ranked, including boot.
    mu_slippi_switch_to_online_menu(); CHECK(switched_menu == 8 && switched_row == 0);
    boot_oneshot = 1; u8 menu, hovered; CHECK(mu_slippi_menu_enter(GM_BOOT, &menu, &hovered)); CHECK(hovered == 0);
    // Build 103: a YouGame start for a local match (FA bit 5) leaves boot to the main menu, once.
    boot_main_menu = boot_oneshot = 1; menu = hovered = 0xEE;
    CHECK(!mu_slippi_menu_enter(GM_BOOT, &menu, &hovered) && menu == 0xEE && hovered == 0xEE && !boot_oneshot);
    // A P2P session's players are known: it still opens Online first.
    p2p = boot_oneshot = 1; CHECK(mu_slippi_menu_enter(GM_BOOT, &menu, &hovered) && menu == MENU_KIND_8 && !boot_oneshot);
    p2p = boot_main_menu = p2p_auto_direct = 0; slippi_submenu = 0; mu_mnmain_install_online_menu();
    // A failed Friends start returns through boot: entry opens Online on Friends, never from the menu's OnEnter.
    friends_return = slippi_submenu = boot_oneshot = 1;
    CHECK(mu_slippi_menu_enter(GM_BOOT, &menu, &hovered));
    CHECK(menu == MENU_KIND_8 && hovered == mu_online_menu_row(ranked_supported, 0, MU_SLP_OPT_FRIENDS));
    CHECK(!friends_return && !slippi_submenu && !boot_oneshot);
    // B out of the host's Friends CSS (50m leave crash): the entry opens Online on Friends, once.
    friends_css_host = 1; CHECK(mu_slippi_menu_enter(GM_VS, &menu, &hovered));
    CHECK(menu == MENU_KIND_8 && hovered == mu_online_menu_row(ranked_supported, 0, MU_SLP_OPT_FRIENDS) && !friends_css_host);
    menu = hovered = 0xEE; CHECK(!mu_slippi_menu_enter(GM_VS, &menu, &hovered) && menu == 0xEE);
    // The mark never outlives a menu entry from anywhere else.
    friends_css_host = 1; CHECK(!mu_slippi_menu_enter(GM_BOOT, &menu, &hovered) && !friends_css_host);
    // B out of a Friends room's online CSS (Direct mode underneath): Online with Friends highlighted,
    // never the Slippi submenu (one more B used to leave the cursor on the hidden Slippi row).
    slp.mode = MU_SLP_MODE_DIRECT; slippi_submenu = 1; friends_css_answer = 1; int queries = friends_css_queries;
    CHECK(mu_slippi_menu_enter(GM_HANYU_CSS, &menu, &hovered) && friends_css_queries == queries + 1);
    CHECK(menu == MENU_KIND_8 && hovered == mu_online_menu_row(1, 0, MU_SLP_OPT_FRIENDS) && !slippi_submenu);
    CHECK(mu_slippi_menu_option(hovered) == MU_SLP_OPT_FRIENDS && last_mode == -1);
    // The answer is one-shot: Direct by connect code still comes back to its own Slippi submenu row.
    CHECK(mu_slippi_menu_enter(GM_HANYU_CSS, &menu, &hovered) && friends_css_queries == queries + 2);
    CHECK(slippi_submenu && hovered == mu_online_menu_row(1, 1, MU_SLP_MODE_DIRECT));
    // Every entry asks, so an answer never outlives an entry from anywhere else.
    friends_css_answer = 1; boot_oneshot = 0; CHECK(!mu_slippi_menu_enter(GM_VS, &menu, &hovered) && !friends_css_answer);
    slippi_submenu = 0;
    // A listing without a ladder keeps Ranked greyed and starts on Unranked.
    ranked_offered = 0;
    CHECK(!mu_slippi_option_unlocked(0) && mu_slippi_first_unlocked() == 1);
    for (int row : {1, 2, 3}) CHECK(mu_slippi_option_unlocked(row));
    mu_slippi_switch_to_online_menu(); CHECK(switched_row == 1);
    boot_oneshot = 1; CHECK(mu_slippi_menu_enter(GM_BOOT, &menu, &hovered)); CHECK(hovered == 1);
    mn_804A04F0.hovered_selection = 1;
    press(MenuInput_Up); CHECK(mn_804A04F0.hovered_selection == 3);
    press(MenuInput_Down); CHECK(mn_804A04F0.hovered_selection == 1);
    int before_unranked = entered; press(MenuInput_Confirm);
    CHECK(entered == before_unranked + 1 && slp.mode == MU_SLP_MODE_UNRANKED);
    mn_804A04F0.hovered_selection = 3; press(MenuInput_Confirm);
    CHECK(slippi_submenu && mu_slippi_first_unlocked() == 0);
    for (int row = 0; row < 3; ++row) CHECK(mu_slippi_option_unlocked(row));
    press(MenuInput_Back); CHECK(!slippi_submenu && switched_row == 3);
    ranked_offered = 1;
    CHECK(mu_slippi_option_unlocked(0) && mu_slippi_first_unlocked() == 0);
    // Preserve the protected Ranked CPU auto-entry despite moving Direct into a submenu.
    p2p = cpu = boot_oneshot = 1;
    CHECK(mu_slippi_menu_enter(GM_BOOT, &menu, &hovered));
    mn_804A04F0.hovered_selection = hovered;
    press(0); CHECK(slp.mode == MU_SLP_MODE_RANKED && !slippi_submenu);
    p2p = cpu = 0;
    const int previous_queries = grant_queries;
    // Standalone retains all eight original IDs, locks, first cursor, wrap and Back.
    ranked_supported = ranked_offered = friends_offered = slippi_submenu = 0; mu_mnmain_install_online_menu();
    CHECK(mu_slippi_menu_count() == 8 && mu_slippi_first_unlocked() == 1);
    for (int row = 0; row < 8; ++row) {
        CHECK(mu_slippi_menu_option(row) == row);
        CHECK(mu_slippi_menu_label_kind(row) == 0);
        CHECK(mn_803EB6B0[8].anim_loop[row].tag == row);
    }
    for (int state = 0; state < 3; ++state) {
        slp.app_state = state;
        for (int row = 0; row < 10; ++row) {
            bool unlocked = state == 1 ? row >= 1 && row <= 4 : state == 2 ? row == 7 : row == 5;
            CHECK(bool(mu_slippi_option_unlocked(row)) == unlocked);
        }
    }
    slp.app_state = 1; mn_804A04F0.hovered_selection = 1;
    press(MenuInput_Up); CHECK(mn_804A04F0.hovered_selection == 4);
    press(MenuInput_Down); CHECK(mn_804A04F0.hovered_selection == 1);
    granted = 0;
    for (int row : {2, 3}) {
        mn_804A04F0.hovered_selection = row; int before = entered;
        press(MenuInput_Confirm); CHECK(entered == before + 1 && slp.mode == row);
    }
    CHECK(grant_queries == previous_queries);
    // Pin the complete payload and the old harness prefix, including mapped bits above bit 31.
    ranked_supported = ranked_offered = friends_offered = 1; slippi_submenu = p2p = cpu = 0;
    mn_804A04F0.hovered_selection = 0; mn_804D6BC8.cooldown = 1; pending_buttons = 0;
    mu_slippi_online_menu_think(nullptr); highlight_lines.clear(); press(0);
    CHECK(highlight_lines == std::vector<std::string>{"online menu: row 0 (option 0) highlighted; frame 1934, input none; yougame 1, submenu 0, ranked offered 1, friends offered 1, app state 1\n"});
    highlight_lines.clear();
    cached_input[0][0] = cached_input[0][1] = cached_input[0][2] = PAD_ANY_DOWN | 4;
    press(MenuInput_Down);
    CHECK(highlight_lines == std::vector<std::string>{"online menu: row 1 (option 1) highlighted; frame 1934, port 1 held 0x2000000004 edge 0x2000000004 repeat 0x2000000004 (press); yougame 1, submenu 0, ranked offered 1, friends offered 1, app state 1\n"});
    highlight_lines.clear(); ++sim_frame; cached_input[0][1] = 0;
    press(MenuInput_Down);
    CHECK(highlight_lines == std::vector<std::string>{"online menu: row 2 (option 8) highlighted; frame 1935, port 1 held 0x2000000004 edge 0x0000 repeat 0x2000000004 (repeat); yougame 1, submenu 0, ranked offered 1, friends offered 1, app state 1\n"});
    // Two contributors remain one move and one line; an opposite direction is not a contributor.
    highlight_lines.clear();
    cached_input[1][0] = cached_input[1][1] = cached_input[1][2] = PAD_ANY_DOWN | 0x20000;
    cached_input[2][0] = cached_input[2][1] = cached_input[2][2] = PAD_ANY_UP | 8;
    press(MenuInput_Down);
    CHECK(highlight_lines == std::vector<std::string>{"online menu: row 3 (option 9) highlighted; frame 1935, port 1 held 0x2000000004 edge 0x0000 repeat 0x2000000004 (repeat); port 2 held 0x2000020000 edge 0x2000020000 repeat 0x2000020000 (press); yougame 1, submenu 0, ranked offered 1, friends offered 1, app state 1\n"});
    // Four maximum-width snapshots must preserve every port and the existing suffix.
    highlight_lines.clear();
    for (auto& port : cached_input) for (auto& bits : port) bits = ~0ULL;
    press(MenuInput_Up);
    std::string all = "online menu: row 2 (option 8) highlighted; frame 1935, ";
    for (int port = 1; port <= 4; ++port) {
        if (port > 1) all += "; ";
        all += "port " + std::to_string(port) + " held 0xffffffffffffffff edge 0xffffffffffffffff repeat 0xffffffffffffffff (press)";
    }
    all += "; yougame 1, submenu 0, ranked offered 1, friends offered 1, app state 1\n";
    CHECK(highlight_lines == std::vector<std::string>{all});
    highlight_lines.clear(); press(0); CHECK(highlight_lines.empty());
    // Confirm wins over a simultaneous direction: this submenu highlight has no directional cause.
    mn_804A04F0.hovered_selection = 3; press(MenuInput_Confirm | MenuInput_Down);
    CHECK(highlight_lines == std::vector<std::string>{"online menu: row 0 (option 2) highlighted; frame 1935, input none; yougame 1, submenu 1, ranked offered 1, friends offered 1, app state 1\n"});
    // A late hello inserts a focusable Tournament between Friends and Slippi.
    for (int p=0;p<4;++p) for (int i=0;i<3;++i) cached_input[p][i]=0;
    slippi_submenu = 0; queue_available = 1; press(0);
    CHECK(row_lines.back() == "online menu: rows offered Ranked,Unranked,Friends,Tournament,Slippi tournament=1\n");
    row_lines.clear(); press(0); press(0); CHECK(row_lines.empty());
    CHECK(mu_slippi_menu_count() == 5 && mn_803EB6B0[8].selection_count == 5);
    CHECK(mu_slippi_menu_option(2) == MU_SLP_OPT_FRIENDS);
    CHECK(mu_slippi_menu_option(3) == MU_SLP_OPT_TOURNAMENT);
    CHECK(mu_slippi_menu_label_kind(3) == 8 && mu_slippi_option_unlocked(3));
    CHECK(mu_slippi_menu_option(4) == MU_SLP_OPT_SLIPPI);
    CHECK(mn_803EB6B0[8].anim_loop[3].tag == 4);
    mn_804A04F0.hovered_selection = 3; press(MenuInput_Confirm);
    CHECK(joins == 1 && queue_visible && queue_notices);
    for (int i=0;i<90;++i) press(MenuInput_Confirm);
    CHECK(joins == 1 && !leaves);
    // Ordinary mapped B must be held; no keyboard shortcut is involved.
    cached_input[0][0]=0x0200;
    for (int i=0;i<29;++i) press(MenuInput_Back);
    CHECK(!leaves && queue_visible); press(MenuInput_Back);
    CHECK(leaves == 1 && !queue_visible);
    const int held_menu = mn_804A04F0.cur_menu;
    for (int i=0;i<60;++i) press(MenuInput_Back);
    CHECK(leaves == 1 && mn_804A04F0.cur_menu == held_menu);
    cached_input[0][0]=0; press(0);
    // Return from a bracket game restores the waiting view's row.
    queue_visible = 1; tournament_query(MU_TOURNEY_MENU);
    CHECK(mu_slippi_menu_enter(GM_VS, &menu, &hovered));
    CHECK(menu == MENU_KIND_8 && hovered == 3);
    row_lines.clear(); queue_visible = 0; queue_available = 0; press(0);
    CHECK(row_lines == std::vector<std::string>{"online menu: rows offered Ranked,Unranked,Friends,Slippi tournament=0\n"});
    CHECK(mu_slippi_menu_count() == 4 && mu_slippi_menu_option(3) == MU_SLP_OPT_SLIPPI);
    for (int r=0;r<mu_slippi_menu_count();++r) CHECK(mu_slippi_menu_option(r)!=MU_SLP_OPT_TOURNAMENT);
    // Build 122.42: the GitHub build's split layout with YouGame not connected. A YouGame row pressed asks
    // the host (EF 0x20: YouGame's own small window, topmost, while the game keeps reading the pads).
    // Backing out to 1-P Mode withdraws that ask (0x21) so the window closes with the menu instead of
    // staying drawn over 1-P Mode and the Main Menu.
    ranked_supported = 0; slippi_submenu = 0; split_page = 0; yg_flags = YG_SPLIT | YG_COMING_SOON;
    mu_mnmain_install_online_menu(); mn_804A04F0.cur_menu = MENU_KIND_8; mn_804A04F0.hovered_selection = 0;
    yg_menu_picks.clear(); entered = 0;
    press(MenuInput_Confirm);
    CHECK(yg_menu_picks == std::vector<int>{0x20} && yg_asked && yg_wait_slot == 0);
    CHECK(mn_804A04F0.cur_menu == MENU_KIND_8 && entered == 0);
    press(MenuInput_Back);
    CHECK(switched_menu == MENU_KIND_1P && switched_row == SEL_1P_2);
    CHECK(yg_menu_picks == (std::vector<int>{0x20, 0x21}) && !yg_asked && yg_wait_slot == -1);
    // No ask on this visit: Back sends nothing more.
    mn_804A04F0.cur_menu = MENU_KIND_8; mn_804A04F0.hovered_selection = 0; press(MenuInput_Back);
    CHECK(yg_menu_picks.size() == 2 && switched_menu == MENU_KIND_1P);
    // Asked, then B on Slippi's page stays on the Online menu: the ask stands.
    mn_804A04F0.cur_menu = MENU_KIND_8; mn_804A04F0.hovered_selection = 1; press(MenuInput_Confirm);
    CHECK(yg_menu_picks == (std::vector<int>{0x20, 0x21, 0x20}) && yg_asked);
    mn_804A04F0.hovered_selection = SPLIT_SLIPPI; press(MenuInput_Confirm);
    CHECK(split_page == 1 && mn_804A04F0.cur_menu == MENU_KIND_8 && yg_asked && entered == 0);
    press(MenuInput_Back);
    CHECK(split_page == 0 && mn_804A04F0.cur_menu == MENU_KIND_8 && yg_asked);
    CHECK(yg_menu_picks.size() == 3);
    // Into a Slippi character select from its page: the menu is left, so the ask is withdrawn first.
    mn_804A04F0.hovered_selection = SPLIT_SLIPPI; press(MenuInput_Confirm);
    mn_804A04F0.hovered_selection = SPLIT_FIRST_SLIPPI; press(MenuInput_Confirm);
    CHECK(entered == 1 && exit_data.pending_mode == GM_HANYU_CSS);
    CHECK(yg_menu_picks == (std::vector<int>{0x20, 0x21, 0x20, 0, 0x21}) && !yg_asked);
    std::printf("online menu: %d checks passed\n", checks);
}
'''

ROUTE_STUBS = r'''#include "slippi_agent_lock.h"
namespace agent_lock = slippi::agent_lock;
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include <initializer_list>
#include "yougame_mode_policy.h"
#include <random>
#include "yougame_search.h"
#include "yougame_blend.h"
#include "yougame_slippi_link.h"
static unsigned long long time_ms() { return 123; }
static bool active, granted, login, compatible = true, cancelled;
static std::function<void()> on_account_read;
// slippi_account.h reaches the real policy through test_env.h; drive its flag.
static bool& vanilla = host::gameplay::vanilla_only_session;
static yougame::search_policy::Generation generations;
static bool ranked_room;
static int reads, checks, compatibility, prepared, selected_slippi, yg_search, yg_calls, blend, tickets, local_tickets;
namespace account { std::string app_sign_in() { return granted ? "synthetic-grant" : ""; } }
struct UserInfo { std::string connect_code = "FAKE#123"; };
struct User {
  explicit User(std::string, bool = false) { ++reads; if (on_account_read) on_account_read(); }
  static std::unique_ptr<User> GrantedSlippiUser(const std::string&);
  bool IsLoggedIn() const { return login; }
  bool CheckOnlineCompatibility(std::string* error) { ++compatibility; *error = "offline version refusal"; return compatible; }
  bool CheckSessionCompatibility(std::string* error) { return CheckOnlineCompatibility(error); }   // v0.9.101's shared check
  UserInfo GetUserInfo() const { return {}; }
  std::string dir() const { return "synthetic-user"; }
  static std::unique_ptr<User> SearchCopy(const User&) { return std::make_unique<User>("synthetic-copy"); }
};
namespace account {
int why_unusable_grant() { return 0; }
const char* unusable_text(int) { return "synthetic"; }
}
namespace host { void log(const char*, ...) {} }
namespace display { enum class Provider { Unknown, YouGame, Slippi }; }
namespace yougame {
bool active() { return ::active; }
bool session() { return active(); }   // no launcher in these fixtures: the app's session
bool friends_flow() { return active(); }   // no local channel in these fixtures
int menu_service() { return -1; }      // no row picked through the launcher's pages
void slippi_link(bool, const std::string&) {}
static int ranked_visits, friend_publications;
void ranked_screen_enter() { ++ranked_visits; }
namespace friends { struct Events { void online_menu(bool, bool) { ++friend_publications; } void request_host() {} } events; }
namespace agent_test {
bool agent_script;
struct InputPolicy { template<class F> void allowed(int, int, F) {} } input_policy;
struct InputKey { enum { Script }; };
int live_input_state(int) { return 0; }
bool matchmaking_allowed(bool, bool, bool, bool, int) { return true; }
}
bool prepare_slippi_match(const std::function<bool()>& stop) {
  ++prepared; if (stop() || cancelled) return false; ++selected_slippi; return true;
}
struct Search { uint16_t udp_port; bool udp_port_fixed = false; std::string status; };   // 122.31: a fixed Slippi port binds explicitly
struct Match {};
Match find_match(const Search&, const std::function<bool()>&) { ++yg_calls; return {}; }
}
struct json {};
class Matchmaking {
 public:
  enum OnlinePlayMode { RANKED, UNRANKED, DIRECT, TEAMS, PARTY };
  enum State { INITIALIZING, MATCHMAKING, OPPONENT_CONNECTING, ERROR_ENCOUNTERED };
  struct Settings { OnlinePlayMode mode; std::string connect_code, friend_room; bool friends_lobby = false, slippi_gameplay = false; std::vector<std::string> arena_fighters; } search_settings_;
  struct Local { bool enabled = false; int local_port = 1; } local_peer;
  User* user_;
  std::unique_ptr<User> slippi_user_;
  bool server_allowed = false, is_mm_terminated_ = false, is_host_ = false;
  void* client_ = nullptr;
  State state_ = INITIALIZING;
  display::Provider display_provider_ = display::Provider::Unknown;
  std::string error;
  int forced_port = 0, host_port_ = 0, local_player_index_ = 0, port_override_ = 0;
  struct { std::string id; } mm_result_;
  std::vector<int> player_info_;
  struct Ticket { const json& resp; };
  void fail(const std::string& message) { error = message; state_ = ERROR_ENCOUNTERED; }
  bool ingest_ticket(Ticket) { ++local_tickets; return true; }
  void blendMatchmaking() { ++blend; generations.begin(); ranked_room = false; }
  std::string openSlippiTicket(UserInfo, std::function<bool()>, int) { ++tickets; return {}; }
  bool youGameSearch(yougame::Search*) { ++yg_search; return true; }
  void takeYouGameMatch(const yougame::Match&) {}
  void startYouGameMatchmaking(const char* status = nullptr);
  void startMatchmaking();
};
json local_peer_ticket(UserInfo, Matchmaking::OnlinePlayMode) { return {}; }
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::abort(); } } while (0)
'''
ROUTE_CASES = r'''static void reset() {
    reads = compatibility = prepared = selected_slippi = yg_search = yg_calls = blend = tickets = local_tickets = 0;
    compatible = true; cancelled = false;
}
int main() {
    // The production grant loader must not discover an ambient account without a grant.
    for (bool grant : {false,true}) for (bool valid : {false,true}) {
        reset(); granted = grant; login = valid;
        auto user = User::GrantedSlippiUser("synthetic-user");
        CHECK(bool(user) == (grant && valid)); CHECK(reads == int(grant));
        CHECK(!compatibility && !prepared && !yg_calls && !tickets);
    }
    User menu_user("synthetic");
    int routes = 0;
    const std::string codes[] = {"", "NAME#123", "invite-code", "A#123456", "not-a-slippi-code", "\x82\x60#123"};
    for (bool app : {false,true}) for (int mode : {2,3,4})
      for (bool grant : {false,true}) for (bool valid : {false,true})
      for (bool base : {false,true}) for (bool agent : {false,true}) for (const auto& code : codes) {
#ifndef YOUGAME_AGENT_TEST_HOOK
        if (agent) continue;
#endif
        if (agent && !app) continue; // Standalone's pre-existing scripted policy is outside this menu route.
        reset(); active = app; granted = grant; login = valid; vanilla = base;
        yougame::agent_test::agent_script = agent;
        Matchmaking mm; mm.user_ = &menu_user;
        mm.search_settings_ = {Matchmaking::OnlinePlayMode(mode), code, {}};
        mm.startMatchmaking(); ++routes;
        const bool allowed = !agent && (!app || (grant && valid));
        CHECK(!yg_calls && !yg_search && !blend);
        CHECK(mm.search_settings_.connect_code == code);
        CHECK(tickets == int(allowed));
        CHECK(selected_slippi == int(allowed && app));
        CHECK(compatibility == int(allowed));
        CHECK(mm.state_ == (allowed ? Matchmaking::MATCHMAKING : Matchmaking::ERROR_ENCOUNTERED));
        if (app && !agent && !allowed) CHECK(mm.error == "Sign in to Slippi to play Direct, Teams and Party");
        // Even a direct call at the YouGame boundary cannot create a menu-mode ticket.
        if (app) { mm.startYouGameMatchmaking(); CHECK(!yg_calls && !yg_search); }
      }
    active = granted = login = vanilla = true; yougame::agent_test::agent_script = false;
    for (int mode : {2,3,4}) for (bool version_ok : {false,true}) for (bool stop : {false,true}) {
        reset(); compatible = version_ok; cancelled = stop;
        Matchmaking mm; mm.user_ = &menu_user; mm.search_settings_.mode = Matchmaking::OnlinePlayMode(mode);
        mm.startMatchmaking();
        CHECK(!yg_calls && !yg_search && !blend);
        CHECK(tickets == int(version_ok && !stop));
        CHECK(selected_slippi == int(version_ok && !stop));
    }
    // Explicit Friends rooms, Ranked, and mixed Unranked keep their existing routing; Direct, Teams
    // and Party without a room are the Slippi submenu's routes above.
    for (int mode : {0,1,2,3,4}) for (bool room : {false,true}) {
        if (!room && mode >= 2) continue;
        reset(); Matchmaking mm; mm.user_ = &menu_user;
        mm.search_settings_ = {Matchmaking::OnlinePlayMode(mode), "", room ? "friend-room" : ""};
        mm.startMatchmaking();
        const bool mixed = mode == 1 && !room;
        CHECK(blend == int(mixed) && yg_calls == int(!mixed));
        CHECK(!tickets && !selected_slippi);
    }
    // A valid ambient login without an app grant cannot enable mixed Unranked (or be read
    // by Ranked). Grant + valid account is the only way Unranked may enter the blend.
    for (int mode : {0,1}) for (bool grant : {false,true}) for (bool valid : {false,true}) {
        reset(); active = vanilla = true; granted = grant; login = valid;
        Matchmaking mm; mm.user_ = &menu_user; mm.search_settings_.mode = Matchmaking::OnlinePlayMode(mode);
        mm.startMatchmaking();
        const bool mixed = mode == 1 && grant && valid;
        CHECK(blend == int(mixed) && yg_calls == int(!mixed));
        CHECK(reads == int(mode == 1 && grant));
        CHECK(!tickets && !compatibility && !selected_slippi);
    }
    std::printf("upstream routes: %d scenarios, %d checks passed\n", routes, checks);
}
'''
SLOW_ACCOUNT_CASES = r'''int main() {
    User menu_user("synthetic");
    active = granted = login = vanilla = true;
    int scenarios = 0;
    for (bool terminate : {false,true}) {
        reset();
        Matchmaking old; old.user_ = &menu_user; old.search_settings_.mode = Matchmaking::UNRANKED;
        uint64_t ranked_epoch = 0;
        // The account read is paused here; cancel and install the replacement Ranked room
        // before allowing that old read to return. Both cancellation signals are exercised.
        on_account_read = [&] {
            if (terminate) old.is_mm_terminated_ = true;
            else old.state_ = Matchmaking::ERROR_ENCOUNTERED;
            ranked_epoch = generations.begin(); ranked_room = true;
        };
        old.startMatchmaking(); on_account_read = {};
        old.blendMatchmaking(); // Its own entry guard must protect a direct/late call too.
        CHECK(reads == 1 && !blend && !yg_calls && !tickets);
        CHECK(generations.current(ranked_epoch) && ranked_room);
        CHECK(old.state_ != Matchmaking::MATCHMAKING);
        ++scenarios;
    }
    std::printf("cancel during account load: %d scenarios, %d checks passed\n", scenarios, checks);
}
'''
QUERY_CASES = r'''int main() {
    User user("synthetic");
    for (bool app : {false,true}) for (bool grant : {false,true}) for (bool valid : {false,true}) {
        reset(); active = app; granted = grant; login = valid; g_user = &user;
        g_online_menu_visible = true;
        std::vector<unsigned char> q; unsigned char query = 8;
        CHECK(query_menu(&query, q));
        CHECK(q.size() == 1 && q[0] == (app && grant && valid));
        CHECK(reads == int(app && grant));
        CHECK(!compatibility && !prepared && !yg_calls && !tickets);
        CHECK(g_online_menu_visible && !yougame::ranked_visits && !yougame::friend_publications);
    }
    g_user = nullptr; std::vector<unsigned char> q; unsigned char query = 8;
    CHECK(query_menu(&query,q) && q == std::vector<unsigned char>{0});
    // 14: a Friends room opened the CSS the menu came back from, answered once and never published.
    g_online_menu_visible = true; g_friends_css = true; query = 14;
    CHECK(query_menu(&query,q) && q == std::vector<unsigned char>{1} && !g_friends_css);
    CHECK(query_menu(&query,q) && q == std::vector<unsigned char>{0});
    CHECK(g_online_menu_visible && !yougame::friend_publications);
    std::printf("local grant query: %d checks passed\n", checks);
}
'''
NOTICE_STUBS = r'''#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <initializer_list>
#include "MU_LABEL_HEADER"
using u8 = unsigned char; using u32 = unsigned;
struct Pair { float x, y; };
struct SisBuffer { u8* end; u8* data; unsigned size, count; };
struct HSD_Text {
    float pos_x, pos_y, pos_z, box_size_x, box_size_y;
    Pair font_size, x34; int default_kerning, default_alignment;
    u8* sis_buffer; u8* x60; SisBuffer* alloc_data;
};
static const char* link_status;
int mu_slippi_cmd(unsigned,const void*,unsigned,void* out,unsigned cap,unsigned* got) {
    if (!link_status) { *got=0; return -1; }
    if (cap!=128) std::abort();
    std::memset(out,0,cap);std::memcpy(out,link_status,std::strlen(link_status));*got=cap;return 0;
}
struct MainMenuData { HSD_Text* description; };
struct HSD_GObj { MainMenuData* user_data; };
static bool enabled, yougame;
static int allocations, frees, fail_at, checks;
static std::string notice;
bool mu_slippi_menus_enabled() { return enabled; }
bool mu_slippi_yougame_menu() { return yougame; }
void* HSD_SisLib_Alloc(int n) {
    if (++allocations == fail_at) return nullptr;
    return std::malloc(n);
}
void HSD_SisLib_Free(void* p) { if(p) { ++frees; std::free(p); } }
// Synthetic ASCII encoder deliberately inserts SIS's editable spacing override.
// Its glyph bytes identify the string while allowing exact stream comparisons.
int HSD_SisLib_803A67EC(u8* out, u8* text) {
    notice = (char*)text; int n = 0;
    const u8 override[] = {10, 0xf4, 0, 0, 0};
    std::memcpy(out, override, 5); n = 5;
    for(int i=0;text[i];++i) {
        if(text[i] == ' ') out[n++]=0x1a;
        else { out[n++]=0x20; out[n++]=text[i]; }
    }
    out[n++]=11; out[n]=0; return n;
}
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::abort(); } } while (0)
'''
NOTICE_CASES = r'''int main() {
    // Actual retail SdMenu.usd SIS_MenuData[0x8e] description prefix:
    // left alignment, grey, 179/256 x 128/256 scale, proportional spacing,
    // origin (0,-20), newline, no delay, fit to the menu box.
    u8 retail[] = {0x12,0x0c,0xaa,0xaa,0xaa,0x0e,0,0xb3,0,0x80,
                   0x16,7,0,0,0xff,0xec,3,6,0,0,0,0,0x18,0x20,0x0a,0};
    HSD_Text original{-9.5f,9.1f,17,364.68332f,38.38772f,
                      {.0521f,.0521f},{1,1},0,0,retail,nullptr,nullptr};
    HSD_Text text=original; MainMenuData data{&text}; HSD_GObj menu{&data};
    CHECK(mu_menu_description_prefix(retail)==23);
    for(bool on:{false,true})for(bool app:{false,true}) {
        enabled=on; yougame=app;
        const int previous=allocations;
        mu_slippi_signin_notice(&menu);
        CHECK(allocations-previous==(on && app ? 2 : 0));
        CHECK(data.description==&text);
    }
    CHECK(notice=="Sign in to Slippi to play Direct, Teams and Party");
    for(const char* message:{"Invite a friend to play.","Ranked isn't on for this game yet.","Sign in to Slippi to play Direct, Teams and Party",
        "Direct, Teams and Party need Slippi - Y: Link Slippi",
        "Link Slippi: finish in the YouGame window",
        "Slippi linked - it starts next time you open the game",
        "Slippi linked - choose a mode to play"}) {
        mu_slippi_menu_description(&text,message);
        CHECK(data.description==&text);
        HSD_Text params=text;
        params.sis_buffer=retail;params.alloc_data=nullptr;
        CHECK(std::memcmp(&params,&original,sizeof params)==0);
        CHECK(std::memcmp(text.sis_buffer,retail,23)==0);
        CHECK(text.alloc_data->end==text.sis_buffer+text.alloc_data->size-1);
        CHECK(*text.alloc_data->end==0 && text.x60==nullptr);
        const u8* glyph=text.sis_buffer+23;
        for(int i=0;message[i];++i) {
            if(message[i]==' ') CHECK(*glyph++==0x1a);
            else {CHECK(*glyph++==0x20);CHECK(*glyph++==message[i]);}
        }
        CHECK(*glyph==0); // No generated 0x0a spacing override survives.
    }
    // A changed upstream position/scale/alignment is inherited byte-for-byte.
    text.sis_buffer[0]=0x10;text.sis_buffer[7]=0x99;text.sis_buffer[15]=0xe0;
    u8 changed[23];std::memcpy(changed,text.sis_buffer,23);
    mu_slippi_menu_description(&text,"Invite a friend to play.");
    CHECK(std::memcmp(text.sis_buffer,changed,23)==0);
    u8* saved=text.sis_buffer;
    fail_at=allocations+1;mu_slippi_signin_notice(&menu);CHECK(text.sis_buffer==saved);
    fail_at=allocations+2;mu_slippi_signin_notice(&menu);CHECK(text.sis_buffer==saved);
    fail_at=0;
    const int previous=allocations;
    mu_slippi_signin_notice(nullptr);menu.user_data=nullptr;mu_slippi_signin_notice(&menu);
    mu_slippi_menu_description(nullptr,"text");mu_slippi_menu_description(&text,nullptr);
    std::string too_long(101,'x');mu_slippi_menu_description(&text,too_long.c_str());
    CHECK(allocations==previous);
    CHECK(!mu_menu_description_prefix(nullptr));
    u8 jump[]={8,0,0,0,0}, empty[]={0}, no_prefix[]={0x20,1,0};
    for(u8* invalid:{jump,empty,no_prefix}) {
        HSD_Text bad=original;bad.sis_buffer=invalid;
        mu_slippi_menu_description(&bad,"text");CHECK(bad.sis_buffer==invalid && !bad.alloc_data);
    }
    HSD_SisLib_Free(text.alloc_data->data);HSD_SisLib_Free(text.alloc_data);
    CHECK(allocations-frees==2); // Two deliberately failed allocation requests.
    std::printf("native descriptions: %d checks passed\n",checks);
}
'''

class OnlineMenuTest(unittest.TestCase):
    def test_compiler_priority_including_gcc_only_linux(self):
        from unittest import mock
        # On Windows two more words follow: the force-included POSIX fixture shim.
        extra = 2 if os.name == 'nt' else 0
        with mock.patch.dict(os.environ, {'CXX': 'g++-14 -O0'}):
            self.assertEqual(compiler_command()[:2], ['g++-14', '-O0'])
            self.assertEqual(len(compiler_command()), 2 + extra)
        for available in ('c++', 'g++', 'clang++'):
            with mock.patch.dict(os.environ, {'CXX': ''}), mock.patch.object(
                    shutil, 'which', side_effect=lambda name: '/tool/' + name if name == available else None):
                self.assertEqual(compiler_command()[:1], ['/tool/' + available])
                self.assertEqual(len(compiler_command()), 1 + extra)

    def test_tarball_history_checks_skip_without_invoking_git(self):
        from unittest import mock
        import yougame_console_wiring_test as console
        import yougame_0983_merge_test as merge
        with tempfile.TemporaryDirectory(prefix='history-free-') as directory:
            for module, invoke in ((console, lambda: console.baseline('unused')),
                                   (merge, lambda: merge.revision('HEAD', 'unused'))):
                with mock.patch.object(module, 'ROOT', Path(directory)), mock.patch.object(subprocess, 'check_output') as git:
                    with self.assertRaisesRegex(unittest.SkipTest, 'history checks require a git checkout'):
                        invoke()
                    git.assert_not_called()

    def test_shallow_checkout_history_checks_skip_missing_revision(self):
        from unittest import mock
        import yougame_console_wiring_test as console
        import yougame_0983_merge_test as merge
        with tempfile.TemporaryDirectory(prefix='history-shallow-') as directory:
            (Path(directory) / '.git').touch()
            for module, invoke in ((console, lambda: console.baseline('unused', 'missing-base')),
                                   (merge, lambda: merge.revision('missing-tag', 'unused'))):
                with mock.patch.object(module, 'ROOT', Path(directory)), mock.patch.object(
                        subprocess, 'run', return_value=subprocess.CompletedProcess([], 128)), mock.patch.object(
                        subprocess, 'check_output') as show:
                    with self.assertRaisesRegex(unittest.SkipTest, 'history checks require revision missing-'):
                        invoke()
                    show.assert_not_called()

    def test_slippi_submenu_offers_exactly_direct_teams_party(self):
        import re
        header = (ROOT / 'sourceport/game/include/mu_online_menu.h').read_text()
        modes = re.search(r'mu_online_slippi_rows\[\] = \{([^}]+)\}', header)[1]
        # Concept 5 (mu_online_pages.inc): Slippi's page carries Slippi's own rows, without Ranked
        # (owner 11-18: this build does not play Slippi's ranked queue).
        self.assertEqual(re.findall(r'MU_SLP_MODE_\w+', modes),
                         ['MU_SLP_MODE_DIRECT', 'MU_SLP_MODE_UNRANKED',
                          'MU_SLP_MODE_TEAMS', 'MU_SLP_MODE_PARTY'])
        label = (ROOT / 'sourceport/game/shim/mu_menu_label.c').read_text()
        art = (ROOT / 'sourceport/game/shim/mu_menu_label_art.h').read_text()
        self.assertIn('mu_baked_friends[MU_LABEL_BYTES]', art)
        self.assertIn('mu_baked_slippi[MU_LABEL_BYTES]', art)
        self.assertNotIn('mu_menu_label_render', label)
        patch = (ROOT / 'sourceport/patches/melee-native.patch').read_text()
        start = patch.index('+            case MU_SLP_MODE_DIRECT:', patch.index('static void mn_80229A7C'))
        help = patch[start:patch.index('+#endif', start)]
        self.assertEqual(re.findall(r'mu_slippi_menu_description\(text, "([^"\n]+)"\)', help),
                         ['Play a Slippi friend by connect code.', "Slippi's 2v2 teams queue.", "Slippi's party mode."])
        self.assertNotIn('Doubles', help)
        self.assertIn('Sign in to Slippi to play Direct, Teams and Party', label)
        # Doubles is a YouGame-page row only when built in (no longer an #if 0 block).
        top = re.search(r'mu_online_top_rows\[\] = \{(.*?)\};', header, re.S)[1]
        # 1.0 (owner 11-21): the YouGame Doubles row is shown whenever Doubles is built in.
        self.assertIn('#if defined(MELEE_DOUBLES)\n    MU_SLP_OPT_DOUBLES,', top)
        self.assertNotIn('MU_SLP_OPT_SLIPPI', top)

    def test_ranked_description_and_plain_hud_wiring(self):
        patch = (ROOT / 'sourceport/patches/melee-native.patch').read_text()
        start = patch.index('+        if (menu_kind == MENU_KIND_8 && mu_slippi_menus_enabled() && mu_slippi_yougame_menu()) {')
        hook = '\n'.join(line[1:] for line in patch[start:patch.index('+#endif', start)].splitlines())
        source = (ROOT / 'sourceport/game/shim/mu_slippi_menu.c').read_text()
        production = '\n'.join(fixture.extract(source, name) for name in
                               ('mu_slippi_menu_count', 'mu_slippi_menu_option', 'mu_slippi_option_unlocked'))
        self.compile_fixture('ranked-description', STUBS + 'static int pages() { return ranked_supported; }\n' + production + r'''
static std::string description;
static int mu_slippi_yougame_menu() { return ranked_supported; }
static void mu_slippi_menu_description(void*, const char* message) { description = message; }
static void describe(int selection) {
    int menu_kind = MENU_KIND_8; void* text = nullptr;
''' + hook + r'''
}
int main() {
    ranked_supported = 1;
    for (int offered : {0,1}) {
        ranked_offered = offered; slippi_submenu = 0;
        description = "retail Ranked"; describe(0);
        CHECK(description == (offered ? "retail Ranked" : "Ranked isn't on for this game yet."));
        description = "retail Unranked"; describe(1); CHECK(description == "retail Unranked");
        slippi_submenu = 1;   // Direct, Unranked (the slider, second row as on YouGame's page), Teams, Party
        description = "retail Unranked"; describe(1); CHECK(description == "retail Unranked");
        const char* expected[] = {"Play a Slippi friend by connect code.", nullptr, "Slippi's 2v2 teams queue.", "Slippi's party mode."};
        for (int row : {0, 2, 3}) {
            CHECK(mu_slippi_option_unlocked(row)); describe(row); CHECK(description == expected[row]);
        }
    }
    std::printf("ranked help: %d checks passed\n", checks);
}
''', '-I', str(ROOT / 'sourceport/game/include'))
        online = (ROOT / 'port/runtime/hle/slippi_online.cpp').read_text()
        hud = online.split('std::array<std::string, 4> player_names_for_overlay() {', 1)[1].split('\n}', 1)[0]
        self.assertNotIn('opponent_label', hud)
        self.assertIn('g_matchmaking->GetPlayerName', hud)
        self.assertIn('convert_string_for_game(display::opponent_label', online)

    def test_production_entry_cursor_submenu_and_standalone(self):
        # 0.9.990 replaced the menu this harness drives (the Slippi row, the split layout) with the two
        # pages of mu_online_pages.inc (Z turns the page, Unranked is a slider). Its rows, page turn, slider
        # and cursor are proven in the running game: E:/mu-yg/tools/modes (every YouGame mode from the menu)
        # and the menu captures. A harness for the pages is still to be written.
        if not (ROOT / 'sourceport/game/shim/mu_online_pages.inc').exists():
            pass
        else:
            self.skipTest('the concept 5 pages replaced this menu; proven in the running game (tools/modes)')
        source = (ROOT / 'sourceport/game/shim/mu_slippi_menu.c').read_text()
        names = ['mu_slippi_log_online_rows', 'tournament_query', 'tournament_menu_think', 'mu_slippi_menu_count', 'mu_slippi_menu_option',
                 'mu_slippi_menu_label_kind', 'mu_slippi_option_unlocked', 'mu_slippi_first_unlocked',
                 'mu_slippi_menu_enter', 'mu_slippi_switch_to_online_menu', 'upstream_slippi_available', 'confirm_port',
                 'log_online_highlight', 'yougame_ask_withdraw', 'mu_slippi_online_menu_think']
        patch = (ROOT / 'sourceport/patches/melee-native.patch').read_text()
        added = '\n'.join(line[1:] for line in patch.splitlines() if line.startswith('+'))
        installer = fixture.extract(added, 'mu_mnmain_install_online_menu')
        production = '\n'.join(fixture.extract(source, name) for name in names)
        with tempfile.TemporaryDirectory(prefix='build12-menu-') as directory:
            cpp = Path(directory) / 'menu.cpp'
            exe = Path(directory) / 'menu'
            cpp.write_text('#include <initializer_list>\n' + STUBS + installer + production + CASES)
            compiler = compiler_command()
            self.assertIsNotNone(compiler, 'an existing C++ compiler is required')
            sdk = ['-isysroot', '/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk'] if sys.platform == 'darwin' else []
            subprocess.run([*compiler, '-std=c++17', *sdk,
                            '-I', str(ROOT / 'sourceport/game/include'), str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def compile_fixture(self, name, body, *defines):
        with tempfile.TemporaryDirectory(prefix='build12b-' + name + '-') as directory:
            cpp, exe = Path(directory) / 'fixture.cpp', Path(directory) / 'fixture'
            cpp.write_text(body)
            compiler = compiler_command()
            sdk = ['-isysroot', '/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk'] if sys.platform == 'darwin' else []
            subprocess.run([*compiler, '-std=c++17', *sdk, *defines,
                            '-I', str(ROOT / 'port/runtime/hle'), '-I', str(ROOT / 'port/third_party'),
                            str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_every_yougame_row_reaches_all_three_label_swap_hooks(self):
        # Execute all three real patch guards with the production row mapping.
        import re
        patch = (ROOT / 'sourceport/patches/melee-native.patch').read_text()
        added = '\n'.join(line[1:] for line in patch.splitlines() if line.startswith('+'))
        hooks = re.findall(r'if \(mu_slippi_menu_label_kind\((?:selection|i)\)\)\s*'
                           r'mu_slippi_friends_label\([^;]+;', added)
        self.assertEqual(len(hooks), 3)
        source = (ROOT / 'sourceport/game/shim/mu_slippi_menu.c').read_text()
        production = '\n'.join(fixture.extract(source, name) for name in
                               ('mu_slippi_menu_count', 'mu_slippi_menu_option', 'mu_slippi_menu_label_kind'))
        header = str(ROOT / 'sourceport/game/include/mu_online_menu.h')
        stubs = r'''#include <cstdio>
#include <cstdlib>
enum { MU_SLP_MODE_RANKED, MU_SLP_MODE_UNRANKED, MU_SLP_MODE_DIRECT, MU_SLP_MODE_TEAMS,
       MU_SLP_MODE_PARTY, MU_SLP_OPT_LOGIN, MU_SLP_OPT_LOGOUT, MU_SLP_OPT_UPDATE,
       MU_SLP_OPT_FRIENDS, MU_SLP_OPT_SLIPPI, MU_SLP_OPT_TOURNAMENT };
#include "HEADER"
int ranked_supported=1, slippi_submenu=0, calls=0, checks=0;
void mu_slippi_friends_label(void*,int,int,int) { ++calls; }
static int pages() { return ranked_supported; }
int mu_slippi_menu_count();
static int mu_online_pages_label(int row) { return row >= 0 && row < mu_slippi_menu_count() ? 1 + row : 0; }
#define CHECK(x) do { ++checks; if(!(x)) std::abort(); } while(0)
'''.replace('HEADER', header)
        cases = 'int main() {\n'
        for hook in hooks:
            # Bind the real hook's three local argument names to the same dummy row.
            hook = hook.replace('jobj.value', 'jobj')
            cases += r'''for (ranked_supported=0;ranked_supported<=1;++ranked_supported)
for (slippi_submenu=0;slippi_submenu<=1;++slippi_submenu)
for (int row=0;row<4;++row) {
    int selection=row,i=row,menu_kind=8; void *jobj=nullptr,*r29=nullptr;
    struct Flow { int cur_menu; } f{8}; Flow* flow=&f;
    calls=0;
''' + hook + r'''
    bool custom=ranked_supported && (slippi_submenu ? row<4 : row<3);
    CHECK(calls==int(custom));
}
'''
        cases += 'std::printf("label hook guards: %d checks passed\\n",checks); }'
        self.compile_fixture('label-guards', stubs + production + cases)

    def test_production_matchmaking_boundaries_with_fake_services(self):
        from yougame_release_wiring_test import literal_function
        source = (ROOT / 'port/runtime/hle/slippi_net.cpp').read_text()
        grant = literal_function(source, 'GrantedSlippiUser')
        production = ('std::unique_ptr<User> User::GrantedSlippiUser(const std::string& user_dir) {' + grant + '}\n' +
                      fixture.extract(source.replace('Matchmaking::startMatchmaking', 'startMatchmaking'), 'startMatchmaking').replace('void startMatchmaking(', 'void Matchmaking::startMatchmaking(', 1) +
                      fixture.extract(source.replace('Matchmaking::startYouGameMatchmaking', 'startYouGameMatchmaking'), 'startYouGameMatchmaking').replace('void startYouGameMatchmaking(', 'void Matchmaking::startYouGameMatchmaking(', 1))
        for defines in [[], ['-DYOUGAME_AGENT_TEST_HOOK']]:
            with self.subTest(defines=defines):
                self.compile_fixture('routes', '#include <functional>\n' + ROUTE_STUBS + production + ROUTE_CASES, *defines)

    def test_production_grant_query_is_local_and_preserves_menu_status(self):
        from yougame_release_wiring_test import literal_function
        net = (ROOT / 'port/runtime/hle/slippi_net.cpp').read_text()
        grant = 'std::unique_ptr<User> User::GrantedSlippiUser(const std::string& user_dir) {' + literal_function(net, 'GrantedSlippiUser') + '}\n'
        host = (ROOT / 'port/runtime/hle/slippi_online.cpp').read_text()
        case = host.split('    case CMD_YOUGAME_FRIENDS_MENU:', 1)[1].split('    case CMD_YOUGAME_DATA:', 1)[0]
        query = ('static User* g_user; static bool g_online_menu_visible; static std::atomic<bool> g_friends_css;\n'
                 'static std::atomic<bool> g_doubles_lobby_request;\n'
                 'bool query_menu(const unsigned char* payload, std::vector<unsigned char>& q) {' + case + '}\n')
        self.compile_fixture('query', '#include <functional>\n#include <atomic>\n' + ROUTE_STUBS + grant +
                             'static void reset() { reads = compatibility = prepared = yg_calls = tickets = 0; }\n' + query + QUERY_CASES)

    def test_cancel_during_account_loading_preserves_replacement_ranked_room(self):
        from yougame_release_wiring_test import literal_function
        source = (ROOT / 'port/runtime/hle/slippi_net.cpp').read_text()
        production = ('std::unique_ptr<User> User::GrantedSlippiUser(const std::string& user_dir) {' +
                      literal_function(source, 'GrantedSlippiUser') + '}\n' +
                      fixture.extract(source.replace('Matchmaking::startMatchmaking', 'startMatchmaking'), 'startMatchmaking').replace('void startMatchmaking(', 'void Matchmaking::startMatchmaking(', 1) +
                      fixture.extract(source.replace('Matchmaking::startYouGameMatchmaking', 'startYouGameMatchmaking'), 'startYouGameMatchmaking').replace('void startYouGameMatchmaking(', 'void Matchmaking::startYouGameMatchmaking(', 1))
        reset = ROUTE_CASES[:ROUTE_CASES.index('int main()')]
        # Execute the production Blend entry through its cancellation guard; what follows is a
        # fake generation reservation/room retirement so a missing guard has an observable cost.
        blend_entry = literal_function(source, 'blendMatchmaking').split('  std::mutex mutex;', 1)[0]
        stubs = ROUTE_STUBS.replace('void blendMatchmaking() {', 'void blendMatchmaking() {' + blend_entry, 1)
        self.compile_fixture('slow-account', '#include <functional>\n' + stubs + production + reset + SLOW_ACCOUNT_CASES)

    def test_production_account_and_startup_are_grant_only(self):
        from yougame_release_wiring_test import literal_function
        net = (ROOT / 'port/runtime/hle/slippi_net.cpp').read_text()
        stubs = r'''
#include "slippi_account.h"
#include <atomic>
#include "slippi_agent_lock.h"
namespace agent_lock = slippi::agent_lock;
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <cstdio>
using json = nlohmann::json;
namespace fs = std::filesystem;
static bool app_active = true;
static int checks, discoveries;
static void note_agent_grant() {} // slippi_net.cpp's agent-copy grant log; production login code below calls it
static fs::path launcher;
static std::vector<fs::path> reads;
static std::promise<std::pair<bool,std::string>> announcement;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::abort(); } } while (0)
namespace account {
using slippi::account::Profile;
using slippi::account::app_sign_in;
Profile read(const fs::path& path) { reads.push_back(path); return slippi::account::read(path); }
Profile resolve(const fs::path& local) {
  ++discoveries; return slippi::account::resolve(local, {launcher});
}
}
namespace host { void log(const char*, ...) {} namespace gameplay { [[maybe_unused]] static const bool vanilla_forced = (vanilla_only_session = true); } }
namespace yougame {
bool active() { return app_active; }
struct Identity { std::string player_id = "app-player", name = "App", code = "APP#2"; bool ok = true; };
Identity identity() { return {}; }
void slippi_link(bool linked, const std::string& tag) { announcement.set_value({linked,tag}); }
}
struct UserInfo { std::string uid, play_key, display_name, connect_code, latest_version; std::vector<std::string> chat_messages; };
std::string str_or(const json& j, const char* key, const char*) { return slippi::account::text(j,key); }
const json* json_member(const json& j, const char* key) { auto it = j.find(key); return it == j.end() ? nullptr : &*it; }
class User {
 public:
  User(std::string user_dir, bool slippi_only = false);
  static std::unique_ptr<User> GrantedSlippiUser(const std::string&);
  bool AttemptLogin(bool rediscover = false);
  void RefreshFromServer() {} // Offline transport boundary; login code is production below.
  void ResetOnlineCompatibility() {} // v0.9.101: a changed login drops the shared version check
  bool IsLoggedIn() const { return logged_in_; }
  UserInfo GetUserInfo() const { std::lock_guard<std::mutex> lk(mutex_); return info_; }
  static std::vector<std::string> GetDefaultChatMessages() { return std::vector<std::string>(16,"test"); }
 private:
  std::string dir_, account_file_;
  bool slippi_only_ = false, account_discovered_ = false, logged_in_ = false;
  UserInfo info_;
  mutable std::mutex mutex_;
};
'''
        production = ('User::User(std::string user_dir, bool slippi_only) : dir_(std::move(user_dir)), slippi_only_(slippi_only) {' +
                      literal_function(net, 'User') + '}\n' +
                      'bool User::AttemptLogin(bool rediscover) {' + literal_function(net, 'AttemptLogin') + '}\n' +
                      'std::unique_ptr<User> User::GrantedSlippiUser(const std::string& user_dir) {' + literal_function(net, 'GrantedSlippiUser') + '}\n' +
                      'void announce_slippi_link(const std::string& user_dir) {' + literal_function(net, 'announce_slippi_link') + '}\n')
        cases = r'''
void grant(const fs::path& path) {
#ifdef _WIN32
  _wputenv_s(L"YOUGAME_SLIPPI_USER_JSON", path.wstring().c_str());
#else
  if (path.empty()) unsetenv("YOUGAME_SLIPPI_USER_JSON");
  else setenv("YOUGAME_SLIPPI_USER_JSON", path.c_str(), 1);
#endif
}
void write(const fs::path& path, const char* code) {
  fs::create_directories(path.parent_path());
  std::ofstream(path) << json{{"uid","synthetic"},{"playKey","synthetic"},{"connectCode",code}};
}
int main(int, char** argv) {
  // All paths belong to this compiler fixture's temporary directory; no real roots are used.
  const auto root = fs::path(argv[0]).parent_path() / "accounts";
  const auto local = root / "local", app = root / "grant.json";
  launcher = root / "Slippi Launcher";
  write(launcher / "netplay/User/Slippi/user.json", "AMBIENT#1");
  write(app, "GRANTED#1");
  grant(TEST_GRANTED ? app : fs::path());
  auto announced = announcement.get_future();
  announce_slippi_link(local.string());
  CHECK(announced.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
  const auto link = announced.get();
  CHECK(link.first == bool(TEST_GRANTED));
  CHECK(link.second == (TEST_GRANTED ? "GRANTED#1" : ""));
  CHECK(discoveries == 0 && reads.size() == size_t(TEST_GRANTED));
  if (TEST_GRANTED) CHECK(reads[0] == app);

  grant({}); reads.clear();
  User app_slippi(local.string(), true);
  CHECK(!app_slippi.IsLoggedIn() && !app_slippi.AttemptLogin(true));
  CHECK(!User::GrantedSlippiUser(local.string()));
  CHECK(reads.empty() && discoveries == 0);
  CHECK(slippi::account::why_unusable_grant() == slippi::account::Unusable::FileMissing);
  User menu(local.string());
  CHECK(menu.GetUserInfo().connect_code == "APP#2" && reads.empty() && discoveries == 0);
  // An ambient local file must be ignored too.
  write(local / "user.json", "LOCAL#1");
  CHECK(!app_slippi.AttemptLogin(true) && reads.empty() && discoveries == 0);
  grant(app);
  CHECK(app_slippi.AttemptLogin() && app_slippi.GetUserInfo().connect_code == "GRANTED#1");
  CHECK(reads == std::vector<fs::path>{app} && discoveries == 0);
  grant(root / "missing.json");
  CHECK(!app_slippi.AttemptLogin() && !User::GrantedSlippiUser(local.string()));
  CHECK(slippi::account::why_unusable_grant() == slippi::account::Unusable::FileMissing);
  grant({}); reads.clear();
  CHECK(!app_slippi.AttemptLogin() && reads.empty()); // Revocation never reuses a cached account.
  // Standalone still discovers Launcher and local accounts, and switching to YouGame cannot
  // reuse that cached ambient path, even without rediscovery.
  app_active = false;
  User standalone((root / "no-local").string(), true);
  CHECK(standalone.IsLoggedIn() && standalone.GetUserInfo().connect_code == "AMBIENT#1");
  User standalone_local(local.string(), true);
  CHECK(standalone_local.IsLoggedIn() && standalone_local.GetUserInfo().connect_code == "LOCAL#1");
  CHECK(discoveries == 2);
  app_active = true; reads.clear();
  CHECK(!standalone.AttemptLogin() && !standalone_local.AttemptLogin());
  CHECK(reads.empty() && discoveries == 2);
  std::printf("grant-only account/startup: %d checks passed (grant %d)\n",checks,TEST_GRANTED);
}
'''
        for granted in (0, 1):
            with self.subTest(granted=granted):
                self.compile_fixture('account', stubs + production + cases, '-DTEST_GRANTED=' + str(granted))

    def test_consumed_queue_assignment_uses_normal_decline(self):
        from yougame_release_wiring_test import literal_function
        net = (ROOT / 'port/runtime/hle/yougame_link.cpp').read_text()
        find = literal_function(net, 'find_match')
        decline = find[find.index('  const auto decline = [&] {'):find.index('\n  if (cancelled()) {', find.index('  const auto decline = [&] {'))]
        queue = find[find.index('  const bool queued = room_code.empty();'):find.index('\n\n  // An adopted lobby room')]
        stubs = r'''
#include "yougame_search.h"
#include "yougame_transport.h"
#include "yougame_blend.h"
#include "yougame_update.h"
#include <cstdio>
using json = nlohmann::json;
namespace search_policy = yougame::search_policy;
namespace transport = yougame::transport;
namespace update = yougame::update;
static search_policy::Generation g_search_generation;
static yougame::blend::Coordinator coord(0);
static bool slippi_wins, stale_epoch;
static int checks;
static std::vector<std::string> transcript;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::abort(); } } while (0)
namespace host { void log(const char*, ...) {} }
namespace friends { const char* kTeamWaiting = "team"; const char* kWaiting = "friend"; }
namespace lobby { const char* kWaiting = "friends lobby"; const char* kDoublesWaiting = "doubles"; }
struct { void refuse() {} } g_update_notice;
static uint64_t now_ms() { return 1; }
static void Sleep(int) { std::abort(); }
static void set_status(const std::string&) {}
struct Match { std::string error; };
static Match fail(const std::string& e, bool = false, bool = false) { return {e}; }
struct Socket {
  bool open_session(const std::string&, const std::string&, const std::string& room,
                    std::string*, int, const std::function<bool()>& stop) {
    if (stop()) return false;
    transcript.push_back(room.empty() ? "queue" : "join:" + room); return true;
  }
  void send(const json& m) { transcript.push_back(m.value("t", "")); }
  bool next(json& m, int) {
    m = {{"t","matched"},{"room","assigned-room"}};
    if (slippi_wins) CHECK(coord.matched(yougame::blend::Side::Slippi, 1));
    if (stale_epoch) g_search_generation.begin();
    return true;
  }
  bool closed() const { return false; }
  bool recoverable() const { return true; }
  int close_code() const { return 0; }
};
static void set_search_paused(const std::string&) {}
static Match run() {
  const auto epoch = g_search_generation.begin();
  const bool blend = true, friends_mode = false, team_code = false, lobby_route = false;
  const bool doubles_home = false, doubles_room = false;   // the Doubles home and its match room (G2)
  const bool arena = false;   // an arena game under YouGame (PR 86): not this search
  const void* parked = nullptr;   // a parked Friends lobby room (none here)
  const std::string url = "fake", direct_code;
  std::string pass = "synthetic";   // 122.31: a "matched" room ticket replaces the queue ticket
  uint64_t pass_at = now_ms();
  std::string room_code, error;
  struct Search { std::function<bool()> claim; std::function<void(int)> on_searching; std::string direct_code; } search;
  search.claim = [] { return coord.matched(yougame::blend::Side::YouGame,1); };
  const auto cancelled = [&] { return !g_search_generation.current(epoch) || !coord.live(yougame::blend::Side::YouGame); };
  const auto renew_ticket = [](uint64_t) { return false; };
  bool update_refused = false;
'''
        cases = r'''
  return {};
}
int main() {
  for (bool win : {false,true}) for (bool stale : {false,true}) {
    coord = yougame::blend::Coordinator(0); coord.searching(1,0);
    slippi_wins = win; stale_epoch = stale; transcript.clear();
    const auto result = run();
    CHECK(result.error == (win || stale ? "Cancelled" : ""));
    CHECK(transcript == (win && !stale ? std::vector<std::string>{"queue","join:assigned-room","leave"}
                                      : std::vector<std::string>{"queue"}));
  }
  std::printf("consumed queue assignment: 4 scenarios, %d checks passed\n",checks);
}
'''
        self.compile_fixture('decline', stubs + decline + queue + cases)

    def test_production_notice_uses_owned_game_text(self):
        source = (ROOT / 'sourceport/game/shim/mu_menu_label.c').read_text()
        header = str(ROOT / 'sourceport/game/shim/mu_menu_label.h')
        production = fixture.extract(source, 'mu_slippi_menu_description') + fixture.extract(source, 'mu_slippi_signin_notice')
        self.compile_fixture('notice', NOTICE_STUBS.replace('MU_LABEL_HEADER', header) + production + NOTICE_CASES)

    def test_status_window_requires_message_and_preserves_update_notice(self):
        ui = (ROOT / 'port/runtime/gx/pc_settings.cpp').read_text()
        start = ui.index('  if (yougame::active() || yougame::channel_connected() || slippi::online::online_status_visible()) {')
        area = ui[start:ui.index('  ImGuiStyle launcher_saved_style;', start)]
        self.assertNotIn('version_line()', area)
        self.assertNotIn('MELEE_PORT_VERSION', area)
        self.compile_fixture('status-window', r'''
#include "yougame_friends.h"
#include "yougame_blend.h"
#include "yougame_update.h"
#include "yougame_search.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
static bool app_active, online_visible, playing;
static std::string link_message, console_message;
static int begins, ends, positions, checks, styles, fonts, same_lines, reserves, notices;
static float g_overlay_game_aspect = 73.0f / 60.0f, pill_x, pill_y, pill_font;
static std::vector<std::string> lines;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::abort(); } } while (0)
namespace yougame {
bool active() { return app_active; }
bool channel_connected() { return false; }
std::string search_paused_line() { return {}; }
std::string status_line() { return link_message; }
std::string slippi_link_status(const std::string& status) { return status; }
namespace console { std::string waiting_status() { return console_message; } }
}
namespace slippi::online {
bool online_status_visible() { return online_visible; }
bool is_online_match() { return playing; }
}
namespace screen_label { float corner_hint_bottom() { return 0.0f; } void reserve_bottom(float) { ++reserves; } }
struct ImVec2 { float x, y; ImVec2(float a, float b):x(a),y(b){} };
enum { ImGuiCond_Always = 1, ImGuiWindowFlags_NoDecoration = 1,
       ImGuiWindowFlags_AlwaysAutoResize = 2, ImGuiWindowFlags_NoSavedSettings = 4,
       ImGuiWindowFlags_NoInputs = 8, ImGuiWindowFlags_NoFocusOnAppearing = 16, ImGuiStyleVar_WindowPadding = 1 };
namespace ImGui {
struct IO { ImVec2 DisplaySize{1280,720}; } io;
IO& GetIO() { return io; }
void SetNextWindowPos(ImVec2 at, int, ImVec2) { ++positions; pill_x = at.x; pill_y = at.y; }
struct Style { float FontSizeBase = 13.0f; } style;
Style& GetStyle() { return style; }
float GetFontSize() { return 26.0f; }
void PushStyleVar(int, ImVec2) { ++styles; } void PopStyleVar() { --styles; }
void PushFont(void*, float size) { ++fonts; pill_font = size; } void PopFont() { --fonts; }
void SameLine() { ++same_lines; }
void SetNextWindowBgAlpha(float) {}
void Begin(const char* id, void*, int) {
  CHECK(std::string(id)=="##yougame_friend_waiting"); CHECK(styles==1 && fonts==1);  // padding and font apply to this window
  ++begins;
}
void PushTextWrapPos(float) {} void PopTextWrapPos() {} float GetCursorPosX() { return 0; }
ImVec2 GetWindowPos() { return {0,0}; } ImVec2 GetWindowSize() { return {0,0}; }
void TextUnformatted(const char* text) { lines.emplace_back(text); }
void TextDisabled(const char* text, ...) { lines.emplace_back(text); }
void End() { ++ends; }
}
// The game's own notice pills (pc_settings.cpp), drawn only without YouGame: counted here.
static void draw_yougame_notices(const std::string&, uint64_t, const std::string&, uint64_t) { ++notices; }
static void draw() {
''' + area + r'''
}
int main() {
  using namespace yougame;
  struct Message { const char* text; bool visible; } messages[] = {
    {"",false}, {"offline",false}, {"connected",false},
    {friends::kWaiting,true}, {friends::kJoined,true}, {friends::kFailed,true},
    {friends::kVersion,true}, {update::kMessage,true},
    {blend::kSearchBoth,true}, {blend::kSearchYouGameOnly,true}
  };
  int scenarios = 0;
  for (bool app : {false,true}) for (bool online : {false,true}) for (bool match : {false,true})
    for (auto message : messages) for (const char* console : {"", "Connecting...", "Cancelled", "Couldn't connect"}) {
      app_active=app; online_visible=online; playing=match;
      link_message=message.text; console_message=console;
      begins=ends=positions=same_lines=reserves=notices=0; lines.clear(); draw();
      CHECK(styles==0); CHECK(fonts==0);
      CHECK(notices==int(!app && online));   // YouGame draws its own notices whenever it is there
      bool expected=(app || online) && !match && (*console || message.visible);
      CHECK(begins==int(expected)); CHECK(ends==begins); CHECK(positions==begins); CHECK(reserves==begins);
      if (expected) {
        bool cancel=*console || link_message==friends::kWaiting;
        CHECK(lines.size()==(cancel?2u:1u));
        CHECK(lines[0]==(*console?console:message.text));
        if (cancel) CHECK(lines[1]=="Z: cancel");
        CHECK(same_lines==int(cancel));  // one row
        // 1280x720 at 73:60: the picture fills the height; the row sits on its bottom edge at 0.026 of it.
        CHECK(pill_x==640.0f); CHECK(pill_y>714.0f && pill_y<718.0f);
        CHECK(pill_font>9.3f && pill_font<9.4f);
      } else CHECK(lines.empty());
      ++scenarios;
    }
  // Letterbox (window taller than the picture): the row sits on the picture's bottom edge, not the window's;
  // the font stops at the UI font. A tiny picture keeps the 8 px floor.
  app_active=true; online_visible=false; playing=false; link_message=friends::kWaiting; console_message="";
  ImGui::io.DisplaySize = ImVec2(1280,1600); draw();
  const float picture = 1280.0f/(73.0f/60.0f);
  CHECK(pill_y>(1600+picture)*0.5f-7 && pill_y<(1600+picture)*0.5f-6); CHECK(pill_font==13.0f);
  ImGui::io.DisplaySize = ImVec2(200,150); draw();
  CHECK(pill_y==150.0f-2.0f); CHECK(pill_font==4.0f);
  CHECK(styles==0); CHECK(fonts==0);
  std::printf("status window: %d scenarios, %d checks passed\n",scenarios,checks);
}
''')

    def test_direct_has_no_picker_wiring_or_empty_code_fallback(self):
        for file in ['port/runtime/hle/yougame_friends.h', 'port/runtime/hle/slippi_online.cpp',
                     'port/runtime/hle/native_practice.cpp', 'port/runtime/gx/pc_settings.cpp']:
            text = (ROOT / file).read_text()
            self.assertNotIn('code_screen', text)
            self.assertNotIn('code_empty', text)
        self.assertNotIn('empty_code(', (ROOT / 'port/runtime/hle/yougame_link.cpp').read_text())

if __name__ == '__main__':
    unittest.main()
