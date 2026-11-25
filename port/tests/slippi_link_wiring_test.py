"""Check the shipped mapped-button paths and platform-neutral link wiring offline."""
from pathlib import Path
import unittest
import online_menu_test as menu
from yougame_unranked_wiring_test import function
ROOT = Path(__file__).resolve().parents[2]
def source(path):
    return (ROOT / path).read_text(encoding='utf-8-sig')

class SlippiLinkWiringTest(unittest.TestCase):
    def test_boot_gate_request_channel_and_generation(self):
        link = source('port/runtime/hle/yougame_link.cpp')
        self.assertIn('presentation.enabled = take("YOUGAME_SLIPPI_LINKABLE") == "1";', function(link, 'start'))
        self.assertEqual(link.count('take("YOUGAME_SLIPPI_LINKABLE")'), 1)
        self.assertNotIn('slippi_off', link)
        self.assertLess(link.index('slippi_linking::presentation.linked = linked;'), link.index('if (line == sent) return;'))
        self.assertIn('return bridge(request, timeout, cancelled);', function(link, 'request_slippi_link'))
        self.assertIn('if (body.value("type", "") == "slippiLink") return j;', function(link, 'bridge'))
        online = source('port/runtime/hle/slippi_online.cpp')
        tick = function(online, 'slippi_link_tick')
        self.assertIn('g_user->AttemptLogin(true)', tick)
        self.assertLess(tick.index('cleanup_connection('), tick.index('start_find_match(payload)'))
        self.assertIn('g_local_selections = selections;', tick)
        self.assertIn('yougame: new search (Slippi link)', tick)
        self.assertIn('slippi::online::slippi_link_tick(major, minor, online_mode());', source('port/runtime/hle/native_practice.cpp'))
        net = source('port/runtime/hle/slippi_net.cpp')
        self.assertIn('!yougame::slippi_linking::presentation.later', function(net, 'startMatchmaking'))
        self.assertIn('fail("Sign in to Slippi to play Direct, Teams and Party");', function(net, 'startMatchmaking'))
        self.assertNotIn('kSlippiLockedError', net)
        self.assertIn('yougame::begin_search()', net)
        command = online[online.index('case CMD_YOUGAME_SLIPPI_LINK:'):online.index('case CMD_YOUGAME_FRIENDS_MENU:')]
        for guard in ('yougame::active()', 'host::gameplay::vanilla_only()', '!g_in_online_match', 'friend_room.empty()', 'Screen::None'):
            self.assertIn(guard, command)
        root = source('CMakeLists.txt')
        self.assertLess(root.index('add_executable(port_slippi_link_test'), root.index('  return()'))
        self.assertLess(root.index('add_test(NAME port_slippi_link_wiring'), root.index('  return()'))

    def test_only_current_answer_installs_override_before_login(self):
        header = source('port/runtime/hle/yougame_slippi_link.h')
        tick = function(header, 'tick')
        self.assertLess(tick.index('if (cancelled_) return;'), tick.index('set_app_sign_in(reply.file)'))
        self.assertLess(tick.index('set_app_sign_in(reply.file)'), tick.index('login(); restart();'))
        self.assertNotIn('set_app_sign_in', function(header, 'press'))
        self.assertIn('result == Answer::Later || result == Answer::NoFile', tick)
        account = source('port/runtime/hle/slippi_account.h')
        setter = function(account, 'set_app_sign_in')
        self.assertIn('std::lock_guard<std::mutex>', setter)
        self.assertIn('if (detail::app_sign_in_override.empty())', setter)
        lookup = function(account, 'app_sign_in')
        self.assertLess(lookup.index('app_sign_in_override'), lookup.index('YOUGAME_SLIPPI_USER_JSON'))
        resolve = function(account, 'resolve')
        self.assertLess(resolve.index('return read(app);'), resolve.index('for (const auto& root'))

    def test_agent_copy_logs_the_grant_without_reading_it(self):
        net = source('port/runtime/hle/slippi_net.cpp')
        start = net.index('static void note_agent_grant() {')
        branch = net[start:net.index('\n}\n', start)]
        self.assertIn('symlink_status(grant, ec)', branch)
        self.assertIn('"slippi: app sign-in file %s (agent copy, not used)"', branch)
        login = net[net.index('bool User::AttemptLogin('):]
        self.assertLess(login.index('if (yougame::active() && agent_lock::blocked(agent_lock::Operation::Login)) note_agent_grant();'),
                        login.index('if (agent_lock::blocked(agent_lock::Operation::Login)) return false;'))
        for reading in ('account::read(', 'resolve(', 'ifstream'):
            self.assertNotIn(reading, branch)

    def test_mac_and_linux_mainline_sign_in_folders_are_searched(self):
        roots = function(source('port/runtime/hle/slippi_account.h'), 'launcher_roots')
        self.assertIn('/ ".config" / "slippi-dolphin"', roots)
        apple = roots[roots.index('#ifdef __APPLE__'):]
        self.assertIn('/ "Library" / "Application Support" / "com.project-slippi.dolphin"', apple[:apple.index('#endif')])
        self.assertLess(roots.index('if (host::test_env::agent_copy()) return roots;'), roots.index('#ifdef __APPLE__'))

    def test_search_and_refusal_buttons_never_start_setup_or_practice(self):
        css = source('sourceport/game/shim/mu_slippi_css.c')
        inputs = menu.fixture.extract(css, 'mu_slippi_css_inputs')
        # Compile all real sounds, Y, idle and searching/error branches. Connected play is separate.
        inputs = inputs[:inputs.index('    case MU_SLP_MM_CONNECTION_SUCCESS:')] + '    default: return 0;\n    }\n}\n'
        stubs = r'''
#include <cstdio>
#include <cstdlib>
using u8=unsigned char; using u32=unsigned; struct MuSlippiMenuState { int mode,is_winner,chose_stage; } state;
MuSlippiMenuState* mu_slippi_state() { return &state; }
enum { MU_SLP_MM_IDLE, MU_SLP_MM_INITIALIZING, MU_SLP_MM_MATCHMAKING, MU_SLP_MM_OPPONENT_CONNECTING,
MU_SLP_MM_CONNECTION_SUCCESS, MU_SLP_MM_ERROR, MU_SLP_MODE_RANKED=0, MU_SLP_MODE_UNRANKED=1,
MU_SLP_MODE_DIRECT=2,MU_SLP_MODE_TEAMS=3,MU_SLP_MODE_PARTY=4,MU_SLP_WINNER_NULL=-1,
HSD_PAD_Z=0x10,HSD_PAD_A=0x100,HSD_PAD_B=0x200,HSD_PAD_X=0x400,HSD_PAD_Y=0x800,HSD_PAD_START=0x1000,SB_RAND=-2,
MU_YG_PRACTICE_START=1,MU_YG_PRACTICE_STOP=2 };
struct MuMatchState { u8 connection_state,local_ready; };
struct { MuMatchState msrb; u8 prev_lock_in,prev_connected; int chat_window_opened; u8 doubles_searched; } dt;
int doubles_teams() { return 0; } void mu_slippi_sss_find_match() { std::abort(); }   // the 4-peer Doubles CSS: off here
int starts,searches,setups,requests,cleanups; unsigned char chosen=1;unsigned char* char_chosen=&chosen;
void lbAudioAx_80024030(int) {} int mu_online_p2p_menus() { return 0; }
int mu_slippi_practice_available() { return 0; } void mu_slippi_sss_report_pick(int) {}
int mu_ranked_practice_control(int) { ++starts; return 1; }
int gm_801A4BB8() { return 1; }
void lock_in_and_search(int) { ++searches; } void load_code_entry() { ++setups; }
void mu_slippi_cleanup_connections() { ++cleanups; }
int mu_slippi_yougame_pick() { return 0; }   // the app: never a launcher YouGame row (upstream 0.9.97)
void mu_slippi_yougame_restart_older() { std::abort(); }
// The Arena test hook in the CSS (MELEE_TEST_ARENA_PRESS_START): never set here.
int test_skip_code_entry; const char* mu_test_getenv(const char*) { return nullptr; } void OSReport(const char*, ...) {}
// Tap jump off and tilt stick: X / Y answer only on a connected select, never while searching or in an error.
enum { CTL_NONE, CTL_ASKED, CTL_ALLOWED_OWN }; u8 ctl_line[8]; void controls_send(u8, u8) { std::abort(); }
int mu_slippi_cmd(unsigned cmd,const void* p,unsigned,void*,unsigned,unsigned*) {
 if(cmd!=0xE7 || *(const u8*)p!=1)std::abort();++requests;return 0;
}
'''
        cases = r'''
int main() {
 int checks=0;
 for(int mode=1;mode<=4;++mode)for(int conn : {MU_SLP_MM_MATCHMAKING,MU_SLP_MM_ERROR}) {
  state.mode=mode;dt.msrb.connection_state=conn;
  for(unsigned button : {unsigned(HSD_PAD_Z),unsigned(HSD_PAD_X),unsigned(HSD_PAD_Y),unsigned(HSD_PAD_Y|HSD_PAD_Z),unsigned(HSD_PAD_A),unsigned(HSD_PAD_B)}) {
   starts=searches=setups=requests=cleanups=0;
   if(mu_slippi_css_inputs(button)!=0 || starts || searches || setups)std::abort();
   if(requests!=(button==HSD_PAD_Y) || cleanups!=bool(button&(conn==MU_SLP_MM_ERROR ? HSD_PAD_Z|HSD_PAD_A|HSD_PAD_B : HSD_PAD_Z)))std::abort();++checks;
  }
 }
 std::printf("search/refusal Z/X/Y/A/B (an error clears on A, B or Z): %d combinations, no search/setup/practice\n",checks);
}
'''
        menu.OnlineMenuTest().compile_fixture('slippi-link-input', '#include <initializer_list>\n' + stubs + inputs + cases, '-DYOUGAME_RANKED_BRANCH=1')

    def test_ascii_text_and_native_reply_bounds(self):
        header = source('port/runtime/hle/yougame_slippi_link.h')
        self.assertTrue(header.isascii())
        bridge = source('port/runtime/hle/native_slippi_bridge.h')
        self.assertIn('CMD_YOUGAME_SLIPPI_LINK = 0xE7', bridge)
        self.assertIn('case CMD_YOUGAME_FRIENDS_MENU: case CMD_YOUGAME_SLIPPI_LINK: expected = 1;', bridge)
        css = source('sourceport/game/shim/mu_slippi_css.c')
        self.assertIn('link_line[sizeof link_line - 1] = 0;', css)
        self.assertIn('if (link_line[0]) set_text(idx, link_line);', css)
        self.assertIn('yougame::slippi_link_status(status).c_str()', source('port/runtime/gx/pc_settings.cpp'))
        flow = function(header, 'tick')
        self.assertIn('scene != scene_ || match', flow)
        for transient in ('focus', 'selected', 'cooldown', 'active_window'):
            self.assertNotIn(transient, flow)

if __name__ == '__main__':
    unittest.main()
