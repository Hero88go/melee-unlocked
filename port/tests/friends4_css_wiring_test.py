"""The 4-player Friends lobby's select: Melee's VS 4-door screen, driven from the lobby's view.

Pins the game side (sourceport/game/include/mu_friends4_css.inc, the mncharsel.c hooks in the patch,
shim/mu_slippi_menu.c's lobby block, the PADRead remap) and that the 1-door online select the other rows
use (Unranked, Ranked, Direct, Squad, Doubles) is left as it was. The hint for each Start block and Rooms
refusal is unit tested in port/tests/yougame_lobby_screen_test.cpp; here, that the screen shows it.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


def body(source, name):
    at = source.index(name)
    at = source.index('{', at)
    depth, end = 1, at + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[at:end]


def patch_file(name):
    patch = read('sourceport/patches/melee-native.patch')
    head = f'diff --git a/{name} b/{name}\n'
    at = patch.index(head)
    end = patch.find('\ndiff --git ', at + len(head))
    return patch[at:end if end >= 0 else len(patch)]


INC = 'sourceport/game/include/mu_friends4_css.inc'
MENU = 'sourceport/game/shim/mu_slippi_menu.c'


class SelectHooks(unittest.TestCase):
    """The hooks the patch puts into Melee's select."""

    def setUp(self):
        self.css = patch_file('src/melee/mn/mncharsel.c')

    def test_screen_is_included_after_the_doubles_one(self):
        self.assertIn('+#include <mu_doubles_css.inc>\n+#include <mu_friends4_css.inc>\n', self.css)
        for proto in ('+static int mu_friends4_coin_locked(u8 door);',
                      '+static int mu_friends4_host_only(void);',
                      '+static u32 mu_friends4_css_start(u32 trigger, int ready);',
                      '+static void mu_friends4_css_build(void);'):
            self.assertIn(proto, self.css)

    def test_frame_runs_last_in_every_select_frame(self):
        self.assertIn('+    mu_css_state_log(gm_GetCurrentGameMode(), gm_GetCurrentSceneIndex(), doors, names);\n'
                      '+    mu_friends4_css_frame();\n+}\n', self.css)

    def test_every_build_of_the_screen_makes_its_texts_anew(self):
        # The Rules overlay and name entry free every SisLib text; the screen's build makes them again.
        self.assertIn('+    mu_skin_css_reset();   /* every build of the screen makes its texts anew */\n'
                      '+    mu_friends4_css_build();\n', self.css)

    def test_start_is_the_lobby_hosts_alone_before_the_online_path(self):
        hunk = self.css[self.css.index('+        if (mu_lobby_css()) {'):]
        hunk = hunk[:hunk.index('} else if (mu_slippi_css_online()) {')]
        # The lobby always gets START; the Friends lobby waits for vanilla's Ready to Fight delay inside, the
        # Doubles home (one team, or an open door: vanilla never says ready) goes by the room's hint.
        self.assertIn('trigger = mu_friends4_css_start(trigger, mnCharSel_804D6CF2 == 0);', hunk)
        start = body(read(INC), 'static u32 mu_friends4_css_start(u32 trigger, int ready)')
        self.assertLess(start.index('if (mu_f4_doubles())'), start.index('if (!ready && !(mu_f4.view.mode & MU_LOBBY_M_SQUAD)) return 0;'))
        self.assertLess(start.index('if (!ready && !(mu_f4.view.mode & MU_LOBBY_M_SQUAD)) return 0;'), start.index('MU_LOBBY_F_HOST'))
        self.assertNotIn('ready', body(start, 'if (mu_f4_doubles())'))

    def test_rules_and_teams_are_host_only(self):
        rules = '+                            if (mu_friends4_host_only()) return;   /* the lobby\'s rules are the host\'s */\n' \
                '+#endif\n                             mnCharSel_804D6CF6 = 3;'
        teams = '+                            if (mu_friends4_cycle_mode()) break;   /* the lobby owns its mode cycle */\n' \
                '+#endif\n                             sfxMove();'
        self.assertIn(rules, self.css)
        self.assertIn(teams, self.css)

    def test_coin_pickup_is_own_coin_or_hosts_cpu(self):
        self.assertIn('+                                && !mu_friends4_coin_locked((u8) closest)\n', self.css)


