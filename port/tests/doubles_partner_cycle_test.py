"""The Doubles home's partner tab (build 95, 11-10, report e22413bd).

Seat 0's tab on the partner door cycles Find -> Invite -> P2 -> CPU -> Find, starting on Find each home
(sourceport/game/include/mu_friends4_css.inc mu_friends4_partner_cycle; the choice is kept on this machine by
shim/mu_slippi_menu.c). START on Invite sends MU_LOBBY_OP_INVITE, which the runtime turns into a Teams invite for
this home (mode "teams", teamsLobby 1; without them the site sends a Friends invite and the join takes the
seat-count path, the 11-10 black screen). B while the invite is out takes it back. P2 (11-11 report: "Player 2
should create the player 2 portrait and create a control"): the tab itself sends LOCAL_SEAT {1}, so the door is
Player 2's at once, played by the first other connected controller (a still pad until one is plugged in); that
controller's B gives it back and the tab goes to Find. CPU: the SEAT request Melee's own switch always sent.
Nothing new goes over the wire. The tab's code, the pad router and the invite payload are compiled and run here
against stubs; the rest is pinned by source.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
INC = 'sourceport/game/include/mu_friends4_css.inc'
MENU = 'sourceport/game/shim/mu_slippi_menu.c'
PATCH = 'sourceport/patches/melee-native.patch'
RUNTIME = 'port/runtime/hle/slippi_online.cpp'


def read(path):
    return (ROOT / path).read_text(encoding="utf-8-sig", errors="replace")


def body(source, name):
    at = source.index(name)
    at = source.index('{', at)
    depth, end = 1, at + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[at:end]


def function(source, decl):
    return decl + body(source, decl) + '\n'


def compile_and_run(test, source, prefix, includes):
    if shutil.which('c++') is None:
        test.skipTest('no C++ compiler')
    with tempfile.TemporaryDirectory(prefix=prefix) as d:
        cpp, exe = Path(d) / 'harness.cpp', Path(d) / 'harness'
        cpp.write_text(source)
        result = subprocess.run(['c++', '-std=c++17', '-Wall', '-Wno-unused-function'] +
                                ['-I' + str(ROOT / i) for i in includes] + [str(cpp), '-o', str(exe)],
                                capture_output=True, text=True)
        test.assertEqual(result.returncode, 0, result.stderr)
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        test.assertEqual(run.returncode, 0, run.stderr + run.stdout)


class TabWiring(unittest.TestCase):
    def setUp(self):
        self.inc = read(INC)

    def test_the_tab_press_goes_to_the_cycle_before_melee_flips_the_door(self):
        patch = read(PATCH)
        self.assertIn('+static int mu_friends4_partner_cycle(u8 door);', patch)
        hook = patch.index('+                                            if (mu_friends4_partner_cycle((u8) door)) break;')
        # Right after Melee's tab hit (cursor->x10 = -2.2f), before its p_kind flip (the next hunk's case 3/2/4).
        self.assertGreater(hook - patch.rindex('cursor->x10 = -2.2f;', 0, hook), 0)
        self.assertLess(hook - patch.rindex('cursor->x10 = -2.2f;', 0, hook), 300)
        self.assertLess(hook, patch.index('.p_kind = 3;', hook))

    def test_find_is_the_default_every_home_and_never_on_the_wire(self):
        menu = read(MENU)
        self.assertIn('int partner_want;', menu)
        # lobby_open clears the struct (partner_want = FIND) each time the major opens; matches keep it.
        self.assertIn('memset(&lobby, 0, sizeof lobby);', body(menu, 'static void lobby_open(void)'))
        self.assertEqual(read('sourceport/game/include/mu_native.h').count('MU_LOBBY_WANT_FIND = 0'), 1)
        wire = read('port/runtime/abi/mu_lobby_wire.h')
        self.assertNotIn('WANT', wire)
        self.assertIn('int mu_lobby_partner_want(void) { return 0; }', read('sourceport/game/shim/mu_noslippi.c'))
        self.assertIn('inline constexpr int kOnlineProtocol = 2;', read('port/runtime/hle/yougame_compatibility.h'))

    def test_only_the_tab_invites_in_doubles(self):
        invite = body(self.inc, 'static void mu_f4_invite(int mine)')
        self.assertLess(invite.index('if (mu_f4_doubles()) return;'), invite.index('MU_LOBBY_OP_INVITE'))
        start = body(self.inc, 'static u32 mu_friends4_css_start(u32 trigger, int ready)')
        dbl = body(start, 'if (mu_f4_doubles())')
        self.assertLess(dbl.index('if (mu_f4_partner_start()) return 0;'), dbl.index('MU_LOBBY_HINT_START_SEARCH'))

    def test_the_tab_turns_with_player_two_and_never_on_a_friend(self):
        bounds = body(self.inc, 'static void mu_f4_bounds(int mine)')
        self.assertIn('const int toggle = host && kind != MU_LOBBY_KIND_HUMAN && (doubles || kind != MU_LOBBY_KIND_LOCAL);',
                      bounds)
        self.assertIn('const int host = mu_f4_host() && i != mine && i < doors && !locked;', bounds)

    def test_the_frame_follows_the_seat_before_the_host_sends(self):
        frame = body(self.inc, 'static void mu_friends4_css_frame(void)')
        self.assertLess(frame.index('mu_f4_doors(css, mine);'), frame.index('mu_f4_partner_sync(css, mine);'))
        self.assertLess(frame.index('mu_f4_partner_sync(css, mine);'), frame.index('mu_f4_host_doors(css, mine);'))


class TabHarness(unittest.TestCase):
    """mu_friends4_partner_cycle, mu_f4_p2_claim, mu_f4_partner_sync, mu_f4_partner_start and mu_f4_host_doors,
    compiled from the select's own code, against a stub room that seats what the requests ask for."""

    def test_cycle_start_and_the_room(self):
        inc = read(INC)
        names = inc[inc.index('static const char* const mu_f4_want_names'):]
        names = names[:names.index(';') + 1] + '\n'
        code = names
        for decl in ('static void mu_f4_partner_door(CSSData* css, u8 kind)',
                     'static void mu_f4_want(int want, const char* why)',
                     'static int mu_f4_want_next(int want)',
                     'static int mu_f4_p2_claim(const char* why)',
                     'static int mu_friends4_partner_cycle(u8 door)',
                     'static void mu_f4_partner_sync(CSSData* css, int mine)',
                     'static int mu_f4_partner_start(void)',
                     'static void mu_f4_host_doors(CSSData* css, int mine)'):
            code += function(inc, decl)
        source = r'''#include <cassert>
#include <cstring>
#include <vector>
#include "mu_lobby_wire.h"
using u8 = unsigned char; using u32 = unsigned; using s8 = signed char;
enum { Gm_PKind_Human = 0, Gm_PKind_Cpu = 1, Gm_PKind_NA = 3 };
enum { MU_LOBBY_WANT_FIND = 0, MU_LOBBY_WANT_INVITE = 1, MU_LOBBY_WANT_P2 = 2, MU_LOBBY_WANT_CPU = 3,
       MU_LOBBY_WANT_PROAI = 4, MU_LOBBY_WANTS = 5 };
#define MU_F4_HOLD 60
struct CSSDoor { u8 p_kind = Gm_PKind_NA, p_kind_prev = Gm_PKind_NA, selected_since_load = 0, costume = 0, team = 0; };
struct { CSSDoor doors[4]; } mnCharSel_803F0DFC;
struct PlayerInitData { u8 slot_type = Gm_PKind_NA, nametag = 0; s8 ckind = -1; u8 cpu_level = 3; };
struct CSSData { struct { struct { PlayerInitData players[4]; } start; } vs; };
CSSData css_data;
CSSData* mnCharSel_804D6CB0 = &css_data;
struct TagData { int use_tag = 1; } tag_data[4];
struct CSSTag { TagData* data; } mnCharSel_803F0E8C[4] = {{&tag_data[0]}, {&tag_data[1]}, {&tag_data[2]}, {&tag_data[3]}};
struct CSSCharModel { int x5 = 0; } coin1;
CSSCharModel* mnCharSel_804A0BD0[4] = {nullptr, &coin1, nullptr, nullptr};
int random_picks, redraws, buzzes, notes, last_note = -1, want_v;
void mnCharSel_8025FB50(u8 door, int) { assert(door == 1); ++random_picks; }
void mnCharSel_8025DB34(u8) { ++redraws; }
struct {
    MuLobbyView view{}; int view_ok = 1; int hold[4]{}; unsigned sig[4]{}; unsigned char kind_seen[4]{};
    unsigned char seat_sent[4][2]; unsigned char cpu_sent[4][3];
    unsigned char partner_kind = 0xFF; int partner_cpu_wait = 0, partner_p2_wait = 0;
} mu_f4;
int mu_lobby_partner_want() { return want_v; }
void mu_lobby_set_partner_want(int w) { want_v = w; }
int pro_v, proai_lifts;   // the partner tab's Pro AI (mu_lobby_partner_pro_*): which pro, in order
int mu_lobby_partner_pro() { return pro_v; }
void mu_lobby_set_partner_pro(int p) { pro_v = p; }
const char* mu_lobby_partner_pro_name() { return "Pro AI"; }
void mu_f4_proai_lift(CSSData*) { ++proai_lifts; }
#define MU_LOBBY_PROS 4
int mu_lobby_css() { return 1; }
int mu_f4_doubles() { return mu_f4.view_ok && (mu_f4.view.mode & MU_LOBBY_M_DOUBLES); }
int mu_f4_searching() { return mu_f4_doubles() && (mu_f4.view.mode & MU_LOBBY_M_SEARCHING); }
int mu_f4_host() { return (mu_f4.view.flags & MU_LOBBY_F_HOST) && !(mu_f4.view.flags & MU_LOBBY_F_STARTED); }
int mu_f4_mine() { return mu_f4.view.my_seat; }
int mu_f4_doors_open() { return 2; }
void mu_f4_note(unsigned n) { ++notes; last_note = (int) n; }
void lbAudioAx_80024030(int) { ++buzzes; }
void sfxMove() {} void sfxForward() {}
void OSReport(const char*, ...) {}
unsigned char mu_f4_u8(unsigned v, unsigned max) { return (unsigned char) (v > max ? max : v); }
unsigned char mu_f4_door_color(int, unsigned, unsigned c) { return (unsigned char) c; }
std::vector<std::vector<unsigned char>> sent;
int mu_f4_send(const unsigned char* p, unsigned n) { sent.emplace_back(p, p + n); return 1; }
CODE
CSSDoor& door1 = mnCharSel_803F0DFC.doors[1];
// The room seats what was asked; the view comes back with it.
void room() {
    for (const auto& r : sent) {
        if (r[0] == MU_LOBBY_OP_SEAT && r[1] == 1) { mu_f4.view.seats[1].kind = r[2] ? MU_LOBBY_KIND_CPU : MU_LOBBY_KIND_OPEN; mu_f4.view.seats[1].level = r[2]; }
        if (r[0] == MU_LOBBY_OP_LOCAL_SEAT && r[1] == 0) { mu_f4.view.seats[1].kind = MU_LOBBY_KIND_OPEN; mu_f4.view.mode &= ~MU_LOBBY_M_LOCAL_OWNER; }
        if (r[0] == MU_LOBBY_OP_LOCAL_SEAT && r[1] == 1) { mu_f4.view.seats[1].kind = MU_LOBBY_KIND_LOCAL; mu_f4.view.mode |= MU_LOBBY_M_LOCAL_OWNER; }
    }
}
// One select frame as mu_friends4_css_frame runs it: the doors from the view (mu_f4_doors, which waits out a
// hold), the tab follows the seat, then the host's door goes to the room.
void frame() {
    if (mu_f4.hold[1] > 0) --mu_f4.hold[1];
    else if (!mu_lobby_local_owned(&mu_f4.view)) {
        const unsigned k = mu_f4.view.seats[1].kind;
        door1.p_kind = door1.p_kind_prev = k == MU_LOBBY_KIND_CPU ? Gm_PKind_Cpu : k == MU_LOBBY_KIND_OPEN ? Gm_PKind_NA : Gm_PKind_Human;
        mu_f4.seat_sent[1][0] = mu_f4.cpu_sent[1][0] = 0xFF;
    }
    mu_f4_partner_sync(mnCharSel_804D6CB0, mu_f4.view.my_seat);
    mu_f4_host_doors(mnCharSel_804D6CB0, mu_f4.view.my_seat);
    mu_f4.kind_seen[1] = door1.p_kind;
}
void home() {
    std::memset(&mu_f4.view, 0, sizeof mu_f4.view);
    mu_f4.view.flags = MU_LOBBY_F_ARMED | MU_LOBBY_F_KNOWN | MU_LOBBY_F_HOST;
    mu_f4.view.mode = MU_LOBBY_M_DOUBLES; mu_f4.view.my_seat = 0;
    mu_f4.view.seats[0].kind = MU_LOBBY_KIND_HUMAN;
    for (int i = 0; i < 4; ++i) { mu_f4.seat_sent[i][0] = mu_f4.cpu_sent[i][0] = 0xFF; mu_f4.hold[i] = 0; }
    door1 = CSSDoor{}; mu_f4.partner_kind = 0xFF; mu_f4.partner_cpu_wait = mu_f4.partner_p2_wait = 0;
    want_v = MU_LOBBY_WANT_FIND; sent.clear();
}
int main() {
    home();
    frame(); assert(want_v == MU_LOBBY_WANT_FIND && sent.empty());
    // Find -> Invite: nothing sent, the door stays empty.
    assert(mu_friends4_partner_cycle(1) == 1 && want_v == MU_LOBBY_WANT_INVITE && sent.empty() && door1.p_kind == Gm_PKind_NA);
    // START on Invite: one INVITE op (the runtime makes it a Teams invite).
    assert(mu_f4_partner_start() == 1 && sent.size() == 1 && sent[0] == std::vector<unsigned char>{MU_LOBBY_OP_INVITE});
    mu_f4.view.flags |= MU_LOBBY_F_INVITING;
    assert(mu_f4_partner_start() == 1 && sent.size() == 1 && buzzes == 1);   // already out: buzz
    assert(mu_friends4_partner_cycle(1) == 1 && want_v == MU_LOBBY_WANT_INVITE && buzzes == 2);   // locked while out
    mu_f4.view.flags &= ~MU_LOBBY_F_INVITING;   // B took it back: the door is still on Invite
    frame(); assert(want_v == MU_LOBBY_WANT_INVITE);
    // Invite -> Pro AI: Player 2's door is claimed for the Pro AI (LOCAL_SEAT {1}), the first pro.
    sent.clear();
    const std::vector<unsigned char> claim{MU_LOBBY_OP_LOCAL_SEAT, 1};
    assert(mu_friends4_partner_cycle(1) == 1 && want_v == MU_LOBBY_WANT_PROAI && pro_v == 0 && sent.size() == 1 && sent[0] == claim);
    { const int bp = buzzes; assert(mu_friends4_partner_cycle(1) == 1 && want_v == MU_LOBBY_WANT_PROAI && pro_v == 0 && buzzes == bp + 1); }   // sitting down
    // The room never seats it: once the wait runs out, each press is the next pro, claiming the door again.
    for (int p = 1; p < MU_LOBBY_PROS; ++p) {
        for (int i = 0; i < MU_F4_HOLD; ++i) frame();
        const size_t before = sent.size();
        assert(mu_friends4_partner_cycle(1) == 1 && want_v == MU_LOBBY_WANT_PROAI && pro_v == p);
        assert(sent.size() == before + 1 && sent.back() == claim);
    }
    for (int i = 0; i < MU_F4_HOLD; ++i) frame();
    // The last pro -> P2: Player 2 takes the door at once (LOCAL_SEAT {1}), as if controller 2 were plugged in.
    sent.clear();
    assert(mu_friends4_partner_cycle(1) == 1 && want_v == MU_LOBBY_WANT_P2 && sent.size() == 1 && sent[0] == claim);
    assert(mu_f4.partner_p2_wait == MU_F4_HOLD);
    int b0 = buzzes;
    assert(mu_friends4_partner_cycle(1) == 1 && want_v == MU_LOBBY_WANT_P2 && buzzes == b0 + 1);   // sitting down
    assert(mu_f4_partner_start() == 1 && sent.size() == 1 && buzzes == b0 + 2);   // already claimed: buzz
    // The room never seats it (a refusal): once the wait runs out, START on P2 claims the door again.
    for (int i = 0; i < MU_F4_HOLD; ++i) frame();
    assert(want_v == MU_LOBBY_WANT_P2 && mu_f4.partner_p2_wait == 0);
    assert(mu_f4_partner_start() == 1 && sent.size() == 2 && sent[1] == claim && buzzes == b0 + 2);
    for (int i = 0; i < MU_F4_HOLD; ++i) frame();
    sent.clear();
    // P2 -> CPU on the empty seat: the door is a CPU at once (Melee's random fighter), SEAT {1, level} goes out.
    assert(mu_friends4_partner_cycle(1) == 1 && want_v == MU_LOBBY_WANT_CPU && sent.empty());
    assert(door1.p_kind == Gm_PKind_Cpu && css_data.vs.start.players[1].slot_type == Gm_PKind_Cpu);
    assert(random_picks == 1 && css_data.vs.start.players[1].nametag == 0x78 && tag_data[1].use_tag == 0);
    b0 = buzzes;
    assert(mu_friends4_partner_cycle(1) == 1 && want_v == MU_LOBBY_WANT_CPU && buzzes == b0 + 1);   // still sitting down
    frame();
    assert(sent.size() == 1 && sent[0] == (std::vector<unsigned char>{MU_LOBBY_OP_SEAT, 1, 3}));
    room(); frame(); assert(want_v == MU_LOBBY_WANT_CPU);
    assert(mu_f4_partner_start() == 0);   // a CPU is seated: START searches for opponents
    // CPU -> Find: the door empties, SEAT {1, 0}.
    sent.clear(); for (int i = 0; i < 70; ++i) frame();
    assert(mu_friends4_partner_cycle(1) == 1 && want_v == MU_LOBBY_WANT_FIND && door1.p_kind == Gm_PKind_NA);
    frame();
    assert(sent.size() == 1 && sent[0] == (std::vector<unsigned char>{MU_LOBBY_OP_SEAT, 1, 0}));
    room(); for (int i = 0; i < 70; ++i) frame();
    assert(want_v == MU_LOBBY_WANT_FIND && mu_f4.view.seats[1].kind == MU_LOBBY_KIND_OPEN);
    assert(mu_f4_partner_start() == 0);   // Find: the search

    // The tab to P2 and the room seats Player 2: the door is a person's, the wait ends, the tab turns again.
    home(); frame(); want_v = MU_LOBBY_WANT_PROAI; pro_v = MU_LOBBY_PROS - 1;   // the last pro: the next press is P2
    assert(mu_friends4_partner_cycle(1) == 1 && want_v == MU_LOBBY_WANT_P2);
    assert(sent.size() == 1 && sent[0] == claim);
    room(); frame();
    assert(mu_f4.view.seats[1].kind == MU_LOBBY_KIND_LOCAL && mu_lobby_local_owned(&mu_f4.view));
    assert(want_v == MU_LOBBY_WANT_P2 && mu_f4.partner_p2_wait == 0);
    assert(mu_f4_partner_start() == 0);   // Player 2 is seated: START searches for opponents

    // Player 2 in the seat, then the tab to CPU: Player 2 leaves first (LOCAL_SEAT {0}), then the CPU sits.
    home(); want_v = MU_LOBBY_WANT_P2;
    mu_f4.view.seats[1].kind = MU_LOBBY_KIND_LOCAL; mu_f4.view.mode |= MU_LOBBY_M_LOCAL_OWNER;
    door1.p_kind = Gm_PKind_Human; frame();
    assert(want_v == MU_LOBBY_WANT_P2 && sent.empty());
    assert(mu_friends4_partner_cycle(1) == 1 && want_v == MU_LOBBY_WANT_CPU);
    assert(sent.size() == 1 && sent[0] == (std::vector<unsigned char>{MU_LOBBY_OP_LOCAL_SEAT, 0}));
    const int b = buzzes;
    assert(mu_friends4_partner_cycle(1) == 1 && buzzes == b + 1 && want_v == MU_LOBBY_WANT_CPU);   // wait for the seat
    frame(); assert(want_v == MU_LOBBY_WANT_CPU && door1.p_kind == Gm_PKind_Human);
    room(); frame();
    assert(door1.p_kind == Gm_PKind_Cpu && sent.back() == (std::vector<unsigned char>{MU_LOBBY_OP_SEAT, 1, 3}));

    // The tab follows the room: a seated CPU, Player 2 or a friend who accepted the invite.
    home(); frame();
    mu_f4.view.seats[1].kind = MU_LOBBY_KIND_LOCAL; frame(); assert(want_v == MU_LOBBY_WANT_P2);
    mu_f4.view.seats[1].kind = MU_LOBBY_KIND_HUMAN; frame(); assert(want_v == MU_LOBBY_WANT_FIND);
    const int b2 = buzzes;
    assert(mu_friends4_partner_cycle(1) == 1 && buzzes == b2 + 1 && want_v == MU_LOBBY_WANT_FIND);   // a friend: locked
    mu_f4.view.seats[1].kind = MU_LOBBY_KIND_CPU; frame(); assert(want_v == MU_LOBBY_WANT_CPU);
    // A CPU the room never seated: Find again once the door's hold runs out.
    home(); frame(); want_v = MU_LOBBY_WANT_CPU; mu_f4.hold[1] = 2;
    frame(); assert(want_v == MU_LOBBY_WANT_CPU);
    frame(); frame(); assert(want_v == MU_LOBBY_WANT_FIND);

    // Locks: searching, a match being set up, the guest, door 1 only; outside Doubles Melee's switch.
    home(); mu_f4.view.mode |= MU_LOBBY_M_SEARCHING; notes = 0;
    assert(mu_friends4_partner_cycle(1) == 1 && want_v == MU_LOBBY_WANT_FIND && last_note == MU_LOBBY_HINT_SEARCHING);
    home(); mu_f4.view.flags |= MU_LOBBY_F_STARTED;
    assert(mu_friends4_partner_cycle(1) == 1 && want_v == MU_LOBBY_WANT_FIND && last_note == MU_LOBBY_HINT_IN_MATCH);
    home(); mu_f4.view.flags &= ~MU_LOBBY_F_HOST; mu_f4.view.my_seat = 1;
    assert(mu_friends4_partner_cycle(0) == 1 && want_v == MU_LOBBY_WANT_FIND && mu_f4_partner_start() == 0);
    frame(); assert(want_v == MU_LOBBY_WANT_FIND && sent.empty());
    home(); assert(mu_friends4_partner_cycle(0) == 1 && want_v == MU_LOBBY_WANT_FIND);
    home(); mu_f4.view.mode = 0;
    assert(mu_friends4_partner_cycle(1) == 0);   // the Friends lobby: Melee's own HMN/CPU switch
    return 0;
}
'''.replace('CODE', code)
        compile_and_run(self, source, 'doubles-partner-tab-', ['port/runtime/abi'])