class Screen(unittest.TestCase):
    """mu_friends4_css.inc."""

    def setUp(self):
        self.inc = read(INC)

    def test_frame_is_inert_outside_the_lobby(self):
        frame = body(self.inc, 'static void mu_friends4_css_frame(void)')
        self.assertLess(frame.index('if (!mu_lobby_css()) {'), frame.index('mu_lobby_read()'))
        self.assertIn('mu_f4_restore();', frame[:frame.index('mu_lobby_read()')])
        self.assertIn('if (mnCharSel_804D6CF6 >= 3) {', frame)   # the Rules overlay / name entry
        # The view's doors before the host's: the host's sends then compare with what the room has.
        order = ['mu_f4_local(css, mine);', 'mu_f4_doors(css, mine);', 'mu_f4_host_doors(css, mine);',
                 'mu_f4_rules(css);', 'mu_f4_invite(mine);', 'mu_f4_lock_in(mine);']
        at = [frame.index(step) for step in order]
        self.assertEqual(at, sorted(at))
        for name in ('mu_friends4_coin_locked', 'mu_friends4_host_only'):
            self.assertIn('if (!mu_lobby_css()', body(self.inc, 'static int ' + name))

    def test_own_door_sends_hover_pick_unpick(self):
        local = body(self.inc, 'static void mu_f4_local(CSSData* css, int mine)')
        for op in ('MU_LOBBY_OP_PICK', 'MU_LOBBY_OP_HOVER', 'MU_LOBBY_OP_UNPICK'):
            self.assertIn(op, local)
        self.assertIn('door->p_kind = door->p_kind_prev = Gm_PKind_Human;', local)
        self.assertIn('if (memcmp(req, sent, size) == 0) return;', local)   # only on a change

    def test_other_doors_come_from_the_view_with_melees_door_code(self):
        draw = body(self.inc, 'static void mu_f4_draw(CSSData* css, int i, const MuLobbySeat* s)')
        for step in ('mnCharSel_8025FDEC((u8) i);', 'mnCharSel_8025DB34((u8) i);', 'p->cpu_level = s->level;',
                     'door->costume = mu_f4_color(s->pick_ch, s->pick_color);', 'p->team = door->team = s->pick_team;',
                     'door->sel_icon = (u8) icon;'):
            self.assertIn(step, draw)
        doors = body(self.inc, 'static void mu_f4_doors(CSSData* css, int mine)')
        self.assertIn('if (i == mine || (i == 1 && mu_lobby_local_owned(&mu_f4.view))) continue;', doors)
        self.assertIn('if (sig == mu_f4.sig[i] && !reopen) continue;', doors)

    def test_a_person_s_door_closed_by_the_rebuild_is_drawn_again(self):
        doors = body(self.inc, 'static void mu_f4_doors(CSSData* css, int mine)')
        self.assertIn('reopen = mu_friends_css_frames >= 2 && s->kind == MU_LOBBY_KIND_HUMAN &&', doors)
        self.assertIn('mnCharSel_803F0DFC.doors[i].p_kind != Gm_PKind_Human;', doors)
        self.assertLess(doors.index('reopen ='), doors.index('mu_f4_draw(css, i, s);'))

    def test_host_sets_cpus_and_levels(self):
        host = body(self.inc, 'static void mu_f4_host_doors(CSSData* css, int mine)')
        self.assertIn('if (!mu_f4_host() || mu_f4_searching()) return;', host)
        self.assertIn('if (i == mine || s->kind == MU_LOBBY_KIND_HUMAN || s->kind == MU_LOBBY_KIND_LOCAL) continue;', host)
        self.assertIn('req[0] = MU_LOBBY_OP_SEAT;', host)
        self.assertIn('req[0] = MU_LOBBY_OP_CPU_PICK;', host)
        self.assertIn('level = p->cpu_level < 1 ? 1 : p->cpu_level > 9 ? 9 : p->cpu_level;', host)
        bounds = body(self.inc, 'static void mu_f4_bounds(int mine)')
        # Doubles: the partner tab also turns with Player 2 in the seat (P2 -> CPU); never on a friend.
        self.assertIn('const int toggle = host && kind != MU_LOBBY_KIND_HUMAN && (doubles || kind != MU_LOBBY_KIND_LOCAL);', bounds)
        # Doubles: the team box picks the pair's colour on this machine's doors (doubles_pair_colour_test.py).
        self.assertIn('const int teambox = doubles ? own && !locked && !(mu_f4.view.flags & MU_LOBBY_F_STARTED)\n'
                      '                                    : i == mine || (host && kind == MU_LOBBY_KIND_CPU);', bounds)

    def test_rules_are_exactly_the_six_and_only_the_host_sends_them(self):
        rules = body(self.inc, 'static void mu_f4_rules(CSSData* css)')
        self.assertIn('if (mu_f4_host() && !mu_f4_doubles() && !mu_f4.mode_pending && !mu_f4.rules_sync &&', rules)
        self.assertIn('req[0] = MU_LOBBY_OP_RULES;', rules)
        sent = re.findall(r'req\[(\d)\] = mine\.(\w+);', rules)
        self.assertEqual(sent, [('1', 'teams'), ('2', 'ff'), ('3', 'time'), ('4', 'stocks'), ('5', 'minutes'),
                                ('6', 'pause')])
        # Everyone else plays the room's rules, into the GameRules and Teams the select and match read.
        guest = rules[rules.index('if (!mu_lobby_rules_ok(bytes)) return;'):]
        for field in ('g->friendly_fire = room->ff;', 'g->mode = room->time ? 0 : 1;',
                      'g->stock_count = room->stocks;', 'g->time_limit = room->minutes;', 'g->pause = room->pause;',
                      'css->vs.start.rules.is_teams = room->teams;'):
            self.assertIn(field, guest)
        self.assertEqual(read('port/runtime/abi/mu_lobby_wire.h').count('MU_LOBBY_RULES_SIZE 6'), 1)

    def test_start_counts_only_when_nothing_blocks_it(self):
        start = body(self.inc, 'static u32 mu_friends4_css_start(u32 trigger, int ready)')
        self.assertIn('if (mu_f4.view.block_hint != MU_LOBBY_HINT_PRESS_START) {', start)
        self.assertIn('mu_f4_note(mu_f4.view.block_hint);', start)
        self.assertIn('MU_LOBBY_HINT_WAIT_HOST', start)
        self.assertLess(start.index('MU_LOBBY_HINT_PRESS_START'), start.index('mu_lobby_css_exit(MU_LOBBY_EXIT_SSS);'))
        self.assertTrue(start.rstrip().endswith('return 0;\n}'))   # vanilla START never sees it

    def test_refusals_and_blocks_reach_the_hint_line(self):
        texts = body(self.inc, 'static void mu_f4_texts(int mine)')
        self.assertIn('unsigned hint = v ? v->block_hint : MU_LOBBY_HINT_OPENING;', texts)
        self.assertIn('mu_f4_note(v->refusal_hint);', texts)
        self.assertIn('mu_f4_line(0, hint == MU_LOBBY_HINT_PRESS_START ? "" : mu_lobby_hint_text(hint));', texts)

    def test_no_rules_line_over_the_select(self):
        texts = body(self.inc, 'static void mu_f4_texts(int mine)')
        for gone in ('Friendly fire', 'Teams  FF', 'Pause %s'):
            self.assertNotIn(gone, texts)
        self.assertIn('"Coin and Bonus play as Stock"', texts)

    def test_other_players_hover_shows_silently(self):
        draw = body(self.inc, 'static void mu_f4_draw(')
        self.assertIn('door->sel_icon = (u8) icon;', draw)
        # Equal before the door's update: that update makes the sound and resets the colour otherwise.
        self.assertLess(draw.index('door->sel_icon_prev = door->sel_icon;'), draw.rindex('mnCharSel_8025DB34((u8) i);'))
        hand = body(self.inc, 'static int mu_friends4_view_hand(u8 port)')
        self.assertIn('port == mu_f4_mine()', hand)
        self.assertIn('mu_lobby_local_owned', hand)
        patch = (ROOT / 'sourceport/patches/melee-native.patch').read_text(errors='replace')
        self.assertRegex(patch, r'\+\s+door = cursor->x6;|if \(mu_friends4_view_hand\(port\)\) goto update_display;')
        self.assertIn('if (mu_friends4_view_hand(port)) goto update_display;', patch)

    def test_stage_select_goes_straight_to_the_match(self):
        menu = (ROOT / 'sourceport/game/shim/mu_slippi_menu.c').read_text(errors='replace')
        decide = body(menu, 'static int lobby_sss_decide(GameModeState* state)')
        self.assertIn('if (mu_lobby_send(start, sizeof start)) lobby.curtain = 1;', decide)   # host
        self.assertIn('lobby.curtain = 1;   /* a guest', decide)                               # guest
        frame = body(menu, 'void mu_lobby_curtain_frame(const MuLobbyView* v)')
        self.assertIn('LOBBY_CURTAIN_GRACE', frame)                                            # a refused Start shows
        self.assertIn('LOBBY_CURTAIN_MAX', frame)                                              # never black for long
        self.assertIn('lobby.curtain_started', frame)                                          # a call-off shows
        self.assertIn('if (lobby.curtain) memset(&mine, 0, sizeof mine);', menu)
        css_decide = body(menu, 'static int lobby_css_decide(GameModeState* state)')
        self.assertIn('lobby.curtain = 0;', css_decide)                                        # the next select shows
        build = body(self.inc, 'static void mu_friends4_css_build(void)')
        # The build runs before the screen's text camera exists: a text made there crashes.
        self.assertNotIn('HSD_SisLib_803A6754', build)
        self.assertIn('mu_f4.curtain_made = 0;', build)
        css_frame = body(self.inc, 'static void mu_friends4_css_frame(void)')
        self.assertLess(css_frame.index('mu_lobby_curtain_frame(v);'), css_frame.index('if (!mu_f4.curtain_made) {'))
        self.assertIn('curtain->render_callback = mu_f4_curtain_draw;', css_frame)
        # Made on every lobby select and drawn with no area until needed, so its shader is built before the real cover.
        draw_cover = body(self.inc, 'static void mu_f4_curtain_draw(void* object)')
        self.assertIn('mu_lobby_curtain() ? 2000.0f : 0.0f', draw_cover)
        self.assertNotIn('if (!mu_lobby_curtain()) return;', draw_cover)
        self.assertIn('HSD_GObjGXLink_80390908(curtain->entity, 4, 0xFF);', css_frame)   # over coins, hands, texts
        self.assertIn('v->refusal_seq != lobby.curtain_refusals', frame)                 # a refused Start shows at once
        noslippi = (ROOT / 'sourceport/game/shim/mu_noslippi.c').read_text()
        self.assertIn('int mu_lobby_curtain(void) { return 0; }', noslippi)
        self.assertIn('void mu_lobby_curtain_frame(', noslippi)
        draw = body(self.inc, 'static void mu_f4_draw(CSSData* css, int i, const MuLobbySeat* s)')
        self.assertIn('if (mu_friends_css_frames < 2 || mu_lobby_curtain()) mnCharSel_804D6CF6 = 3;', draw)   # no pick burst
        texts = body(self.inc, 'static void mu_f4_texts(int mine)')
        self.assertIn('if (!mu_lobby_curtain()) --mu_f4.note_frames;', texts)
        self.assertIn('if (mu_lobby_curtain()) s = "";', body(self.inc, 'static void mu_f4_line('))

    def test_open_doors_invite_and_links_show_once_up(self):
        texts = body(self.inc, 'static void mu_f4_texts(int mine)')
        self.assertIn('"Inviting..." : "Invite"', texts)
        self.assertIn('"CPU Lv %u"', texts)
        self.assertIn('else if (s->route != MU_LOBBY_ROUTE_NONE)', texts)
        # Build 95: Doubles draws no door labels (build 96: its Slippi panel says it); every other select keeps them.
        # A closed lobby draws no door labels (no Invite to offer, and its hint would sit on P2 and P3's).
        self.assertIn('if (s && i != mine && (v->flags & MU_LOBBY_F_KNOWN) && !doubles && v->block_hint != MU_LOBBY_HINT_CLOSED) {', texts)
        invite = body(self.inc, 'static void mu_f4_invite(int mine)')
        self.assertIn('unsigned char req = MU_LOBBY_OP_INVITE;', invite)
        self.assertIn('mu_f4.view.seats[i].kind != MU_LOBBY_KIND_OPEN', invite)
        self.assertIn('else if (mu_f4.view.flags & MU_LOBBY_F_STARTED) why = "a match is being set up";', invite)
        self.assertIn('else if (mu_f4.view.flags & MU_LOBBY_F_INVITING) why = "the invite picker is already open";', invite)
        self.assertIn('for (i = 0; why == NULL && i < MU_LOBBY_SEATS; ++i) {', invite)
        # Only the door whose card opened the picker says "Inviting..."; the other open doors keep "Invite".
        self.assertIn('mu_f4.invite_door = (unsigned char) (i + 1);', invite)
        self.assertIn('i == mu_f4_inviting_door(v, mine) ? "Inviting..." : "Invite"', texts)
        door = body(self.inc, 'static int mu_f4_inviting_door(const MuLobbyView* v, int mine)')
        self.assertIn('if (!v || !(v->flags & MU_LOBBY_F_INVITING)) return -1;', door)
        self.assertIn("if (d >= 0 && d < 4 && d != mine && v->seats[d].kind == MU_LOBBY_KIND_OPEN) return d;", door)

    def test_a_started_match_is_never_followed_back_to_the_stage_select(self):
        self.assertIn('!(mu_f4.view.flags & (MU_LOBBY_F_HOST | MU_LOBBY_F_STARTED)) && mu_f4.view.my_seat < MU_LOBBY_SEATS', self.inc)

    def test_lock_in_after_the_frozen_setup_then_the_splash(self):
        lock = body(self.inc, 'static void mu_f4_lock_in(int mine)')
        self.assertIn('if (!(mu_f4.view.flags & MU_LOBBY_F_STARTED)) {', lock)
        self.assertIn('sel.char_opt = 1;', lock)
        self.assertIn('m.connection_state != MU_SLP_MM_CONNECTION_SUCCESS', lock)
        self.assertLess(lock.index('if (m.local_ready && m.remote_ready) {'),
                        lock.index('mu_lobby_css_exit(MU_LOBBY_EXIT_SPLASH);'))

    def test_own_door_is_drawn_hmn_once_it_can_be(self):
        # The host's own door turned HMN before the screen could draw it and kept Melee's N/A.
        own = body(self.inc, 'static void mu_f4_own_drawn(int mine)')
        self.assertIn('if (mu_friends_css_frames < 2 || mine < 0 || mine > 3 || mu_f4.own_drawn == mine + 1 ||', own)
        self.assertIn('mnCharSel_8025DB34((u8) mine);', own)
        frame = self.inc[self.inc.index('mu_f4_own_drawn(mine);\n'):]
        self.assertLess(frame.index('mu_f4_own_drawn(mine);'), frame.index('mu_f4_texts(mine);'))

    def test_labels_stay_in_the_menu_font(self):
        line = body(self.inc, 'static void mu_f4_line(int line, const char* s)')
        self.assertIn("c == '.' || (((mu_f4.view.mode & MU_LOBBY_M_SQUAD) || mu_f4.note >= MU_LOBBY_HINT_SQUAD_TWO) &&", line)
        self.assertNotIn('snprintf', self.inc)   # not in the decomp's MSL