class PadsAndInvite(unittest.TestCase):
    def test_controller_two_and_b_on_an_invite(self):
        router = body(read(MENU), 'void mu_lobby_pads(PADStatus* status)')
        source = r'''#include <cassert>
#include <cstring>
#include <cstdint>
#include <initializer_list>
#include "mu_lobby_wire.h"
using u16 = uint16_t;
struct PADStatus { u16 button = 0; int stickX = 0; int err = 0; };
constexpr int PAD_ERR_NO_CONTROLLER = -1, PAD_BUTTON_START = 0x1000, PAD_BUTTON_B = 0x200, PAD_BUTTON_A = 0x100;
enum { MU_LOBBY_WANT_FIND = 0, MU_LOBBY_WANT_INVITE = 1, MU_LOBBY_WANT_P2 = 2, MU_LOBBY_WANT_CPU = 3,
       MU_LOBBY_WANT_PROAI = 4 };
struct { int view_ok = 1, partner_source = -1, partner_b_held = 0, partner_want = 0, curtain = 0, cancel_held = 0, cancel_mask = 0; MuLobbyView view{}; PADStatus extra[2]{}; } lobby;
int requests = 0, op = -1, arg = -1;
int mu_lobby_css() { return 1; }
int gm_801677F0() { return 0; }
int mu_lobby_send(const unsigned char* p, unsigned n) { ++requests; op = p[0]; arg = n > 1 ? p[1] : -1; return 1; }
void OSReport(const char*, ...) {}
void mu_lobby_pads(PADStatus* status) ROUTER
PADStatus pads[4];
int plugged = 2;   // controllers 1..plugged are connected
void sample(int one, int two) {
    for (int i = 0; i < 4; ++i) { pads[i] = {}; pads[i].err = i < plugged ? 0 : -1; }
    pads[0].button = (u16) one; pads[1].button = (u16) two;
    mu_lobby_pads(pads);
}
int main() {
    auto& v = lobby.view;
    v.flags = MU_LOBBY_F_ARMED | MU_LOBBY_F_KNOWN | MU_LOBBY_F_HOST; v.mode = MU_LOBBY_M_DOUBLES; v.my_seat = 0;
    v.seat_count = MU_LOBBY_SEATS_V1;
    v.seats[0].kind = MU_LOBBY_KIND_HUMAN;
    // Controller 2's START never claims anything (the tab does): the seat stays open, its door gets no pad.
    for (int want : {MU_LOBBY_WANT_FIND, MU_LOBBY_WANT_INVITE, MU_LOBBY_WANT_P2, MU_LOBBY_WANT_CPU}) {
        lobby.partner_want = want; sample(0, PAD_BUTTON_START); sample(0, 0);
        assert(requests == 0 && lobby.partner_source == -1 && pads[1].err == PAD_ERR_NO_CONTROLLER);
    }
    // Player 2 seated with no 2nd controller: the door still gets a still, connected pad (a person's door).
    v.seats[1].kind = MU_LOBBY_KIND_LOCAL; v.mode |= MU_LOBBY_M_LOCAL_OWNER; lobby.partner_want = MU_LOBBY_WANT_P2;
    plugged = 1; sample(0, 0);
    assert(lobby.partner_source == -1 && pads[1].err == 0 && pads[1].button == 0 && requests == 0);
    // Controller 2 plugged in: it plays the door, START stripped (only the main controller searches).
    plugged = 2; sample(0, PAD_BUTTON_START | PAD_BUTTON_A);
    assert(lobby.partner_source == 1 && pads[1].err == 0 && pads[1].button == PAD_BUTTON_A && requests == 0);
    // Its B on an unpicked door gives the seat back (LOCAL_SEAT {0}) once per press; the tab goes to Find.
    sample(0, PAD_BUTTON_B);
    assert(requests == 1 && op == MU_LOBBY_OP_LOCAL_SEAT && arg == 0 && pads[1].button == 0);
    assert(lobby.partner_want == MU_LOBBY_WANT_FIND);
    sample(0, PAD_BUTTON_B); assert(requests == 1);
    // Picked: B goes to the door (takes the pick back), never the seat.
    sample(0, 0); v.seats[1].flags |= MU_LOBBY_S_PICKED; sample(0, PAD_BUTTON_B);
    assert(requests == 1 && pads[1].button == PAD_BUTTON_B);
    v.seats[1].flags = 0; v.seats[1].kind = MU_LOBBY_KIND_OPEN; v.mode &= ~MU_LOBBY_M_LOCAL_OWNER; requests = 0;
    // The invite is out: the host's B takes it back once per press, and that B never reaches the select.
    lobby.partner_want = MU_LOBBY_WANT_INVITE; requests = 0;
    sample(PAD_BUTTON_B, 0); assert(requests == 0);   // a B held into the invite never cancels it
    v.flags |= MU_LOBBY_F_INVITING;
    sample(PAD_BUTTON_B, 0); assert(requests == 0);
    sample(0, 0); sample(PAD_BUTTON_B, 0);
    assert(requests == 1 && op == MU_LOBBY_OP_CANCEL && pads[0].button == 0);
    sample(PAD_BUTTON_B, 0); assert(requests == 1 && pads[0].button == 0);
    v.flags &= ~MU_LOBBY_F_INVITING;
    sample(PAD_BUTTON_B, 0); assert(requests == 1 && pads[0].button == 0);   // still the cancel's B
    sample(0, 0); sample(PAD_BUTTON_B, 0); assert(requests == 1 && pads[0].button == PAD_BUTTON_B);
    assert(lobby.partner_want == MU_LOBBY_WANT_INVITE);   // back on Invite: START opens the list again
    // Only the host's invite: a guest's B goes to its select.
    v.flags = MU_LOBBY_F_ARMED | MU_LOBBY_F_KNOWN | MU_LOBBY_F_INVITING; v.my_seat = 1;
    sample(0, 0); sample(PAD_BUTTON_B, 0); assert(requests == 1);
    return 0;
}
'''.replace('ROUTER', router)
        compile_and_run(self, source, 'doubles-partner-pads-', ['port/runtime/abi'])

    def test_b_takes_the_invite_back_in_the_runtime(self):
        runtime = read(RUNTIME)
        cancel = body(runtime, 'case MU_LOBBY_OP_CANCEL: {')
        self.assertIn('if (lobby.profile != lb::Profile::Doubles) break;', cancel)
        self.assertIn('if (lobby.inviting->load() && lobby.invite_stop) lobby.invite_stop->store(true);', cancel)
        link = read('port/runtime/hle/yougame_link.cpp')
        invite = body(link, 'void doubles_invite(const std::string& room, const std::function<bool()>& cancelled)')
        self.assertIn('bridge(friends::doubles_invite(room), friends::kPickerTimeout, cancelled)', invite)
        self.assertIn('if (cancelled()) { close(); break; }', link)