class ReviewFixes(unittest.TestCase):
    """The 11-06 review of the select: each fix pinned where no unit test reaches it."""

    def setUp(self):
        self.inc = read(INC)
        self.menu = read(MENU)
        self.online = read('port/runtime/hle/slippi_online.cpp')

    def test_stock_plays_its_own_timer_and_zero_is_none(self):
        local = body(self.inc, 'static void mu_f4_rules_local(CSSData* css, MuLobbyRules* r)')
        self.assertIn('r->minutes = mu_f4_u8(g->stock_time_limit, MU_LOBBY_RULE_MAX);', local)
        self.assertLess(local.index('if (r->time)'), local.index('g->stock_time_limit'))
        self.assertNotIn('MU_LOBBY_RULE_MIN : mu_f4_u8(g->stock_time_limit', local)   # 0 stays 0
        rules = body(self.inc, 'static void mu_f4_rules(CSSData* css)')
        self.assertIn('g->stock_time_limit = room->minutes;   /* 0: no timer */', rules)
        self.assertLess(rules.index('if (room->time)'), rules.index('g->time_limit = room->minutes;'))
        texts = body(self.inc, 'static void mu_f4_texts(int mine)')
        self.assertIn('} else if (mu_f4_host() && gmMainLib_GetGameRules()->mode >= 2) {', texts)
        self.assertIn('"Coin and Bonus play as Stock"', texts)

    def test_select_from_another_arming_never_sends_leave(self):
        self.assertIn('lobby.arm = v.arm;', body(self.menu, 'static void lobby_open(void)'))
        close = body(self.menu, 'static void lobby_close(void)')
        self.assertIn('if (lobby_view_op(MU_LOBBY_OP_PEEK, &now) && (now.flags & MU_LOBBY_F_ARMED) && now.arm == lobby.arm)', close)
        self.assertLess(close.index('now.arm == lobby.arm'), close.index('mu_slippi_cmd(MU_LOBBY_COMMAND, &op'))
        self.assertIn('*gmMainLib_GetGameRules() = lobby.rules;', close)   # restored either way
        practice = read('port/runtime/hle/native_practice.cpp')
        one = practice[practice.index('slippi::online::squad_friend_room(room, picked);'):]
        one = one[:one.index('native_start_match(2, {}, 0, 0, &error, room)')]
        self.assertLess(one.index('slippi::online::disarm_friends_lobby();'),
                        one.index('slippi::online::native_cleanup_match();'))
        disarm = body(self.online, 'void disarm_friends_lobby()')
        for step in ('lobby.invite_stop->store(true);', 'lobby = FriendsLobby{};', '++g_lobby_arm;'):
            self.assertIn(step, disarm)
        # The one-argument arm is the Friends profile of the profiled one, which bumps the arm.
        self.assertIn('arm_friends_lobby(room, yougame::lobby::Profile::Friends4);',
                      body(self.online, 'void arm_friends_lobby(const std::string& room)'))
        self.assertIn('++g_lobby_arm;', body(self.online, 'void arm_friends_lobby(const std::string& room, yougame::lobby::Profile profile)'))
        command = body(self.online, 'static bool lobby_command(')
        leave = command[command.index('if (op == MU_LOBBY_OP_LEAVE) {'):]
        self.assertIn('++g_lobby_arm;', leave[:leave.index('return true;')])
        self.assertIn('flags.arm = g_lobby_arm;', command)

    def test_a_drop_before_the_splash_takes_the_parked_room_back(self):
        cleanup = body(self.online, 'void cleanup_connection(yougame::Leave why')
        self.assertIn('if (g_friends_lobby.armed && why == yougame::Leave::Lost && !g_in_online_match) {\n'
                      '    g_friends_lobby.dropped = true;', cleanup)
        command = body(self.online, 'static bool lobby_command(')
        read_ = command[command.index('if (op == MU_LOBBY_OP_READ || op == MU_LOBBY_OP_PEEK) {'):command.index('bool ok = false;')]
        self.assertIn('if (op == MU_LOBBY_OP_READ && lobby.dropped && lobby.armed && !lobby.closed &&', read_)
        self.assertIn('g_matchmaking->GetMatchmakeState() == Matchmaking::CONNECTION_SUCCESS', read_)
        self.assertLess(read_.index('if (!g_forced_error.empty()) lobby.note = g_forced_error;'), read_.index('start_lobby_search()'))
        self.assertIn('if (!yougame::lobby_parked() || !start_lobby_search()) lobby.closed = true;', read_)
        self.assertNotIn('lobby.note.clear();', read_)
        self.assertIn('g_in_online_match = true;\n    g_friends_lobby.note.clear();', self.online)
        self.assertIn('flags.note = lobby.note;', read_)
        link = read('port/runtime/hle/yougame_link.cpp')
        started = body(link, 'void game_started(int64_t rng, int stage)')
        self.assertIn('if (g_room->lobby_session) g_room->lobby_session->match_began();', started)
        self.assertLess(started.index('g_room->in_game = true;'), started.index('match_began()'))
        self.assertIn('mu_f4.note_text_frames = 2 * MU_F4_NOTE;',
                      body(self.inc, 'static void mu_f4_texts(int mine)'))
        self.assertIn('mu_f4_line(0, mu_f4.note_text);', body(self.inc, 'static void mu_f4_texts(int mine)'))

    def test_host_doors_follow_the_view_and_only_a_press_makes_a_cpu(self):
        host = body(self.inc, 'static void mu_f4_host_doors(CSSData* css, int mine)')
        human = host[host.index('if (door->p_kind == Gm_PKind_Human) {'):host.index('cpu = door->p_kind != Gm_PKind_NA;')]
        self.assertIn('if (mu_f4.kind_seen[i] == 0xFF || mu_f4.kind_seen[i] == Gm_PKind_Human) {', human)
        self.assertLess(human.index('mu_f4.sig[i] = 0;'), human.index('door->p_kind = door->p_kind_prev = Gm_PKind_Cpu;'))
        frame = body(self.inc, 'static void mu_friends4_css_frame(void)')
        self.assertIn('mu_f4.kind_seen[i] = mnCharSel_803F0DFC.doors[i].p_kind;', frame)
        self.assertIn('mu_f4.kind_seen[i] = kind;', body(self.inc, 'static void mu_f4_draw('))
        self.assertIn('mu_f4.kind_seen[i] = 0xFF;', body(self.inc, 'static void mu_friends4_css_build(void)'))

    def test_a_hold_running_out_draws_the_door_again(self):
        doors = body(self.inc, 'static void mu_f4_doors(CSSData* css, int mine)')
        self.assertIn('if (--mu_f4.hold[i] == 0) mu_f4.sig[i] = 0;', doors)

    def test_unpick_only_after_a_pick(self):
        local = body(self.inc, 'static void mu_f4_local(CSSData* css, int mine)')
        self.assertIn('if (mu_f4.view.flags & MU_LOBBY_F_STARTED) return;', local)
        self.assertLess(local.index('MU_LOBBY_F_STARTED'), local.index('mu_f4_send(req, size)'))
        head = '} else if (sent[0] == MU_LOBBY_OP_PICK) {'
        unpick = local[local.index(head) + len(head):]
        self.assertIn('req[0] = MU_LOBBY_OP_UNPICK;', unpick[:unpick.index('}')])
        head = '} else if (req[0] == MU_F4_IDLE) {'
        idle = local[local.index(head) + len(head):]
        idle = idle[:idle.index('}')]
        self.assertIn('sent[0] = MU_F4_IDLE;', idle)
        self.assertIn('return;', idle)                         # off the icons after a hover: nothing
        self.assertEqual(local.count('MU_LOBBY_OP_UNPICK'), 1)  # the only UNPICK is the one after a PICK
        # A new visit (back from the stage select) whose pick the room has sends nothing.
        self.assertIn('if (sent[0] == 0xFF && (me->flags & MU_LOBBY_S_PICKED) &&', local)

    def test_own_coin_is_locked_once_the_setup_froze(self):
        coin = body(self.inc, 'static int mu_friends4_coin_locked(u8 door)')
        self.assertLess(coin.index('if (mu_f4.view_ok && (mu_f4.view.flags & MU_LOBBY_F_STARTED)) return 1;'),
                        coin.index('if (door == mu_f4_mine()) return 0;'))

    def test_name_tag_box_is_off_on_the_lobby_select(self):
        css = patch_file('src/melee/mn/mncharsel.c')
        self.assertIn('+                                /* DisableNametagBox (and on the Friends lobby\'s select) */\n'
                      '+                                if (!mu_slippi_css_online() && !mu_lobby_css())\n', css)

    def test_colours_clamp_to_this_builds_costumes_everywhere(self):
        colors = body(self.inc, 'static unsigned mu_f4_colors(unsigned ch)')
        self.assertIn('gm_GetNumCostumesForCKind((u8) ch)', colors)
        self.assertIn('n < 1 ? 1 : n > MU_LOBBY_COLOR_MAX + 1 ? MU_LOBBY_COLOR_MAX + 1 : n', colors)
        color = body(self.inc, 'static unsigned char mu_f4_color(unsigned ch, unsigned color)')
        self.assertIn('color < n ? color : n - 1', color)   # over the count: its last, as costume_for
        self.assertIn('req[2] = mu_f4_door_color(mine, req[1], door->costume);', body(self.inc, 'static void mu_f4_local('))
        self.assertIn('req[3] = mu_f4_door_color(i, req[2], door->costume);', body(self.inc, 'static void mu_f4_host_doors('))
        self.assertIn('door->costume = mu_f4_color(s->hover_ch, s->hover_color);', body(self.inc, 'static void mu_f4_draw('))
        self.assertNotIn('mu_f4_u8(door->costume', self.inc)
        prep = body(self.menu, 'static void lobby_css_prep(GameModeState* state)')
        self.assertIn('costumes[0] = MU_LOBBY_OP_COSTUMES;', prep)
        self.assertIn('gm_GetNumCostumesForCKind((u8) ch)', prep)
        self.assertLess(prep.index('mu_lobby_send(costumes, sizeof costumes)'),
                        prep.index('mu_slippi_cmd(MU_LOBBY_COMMAND, &op, 1, &ok, 1, NULL);'))   # before ENTER
        self.assertIn('yougame::lobby::apply(*lobby_plan, block.data(), block.size(), timer, g_lobby_costumes.data(), &team_colors);',
                      self.online)
        command = body(self.online, 'static bool lobby_command(')
        costumes = command[command.index('if (op == MU_LOBBY_OP_COSTUMES) {'):]
        self.assertIn('std::copy(p + 1, p + 1 + MU_LOBBY_CHARS, g_lobby_costumes.begin());', costumes[:costumes.index('}')])
        # The clamp is in apply(), so it is in the block the setup hash reads, the same on every machine.
        setup = read('port/runtime/hle/yougame_lobby_setup.h')
        self.assertIn('r[0x3] = costume_for(x.pick.ch, x.pick.color, costumes);', setup)

    def test_invite_size_is_only_the_first_try(self):
        practice = read('port/runtime/hle/native_practice.cpp')
        self.assertIn('const int kind = room.empty() ? -1 : yougame::friends::events.room_kind(room);', practice)
        # The Doubles home (G2) is a room kind like the others: the room decides, the invite picks the first try.
        self.assertIn('const bool doubles_guest = !room.empty() && (kind >= 0 ? kind == 2 : '
                      'yougame::friends::events.join_doubles());', practice)
        self.assertLess(practice.index('const int kind = room.empty() ? -1 : yougame::friends::events.room_kind(room);'),
                        practice.index('const bool doubles_guest = !room.empty()'))
        self.assertIn('const bool lobby_guest = !doubles_guest && !room.empty() && (kind >= 0 ? kind == 1 : '
                      'yougame::friends::events.join_players() == 4);', practice)
        self.assertNotIn('join_players() != 2', practice)

    def test_control_reset_is_gone(self):
        self.assertNotIn('void reset()', body(read('port/runtime/hle/yougame_lobby.h'), 'class Control {'))


class VerifyFixes(unittest.TestCase):
    """The 11-06 verify review: each fix pinned where no unit test reaches it."""

    def setUp(self):
        self.inc = read(INC)
        self.menu = read(MENU)
        self.online = read('port/runtime/hle/slippi_online.cpp')
        self.link = read('port/runtime/hle/yougame_link.cpp')

    def find_match(self):
        return self.link[self.link.index('Match find_match('):self.link.index('// The Friends lobby plays its setup\'s humans')]

    def test_the_host_calls_off_a_match_that_never_began(self):
        lobby = read('port/runtime/hle/yougame_lobby.h')
        nxt = lobby[lobby.index('void next_match() {'):lobby.index(' private:')]
        self.assertIn('if (has_setup_ && !began_ && !setup_.match_id.empty()) {', nxt)
        self.assertLess(nxt.index('pending_call_off_ = setup_.match_id;'), nxt.index('if (is_host()) send_call_off();'))
        self.assertLess(nxt.index('send_call_off()'), nxt.index('has_setup_ = false;'))
        # One place holds the message's shape: Rooms' end-session for the match.
        call = body(lobby, 'void call_off_setup(const std::string& match_id)')
        self.assertIn('queue_.push_back({{"t", "end-session"}, {"sessionId", match_id}});', call)
        self.assertEqual(lobby.count('"end-session"'), 1)
        self.assertIn('call_off_setup(pending_call_off_);', body(lobby, 'void send_call_off()'))
        # No fallback id: a setup without its own matchId never reaches a call-off (plan() refuses it).
        fm = self.find_match()
        self.assertNotIn('adopted_match', fm)
        self.assertIn('lobby_session->next_match();', fm)

    def test_session_ended_is_a_match_ending_not_the_room(self):
        fm = self.find_match()
        self.assertIn('if (t == "sessionEnded")', fm)
        ended = fm[fm.index('if (t == "sessionEnded")'):]
        ended = ended[:ended.index(';') + 1]
        self.assertNotIn('return', ended)
        io = body(self.link, 'transport::RoomIO room_io(Room& r)')
        self.assertIn('if (ranked::str(m, "t") == "sessionEnded")', io)
        pump = read('port/runtime/hle/yougame_room.h')
        stop = pump[pump.index('if (broker && (t == "ended"'):]
        self.assertNotIn('sessionEnded', stop[:stop.index('\n')])

    def test_a_released_setup_releases_the_rooms_plan(self):
        io = body(self.link, 'transport::RoomIO room_io(Room& r)')
        self.assertIn('if (r.lobby && !r.lobby_session->has_setup()) {', io)
        self.assertIn('r.lobby = nullptr;', io)
        self.assertLess(io.index('r.lobby_session->receive(m);'), io.index('r.lobby = nullptr;'))
        fm = self.find_match()
        self.assertLess(fm.index('if (lobby_plan && !lobby_session->has_setup()) {'),
                        fm.index('if (!lobby_plan && lobby_session->has_setup()) {'))

    def test_the_room_decides_lobby_or_1v1(self):
        fm = self.find_match()
        # Doubles joins get the room-kind check too (review G2 #1): no doubles_home exclusion.
        self.assertIn('const bool invited = (lobby_route && !search.lobby_room.empty() && !adopted) || friends_mode;', fm)
        self.assertNotIn('!doubles_home && !search.lobby_room.empty()', fm)
        self.assertIn('const int joined_kind = doubles_home ? friends::kRoomDoubles : lobby_route ? friends::kRoomFriends4 : friends::kRoom1v1;', fm)
        self.assertIn('bool kind_heard = !invited || friends::events.room_kind(room_code) >= 0;', fm)
        self.assertIn('(t == "welcome" && m.count("muLobby") && m["muLobby"].is_object()) ||', fm)
        self.assertIn('(t == lobby::kType && ranked::str(m, "op") == "state");', fm)
        self.assertIn('const int kind = lobby::doubles_frame(t == "welcome" ? m["muLobby"] : m) ? friends::kRoomDoubles : friends::kRoomFriends4;', fm)
        self.assertIn('return reroute(kind, true, "its first frames carry that lobby");', fm)
        self.assertIn('return reroute(friends::kRoom1v1, true, "its first frames carry no lobby");', fm)
        self.assertIn('const bool kind_refusal = room->socket->kind_refused();\n'
                      '    if (!kind_heard && !cancelled() && (room->socket->lobby_mismatch() || kind_refusal) &&\n'
                      '        friends::events.mismatch_reroutes(room_code, kind_refusal)) {\n'
                      '      const int next = friends::events.mismatch_next(room_code, joined_kind, error, kind_refusal);\n'
                      '      if (next >= 0) return reroute(next, false, "the room refused this kind");\n'
                      '      host::log("friends: room %s refused every kind; not joining", room_code.c_str());\n'
                      '      return fail(friends::kJoinRefused);\n'
                      '    }\n', fm)
        self.assertIn('    return fail(friends::events.refusal_text(room_code, error, false), !direct);', fm)
        self.assertIn('if (known) friends::events.room_decided(room_code, kind);', fm)
        self.assertIn('else friends::events.room_retry(room_code, kind);', fm)
        # A room of another kind is left before the menu takes the queued join; the lobby's own guard leaves its room.
        lobby_frame = fm[fm.index('if (lobby_frame) {'):fm.index('friends::events.room_seen(room_code, kind);')]
        self.assertLess(lobby_frame.index('room->socket->send({{"t", "leave"}});'), lobby_frame.index('return reroute(kind'))
        no_lobby = fm[fm.index('if (joined_kind != friends::kRoom1v1) {'):fm.index('friends::events.room_seen(room_code, friends::kRoom1v1);')]
        self.assertLess(no_lobby.index('room->socket->send({{"t", "leave"}});'), no_lobby.index('return reroute(friends::kRoom1v1'))

    def test_g2_fix_kind_refusals_hints_and_last_words(self):
        friends = (ROOT / 'port/runtime/hle/yougame_friends.h').read_text()
        body = friends[friends.index('inline bool doubles_kind_refusal('):]
        body = body[:body.index('}') + 1]
        # 1. teams_home is a gone/busy/full/blocked/other-remix home too: never a kind refusal.
        self.assertNotIn('teams_home', body)
        for code in ('teams_lobby_off', 'bad_teams_lobby', 'teams_lobby_branch'):
            self.assertIn(f'"{code}"', body)
        # 2. md:1 / mu_doubles:1 marks the join hinted, so mismatch_reroutes and mismatch_next say no.
        hinted = friends[friends.index('pending_hinted_ = pending_players_ != 0 ||'):]
        hinted = hinted[:hinted.index(';') + 1]
        self.assertIn('value.value("md", json()) == 1 || value.value("mu_doubles", json()) == 1', hinted)
        # md:1 is strict: no kind refusal reroutes it either (a size or key hint yields to one).
        self.assertIn('pending_strict_ = value.value("md", json()) == 1 || value.value("mu_doubles", json()) == 1;', friends)
        self.assertIn('bool blocked_locked(bool kind_refusal) const { return join_strict_ || (join_hinted_ && !kind_refusal); }', friends)
        self.assertIn('return !code.empty() && !room_kinds_.count(code) && !blocked_locked(kind_refusal);', friends)
        self.assertIn('if (code.empty() || room_kinds_.count(code) || blocked_locked(kind_refusal) || tried < 0 || tried > 2) return -1;', friends)
        # 3. A failed try that isn't a kind refusal shows its own words.
        text = friends[friends.index('std::string refusal_text('):]
        text = text[:text.index('\n  }') + 4]
        self.assertIn('if (!kind) return error;', text)
        self.assertLess(text.index('if (!kind) return error;'), text.index('first_refusal(code)'))
        # 4. Rooms' slug in "answered <status> [<slug>]: ..." decides before the words.
        transport = (ROOT / 'port/runtime/hle/yougame_transport.h').read_text()
        self.assertIn('return slug == "teams_protocol" || slug == "doubles_profile";', transport)
        self.assertIn('static const std::string lead = "the rooms server answered ";', transport)
        self.assertIn('if (text.compare(0, lead.size(), lead) != 0) return {};', transport)
        krf = transport[transport.index('inline bool room_kind_refusal('):]
        self.assertLess(krf.index('if (const int slug = room_kind_slug(p); slug >= 0) return slug == 1;'),
                        krf.index('another teams protocol'))
        self.assertIn('if (room_kind_slug(p) == 1) kind_refused_ = true;', transport)
        self.assertIn('kind_refused_ = code_ == 1006 && (kind_refused_ || room_kind_refusal(p));', transport)

    def test_wrong_kind_refusals_and_gone_homes(self):
        fm = self.find_match()
        # A ticket refusal never tries another kind: every ok:false answer (teams_lobby_off and the
        # other wrong-kind codes included) shows its own words and the game goes back to the menu.
        self.assertIn('g_bridge_refusal.clear();   // read in the catch below', fm)
        self.assertLess(fm.index('g_bridge_refusal.clear();   // read in the catch below'), fm.index('if (lobby_route && parked) {'))
        catch = fm[fm.index('} catch (const update::Required&) {'):fm.index('if (search.mode == 0 && !ranked::set_contract_matches(ticket))')]
        for gone in ('room_retry', 'room_decided', 'mismatch_next', 'mismatch_reroutes', 'doubles_kind_refusal', '"Cancelled"'):
            self.assertNotIn(gone, catch)
        self.assertIn('return fail(e.what());', catch)
        # update_required shows the answer's own words (the app's line, desktop PR 1145), else ours, and never retries.
        self.assertIn('return fail(g_bridge_update_text.empty() ? std::string(update::kMessage) : g_bridge_update_text, false, true);', catch)
        # A friend's room that refuses the socket after a good ticket: kind tries stay bounded by mismatch_next, then
        # one plain line; a bare refusal (no kind) shows it at once. Neither searches again.
        refused = fm[fm.index('if (!adopted && !room->socket->open_session(url, pass, room_code, &error, 10000, cancelled)) {'):
                     fm.index('  if (cancelled()) {\n    if (adopted && room->lobby_session) {')]
        self.assertIn('if (next >= 0) return reroute(next, false, "the room refused this kind");', refused)
        self.assertEqual(refused.count('return fail(friends::kJoinRefused);'), 2)
        self.assertLess(refused.index('fail_room_full(room_code)'), refused.index('friends::kJoinRefused'))
        self.assertIn('if (invited && !cancelled()) {', refused)
        # An opened friend's room with no welcome in kHelloMs ends the join on the same line: never a silent wait.
        self.assertIn('if (invited && !adopted) hello_by = now_ms() + friends::kHelloMs;', fm)
        self.assertIn('if (t == "welcome") { room->me = m.value("me", ""); hello_by = 0; }', fm)
        # A reopen re-arms the welcome wait once only, so a flapping room still ends on a line (review 11-11).
        self.assertIn('if (hello_by && !hello_rearmed) { hello_by = now_ms() + friends::kHelloMs; hello_rearmed = true; }', fm)
        self.assertIn('if ((friends_mode || invited) && !cancelled() && room->socket->room_full()) return fail_room_full(room_code);', fm)
        hello = fm[fm.index('if (hello_by && now_ms() >= hello_by) {'):]
        self.assertLess(hello.index('room->socket->send({{"t", "leave"}});'), hello.index('return fail(friends::kJoinRefused);'))
        self.assertLess(fm.index('if (invited && !adopted) hello_by'), fm.index('if (hello_by && now_ms() >= hello_by) {'))
        friends_h = (ROOT / 'port/runtime/hle/yougame_friends.h').read_text()
        self.assertIn('kJoinRefused = "Couldn\'t join your friend\'s game. Try again from their invite.";', friends_h)
        # An invited Doubles guest alone in the home it joined leaves with kRoomGone (r2 Low), before its frames can
        # record the fresh home's kind or reroute: a gone room is never another kind.
        gone = 'if (t == "welcome" && invited && doubles_home && !home_checked) {'
        self.assertIn(gone, fm)
        block = fm[fm.index(gone):fm.index('return fail(friends::kRoomGone);')]
        self.assertIn('if (lobby::lone_welcome(m)) {', block)
        self.assertLess(block.index('room->socket->send({{"t", "leave"}});'), len(block))
        self.assertLess(fm.index('return fail(friends::kRoomGone);'), fm.index('if (!kind_heard) {\n        const bool lobby_frame'))
        self.assertNotIn('room_retry', block)
        self.assertNotIn('room_decided', block)

    def test_the_drop_note_survives_the_selects_cleanup(self):
        cleanup = body(self.online, 'void cleanup_connection(yougame::Leave why')
        keep = 'if (g_friends_lobby.armed && g_friends_lobby.dropped && !g_forced_error.empty()) g_friends_lobby.note = g_forced_error;'
        self.assertIn(keep, cleanup)
        self.assertLess(cleanup.index(keep), cleanup.index('g_forced_error.clear();'))
        self.assertIn('lobby.dropped = false;', body(self.online, 'bool start_lobby_search()'))

    def test_b_back_returns_to_friends_not_the_slippi_submenu(self):
        decide = body(self.menu, 'static int lobby_css_decide(GameModeState* state)')
        back = decide[decide.index('pending_scene_change == 2'):decide.index('gmVsMelee_ExitCss')]
        self.assertIn('if (lobby.view.mode & MU_LOBBY_M_DOUBLES) doubles_home_return = 1;\n        else friends_return = 1;', back)
        self.assertLess(back.index('friends_return = 1'), back.index('gm_ChangeGameModeAfterCurrentScene(GM_MENU)'))
        enter = self.menu[self.menu.index('if (friends_return || from_friends_css) {'):]
        self.assertIn('MU_SLP_OPT_FRIENDS', enter[:enter.index('return 1;')])
        # The Doubles home's B goes back to the Doubles row, ahead of the Friends return and the Direct one.
        home = self.menu[self.menu.index('    if (doubles_home_return) {'):]
        self.assertIn('MU_SLP_OPT_DOUBLES', home[:home.index('return 1;')])
        self.assertLess(self.menu.index('    if (doubles_home_return) {'), self.menu.index('if (friends_return || from_friends_css) {'))
        self.assertIn('friends_return = friends_css_host = doubles_home_return = 0;', self.menu)

    def test_close_peeks_never_reads(self):
        close = body(self.menu, 'static void lobby_close(void)')
        self.assertNotIn('lobby_read_into', close)
        self.assertIn('MU_LOBBY_OP_PEEK', close)

    def test_mex_costumes_show_what_plays(self):
        local = body(self.inc, 'static void mu_f4_local(CSSData* css, int mine)')
        self.assertIn('if (door->costume != req[2] && door->costume > MU_LOBBY_COLOR_MAX) {', local)
        clamp = local[local.index('if (door->costume != req[2]'):]
        self.assertLess(clamp.index('door->costume = req[2];'), clamp.index('mnCharSel_8025DB34((u8) mine);'))
        self.assertLess(local.index('req[2] = mu_f4_door_color(mine, req[1], door->costume);'),
                        local.index('door->costume = req[2];'))

    def test_the_note_shows_once_per_drop(self):
        build = body(self.inc, 'static void mu_friends4_css_build(void)')
        self.assertIn('memcpy(note_seen, mu_f4.note_seen, sizeof note_seen);', build)
        self.assertLess(build.index('memset(&mu_f4, 0, sizeof mu_f4);'),
                        build.index('memcpy(mu_f4.note_seen, note_seen, sizeof note_seen);'))
        texts = body(self.inc, 'static void mu_f4_texts(int mine)')
        self.assertIn('if (v && v->note[0] && (v->note_seq != mu_f4.note_seq_seen || strcmp(v->note, mu_f4.note_seen) != 0)) {',
                      texts)