class TeamsInvite(unittest.TestCase):
    """START on Invite must reach the app as a Teams invite for this home (fact check, 11-11):
    the stored invite is teams:tl1."""

    def test_the_doubles_invite_op_is_a_teams_invite(self):
        runtime = read(RUNTIME)
        op = body(runtime, 'case MU_LOBBY_OP_INVITE: {')
        self.assertIn('const bool doubles = lobby.profile == lb::Profile::Doubles;', op)
        self.assertIn('if (doubles) yougame::doubles_invite(room, [stop] { return stop->load(); });', op)
        source = r'''#include <cassert>
#include <string>
#include "yougame_friends.h"
int main() {
    const auto j = yougame::friends::doubles_invite("abcdefghjkmn");
    assert(j["mode"] == "teams");
    assert(j["teamsLobby"] == 1);
    assert(j["type"] == "mpFriend" && j["room"] == "abcdefghjkmn" && j["players"] == 2);
    assert(!j.count("multi"));   // one partner, never the Friends multi-invite
    return 0;
}
'''
        compile_and_run(self, source, 'doubles-teams-invite-',
                        ['port/runtime/hle', 'port/third_party', 'port/runtime/abi', 'port/runtime/host',
                         'sourceport/game/include', 'port/app'])


if __name__ == '__main__':
    unittest.main()