class SecondVerifyFixes(unittest.TestCase):
    """The narrow verify of 2e7ab97ec: mismatch hints, carried and moved call-offs, the m-ex wrap, docs,
    the note per drop."""

    def setUp(self):
        self.inc = read(INC)
        self.online = read('port/runtime/hle/slippi_online.cpp')
        self.link = read('port/runtime/hle/yougame_link.cpp')
        self.lobby = read('port/runtime/hle/yougame_lobby.h')
        self.wire = read('port/runtime/abi/mu_lobby_wire.h')

    def find_match(self):
        return self.link[self.link.index('Match find_match('):self.link.index('// The Friends lobby plays its setup\'s humans')]

    def test_a_hinted_invite_keeps_the_mismatch_error(self):
        friends = read('port/runtime/hle/yougame_friends.h')
        reroutes = body(friends, 'bool mismatch_reroutes(const std::string& code, bool kind_refusal = false) const')
        self.assertIn('return !code.empty() && !room_kinds_.count(code) && !blocked_locked(kind_refusal);', reroutes)
        self.assertIn('join_hinted_ = std::exchange(pending_hinted_, false);', body(friends, 'std::string take_join(bool safe)'))
        fm = self.find_match()
        site = fm[fm.index('if (!adopted && !room->socket->open_session('):]
        site = site[:site.index('if (cancelled()) {')]
        # Otherwise the original error, as before.
        self.assertLess(site.index('mismatch_reroutes(room_code, kind_refusal)'),
                        site.index('return fail(friends::events.refusal_text(room_code, error, false), !direct);'))

    def test_a_closed_parked_room_carries_its_call_off(self):
        fm = self.find_match()
        closed = fm[fm.index('if (parked->socket->closed() || !parked->lobby_session) {'):]
        closed = closed[:closed.index('} else host::log("friends lobby: next match in the same room");')]
        self.assertIn('if (parked->lobby_session) carried_call_off = parked->lobby_session->call_off();', closed)
        self.assertLess(closed.index('carried_call_off ='), closed.index('shutdown_room(std::move(old))'))
        self.assertIn('o.call_off_id = carried_call_off.id; o.call_off_setup = carried_call_off.setup;', fm)
        self.assertLess(fm.index('o.call_off_id = carried_call_off.id;'), fm.index('lobby_session = std::make_unique<lobby::Session>(o);'))

    def test_a_pending_call_off_waits_for_the_host_and_clears_on_no_setup(self):
        adopt = body(self.lobby, 'void adopt(const State& s)')
        null = adopt[adopt.index('if (state_.setup.is_null()) {'):adopt.index('} else if (!pending_call_off_.empty()')]
        self.assertIn('pending_call_off_.clear();', null)
        held = adopt[adopt.index('} else if (!pending_call_off_.empty()'):]
        held = held[:held.index('\n    }\n')]
        self.assertIn('state_.setup == pending_frozen_', held)
        self.assertIn('stale_frozen_ = true;', held)
        self.assertIn('if (is_host()) send_call_off();', held)
        # Before the frozen setup is taken (as stale, it never is).
        self.assertLess(adopt.index('stale_frozen_ = true;'), adopt.index('if (!state_.setup.is_null() && !has_setup_ && !stale_frozen_) {'))
        self.assertIn('if (pending_call_off_.empty() || call_off_sent_) return;', body(self.lobby, 'void send_call_off()'))

    def test_the_mex_wrap_keeps_both_directions(self):
        door = body(self.inc, 'static unsigned char mu_f4_door_color(int i, unsigned ch, unsigned costume)')
        self.assertIn('if (costume > MU_LOBBY_COLOR_MAX && costume == n && mu_f4.door_color[i] == n - 1) c = 0;', door)
        self.assertIn('mu_f4.door_color[i] = c;', door)
        setup = read('port/runtime/hle/yougame_lobby_setup.h')
        self.assertIn('return (unsigned)color < n ? (uint8_t)color : (uint8_t)(n - 1);',
                      body(setup, 'inline uint8_t costume_for(int ch, int color, const uint8_t* costumes)'))

    def test_peek_is_documented(self):
        head = self.wire[:self.wire.index('#ifndef MU_LOBBY_WIRE_H')]
        self.assertIn(' *   PEEK     {op}', head)
        self.assertNotIn('Every request but READ answers one byte', head)
        self.assertNotIn('Every request but READ answers one byte', self.online)
        self.assertIn('READ and PEEK answer the view', head)

    def test_each_drop_bumps_the_note_seq(self):
        cleanup = body(self.online, 'void cleanup_connection(yougame::Leave why')
        drop = cleanup[cleanup.index('g_friends_lobby.dropped = true;'):]
        self.assertLess(drop.index('++g_friends_lobby.note_seq;'), drop.index('}'))
        self.assertIn('flags.note_seq = lobby.note_seq;', body(self.online, 'static bool lobby_command('))
        build = body(self.inc, 'static void mu_friends4_css_build(void)')
        self.assertLess(build.index('memset(&mu_f4, 0, sizeof mu_f4);'), build.index('mu_f4.note_seq_seen = note_seq_seen;'))


class MenuFlow(unittest.TestCase):
    """shim/mu_slippi_menu.c: the online major's select, stage select and splash for the lobby."""

    def setUp(self):
        self.menu = read(MENU)

    def test_lobby_opens_with_the_major_and_leaves_with_it(self):
        self.assertIn('lobby_open();', body(self.menu, 'online_on_load('))
        unload = body(self.menu, 'online_on_unload(')
        self.assertIn('lobby_close();', unload)
        close = body(self.menu, 'static void lobby_close(void)')
        self.assertIn('unsigned char op = MU_LOBBY_OP_LEAVE', close)
        self.assertIn('*gmMainLib_GetGameRules() = lobby.rules;', close)
        self.assertIn('if (!lobby_read_into(&v) || !(v.flags & MU_LOBBY_F_ARMED)) return;',
                      body(self.menu, 'static void lobby_open(void)'))

    def test_select_is_melees_vs_one_with_its_own_picks(self):
        prep = body(self.menu, 'static void lobby_css_prep(GameModeState* state)')
        self.assertIn('gmVsMelee_EnterCss(state, &lobby.vs, VS_MELEE);', prep)
        self.assertIn('unsigned char op = MU_LOBBY_OP_ENTER', prep)
        # Back from a match the room parks with its links (cleanup 2), before ENTER adopts it.
        self.assertLess(prep.index('mu_slippi_cmd(CMD_CLEANUP_CONNECTIONS, &next, 1, NULL, 0, NULL);'),
                        prep.index('mu_slippi_cmd(MU_LOBBY_COMMAND, &op, 1, &ok, 1, NULL);'))
        self.assertIn('unsigned char next = 2;', prep)
        self.assertIn('if (lobby.major) {\n        lobby_css_prep(state);\n        return;\n    }', self.menu)
        self.assertIn('gmVsMelee_EnterSss(state, &lobby.vs);', self.menu)

    def test_exits_route_by_why_the_select_left(self):
        decide = body(self.menu, 'static int lobby_css_decide(GameModeState* state)')
        self.assertIn('if (css->pending_scene_change == 2) {', decide)   # B out: the unload leaves
        self.assertIn('gmVsMelee_ExitCss(state, &lobby.vs);', decide)
        for step in ('gm_SetNextGameModeStateId(MU_SLP_STATE_SSS);', 'lobby.played = 1;',
                     'gm_SetNextGameModeStateId(MU_SLP_STATE_SPLASH);', 'gm_SetNextGameModeStateId(MU_SLP_STATE_CSS);'):
            self.assertIn(step, decide)
        self.assertIn('if (lobby_css_decide(state)) return;', self.menu)
        self.assertIn('if (lobby_sss_decide(state)) return;', self.menu)

    def test_stage_goes_with_start_and_random_lets_the_room_pick(self):
        sss = body(self.menu, 'static int lobby_sss_decide(GameModeState* state)')
        self.assertIn('start[0] = MU_LOBBY_OP_START;', sss)
        self.assertIn('start[1] = (unsigned char) (mu_lobby_stage_ok(stage) && stage != 0 ? stage : 0);', sss)
        self.assertIn('gm_SetNextGameModeStateId(MU_SLP_STATE_CSS);', sss)
        wire = read('port/runtime/abi/mu_lobby_wire.h')
        ok = body(wire, 'mu_lobby_stage_ok(')
        self.assertIn('21', ok)
        self.assertIn('26', ok)

    def test_only_this_players_pad_on_its_own_door(self):
        pads = body(self.menu, 'void mu_lobby_pads(PADStatus* status)')
        self.assertIn('status[i].err = person ? 0 : PAD_ERR_NO_CONTROLLER;', pads)
        self.assertIn('status[seat] = mine;', pads)
        read_ = body(read('sourceport/game/shim/mu_pad.c'), 'PADRead(')
        hook = read_[read_.index('if (mu_lobby_css()) {'):]
        self.assertIn('mu_lobby_pads(status);', hook)
        self.assertIn('if (status[i].err == 0)', hook)


class LocalPartnerRouting(unittest.TestCase):
    def test_actual_pad_router_claim_release_and_mode_guards(self):
        router = body(read(MENU), 'void mu_lobby_pads(PADStatus* status)')
        source = r'''#include <cassert>
#include <cstring>
#include <cstdint>
#include "mu_lobby_wire.h"
using u16 = uint16_t;
struct PADStatus { u16 button = 0; int stickX = 0; int err = 0; };
constexpr int PAD_ERR_NO_CONTROLLER = -1, PAD_BUTTON_START = 0x1000, PAD_BUTTON_B = 0x200, PAD_BUTTON_A = 0x100;
constexpr int MU_LOBBY_WANT_FIND = 0, MU_LOBBY_WANT_P2 = 2;
struct { int view_ok = 1, partner_source = -1, partner_b_held = 0, partner_want = 0, curtain = 0, cancel_held = 0, cancel_mask = 0; MuLobbyView view{}; } lobby;
int screen = 1, requests = 0, op = -1, arg = -1;
int mu_lobby_css() { return screen; }
int gm_801677F0() { return 2; }
int mu_lobby_send(const unsigned char* p, unsigned n) { ++requests; op = p[0]; arg = n > 1 ? p[1] : -1; return 1; }
void OSReport(const char*, ...) {}
void mu_lobby_pads(PADStatus* status) ROUTER
void sample(PADStatus* pads, int second_button) {
    for (int i = 0; i < 4; ++i) { pads[i] = {}; pads[i].err = i == 2 || i == 3 ? 0 : -1; }
    pads[2].stickX = 10; pads[3].stickX = 47; pads[3].button = second_button;
    mu_lobby_pads(pads);
}
int main() {
    PADStatus pads[4];
    auto& v = lobby.view;
    v.flags = MU_LOBBY_F_ARMED | MU_LOBBY_F_KNOWN | MU_LOBBY_F_HOST;
    v.my_seat = 0; v.mode = MU_LOBBY_M_DOUBLES; v.seats[0].kind = MU_LOBBY_KIND_HUMAN;
    // Build 96: the tab claims Player 2's seat (mu_f4_p2_claim); controller 2's START never does, on any tab.
    sample(pads, PAD_BUTTON_START); assert(requests == 0 && lobby.partner_source == -1);
    lobby.partner_want = MU_LOBBY_WANT_P2; sample(pads, 0); sample(pads, PAD_BUTTON_START);
    assert(requests == 0 && lobby.partner_source == -1);
    assert(pads[0].stickX == 10 && pads[1].err == PAD_ERR_NO_CONTROLLER);
    // The room seated Player 2: the first other connected controller (pad 4 here) plays the door, START stripped.
    v.seats[1].kind = MU_LOBBY_KIND_LOCAL; v.mode |= MU_LOBBY_M_LOCAL_OWNER;
    sample(pads, PAD_BUTTON_START | PAD_BUTTON_A);
    assert(lobby.partner_source == 3 && pads[1].stickX == 47 && pads[1].button == PAD_BUTTON_A && requests == 0);
    v.seats[1].flags = MU_LOBBY_S_PICKED;
    sample(pads, PAD_BUTTON_B); assert(requests == 0 && pads[1].button == PAD_BUTTON_B); // first B takes pick back
    sample(pads,0); v.seats[1].flags = 0;
    sample(pads,PAD_BUTTON_B); assert(requests == 1 && arg == 0 && pads[1].button == 0);
    assert(lobby.partner_want == MU_LOBBY_WANT_FIND);      // controller 2's B leaves: the tab is back on Find
    sample(pads,PAD_BUTTON_B); assert(requests == 1);
    for (int mode = 0; mode < 5; ++mode) {
        v.mode = MU_LOBBY_M_DOUBLES | MU_LOBBY_M_LOCAL_OWNER;
        v.flags = MU_LOBBY_F_ARMED | MU_LOBBY_F_KNOWN | MU_LOBBY_F_HOST; v.my_seat = 0;
        if (mode == 0) v.mode |= MU_LOBBY_M_SEARCHING;
        if (mode == 1) v.mode = 0; // Friends
        if (mode == 2) v.flags |= MU_LOBBY_F_STARTED;
        if (mode == 3) v.my_seat = 1;
        if (mode == 4) v.mode &= ~MU_LOBBY_M_LOCAL_OWNER; // belongs to another connection
        sample(pads,PAD_BUTTON_START | PAD_BUTTON_A | PAD_BUTTON_B);
        assert(requests == 1 && pads[1].stickX != 47);
    }
    v.mode = MU_LOBBY_M_DOUBLES | MU_LOBBY_M_SEARCHING; v.my_seat = 0; v.seats[1].kind = MU_LOBBY_KIND_OPEN;
    lobby.partner_want = MU_LOBBY_WANT_P2;
    sample(pads,PAD_BUTTON_START); assert(requests == 1 && pads[1].err == PAD_ERR_NO_CONTROLLER);
    screen = 0; sample(pads,PAD_BUTTON_A); assert(pads[3].stickX == 47 && requests == 1);
}
'''.replace('ROUTER', router)
        with tempfile.TemporaryDirectory(prefix='local-partner-pads-') as d:
            cpp, exe = Path(d) / 'pads.cpp', Path(d) / 'pads'
            cpp.write_text(source)
            subprocess.run(['c++', '-std=c++17', '-I'+str(ROOT/'port/runtime/abi'), str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_confirmed_partner_replays_without_early_input_or_prediction(self):
        predictor = body(read('sourceport/game/shim/mu_online_prediction.inc'), 'static int load_opponent_inputs(')
        source = r'''#include <cassert>
#include <cstring>
#include <cstdint>
#include "slippi_port_inputs.h"
using u8 = uint8_t;
constexpr int REMOTES = 3, PAD_SIZE = 12, RXB_INPUTS_COUNT = 7, RXB_OPNT_COUNT = 1, RXB_OPNT_FRAME_NUMS = 26, RXB_OPNT_INPUTS = 42;
// Slippi 3.7.0: the host's predicted inputs follow the inputs and the despawn frames.
constexpr int RXB_OPNT_PREDICTED_INPUTS = RXB_OPNT_INPUTS + PAD_SIZE * RXB_INPUTS_COUNT * REMOTES + 4 * REMOTES;
constexpr int START_SYNC_FRAME = 123, PREDICTED_INPUTS_COUNT = 10;
struct PADStatus { u8 wire[12]{}; u8 analogA = 0; };
struct {
    u8 rxb[600]{}, predicted_write[3]{}, predicted_read[3]{}, predicted_early[3][10]{}, predicted[3][10][12]{};
    int local_index = 0, console_active = 0, delay = 2, game_over = 0, early_used = 0;
    int player_predicting[3]{}, player_savestate_frame[3]{}, savestate_predicting = 0, savestate_frame = 0, finalized = 0;
} mu_online;
uint32_t be32(const u8* p) { return slippi::PortInputs::u32(p); }
int early_calls[3]{};
const u8* early_pad(int port, int, int) { ++early_calls[port]; return nullptr; }
void wire_to_pad(PADStatus* p, const u8* b) { std::memcpy(p->wire,b,12); p->analogA = b[8]; }
static int load_opponent_inputs(PADStatus* stat, int frame, int predicting_allowed, unsigned int* predicted_players) PREDICTOR
void put32(u8* p, int n) { for (int i=0;i<4;++i) p[i]=u8(uint32_t(n)>>(24-8*i)); }
int main() {
    slippi::PortInputs bank; bank.main = 0; bank.humans = 4; bank.owner = {{0,0,2,3}};
    for (int frame=1;frame<=126;++frame) { slippi::Pad pad(frame); pad.buf[0]=u8(frame); pad.buf[1]=77; bank.local_partner(pad); }
    for (int remote=0;remote<3;++remote) {
        put32(mu_online.rxb + RXB_OPNT_FRAME_NUMS + 4*remote,remote == 0 ? 126 : 124);
        for (int k=0;k<7;++k) {
            auto* out=mu_online.rxb + RXB_OPNT_INPUTS + remote*7*12 + k*12;
            if (remote == 0) std::memcpy(out,bank.pads[1][k]->buf,12);
        }
    }
    mu_online.rxb[RXB_OPNT_COUNT] = 3;
    PADStatus pads[4]; unsigned int predicted = 0;
    assert(!load_opponent_inputs(pads,125,1,&predicted)); // other machines still need prediction
    assert(!mu_online.player_predicting[0] && !early_calls[0]);
    assert(mu_online.player_predicting[1] && early_calls[1]);
    assert(pads[1].wire[0] == 125 && pads[1].wire[1] == 77);
    // Rollback re-reads exactly the saved partner bytes; no hardware or early-ring sample is used.
    for (int frame=120;frame<=126;++frame) {
        load_opponent_inputs(pads,frame,1,&predicted);
        assert(!(predicted & 1) && pads[1].wire[0] == frame && pads[1].wire[1] == 77);
        assert(!mu_online.player_predicting[0] && !early_calls[0]);
    }
}
'''.replace('PREDICTOR', predictor)
        with tempfile.TemporaryDirectory(prefix='local-partner-replay-') as d:
            cpp, exe = Path(d) / 'replay.cpp', Path(d) / 'replay'
            cpp.write_text(source)
            subprocess.run(['c++', '-std=c++17', '-I'+str(ROOT/'port/runtime/hle'),
                            '-I'+str(ROOT/'port/runtime/host'), str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_partner_cursor_and_match_input_are_wired(self):
        inc, online = read(INC), read('sourceport/game/shim/mu_online.c')
        self.assertIn('if (mu_lobby_local_owned(&mu_f4.view)) mu_f4_local(css, 1);', inc)
        for op in ('MU_LOBBY_OP_LOCAL_HOVER', 'MU_LOBBY_OP_LOCAL_PICK', 'MU_LOBBY_OP_LOCAL_UNPICK'):
            self.assertIn(op, body(inc, 'static void mu_f4_local('))
        self.assertIn('kind != MU_LOBBY_KIND_LOCAL', body(inc, 'static void mu_f4_bounds('))
        self.assertIn('s->kind == MU_LOBBY_KIND_LOCAL) continue;', body(inc, 'static void mu_f4_host_doors('))
        self.assertIn('mu_f4_p2_claim("tab");', body(inc, 'static int mu_friends4_partner_cycle(u8 door)'))
        self.assertIn('strcpy(p->line[2], "Partner: Player 2");', body(inc, 'static void mu_f4_panel_text('))
        self.assertIn('const int partner_source = mu_lobby_partner_source();', online)
        # Slippi 3.7.0: after the 27-byte TXB, the partner's inputs; its RNG bits byte stays zero.
        self.assertIn('memcpy(tx + TXB_SIZE, mu_online.last_partner, PAD_RNG_BITS);', online)
        self.assertIn('memset(tx + TXB_SIZE, 0, TXB_PARTNER_PAD_SIZE);', online)
        net = read('port/runtime/hle/slippi_net.cpp')
        self.assertIn('port_inputs_->local_partner(*partner);', net)
        self.assertIn('port_inputs_->receive(packet.data(),packet.size(),sender,remote_pad_history_)', net)
        self.assertIn('port_inputs_->packet(local_pad_queue_)', net)
        self.assertIn('out->checksum_primary = port == owner;', net)
        self.assertIn('port_inputs_->local_output(index,frame,max_frame_count)', net)
        runtime = read('port/runtime/hle/slippi_online.cpp')
        self.assertIn('!mu_local_partner_capable(rb.caps2)', runtime)
        self.assertIn('setup_hash_gate(bounded_remote_machine_count()', runtime)
        self.assertIn('lobby_plan->machines != remote_count + 1', runtime)
        self.assertIn('if (g_netplay->IsLocalVirtualPort(i)) continue;', runtime)
        self.assertIn('lobby_session->owns_local() ? 1 : party.humans', read('port/runtime/hle/yougame_link.cpp'))
        self.assertIn('player_idx = uint8_t(port_inputs_->owner[player_idx]);', net)


class OtherScreensUnchanged(unittest.TestCase):
    def test_one_door_online_select_excludes_only_the_lobby(self):
        css = body(read('sourceport/game/shim/mu_slippi_css.c'), 'int mu_slippi_css_online(void)')
        self.assertIn('return mu_slippi_menus_enabled() && mu_slippi_on_online_css() && !mu_lobby_css();', css)
        self.assertIn('return lobby.major && mu_slippi_menus_enabled() && mu_slippi_on_online_css();',
                      body(read(MENU), 'int mu_lobby_css(void)'))

    def test_builds_without_slippi_have_inert_stubs(self):
        stubs = read('sourceport/game/shim/mu_noslippi.c')
        for stub in ('int mu_lobby_css(void) { return 0; }',
                     'const struct MuLobbyView* mu_lobby_read(void) { return NULL; }',
                     'void mu_lobby_css_exit(int where) { (void) where; }',
                     'void mu_lobby_pads(struct PADStatus* status) { (void) status; }'):
            self.assertIn(stub, stubs)

    def test_friends_lobby_keeps_four_doors_and_its_labels(self):
        # Build 91 hides doors 3 and 4 in Doubles only (build 95: doors 1 and 2 never move, in any select); the
        # Friends lobby (4 doors) and every other select get them back through mu_f4_restore, and the
        # Friends open door still reads "Invite".
        inc = read('sourceport/game/include/mu_friends4_css.inc')
        frame = body(inc, 'static void mu_friends4_css_frame(void)')
        self.assertIn('if (mu_f4.view_ok ? mu_f4_doubles() : mu_f4.shut) mu_f4_shut();\n    else mu_f4_unshut();', frame)
        restore = body(inc, 'static void mu_f4_restore(void)')
        self.assertIn('} else {\n        mu_f4_unshut();\n    }', restore)
        self.assertNotIn('centre', inc)
        self.assertIn('static int mu_f4_doubles(void) { return mu_f4.view_ok && (mu_f4.view.mode & MU_LOBBY_M_DOUBLES); }', inc)
        texts = body(inc, 'static void mu_f4_texts(int mine)')
        # Build 95: the hint, rules and door labels stay outside Doubles; build 96: the panel is blank there.
        self.assertIn('const int doubles = v ? (v->mode & MU_LOBBY_M_DOUBLES) != 0 : mu_f4.shut;', texts)
        friends_open = texts.index('"Inviting..." : "Invite"')
        self.assertLess(texts.index('&& !doubles && v->block_hint != MU_LOBBY_HINT_CLOSED) {'), friends_open)
        self.assertIn('if (!doubles) mu_f4_panel(v, 0, hint, noted, "", mine);', texts)
        self.assertIn('} else mu_f4_line(0, hint == MU_LOBBY_HINT_PRESS_START ? "" : mu_lobby_hint_text(hint));', texts)
        panel = body(inc, 'static void mu_f4_panel(const MuLobbyView* v, int doubles')
        self.assertNotIn('mu_f4_line(', panel)   # its own text, never the hint lines
        self.assertLess(panel.index('if (!doubles || mu_lobby_curtain()) {'), panel.index('mu_f4_panel_text('))

    def test_runtime_arms_the_lobby_only_from_the_friends_row(self):
        policy = read('port/runtime/hle/yougame_mode_policy.h')
        self.assertIn('constexpr bool friends_lobby4 = YOUGAME_FRIENDS_LOBBY4 != 0;', policy)


class StageSounds(unittest.TestCase):
    """Build 96 (11-11): picking a stage replayed the select's "choose your character" voice and the stage
    select sound was missing. The lobby passes back through the select (hidden, mu_lobby_curtain) on the way
    to the match, and the VS select (match_type 0) voiced every visit; guests' stage select takes no press, so
    they never played the pick."""

    def test_the_hidden_pass_has_no_voice(self):
        sel = patch_file('src/melee/mn/mncharsel.c')
        self.assertIn('     } else {\n+#ifdef MU_NATIVE\n', sel)
        self.assertIn('+        if (!mu_lobby_curtain())\n+#endif\n         switch (gmMainLib_GetGameRules()->mode) {\n', sel)
        # The Slippi select's own skip stays as it was.
        self.assertIn('+        if (!(mu_slippi_menus_enabled() && mu_slippi_css_skip_return_sound()))\n', sel)
        self.assertIn('int mu_lobby_curtain(void) { return 0; }', read('sourceport/game/shim/mu_noslippi.c'))

    def test_the_curtain_is_up_before_the_select_loads_and_down_after(self):
        decide = body(read(MENU), 'static int lobby_sss_decide(GameModeState* state)')
        self.assertLess(decide.index('if (mu_lobby_send(start, sizeof start)) lobby.curtain = 1;'),
                        decide.index('gm_SetNextGameModeStateId(MU_SLP_STATE_CSS);'))
        self.assertLess(decide.index('lobby.curtain = 1;   /* a guest'), decide.index('gm_SetNextGameModeStateId(MU_SLP_STATE_CSS);'))
        css = body(read(MENU), 'static int lobby_css_decide(GameModeState* state)')
        self.assertIn('lobby.played = 1;\n        lobby.curtain = 0;', css)   # back from the match: the voice, as Melee

    def test_the_stage_pick_sounds_for_everyone(self):
        stage = patch_file('src/melee/mn/mnstagesel.c')
        self.assertIn('+        lbAudioAx_80024030(1);\n+        mu_slippi_sss_run_callback(', stage)   # the picker: sfxForward
        watch = body(read(MENU), 'int mu_lobby_watch_pads(PADStatus* status)')
        sound = watch.index('if (v && (v->flags & MU_LOBBY_F_STARTED)) sfxForward();')
        self.assertLess(watch.index('"friends lobby: the stage select ended, back to the select\\n"'), sound)
        self.assertLess(sound, watch.index('lobby.watching = 2;'))
        self.assertEqual(watch.count('sfxForward();'), 1)   # once: watching is 2 from then on


if __name__ == '__main__':
    unittest.main()
