"""The Doubles home's select (G2): the Friends lobby's select on its 2-seat profile, two doors.

Pins the game side of view.mode MU_LOBBY_M_DOUBLES (sourceport/game/include/mu_friends4_css.inc) and the
menu shim (shim/mu_slippi_menu.c): the Doubles row opens the 2-seat home (E6 value 16) and the runtime arms
Profile::Doubles for it; doors 3 and 4 stay shut and are hidden whole with their name plates (two portraits;
build 96: the partner portrait shows the partner choice and the right-hand panel is Slippi's),
doors 1 and 2 stay where Melee puts them, on the far left, from the first frame (build 95); only seat 0 switches the partner door (open, CPU and
level, Invite); the rules are fixed and the teams are matchmaking's; either player's START searches with
no stage select; while Rooms searches the select is locked and B sends CANCEL. The Friends lobby (4 doors)
keeps every path it had. The hints, the view's mode byte and the session are unit tested in
port/tests/yougame_doubles_lobby_test.cpp.
"""
from pathlib import Path
import re
import shutil
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


INC = 'sourceport/game/include/mu_friends4_css.inc'
MENU = 'sourceport/game/shim/mu_slippi_menu.c'
WIRE = 'port/runtime/abi/mu_lobby_wire.h'


class DoublesRowOpensTheHome(unittest.TestCase):
    def setUp(self):
        self.menu = read(MENU)
        self.online = read('port/runtime/hle/slippi_online.cpp')
        self.practice = read('port/runtime/hle/native_practice.cpp')

    def test_row_sends_value_16_not_the_console_home(self):
        at = self.menu.index('selected_option == MU_SLP_OPT_DOUBLES) {')
        press = self.menu[at:self.menu.index('} else if', at)]
        self.assertIn('u8 open_doubles = 16;', press)
        self.assertNotIn('= 12;', press)
        self.assertIn('mu_slippi_cmd(CMD_YOUGAME_FRIENDS_MENU, &open_doubles, 1, NULL, 0, NULL);', press)
        # Still only in a menu that has the Doubles row.
        self.assertGreater(self.menu.rfind('#ifdef MELEE_DOUBLES', 0, at), self.menu.rfind('#endif', 0, at))

    def test_launcher_row_sends_the_pick_first(self):
        # A game started on its own beside the YouGame app (split layout, the app's channel up): the app
        # layout's branch must not take the press, or value 16 reaches the host without the YouGame pick and
        # is dropped (yougame::session() is false), so the row did nothing.
        app = self.menu.index('selected_option == MU_SLP_OPT_DOUBLES) {')
        head = self.menu[self.menu.rindex('} else if', 0, app):app]
        self.assertIn('ranked_supported && !slippi_submenu', head)
        self.assertNotIn('friends_visible', head)
        launcher = self.menu.index('selected_option == MU_SLP_OPT_DOUBLES) {', app + 1)
        press = self.menu[launcher:self.menu.index('} else if', launcher)]
        self.assertLess(press.index('yougame_query(yg_pick, NULL, 0);'),
                        press.index('mu_slippi_cmd(CMD_YOUGAME_FRIENDS_MENU, &open_doubles, 1, NULL, 0, NULL);'))

    def test_value_16_arms_the_doubles_profile(self):
        self.assertIn('if (payload[0] == 16) {', self.online)
        menu16 = self.online[self.online.index('if (payload[0] == 16) {'):]
        self.assertIn('if (yougame::session()) g_doubles_lobby_request = true;', menu16[:300])
        self.assertLess(self.online.index('if (payload[0] == 16) {'), self.online.index('if (payload[0] == 8'))
        self.assertIn('const bool doubles_host = picker_scene && room.empty() && std::exchange(doubles_press, false);',
                      self.practice)
        self.assertIn('slippi::online::arm_friends_lobby(room, doubles ? yougame::lobby::Profile::Doubles : '
                      'yougame::lobby::Profile::Friends4);', self.practice)

    def test_a_press_is_used_this_tick_or_dropped(self):
        # Review G2 #5: the request is taken once per tick, before any early return, so a press that could
        # not open the home (signing in, not on the menu) never opens one later.
        tick = body(self.practice, 'void tick()')
        # A press made before the menu is safe (its first 120 ticks) is kept up to 3 s on the Online menu
        # (report a405af23): the early returns and leaving the menu still drop it.
        take = 'if (slippi::online::take_doubles_lobby_request()) {'
        self.assertEqual(self.practice.count('take_doubles_lobby_request()'), 1)
        self.assertIn(take, tick)
        self.assertIn('current_major() != 1 || std::chrono::steady_clock::now() - doubles_pressed_at > std::chrono::seconds(3)', tick)
        self.assertLess(tick.index(take), tick.index('if (yougame::console::console_active()) { doubles_pending = false; return; }'))
        self.assertLess(tick.index(take), tick.index('if (friends_lobby.held()) { doubles_pending = false; return; }'))
        # Without identity, the Doubles row says what the Friends row says.
        refused = tick[tick.index('if (doubles_press && picker_scene) {'):]
        refused = refused[:refused.index('doubles_press = false;')]
        self.assertIn('if (!id.ok) {', refused)
        self.assertIn('yougame::friends::events.notice(id.error.empty() ? "Signing in to YouGame..." : id.error, friends_now);', refused)
        self.assertLess(tick.index('if (doubles_press && picker_scene) {'), tick.index('std::exchange(doubles_press, false)'))

    def test_console_home_stays_for_md1(self):
        self.assertIn('if (payload[0] == 12) { if (yougame::active()) yougame::doubles::ui.open("new"); return true; }',
                      self.online)
        self.assertIn('if (yougame::friends::events.doubles_join_pending()) {', self.practice)


class TwoDoors(unittest.TestCase):
    def setUp(self):
        self.inc = read(INC)

    def test_mode_helpers(self):
        self.assertIn('return mu_f4.view_ok && (mu_f4.view.mode & MU_LOBBY_M_DOUBLES);', self.inc)
        self.assertIn('return mu_f4_doubles() && (mu_f4.view.mode & MU_LOBBY_M_SEARCHING);', self.inc)
        self.assertIn('return mu_f4_doubles() ? MU_LOBBY_DOUBLES_SEATS : MU_LOBBY_SEATS;', self.inc)
        self.assertIn('#define MU_LOBBY_DOUBLES_SEATS 2', read(WIRE))

    def test_doors_three_and_four_stay_shut(self):
        bounds = body(self.inc, 'static void mu_f4_bounds(int mine)')
        self.assertIn('const int host = mu_f4_host() && i != mine && i < doors && !locked;', bounds)
        host = body(self.inc, 'static void mu_f4_host_doors(CSSData* css, int mine)')
        self.assertIn('for (i = 0; i < mu_f4_doors_open(); ++i) {', host)
        invite = body(self.inc, 'static void mu_f4_invite(int mine)')
        self.assertIn('if (mu_f4_doubles()) return;', invite)   # the partner tab invites in Doubles
        coin = body(self.inc, 'static int mu_friends4_coin_locked(u8 door)')
        self.assertIn('door < mu_f4_doors_open()', coin)
        texts = body(self.inc, 'static void mu_f4_texts(int mine)')
        self.assertIn('if (s && i != mine && (v->flags & MU_LOBBY_F_KNOWN) && !doubles) {', texts)   # no labels
        # The runtime sends them as open, with no fighter (yougame_lobby_screen.h).
        screen = read('port/runtime/hle/yougame_lobby_screen.h')
        self.assertIn('if (doubles && i >= kDoublesSeats) { d.hover_ch = d.pick_ch = MU_LOBBY_NONE; continue; }', screen)

    def test_partner_door_is_seat_zeros(self):
        # HOST in Doubles is seat 0 (yougame_lobby.h is_host); the switch and level slider ride on it.
        lobby = read('port/runtime/hle/yougame_lobby.h')
        self.assertIn('(doubles() ? my_seat() == 0 : state_.host == my_seat())', lobby)
        host = body(self.inc, 'static void mu_f4_host_doors(CSSData* css, int mine)')
        for step in ('req[0] = MU_LOBBY_OP_SEAT;', 'req[2] = (unsigned char) (cpu ? level : 0);',
                     'req[0] = MU_LOBBY_OP_CPU_PICK;'):
            self.assertIn(step, host)
        texts = body(self.inc, 'static void mu_f4_texts(int mine)')
        # Build 95: no door label in Doubles; the partner's route and ping go to the log, once per route.
        for gone in ('"Finding partner"', '"Open"', '"Find me a partner"', '"Local player"'):
            self.assertNotIn(gone, texts)
        self.assertIn('OSReport("doubles: seat %d link %s %ums\\n", i, mu_lobby_route_text(s->route), (unsigned) s->rtt_ms);', texts)
        self.assertIn('mu_f4.route_logged[i] != s->route + 1', texts)

    def test_rules_are_fixed_and_teams_are_matchmakings(self):
        bounds = body(self.inc, 'static void mu_f4_bounds(int mine)')
        # Build 96: matchmaking still sets who plays whom; the box only picks the pair's colour, on this machine's
        # own doors (doubles_pair_colour_test.py).
        self.assertIn('const int teambox = doubles ? own && !locked && ', bounds)
        only = body(self.inc, 'static int mu_friends4_host_only(void)')
        self.assertLess(only.index('if (mu_f4_doubles()) {'), only.index('if (mu_f4_host()) return 0;'))
        self.assertIn('MU_LOBBY_HINT_FIXED_RULES', only)
        rules = body(self.inc, 'static void mu_f4_rules(CSSData* css)')
        # Doubles: the room's fixed rules (Squad mode changes add their own guards after it).
        self.assertIn('if (mu_f4_host() && !mu_f4_doubles() && ', rules)
        self.assertIn("if ((mu_f4.view.flags & MU_LOBBY_F_HOST) && !mu_f4_doubles() && ", rules)
        texts = body(self.inc, 'static void mu_f4_texts(int mine)')
        self.assertNotIn('Doubles  4 stocks', texts)
        self.assertLess(texts.index('MU_LOBBY_M_DOUBLES'), texts.index('"Coin and Bonus play as Stock"'))

    def test_start_searches_without_a_stage_select(self):
        start = body(self.inc, 'static u32 mu_friends4_css_start(u32 trigger, int ready)')
        dbl = body(start, 'if (mu_f4_doubles())')
        self.assertIn('unsigned char start[2] = { MU_LOBBY_OP_START, 0 };', dbl)
        self.assertIn('if (hint != MU_LOBBY_HINT_START_SEARCH && hint != MU_LOBBY_HINT_START_PARTNER) {', dbl)
        self.assertIn('mu_lobby_send(start, sizeof start)', dbl)
        self.assertNotIn('MU_LOBBY_EXIT_SSS', dbl)
        self.assertNotIn('mu_f4.leaving = 1', dbl)
        self.assertLess(start.index('if (mu_f4_doubles())'), start.index('MU_LOBBY_F_HOST'))   # either player

    def test_search_locks_the_select(self):
        for name, line in (('static int mu_friends4_coin_locked(u8 door)', 'if (mu_f4_searching()) return 1;'),
                           ('static void mu_f4_local(CSSData* css, int mine)', 'if (mu_f4_searching()) return;'),
                           ('static void mu_f4_host_doors(CSSData* css, int mine)',
                            'if (!mu_f4_host() || mu_f4_searching()) return;')):
            self.assertIn(line, body(self.inc, name), name)


class BCancels(unittest.TestCase):
    def test_pads_go_still_and_b_cancels_once(self):
        menu = read(MENU)
        pads = body(menu, 'void mu_lobby_pads(PADStatus* status)')
        search = body(pads, 'if (lobby.view_ok && (lobby.view.mode & MU_LOBBY_M_SEARCHING))')
        self.assertIn('const int b = (mine.button & PAD_BUTTON_B) != 0;', search)
        self.assertIn('if (b && !lobby.cancel_held) {', search)
        self.assertIn('unsigned char cancel = MU_LOBBY_OP_CANCEL;', search)
        self.assertIn('memset(&mine, 0, sizeof mine);', search)
        # A B held into the search never cancels it.
        self.assertIn('lobby.cancel_held = (mine.button & PAD_BUTTON_B) != 0;', pads)
        # The B that cancelled stays off the select until it is let go (it never also unpicks).
        self.assertIn('lobby.cancel_mask = 1;', search)
        rest = pads[pads.index('} else {', pads.index('MU_LOBBY_M_SEARCHING')):]
        self.assertIn('if (!lobby.cancel_held) lobby.cancel_mask = 0;', rest)
        self.assertIn('if (lobby.cancel_mask) mine.button &= (u16) ~PAD_BUTTON_B;', rest)
        self.assertLess(rest.index('lobby.cancel_mask = 0;'), rest.index('mine.button &= (u16) ~PAD_BUTTON_B;'))
        self.assertLess(rest.index('mine.button &= (u16) ~PAD_BUTTON_B;'), rest.index('status[seat] = mine;'))
        self.assertLess(pads.index('MU_LOBBY_M_SEARCHING'), pads.index('status[seat] = mine;'))
        wire = read(WIRE)
        self.assertIn('#define MU_LOBBY_OP_CANCEL 13', wire)
        self.assertIn('case MU_LOBBY_OP_PEEK: case MU_LOBBY_OP_CANCEL: case MU_LOBBY_OP_LOCAL_UNPICK: return 1;', wire)
        self.assertIn('case MU_LOBBY_OP_CANCEL: {', read('port/runtime/hle/slippi_online.cpp'))


class HomeSearchAndMatchRoom(unittest.TestCase):
    def setUp(self):
        link = read('port/runtime/hle/yougame_link.cpp')
        self.fm = link[link.index('Match find_match('):]

    def test_a_queue_refusal_also_cancels_the_home(self):
        # Review G2 #4: search-error / error on the queue socket stops the home's search too.
        refused = self.fm[self.fm.index('else if (news.news == lobby::QueueNews::Refused) {'):]
        refused = refused[:refused.index('}')]
        self.assertIn('queue_socket.reset();', refused)
        self.assertIn('lobby_session->cancel();', refused)
        self.assertLess(refused.index('queue_socket.reset();'), refused.index('lobby_session->cancel();'))

    def test_the_match_report_says_playing_once_planned(self):
        # Review G2 #2: casual/assembling, then one more report as playing once the setup is planned.
        room = self.fm[self.fm.index('if (lobby_match && lobby_session && !lobby_route) {'):]
        room = room[:room.index('if (lobby_route && lobby_session) {')]
        self.assertIn('if ((room->revision != lobby_reported || (lobby_plan && !doubles_playing_reported)) && !lobby_me.empty()) {', room)
        self.assertIn('doubles_playing_reported = lobby_plan != nullptr;', room)
        self.assertIn('lobby_me, room->revision, lobby_plan != nullptr), 1000);', room)

    def test_setup_timeout_closes_without_a_leave(self):
        room = self.fm[self.fm.index('if (!lobby_plan && now_ms() - doubles_since > lobby::kDoublesSetupMs) {'):]
        room = room[:room.index('return fail(lobby::kDoublesNoSetup);')]
        self.assertIn('room->socket->close();', room)
        self.assertNotIn('"leave"', room)
        guard = self.fm[self.fm.index('struct LobbyLeave {'):]
        self.assertIn('if (!session || !room.socket || room.socket->closed()) return;', guard[:guard.index('} lobby_leave')])

    def test_the_teammate_link_closes_at_match_found_and_says_why(self):
        found = self.fm[self.fm.index('host::log("doubles: matchmaking placed the home; joining the match");'):]
        found = found[:found.index('Search next = search;')]
        self.assertIn("The teammate's link can't carry over", found)
        self.assertIn('early.links.forget_all(close_app);', found)


class TwoPortraits(unittest.TestCase):
    """Build 91: Doubles shows two portraits. Doors 3 and 4 are hidden whole, their name plates too, and come
    back in Friends and every other select. Build 96 (11-11 report: "I told you I didn't want plain text like
    that ... We should have the same unranked like text in the same spot for doubles"): no banner and no hint,
    door label or rules line in Doubles; the right-hand panel is Slippi's online select's (mu_slippi_css.c),
    same places, sizes, colours and spinners, written only on a change."""

    def setUp(self):
        self.inc = read(INC)

    def test_doors_three_and_four_are_hidden_whole(self):
        shut = body(self.inc, 'static void mu_f4_shut(void)')
        self.assertIn('for (i = MU_LOBBY_DOUBLES_SEATS; i < 4; ++i) {', shut)
        for part in ('door->bg_joint', 'door->emblem_joint', 'door->costume_joint', 'door->team_joint',
                     'door->door_joint', 'door->player_indicator_joint', 'door->slidername_joint',
                     'data2.ko_stars[i].joint', 'tag->x4',
                     'tag->list_joint', 'tag->name_jointl', 'tag->x7', 'tag->kostar_text_joint',
                     'door->cpuslider_joint', 'door->cpuslider2_joint'):
            self.assertIn('joints[n++] = %s;' % part, shut)
        self.assertIn('u8 joints[15];', shut)
        self.assertNotIn('MU_F4_PLATE_SPAN', self.inc)   # build 95's guessed ids are gone
        self.assertIn('lb_80011E24(mnCharSel_804D6CC0, &jobj, joints[k], -1);', shut)
        # Each joint of each part's tree (the display walk draws a hidden joint's children).
        tree = body(self.inc, 'static void mu_f4_shut_tree(HSD_JObj* root)')
        self.assertIn('HSD_JObjSetFlags(j, JOBJ_HIDDEN);', tree)
        self.assertIn('for (c = HSD_JObjGetChild(j); c != NULL; c = HSD_JObjGetNext(c)) {', tree)
        self.assertIn('if (n < 16) {', tree)
        # Build 92: each cap says so once a visit (the joint list, the tree's width, the text list).
        self.assertIn('OSReport("doubles: joint hide list full (%d); later joints are not kept\\n", MU_F4_SHUT_MAX);', tree)
        self.assertIn('OSReport("doubles: a door part\'s joint tree is wider than 16; some joints stay shown\\n");', tree)
        self.assertIn('} else if (!(mu_f4.cap_logged & 1)) {', tree)
        self.assertIn('} else if (!(mu_f4.cap_logged & 2)) {', tree)
        text = body(self.inc, 'static void mu_f4_shut_text(HSD_Text* text)')
        self.assertIn('} else if (k == mu_f4.shut_texts && !(mu_f4.cap_logged & 4)) {', text)
        self.assertIn('text->hidden = 1;', text[text.rindex('}', 0, text.rindex('text->hidden = 1;')):])
        self.assertIn('mu_f4.shut_was[mu_f4.shut_count] = (HSD_JObjGetFlags(j) & JOBJ_HIDDEN) ? 1 : 0;', tree)
        # Every frame again: the door's redraw shows its team mark and name tag.
        self.assertIn('for (k = 0; k < mu_f4.shut_count; ++k) HSD_JObjSetFlags(mu_f4.shut_jobj[k], JOBJ_HIDDEN);', shut)
        for text in ('mu_f4_shut_text(data2.ko_stars[i].text);', 'mu_f4_shut_text(tag->text);',
                     'mu_f4_shut_text(tag->name_ls);', 'mu_f4_shut_text(mu_te_shield_text[i]);'):
            self.assertIn(text, shut)
        self.assertIn('OSReport("doubles: two-door select (doors 3 and 4 hidden, %d joints; their name plates %s)\\n",', shut)
        self.assertIn('if (!mu_f4.shut_logged) {', shut)
        # Build 96 (report: "get rid of those two empty text box controller slots"): the plates, every frame.
        self.assertIn('if (mu_f4.plates == 0) mu_f4_plates_find();', shut)
        self.assertIn('for (k = 0; k < 2; ++k) mu_f4.plate[k]->flags |= DOBJ_HIDDEN;', shut)

    def test_hidden_from_the_first_frame_and_after_the_redraws(self):
        frame = body(self.inc, 'static void mu_friends4_css_frame(void)')
        at = frame.index('if (mu_f4.view_ok ? mu_f4_doubles() : mu_f4.shut) mu_f4_shut();')
        # After this frame's door redraws and texts, and on OnEnter's call (no frame count gate).
        self.assertGreater(at, frame.index('mu_f4_doors(css, mine);'))
        self.assertGreater(at, frame.index('mu_f4_texts(mine);'))
        self.assertIn('else mu_f4_unshut();', frame[at:])
        rules = frame[frame.index('if (mnCharSel_804D6CF6 >= 3) {'):]
        self.assertIn('mu_f4_shut_forget();', rules[:rules.index('return;')])

    def test_friends_and_every_other_select_get_them_back(self):
        restore = body(self.inc, 'static void mu_f4_restore(void)')
        self.assertIn('if (mnCharSel_804D6CF6 >= 3) {\n        mu_f4_shut_forget();', restore)
        self.assertIn('} else {\n        mu_f4_unshut();\n    }', restore)
        frame = body(self.inc, 'static void mu_friends4_css_frame(void)')
        self.assertLess(frame.index('mu_f4_restore();'), frame.index('mu_lobby_read()'))
        unshut = body(self.inc, 'static void mu_f4_unshut(void)')
        self.assertIn('if (!mu_f4.shut_was[k]) HSD_JObjClearFlags(mu_f4.shut_jobj[k], JOBJ_HIDDEN);', unshut)
        self.assertIn('mu_f4.shut_text[k]->hidden = mu_f4.shut_text_was[k];', unshut)
        self.assertIn('if (!mu_f4.plate_was[k]) mu_f4.plate[k]->flags &= ~(unsigned) DOBJ_HIDDEN;', unshut)
        self.assertIn('mnCharSel_8025DB34((u8) i);', unshut)
        # Every build makes new joints: nothing kept from the last one.
        build = body(self.inc, 'static void mu_friends4_css_build(void)')
        tail = build[build.rindex('mu_f4.curtain_made'):]
        self.assertIn('mu_f4.shut = 0;', tail)
        self.assertIn('mu_f4.plates = 0', build)

    def test_door_labels_outside_doubles_only(self):
        texts = body(self.inc, 'static void mu_f4_texts(int mine)')
        order = ['&& !doubles) {', '"Inviting..." : "Invite"', 'sprintf(buf, "CPU Lv %u", (unsigned) s->level);',
                 'else if (s->route != MU_LOBBY_ROUTE_NONE)']
        at = [texts.index(x) for x in order]
        self.assertEqual(at, sorted(at))
        self.assertIn('if (!doubles) mu_f4_panel(v, 0, hint, noted, "", mine);', texts)

    def test_doubles_writes_the_slippi_panel_and_no_banner(self):
        texts = body(self.inc, 'static void mu_f4_texts(int mine)')
        doubles = texts[texts.index('    if (doubles) {\n        const char* note = "";'):texts.index('    } else\n#if MU_SQUAD')]
        self.assertIn('mu_f4_line(0, "");', doubles)
        self.assertIn('note = mu_f4.note_text;   /* why the last setup fell through, 4 s */', doubles)
        self.assertIn('mu_f4_panel(v, 1, hint, noted, note, mine);', doubles)
        self.assertIn('mu_f4_line(1, "");   /* Doubles\' rules are fixed, so nothing to say here */', texts)
        # A stopped search: its note once (2 s), then the steady line.
        self.assertIn('if (doubles && v && v->block_hint == MU_LOBBY_HINT_SEARCH_STOPPED && !mu_f4.stopped_seen)', texts)
        self.assertLess(texts.index('noted = mu_f4.note_frames > 0;'), texts.index('--mu_f4.note_frames;'))
        for gone in ('mu_f4_banner', 'MU_F4_BANNER', 'banner_drawn'):
            self.assertNotIn(gone, self.inc, gone)
        make = body(self.inc, 'static void mu_f4_text_make(void)')
        self.assertIn('mu_f4_panel_make();', make)
        # Every build frees the texts: the panel is made anew and drawn again.
        build = body(self.inc, 'static void mu_friends4_css_build(void)')
        self.assertIn('mu_f4.text = mu_f4.panel = NULL;', build)
        self.assertIn('memset(mu_f4.panel_drawn, 0, sizeof mu_f4.panel_drawn);', build)
        frame = body(self.inc, 'static void mu_friends4_css_frame(void)')
        rules = frame[frame.index('if (mnCharSel_804D6CF6 >= 3) {'):]
        self.assertIn('mu_f4.text = mu_f4.panel = NULL;', rules[:rules.index('return;')])

    def test_the_panel_is_slippis_own_layout(self):
        # Same subtexts in the same order, places and sizes as the online select's mu_slippi_css_text_init.
        slippi = read('sourceport/game/shim/mu_slippi_css.c')
        init = body(slippi, 'void mu_slippi_css_text_init(void)')
        init = init[:init.index('#if MU_SQUAD')]
        theirs = re.findall(r'create_subtext\(t, &color_\w+, ([-\d.]+f), ([-\d.]+f), ([^)]+)\);', init)
        make = body(self.inc, 'static void mu_f4_panel_make(void)')
        ours = re.findall(r'mu_f4_panel_sub\(([-\d.]+f), ([-\d.]+f), ([^)]+)\);', make)
        self.assertEqual(len(theirs), 8)   # in the source: the lines and errors are loops
        self.assertEqual(ours, theirs)
        for table in ('static const f32 line_y[3] = { 52.0f, 75.0f, 98.0f };',
                      'static const f32 err_y[4] = { 52.0f, 70.0f, 88.0f, 106.0f };'):
            self.assertIn(table, init)
            self.assertIn(table.replace('f32', 'float'), make)
        self.assertIn('t->default_alignment = 0;', make)
        self.assertIn('t->font_size.x = 0.1f;', make)
        for theirs_line, ours_line in (
                ('static const GXColor color_gray = { 0x8E, 0x91, 0x96, 0xFF };',
                 'static const GXColor mu_f4_gray = { 0x8E, 0x91, 0x96, 0xFF };'),
                ('static const GXColor color_red = { 0xFF, 0x00, 0x00, 0xFF };',
                 'static const GXColor mu_f4_red = { 0xFF, 0x00, 0x00, 0xFF };'),
                ('static const GXColor spinner_done_color = { 0x33, 0xFF, 0x2F, 0xFF };',
                 'static const GXColor mu_f4_spin_done = { 0x33, 0xFF, 0x2F, 0xFF };'),
                ('static const GXColor spinner_wait_color = { 0x3C, 0xBC, 0xFF, 0xFF };',
                 'static const GXColor mu_f4_spin_wait = { 0x3C, 0xBC, 0xFF, 0xFF };'),
                ('static const char spinner_str[2][3] = { { (char) 0x81, 0x7B, 0 }, { (char) 0x81, 0x7E, 0 } };',
                 'static const char mu_f4_spin_str[2][3] = { { (char) 0x81, 0x7B, 0 }, { (char) 0x81, 0x7E, 0 } };'),
                ('static const char spinner_done_str[3] = { (char) 0x81, 0x7C, 0 };',
                 'static const char mu_f4_spin_done_str[3] = { (char) 0x81, 0x7C, 0 };')):
            self.assertIn(theirs_line, slippi)
            self.assertIn(ours_line, self.inc)
        self.assertIn('SPINNER_TRANSITION_FRAMES = 15,', slippi)
        self.assertIn('#define MU_F4_SPIN_FRAMES 15', self.inc)
        self.assertIn('} else if (line_len > 30) {', slippi)
        self.assertIn('#define MU_F4_PANEL_WRAP 30', self.inc)

    def test_panel_lines_fit_the_font_and_30_letters(self):
        text = body(self.inc, 'static void mu_f4_panel_text(')
        lines = re.findall(r'strcpy\(p->(?:line\[\d\]|error), "([^"]*)"\);', text)
        lines += re.findall(r'p->gray = "([^"]*)";', text)
        self.assertGreaterEqual(len(lines), 20)
        for line in lines:
            if line:
                self.assertRegex(line, r"^[A-Za-z0-9 .:,'-]{1,30}$", line)
        for exact in ('Version mismatch', 'Lobby closed. Hold B to exit', 'Try again', 'Searching for opponents',
                      'Press START to search', 'Select your character', 'Character selected', 'Match found',
                      'Waiting for players', 'Search stopped', 'Partner: find online', 'Inviting a friend',
                      'Partner: Player 2', 'Partner: CPU'):
            self.assertIn(exact, lines)
        self.assertIn('sprintf(p->line[2], "Partner: %s", name);', text)
        self.assertIn('#define MU_LOBBY_NAME 16', read(WIRE))   # 9 + 15 letters: within 30
        copy = body(self.inc, 'static void mu_f4_panel_copy(')
        self.assertIn("c == ':' || c == '-' || c == ',' || c == '\\''", copy)

    def test_partner_door_log_once_per_change(self):
        panel = body(self.inc, 'static void mu_f4_panel(const MuLobbyView* v, int doubles')
        self.assertIn('if (mu_f4.partner_logged != state + 1) {', panel)
        self.assertIn('OSReport("doubles: partner door %s\\n", mu_f4_partner_names[state]);', panel)
        self.assertEqual(self.inc.count('{ "find", "invite", "cpu", "local", "friend" }'), 1)
        state = body(self.inc, 'static int mu_f4_partner_state(const MuLobbyView* v, int mine)')
        self.assertIn('&v->seats[mine == 1 ? 0 : 1]', state)
        self.assertIn('(v->flags & MU_LOBBY_F_INVITING) ? MU_F4_P_INVITE : MU_F4_P_FIND', state)


class PartnerTagAndCaption(unittest.TestCase):
    """Build 96.1: on the host's view the partner door's tag says the choice (FIND, FRIEND, P2; CPU is Melee's
    own) and a plate under the Find / Invite portrait says it in words. The words are mu_f4_partner_tag
    (the harness above); these pin how they are drawn and kept."""

    def setUp(self):
        self.inc = read(INC)

    def test_drawn_from_the_name_text_on_the_art_canvas(self):
        marks = body(self.inc, 'static void mu_f4_partner_marks(int mine)')
        self.assertIn('const CSSTagData* name = mnCharSel_803F0E8C[1].data;', marks)
        self.assertIn('tag = mu_f4_partner_tag(&mu_f4.view, mine, mu_lobby_partner_want(), &caption);', marks)
        self.assertIn('door->p_kind == Gm_PKind_Cpu && strcmp(tag, "CPU") == 0) tag = NULL;', marks)
        self.assertIn('if (mu_f4.art == MU_F4_ART_NONE) caption = NULL;', marks)
        self.assertIn('t->render_callback = mu_f4_caption_draw;', marks)
        self.assertIn('t->bg_color = door->p_kind == Gm_PKind_NA', marks)
        self.assertIn('mu_f4_marks_log("tag", tag, ', marks)
        self.assertIn('mu_f4_marks_log("plate", caption, ', marks)
        make = body(self.inc, 'static HSD_Text* mu_f4_marks_make(void)')
        self.assertIn('HSD_SisLib_803A6754(0, 0)', make)
        self.assertIn('t->x4C = 0;', make)
        log = body(self.inc, 'static void mu_f4_marks_log(')
        self.assertIn('OSReport("doubles: partner %s \\"%s\\"\\n", what, words);', log)

    def test_kept_with_the_screens_texts(self):
        # Each build and the Rules overlay free the texts; another select hides them; the frame draws them last.
        build = body(self.inc, 'static void mu_friends4_css_build(void)')
        self.assertIn('mu_f4_marks_off(1);', build)
        restore = body(self.inc, 'static void mu_f4_restore(void)')
        self.assertIn('mu_f4_marks_off(mnCharSel_804D6CF6 >= 3);', restore)
        frame = body(self.inc, 'static void mu_friends4_css_frame(void)')
        self.assertIn('mu_f4.text = mu_f4.panel = NULL;\n        mu_f4_marks_off(1);', frame)
        self.assertLess(frame.index('mu_f4_art_frame(mine);'), frame.index('mu_f4_partner_marks(mine);'))
        self.assertLess(frame.index('mu_f4_texts(mine);'), frame.index('mu_f4_partner_marks(mine);'))


class PartnerPortrait(unittest.TestCase):
    """Build 96 (11-11 report: "I want the character portrait to change to toggle between the different
    options"): the partner door's portrait is the choice. P2 and CPU are Melee's own doors (a person's, from
    the LOCAL_SEAT claim; Melee's CPU door); Find and Invite draw baked art (tools/bake_doubles_partner_art.py,
    simple shapes, no game pixels) in the portrait's place above the name plate, only on the host's screen
    while the seat is empty."""

    def setUp(self):
        self.inc = read(INC)

    def test_the_art_header_is_current(self):
        result = subprocess.run(['python3', '-I', str(ROOT / 'tools/bake_doubles_partner_art.py'), '--check'],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        header = read('sourceport/game/include/mu_doubles_partner_art.h')
        self.assertIn('#define MU_DPA_W 48', header)
        self.assertIn('#define MU_DPA_H 48', header)
        self.assertIn('MU_DPA_ALIGN static const unsigned char mu_dpa_find[9216] = {', header)
        self.assertIn('MU_DPA_ALIGN static const unsigned char mu_dpa_invite[9216] = {', header)

    def test_find_and_invite_only_on_the_hosts_empty_seat(self):
        frame = body(self.inc, 'static void mu_f4_art_frame(int mine)')
        self.assertIn('const CSSDoor* door = &mnCharSel_803F0DFC.doors[1];', frame)
        self.assertIn("mine == 0 && v->seats[1].kind == MU_LOBBY_KIND_OPEN &&\n        door->p_kind == Gm_PKind_NA", frame)
        self.assertIn('MU_F4_ART_INVITE', frame)
        self.assertIn('MU_F4_ART_FIND', frame)
        self.assertIn('MU_LOBBY_F_INVITING', frame)
        self.assertIn('OSReport("doubles: partner portrait %s\\n"', frame)
        draw = body(self.inc, 'static void mu_f4_art_draw(')
        for call in ('GXInitTexObj(', 'GX_TF_RGBA8', 'GXLoadTexObj(', 'GXBegin(GX_QUADS'):
            self.assertIn(call, draw)
        self.assertIn('#include <mu_doubles_partner_art.h>', self.inc)

    def test_p2_is_claimed_by_the_tab(self):
        cycle = body(self.inc, 'static int mu_friends4_partner_cycle(u8 door)')
        self.assertIn('if (next == MU_LOBBY_WANT_P2 && s->kind == MU_LOBBY_KIND_OPEN) mu_f4_p2_claim("tab");', cycle)
        claim = body(self.inc, 'static int mu_f4_p2_claim(const char* why)')
        self.assertIn('const unsigned char claim[2] = { MU_LOBBY_OP_LOCAL_SEAT, 1 };', claim)


class TwoDoorsOnTheLeft(unittest.TestCase):
    """Build 95 (report e22413bd: "2 potraits should be all the way on the left ... The UI shouldn't change
    after the lobby is created"): no centring. Doors 1 and 2, their hit areas, texts and hands stay where
    Melee puts them; build 92's move is gone."""

    def setUp(self):
        self.inc = read(INC)

    def test_nothing_moves_the_doors(self):
        for gone in ('mu_f4_centre', 'mu_f4_uncentre', 'mu_f4_dx', 'mu_f4_labels', 'centre_dx', 'centred',
                     'MU_F4_CENTRE_', 'label_put', 'HSD_JObjSetTranslateX', 'HSD_SisLib_803A746C'):
            self.assertNotIn(gone, self.inc, gone)

    def test_hit_areas_are_melees_own(self):
        bounds = body(self.inc, 'static void mu_f4_bounds(int mine)')
        self.assertIn('if (mu_f4.toggle[i][0] == mu_f4.toggle[i][1] && door->togglebtn_left != door->togglebtn_right) {',
                      bounds)
        self.assertIn('door->togglebtn_left = toggle ? mu_f4.toggle[i][0] : 0;', bounds)
        self.assertIn('door->teambtn_right = teambox ? mu_f4.teambox[i][1] : 0;', bounds)
        self.assertIn('mu_f4.toggle_put[i][0] = door->togglebtn_left;', bounds)
        over = body(self.inc, 'static int mu_f4_over_door(int i, float x, float y)')
        self.assertIn('float left = mu_f4.toggle[i][0], width = 0;', over)
        # Leaving: a bound goes back where it is empty or still what this code wrote, never another select's.
        restore = body(self.inc, 'static void mu_f4_restore(void)')
        self.assertIn('door->togglebtn_left == mu_f4.toggle_put[i][0] && door->togglebtn_right == mu_f4.toggle_put[i][1]',
                      restore)
        self.assertIn('door->teambtn_left == mu_f4.teambox_put[i][0] && door->teambtn_right == mu_f4.teambox_put[i][1]',
                      restore)
        # A new visit keeps Melee's bounds if the last one never got to put them back.
        build = body(self.inc, 'static void mu_friends4_css_build(void)')
        self.assertIn('toggle_put', build)

    def test_the_plates_are_found_not_guessed(self):
        # Build 95 dumped the tree to find the plates and hid guessed ids; build 96 read MnSlChr off the disc:
        # one joint (0x55) holds all four plates, one shape a door. It is used only when it is that joint.
        for gone in ('mu_f4_jobj_dump', 'TODO(doubles plates)', 'MU_F4_PLATE_SPAN'):
            self.assertNotIn(gone, self.inc, gone)
        self.assertIn('#define MU_F4_PLATES_JOINT 0x55', self.inc)
        find = body(self.inc, 'static void mu_f4_plates_find(void)')
        self.assertIn('HSD_JObjGetParent(plates) != HSD_JObjGetParent(slider)', find)
        self.assertIn('(HSD_JObjGetFlags(plates) & JOBJ_INSTANCE)', find)
        self.assertIn('if (n == MU_F4_PLATES) return;   /* more than four: not the plates */', find)
        self.assertIn('if (n != MU_F4_PLATES) return;', find)
        self.assertIn('mu_f4.plate[k] = shape[MU_LOBBY_DOUBLES_SEATS + k];', find)

    def test_a_on_the_door_body_invites_only_outside_doubles(self):
        # Build 95: in Doubles the partner tab's Invite (and START) opens the friend list, so A on the door's body
        # does nothing there; the Friends lobby keeps its A-on-an-open-door invite.
        invite = body(self.inc, 'static void mu_f4_invite(int mine)')
        self.assertLess(invite.index('if (mu_f4_doubles()) return;'), invite.index('MU_LOBBY_OP_INVITE'))
        self.assertNotIn('opened no invite', self.inc)
        self.assertIn('for (i = 0; why == NULL && i < MU_LOBBY_SEATS; ++i) {', invite)


class TwoPortraitsHarness(unittest.TestCase):
    """The shut (doors 3 and 4 with their name plates), the way back and the panel, compiled from the select's
    own code against a stub joint tree laid out like MnSlChr's (each door part's group and X, the plates joint
    with its four shapes) and a stub sislib that keeps each subtext."""

    @unittest.skipUnless(shutil.which('cc'), 'needs a C compiler')
    def test_hide_restore_plates_and_panel(self):
        inc = read(INC)
        defines = '\n'.join(re.findall(r'^#define MU_F4_(?:LINES|NOTE|SHUT_MAX|SHUT_TEXTS|PANEL_\w+|ST_\w+|SPIN_FRAMES) .*$',
                                       inc, re.M))
        shut = inc[inc.index('static void mu_f4_shut_text(HSD_Text* text)'):inc.index('/* Any other select gets')]
        make = inc[inc.index('/* Doubles: the right-hand panel, made as'):inc.index('static void mu_f4_text_make(void)')]
        panel = inc[inc.index('/* The Doubles partner door, as the log names it. */'):inc.index('static void mu_f4_texts(int mine)')]
        source = HARNESS.replace('@DEFINES@', defines).replace('@SHUT@', shut).replace('@MAKE@', make)
        source = source.replace('@PANEL@', panel)
        with tempfile.TemporaryDirectory(prefix='doubles-two-doors-') as d:
            c, exe = Path(d) / 'two_doors.c', Path(d) / 'two_doors'
            c.write_text(source)
            built = subprocess.run(['cc', '-std=c11', '-Wall', '-Werror', '-Wno-unused-function',
                                    '-I' + str(ROOT / 'port/runtime/abi'), str(c), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr + run.stdout)


HARNESS = r"""#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "mu_lobby_wire.h"
typedef unsigned char u8;
#define JOBJ_HIDDEN (1 << 4)
#define JOBJ_INSTANCE (1 << 12)
#define DOBJ_HIDDEN (1 << 0)
@DEFINES@
typedef struct { unsigned char r, g, b, a; } GXColor;
typedef struct HSD_DObj { struct HSD_DObj* next; void* mobj; unsigned flags; } HSD_DObj;
typedef struct HSD_JObj { struct HSD_JObj *child, *next, *parent; unsigned flags; HSD_DObj* dobj; } HSD_JObj;
static HSD_DObj* HSD_JObjGetDObj(HSD_JObj* j) { return j->dobj; }
typedef struct HSD_Text { int hidden; int default_kerning, default_alignment; float pos_z; struct { float x, y; } font_size; } HSD_Text;
static unsigned HSD_JObjGetFlags(HSD_JObj* j) { return j->flags; }
static void HSD_JObjSetFlags(HSD_JObj* j, unsigned f) { j->flags |= f; }
static void HSD_JObjClearFlags(HSD_JObj* j, unsigned f) { j->flags &= ~f; }
static HSD_JObj* HSD_JObjGetChild(HSD_JObj* j) { return j ? j->child : NULL; }
static HSD_JObj* HSD_JObjGetNext(HSD_JObj* j) { return j ? j->next : NULL; }
static HSD_JObj* HSD_JObjGetParent(HSD_JObj* j) { return j ? j->parent : NULL; }
typedef struct { u8 emblem_joint, costume_joint, team_joint, door_joint, bg_joint, player_indicator_joint, slidername_joint,
    cpuslider_joint, cpuslider2_joint; } CSSDoor;
static struct { CSSDoor doors[4]; } mnCharSel_803F0DFC = { { { 0x2E, 0x33, 0x38, 0x85, 0x29, 0xA6, 0x3D, 0x41, 0x40 },
    { 0x2F, 0x34, 0x39, 0x8D, 0x2A, 0xA8, 0x43, 0x47, 0x46 }, { 0x30, 0x35, 0x3A, 0x95, 0x2B, 0xAA, 0x49, 0x4D, 0x4C },
    { 0x31, 0x36, 0x3B, 0x9D, 0x2C, 0xAC, 0x4F, 0x53, 0x52 } } };
typedef struct { HSD_Text *text, *name_ls; } CSSTagData;
static HSD_Text texts[20];
static CSSTagData tags[4] = { { &texts[0], &texts[1] }, { &texts[2], &texts[3] }, { &texts[4], &texts[5] }, { &texts[6], &texts[7] } };
struct CSSTag { CSSTagData* data; u8 x4, list_joint, name_jointl, x7, kostar_text_joint; };
static struct CSSTag mnCharSel_803F0E8C[4] = { { &tags[0], 0x70, 0x73, 0x74, 0x72, 0x71 },
    { &tags[1], 0x75, 0x78, 0x79, 0x77, 0x76 }, { &tags[2], 0x7A, 0x7D, 0x7E, 0x7C, 0x7B },
    { &tags[3], 0x7F, 0x82, 0x83, 0x81, 0x80 } };
static struct { struct { HSD_Text* text; float x4; u8 joint; } ko_stars[4]; } data2 = { { { &texts[8], 0, 0x57 },
    { &texts[9], 0, 0x5D }, { &texts[10], 0, 0x63 }, { &texts[11], 0, 0x69 } } };
static HSD_Text* mu_te_shield_text[4] = { NULL, NULL, &texts[12], NULL };
static HSD_JObj joints[256];
static HSD_JObj* mnCharSel_804D6CC0 = &joints[0];
/* MnSlChr's walk ends at 0xD0 here: past it there is no joint. */
static void lb_80011E24(HSD_JObj* root, HSD_JObj** out, int idx, int any) { (void) root; (void) any; *out = idx < 0xD0 ? &joints[idx] : NULL; }
static int mu_friends_css_frames = 2, redraws[4];
static void mnCharSel_8025DB34(u8 i) { ++redraws[i]; }
static char log_text[65536];
static void OSReport(const char* f, ...) { va_list a; va_start(a, f); vsnprintf(log_text + strlen(log_text), sizeof log_text - strlen(log_text), f, a); va_end(a); }
static int count(const char* s) { int n = 0; const char* p = log_text; while ((p = strstr(p, s))) { ++n; ++p; } return n; }
static struct { int shut, shut_count, shut_texts; HSD_JObj* shut_jobj[MU_F4_SHUT_MAX]; unsigned char shut_was[MU_F4_SHUT_MAX];
    HSD_Text* shut_text[MU_F4_SHUT_TEXTS]; unsigned char shut_text_was[MU_F4_SHUT_TEXTS]; int shut_logged;
    unsigned char partner_logged; int cap_logged;
    HSD_DObj* plate[2]; unsigned plate_was[2]; int plates;
    HSD_Text* panel; char panel_drawn[MU_F4_PANEL_SUBS][40]; GXColor panel_color[MU_F4_PANEL_SUBS];
    unsigned panel_draws, panel_frame; } mu_f4;
enum { MU_LOBBY_WANT_FIND = 0, MU_LOBBY_WANT_INVITE = 1, MU_LOBBY_WANT_P2 = 2, MU_LOBBY_WANT_CPU = 3 };
static int curtain, want_v;
static int mu_lobby_curtain(void) { return curtain; }
static int mu_lobby_partner_want(void) { return want_v; }
/* The panel names the player's own keys; here no keyboard is bound, so the controller words stay. */
#ifndef HSD_PAD_B
#define HSD_PAD_B 0x200
#endif
#ifndef HSD_PAD_START
#define HSD_PAD_START 0x1000
#endif
static int gm_801677F0(void) { return 0; }
static int mu_host_button_label(int port, unsigned mask, char* out, int n) { (void) port; (void) mask; (void) out; (void) n; return 0; }
/* sislib: one text, each subtext's place, size, colour and words. */
static HSD_Text panel_text;
static int subs, made;
static float sub_x[32], sub_y[32], sub_scale[32];
static GXColor sub_color[32];
static char sub_words[32][64];
static HSD_Text* HSD_SisLib_803A6754(int a, int b) { (void) a; (void) b; ++made; return &panel_text; }
static int HSD_SisLib_803A6B98(HSD_Text* t, float x, float y, const char* s)
{ (void) t; sub_x[subs] = x; sub_y[subs] = y; snprintf(sub_words[subs], sizeof sub_words[subs], "%s", s); return subs++; }
static void HSD_SisLib_803A7548(HSD_Text* t, int i, float x, float y) { (void) t; (void) y; sub_scale[i] = x; }
static void HSD_SisLib_803A74F0(HSD_Text* t, int i, GXColor* c) { (void) t; sub_color[i] = *c; }
static void HSD_SisLib_803A70A0(HSD_Text* t, int i, const char* f, ...)
{ va_list a; (void) t; va_start(a, f); vsnprintf(sub_words[i], sizeof sub_words[i], f, a); va_end(a); }
#define NEAR(a, b) ((a) - (b) < 0.001f && (b) - (a) < 0.001f)
/* A group joint under the screen's root with its children. */
static void group(int g, const int* kids, int n)
{
    int k;
    joints[g].parent = &joints[0];
    for (k = 0; k < n; ++k) {
        joints[kids[k]].parent = &joints[g];
        if (k == 0) joints[g].child = &joints[kids[k]]; else joints[kids[k - 1]].next = &joints[kids[k]];
    }
}
@SHUT@
@MAKE@
@PANEL@
/* One state: the three lines with their spinners (0 none, 1 waiting, 2 done), the gray row and the error. */
static void check(const MuLobbyView* v, unsigned hint, int noted, int want, const char* note,
                  const char* l0, int s0, const char* l1, int s1, const char* l2, int s2, const char* gray, const char* err)
{
    MuF4Panel p;
    int i;
    mu_f4_panel_text(v, hint, noted, want, note, &p);
    if (strcmp(p.line[0], l0) || p.spin[0] != s0 || strcmp(p.line[1], l1) || p.spin[1] != s1 ||
        strcmp(p.line[2], l2) || p.spin[2] != s2 || strcmp(p.gray, gray) || strcmp(p.error, err)) {
        fprintf(stderr, "hint %u noted %d want %d: [%s %d] [%s %d] [%s %d] gray [%s] error [%s]\n", hint, noted, want,
                p.line[0], p.spin[0], p.line[1], p.spin[1], p.line[2], p.spin[2], p.gray, p.error);
        assert(0);
    }
    for (i = 0; i < 3; ++i) assert(strlen(p.line[i]) <= MU_F4_PANEL_WRAP);
    assert(strlen(p.gray) <= MU_F4_PANEL_WRAP);
}
#define LINES(l0, s0, l1, s1, l2, s2) l0, s0, l1, s1, l2, s2
int main(void)
{
    MuLobbyView v;
    HSD_DObj plates[4] = { { &plates[1], NULL, 0 }, { &plates[2], NULL, 0 }, { &plates[3], NULL, 0 }, { NULL, NULL, DOBJ_HIDDEN } };
    unsigned draws;
    int j;
    /* MnSlChr's groups. 0x3C holds each door's name slider and, last, the plates joint 0x55 (four shapes). */
    { const int k28[] = { 0x29, 0x2A, 0x2B, 0x2C }; group(0x28, k28, 4); }
    { const int k2d[] = { 0x2E, 0x2F, 0x30, 0x31 }; group(0x2D, k2d, 4); }
    { const int k32[] = { 0x33, 0x34, 0x35, 0x36 }; group(0x32, k32, 4); }
    { const int k37[] = { 0x38, 0x39, 0x3A, 0x3B }; group(0x37, k37, 4); }
    { const int k3c[] = { 0x3D, 0x43, 0x49, 0x4F, 0x55 }; group(0x3C, k3c, 5); }
    { const int k56[] = { 0x57, 0x5D, 0x63, 0x69 }; group(0x56, k56, 4); }
    { const int k6f[] = { 0x70, 0x73, 0x74, 0x75, 0x78, 0x79, 0x7A, 0x7D, 0x7E, 0x7F, 0x82, 0x83 }; group(0x6F, k6f, 12); }
    { const int a[] = { 0x85 }, b[] = { 0x8D }, c[] = { 0x95 }, d[] = { 0x9D }; group(0x84, a, 1); group(0x8C, b, 1);
      group(0x94, c, 1); group(0x9C, d, 1); }
    { const int a[] = { 0xA6 }, b[] = { 0xA8 }, c[] = { 0xAA }, d[] = { 0xAC }, i[] = { 0xA5, 0xA7, 0xA9, 0xAB };
      group(0xA5, a, 1); group(0xA7, b, 1); group(0xA9, c, 1); group(0xAB, d, 1); group(0xA4, i, 4); }
    joints[0x55].dobj = &plates[0];
    /* Door 4's door panel has 17 parts side by side: wider than the walk's stack. */
    for (j = 0; j < 17; ++j) { joints[0xB0 + j].parent = &joints[0x9D]; if (j) joints[0xB0 + j - 1].next = &joints[0xB0 + j]; }
    joints[0x9D].child = &joints[0xB0];
    /* Door 3's panels: 0x95 > 0x96 (> 0x97, 0x98) and 0x99; 0x98 and door 3's tag arrow start hidden. */
    joints[0x95].child = &joints[0x96]; joints[0x96].next = &joints[0x99];
    joints[0x96].child = &joints[0x97]; joints[0x97].next = &joints[0x98];
    joints[0x98].flags = JOBJ_HIDDEN;
    joints[0x7A].child = &joints[0x7B]; joints[0x7B].flags = JOBJ_HIDDEN;
    joints[0x63].child = &joints[0x64];
    texts[5].hidden = 1;   /* door 3's name list starts hidden */
    mu_f4_shut();
    /* Doors 3 and 4's parts; doors 1 and 2 keep theirs. */
    { static const int hid[] = { 0x49, 0x4C, 0x4D, 0x4F, 0x52, 0x53, 0x7B, 0x7C, 0x7D, 0x7E, 0x80, 0x81, 0x82, 0x83 };
      for (j = 0; j < (int) (sizeof hid / sizeof hid[0]); ++j) assert(joints[hid[j]].flags & JOBJ_HIDDEN); }
    for (j = 0x3D; j <= 0x48; ++j) assert(!(joints[j].flags & JOBJ_HIDDEN));
    for (j = 0x70; j <= 0x79; ++j) assert(!(joints[j].flags & JOBJ_HIDDEN));
    for (j = 0x95; j <= 0x99; ++j) assert(joints[j].flags & JOBJ_HIDDEN);
    assert((joints[0x3A].flags & JOBJ_HIDDEN) && (joints[0x3B].flags & JOBJ_HIDDEN) && (joints[0x64].flags & JOBJ_HIDDEN));
    assert((joints[0x7A].flags & JOBJ_HIDDEN) && (joints[0x4F].flags & JOBJ_HIDDEN) && (joints[0xAC].flags & JOBJ_HIDDEN));
    assert(!(joints[0x39].flags & JOBJ_HIDDEN) && !(joints[0x8D].flags & JOBJ_HIDDEN));   /* doors 1 and 2 stay */
    for (j = 4; j <= 7; ++j) assert(texts[j].hidden);   /* doors 3 and 4: name tag, name list */
    assert(texts[10].hidden && texts[11].hidden && texts[12].hidden);   /* KO counts, shield label */
    for (j = 0; j <= 3; ++j) assert(!texts[j].hidden);
    assert(!texts[8].hidden && !texts[9].hidden);
    /* The plates: the joint stays (doors 1 and 2's plates are its shapes too), shapes 3 and 4 are hidden. */
    assert(mu_f4.plates == 2 && !(joints[0x55].flags & JOBJ_HIDDEN));
    assert(!(plates[0].flags & DOBJ_HIDDEN) && !(plates[1].flags & DOBJ_HIDDEN));
    assert((plates[2].flags & DOBJ_HIDDEN) && (plates[3].flags & DOBJ_HIDDEN));
    assert(count("doubles: two-door select (doors 3 and 4 hidden, ") == 1 && count("their name plates hidden)") == 1);
    /* The door's redraw shows its team mark, tag and plate again; the next frame hides them again, logs once. */
    joints[0x3A].flags = 0; texts[4].hidden = 0; plates[2].flags = 0;
    mu_f4_shut();
    assert((joints[0x3A].flags & JOBJ_HIDDEN) && texts[4].hidden && (plates[2].flags & DOBJ_HIDDEN));
    assert(count("doubles: two-door select") == 1);
    /* Back to four doors: every bit as it was (plate 4 started hidden), and the doors draw themselves again. */
    mu_f4_unshut();
    for (j = 0x95; j <= 0x99; ++j) assert(!!(joints[j].flags & JOBJ_HIDDEN) == (j == 0x98));
    for (j = 0x49; j <= 0x54; ++j) assert(!(joints[j].flags & JOBJ_HIDDEN));
    assert(!(plates[2].flags & DOBJ_HIDDEN) && (plates[3].flags & DOBJ_HIDDEN) && mu_f4.plates == 0);
    assert(!(joints[0x3A].flags & JOBJ_HIDDEN) && (joints[0x7B].flags & JOBJ_HIDDEN) && !(joints[0x7A].flags & JOBJ_HIDDEN));
    assert(!texts[10].hidden && !texts[11].hidden && !texts[4].hidden && texts[5].hidden && !texts[12].hidden);
    assert(redraws[2] == 1 && redraws[3] == 1 && redraws[0] == 0 && redraws[1] == 0 && !mu_f4.shut);
    mu_f4_unshut(); assert(redraws[2] == 1);   /* nothing to put back twice */
    mu_f4_shut(); assert(count("doubles: two-door select") == 1);   /* once per visit */
    assert(count("wider than 16") == 1 && count("hide list full") == 0);   /* each cap once a visit */
    mu_f4_shut_forget(); assert(!mu_f4.shut && mu_f4.shut_count == 0 && mu_f4.plates == 0);
    mu_f4_unshut();
    /* Not the plates (another file's tree): three shapes, five, or under another group: left alone, said once. */
    plates[2].next = NULL; plates[2].flags = 0; mu_f4.shut_logged = 0;
    mu_f4_shut();
    assert(mu_f4.plates == -1 && !(plates[2].flags & DOBJ_HIDDEN) && count("their name plates not found, left showing)") == 1);
    mu_f4_unshut();
    plates[2].next = &plates[3]; plates[3].next = &plates[0]; plates[0].next = NULL;   /* a ring cut to five: 3 0 */
    { HSD_DObj extra = { NULL, NULL, 0 }; plates[0].next = &extra; plates[3].next = NULL;
      plates[3].next = &extra; plates[0].next = &plates[1];
      mu_f4_shut(); assert(mu_f4.plates == -1 && !(plates[2].flags & DOBJ_HIDDEN)); mu_f4_unshut(); plates[3].next = NULL; }
    joints[0x55].parent = &joints[0x56];
    mu_f4_shut(); assert(mu_f4.plates == -1 && !(plates[2].flags & DOBJ_HIDDEN)); mu_f4_unshut();
    joints[0x55].parent = &joints[0x3C];
    mu_f4_shut(); assert(mu_f4.plates == 2 && (plates[2].flags & DOBJ_HIDDEN)); mu_f4_unshut();

    /* The panel: Slippi's subtexts, in its order, places and sizes. */
    mu_f4_panel_make();
    assert(made == 1 && mu_f4.panel == &panel_text && subs == MU_F4_PANEL_SUBS);
    assert(panel_text.default_alignment == 0 && NEAR(panel_text.font_size.x, 0.1f));
    { static const float want[MU_F4_PANEL_SUBS][3] = { { 0.5f, 70, 23 }, { 0.4f, 90, 52 }, { 0.45f, 70, 52 }, { 0.4f, 90, 75 },
          { 0.45f, 70, 75 }, { 0.4f, 90, 98 }, { 0.45f, 70, 98 }, { 0.4f, 70, 132.5f }, { 0.4f, 70, 152.5f },
          { 0.5f, -130, -256 }, { 0.5f, -50, -256 }, { 0.4f, 70, 52 }, { 0.4f, 70, 70 }, { 0.4f, 70, 88 }, { 0.4f, 70, 106 } };
      for (j = 0; j < MU_F4_PANEL_SUBS; ++j)
          assert(NEAR(sub_scale[j], want[j][0]) && NEAR(sub_x[j], want[j][1]) && NEAR(sub_y[j], want[j][2])); }

    /* The words, every state, as the host (seat 0) with seat 1 open. */
    memset(&v, 0, sizeof v);
    v.mode = MU_LOBBY_M_DOUBLES; v.my_seat = MU_LOBBY_NONE;
    check(NULL, MU_LOBBY_HINT_OPENING, 0, 0, "", LINES("Waiting for players", 1, "", 0, "", 0), "", "");
    check(&v, MU_LOBBY_HINT_OPENING, 0, 0, "", LINES("Waiting for players", 1, "", 0, "", 0), "", "");
    v.flags = MU_LOBBY_F_ARMED | MU_LOBBY_F_KNOWN | MU_LOBBY_F_HOST; v.my_seat = 0;
    v.seats[0].kind = MU_LOBBY_KIND_HUMAN; v.seats[1].kind = MU_LOBBY_KIND_OPEN;
    check(&v, MU_LOBBY_HINT_START_PARTNER, 0, MU_LOBBY_WANT_FIND, "",
          LINES("Select your character", 1, "Press START to search", 1, "Partner: find online", 0), "Tab changes partner", "");
    v.seats[0].flags = MU_LOBBY_S_PICKED;
    check(&v, MU_LOBBY_HINT_START_PARTNER, 0, MU_LOBBY_WANT_FIND, "",
          LINES("Character selected", 2, "Press START to search", 1, "Partner: find online", 0), "Tab changes partner", "");
    check(&v, MU_LOBBY_HINT_START_PARTNER, 0, MU_LOBBY_WANT_INVITE, "",
          LINES("Character selected", 2, "Press START to invite", 1, "Partner: invite a friend", 0), "Tab changes partner", "");
    check(&v, MU_LOBBY_HINT_START_PARTNER, 0, MU_LOBBY_WANT_P2, "",   /* Player 2 sitting down */
          LINES("Character selected", 2, "Waiting for players", 1, "Partner: Player 2", 1), "Tab changes partner", "");
    check(&v, MU_LOBBY_HINT_START_PARTNER, 0, MU_LOBBY_WANT_CPU, "",
          LINES("Character selected", 2, "Waiting for players", 1, "Partner: CPU", 1), "Tab changes partner", "");
    v.flags |= MU_LOBBY_F_INVITING;
    check(&v, MU_LOBBY_HINT_START_PARTNER, 0, MU_LOBBY_WANT_INVITE, "",
          LINES("Character selected", 2, "Waiting for players", 1, "Inviting a friend", 1), "Press B to cancel", "");
    v.flags &= ~MU_LOBBY_F_INVITING;
    v.seats[1].kind = MU_LOBBY_KIND_CPU;
    check(&v, MU_LOBBY_HINT_START_SEARCH, 0, MU_LOBBY_WANT_CPU, "",
          LINES("Character selected", 2, "Press START to search", 1, "Partner: CPU", 2), "Tab changes partner", "");
    v.seats[1].kind = MU_LOBBY_KIND_LOCAL;
    check(&v, MU_LOBBY_HINT_START_SEARCH, 0, MU_LOBBY_WANT_P2, "",
          LINES("Character selected", 2, "Press START to search", 1, "Partner: Player 2", 2), "Tab changes partner", "");
    v.seats[1].kind = MU_LOBBY_KIND_HUMAN; strcpy(v.seats[1].name, "Fox (main)");
    check(&v, MU_LOBBY_HINT_START_SEARCH, 0, MU_LOBBY_WANT_FIND, "",   /* a friend: the tab is locked */
          LINES("Character selected", 2, "Press START to search", 1, "Partner: Fox  main ", 2), "", "");
    v.seats[1].name[0] = 0;
    check(&v, MU_LOBBY_HINT_START_SEARCH, 0, MU_LOBBY_WANT_FIND, "",
          LINES("Character selected", 2, "Press START to search", 1, "Partner: a friend", 2), "", "");
    memset(v.seats[1].name, 'W', MU_LOBBY_NAME - 1); v.seats[1].name[MU_LOBBY_NAME - 1] = 0;   /* the longest name */
    { MuF4Panel p; mu_f4_panel_text(&v, MU_LOBBY_HINT_START_SEARCH, 0, 0, "", &p);
      assert(!strncmp(p.line[2], "Partner: WWW", 12) && strlen(p.line[2]) <= MU_F4_PANEL_WRAP); }
    v.seats[1].kind = MU_LOBBY_KIND_OPEN; v.seats[0].flags = 0;
    /* Everyone needs to pick a fighter: the fighter's line says it. */
    check(&v, MU_LOBBY_HINT_PICK, 0, MU_LOBBY_WANT_FIND, "",
          LINES("Select your character", 1, "", 0, "Partner: find online", 0), "Tab changes partner", "");
    /* Searching (the guest's B cancels too: shim/mu_slippi_menu.c). */
    v.mode |= MU_LOBBY_M_SEARCHING; v.seats[0].flags = MU_LOBBY_S_PICKED;
    check(&v, MU_LOBBY_HINT_SEARCHING, 0, MU_LOBBY_WANT_FIND, "",
          LINES("Character selected", 2, "Searching for opponents", 1, "Partner: find online", 0), "Press B to cancel", "");
    check(&v, MU_LOBBY_HINT_SEARCHING, 1, MU_LOBBY_WANT_FIND, "",   /* the tab, locked */
          LINES("Character selected", 2, "Searching for opponents", 1, "Partner: find online", 0), "Press B to cancel", "");
    v.mode &= ~MU_LOBBY_M_SEARCHING;
    /* A match is being set up. */
    check(&v, MU_LOBBY_HINT_STARTING, 0, 0, "", LINES("Character selected", 2, "Match found", 2, "Partner: find online", 0),
          "Tab changes partner", "");
    v.flags |= MU_LOBBY_F_STARTED;
    check(&v, MU_LOBBY_HINT_NONE, 0, 0, "", LINES("Character selected", 2, "Match found", 2, "Partner: find online", 0), "", "");
    v.flags &= ~MU_LOBBY_F_STARTED;
    /* Search stopped: its own words for the note's 2 s, then START is offered again. */
    check(&v, MU_LOBBY_HINT_SEARCH_STOPPED, 1, MU_LOBBY_WANT_FIND, "",
          LINES("Character selected", 2, "Search stopped", 0, "Partner: find online", 0), "Tab changes partner", "");
    check(&v, MU_LOBBY_HINT_SEARCH_STOPPED, 0, MU_LOBBY_WANT_FIND, "",
          LINES("Character selected", 2, "Press START to search", 1, "Partner: find online", 0), "Tab changes partner", "");
    /* Rules refusals: the error sound only, the steady lines stay. */
    check(&v, MU_LOBBY_HINT_FIXED_RULES, 1, MU_LOBBY_WANT_FIND, "",
          LINES("Character selected", 2, "Press START to search", 1, "Partner: find online", 0), "Tab changes partner", "");
    check(&v, MU_LOBBY_HINT_UNRANKED_ONLY, 1, MU_LOBBY_WANT_INVITE, "",
          LINES("Character selected", 2, "Press START to invite", 1, "Partner: invite a friend", 0), "Tab changes partner", "");
    /* Waiting for every game, a player reconnecting. */
    check(&v, MU_LOBBY_HINT_NOT_READY, 0, 0, "",
          LINES("Character selected", 2, "Waiting for players", 1, "Partner: find online", 0), "Tab changes partner", "");
    /* The problems, in red: the lines are cleared (Slippi's). */
    check(&v, MU_LOBBY_HINT_CLOSED, 0, 0, "", LINES("", 0, "", 0, "", 0), "", "Lobby closed. Hold B to exit");
    check(&v, MU_LOBBY_HINT_MISMATCH, 0, 0, "", LINES("", 0, "", 0, "", 0), "", "Version mismatch");
    check(&v, MU_LOBBY_HINT_TEAM_MISMATCH, 1, 0, "", LINES("", 0, "", 0, "", 0), "", "Version mismatch");
    check(&v, MU_LOBBY_HINT_UNSUPPORTED, 0, 0, "", LINES("", 0, "", 0, "", 0), "", "Version mismatch");
    check(&v, MU_LOBBY_HINT_TAKEN, 1, 0, "", LINES("", 0, "", 0, "", 0), "", "Try again");
    check(&v, MU_LOBBY_HINT_CHANGED, 1, 0, "", LINES("", 0, "", 0, "", 0), "", "Try again");
    check(&v, MU_LOBBY_HINT_REFUSED, 1, 0, "", LINES("", 0, "", 0, "", 0), "", "Try again");
    check(&v, MU_LOBBY_HINT_START_SEARCH, 0, 0, "Lost (1/2) #x", LINES("", 0, "", 0, "", 0), "", "Lost  1 2   x");
    /* The guest: the host is the partner; no tab. */
    v.flags = MU_LOBBY_F_ARMED | MU_LOBBY_F_KNOWN; v.my_seat = 1; v.seats[1].kind = MU_LOBBY_KIND_HUMAN;
    v.seats[1].flags = 0; strcpy(v.seats[0].name, "Falco");
    check(&v, MU_LOBBY_HINT_WAIT_HOST, 0, 0, "",
          LINES("Select your character", 1, "Waiting for players", 1, "Partner: Falco", 2), "", "");
    v.my_seat = 2;   /* not a Doubles seat */
    check(&v, MU_LOBBY_HINT_WAIT_HOST, 0, 0, "", LINES("Waiting for players", 1, "", 0, "", 0), "", "");

    /* Drawn only on a change; the spinners turn every MU_F4_SPIN_FRAMES; blank behind the curtain. */
    memset(&v, 0, sizeof v);
    v.flags = MU_LOBBY_F_ARMED | MU_LOBBY_F_KNOWN | MU_LOBBY_F_HOST; v.mode = MU_LOBBY_M_DOUBLES; v.my_seat = 0;
    v.seats[0].kind = MU_LOBBY_KIND_HUMAN; v.seats[1].kind = MU_LOBBY_KIND_OPEN; want_v = MU_LOBBY_WANT_FIND;
    mu_f4_panel(&v, 1, MU_LOBBY_HINT_START_PARTNER, 0, "", 0);
    assert(!strcmp(sub_words[MU_F4_ST_HEADER], "Doubles") && !strcmp(sub_words[1], "Select your character"));
    assert(!strcmp(sub_words[2], mu_f4_spin_str[0]) && sub_color[2].b == 0xFF && sub_color[2].g == 0xBC);
    assert(!strcmp(sub_words[3], "Press START to search") && !strcmp(sub_words[5], "Partner: find online") && sub_words[6][0] == 0);
    assert(!strcmp(sub_words[MU_F4_ST_GRAY], "Tab changes partner") && sub_color[MU_F4_ST_GRAY].r == 0x8E);
    assert(mu_f4.panel_draws == 7);
    draws = mu_f4.panel_draws;
    for (j = 1; j < MU_F4_SPIN_FRAMES; ++j) mu_f4_panel(&v, 1, MU_LOBBY_HINT_START_PARTNER, 0, "", 0);
    assert(mu_f4.panel_draws == draws);   /* nothing changed: nothing written */
    mu_f4_panel(&v, 1, MU_LOBBY_HINT_START_PARTNER, 0, "", 0);   /* frame 15: both spinners turn */
    assert(mu_f4.panel_draws == draws + 2 && !strcmp(sub_words[2], mu_f4_spin_str[1]) && !strcmp(sub_words[4], mu_f4_spin_str[1]));
    assert(count("doubles: panel 1 \"Select your character\"\n") == 1 && count("doubles: panel 2 ") == 0);
    v.seats[0].flags = MU_LOBBY_S_PICKED;
    mu_f4_panel(&v, 1, MU_LOBBY_HINT_START_PARTNER, 0, "", 0);
    assert(!strcmp(sub_words[2], mu_f4_spin_done_str) && sub_color[2].r == 0x33 && sub_color[2].g == 0xFF);
    /* An error: the lines go, the reason wraps at 30 letters on a space, in red. */
    mu_f4_panel(&v, 1, MU_LOBBY_HINT_START_SEARCH, 0, "The other team left before the match began", 0);
    assert(sub_words[1][0] == 0 && sub_words[2][0] == 0 && sub_words[MU_F4_ST_GRAY][0] == 0);
    assert(!strcmp(sub_words[MU_F4_ST_ERR1], "The other team left before the") && sub_color[MU_F4_ST_ERR1].r == 0xFF &&
           sub_color[MU_F4_ST_ERR1].g == 0);
    assert(!strcmp(sub_words[MU_F4_ST_ERR1 + 1], "match began") && sub_words[MU_F4_ST_ERR1 + 2][0] == 0);
    mu_f4_panel(&v, 1, MU_LOBBY_HINT_START_SEARCH, 0, "Couldnotreachtheotherteamsgameintime", 0);   /* no space: cut */
    assert(!strcmp(sub_words[MU_F4_ST_ERR1], "Couldnotreachtheotherteamsgame") && !strcmp(sub_words[MU_F4_ST_ERR1 + 1], "intime"));
    mu_f4_panel(&v, 1, MU_LOBBY_HINT_START_SEARCH, 0, "", 0);
    assert(sub_words[MU_F4_ST_ERR1][0] == 0 && !strcmp(sub_words[1], "Character selected"));
    /* Behind the curtain, and every other select: all blank, written once. */
    curtain = 1; mu_f4_panel(&v, 1, MU_LOBBY_HINT_START_SEARCH, 0, "", 0);
    for (j = 0; j < MU_F4_PANEL_SUBS; ++j) assert(sub_words[j][0] == 0);
    draws = mu_f4.panel_draws;
    mu_f4_panel(&v, 1, MU_LOBBY_HINT_START_SEARCH, 0, "", 0); assert(mu_f4.panel_draws == draws);
    curtain = 0; mu_f4_panel(&v, 0, MU_LOBBY_HINT_START_SEARCH, 0, "", 0); assert(mu_f4.panel_draws == draws);
    mu_f4_panel(&v, 1, MU_LOBBY_HINT_START_SEARCH, 0, "", 0); assert(!strcmp(sub_words[0], "Doubles"));
    mu_f4_panel(NULL, 0, MU_LOBBY_HINT_OPENING, 0, "", 0);
    for (j = 0; j < MU_F4_PANEL_SUBS; ++j) assert(sub_words[j][0] == 0);
    mu_f4.panel = NULL; draws = mu_f4.panel_draws;
    mu_f4_panel(&v, 1, MU_LOBBY_HINT_START_SEARCH, 0, "", 0); assert(mu_f4.panel_draws == draws);   /* no text yet */

    /* The partner door's state goes to the log once per change. */
    log_text[0] = 0; mu_f4.partner_logged = 0; v.seats[0].flags = 0;
    mu_f4_panel(&v, 1, 0, 0, "", 0); mu_f4_panel(&v, 1, 0, 0, "", 0);
    assert(count("doubles: partner door find\n") == 1);
    v.flags |= MU_LOBBY_F_INVITING; mu_f4_panel(&v, 1, 0, 0, "", 0); assert(count("doubles: partner door invite\n") == 1);
    v.flags &= ~MU_LOBBY_F_INVITING;
    v.seats[1].kind = MU_LOBBY_KIND_CPU; mu_f4_panel(&v, 1, 0, 0, "", 0); assert(count("doubles: partner door cpu\n") == 1);
    v.seats[1].kind = MU_LOBBY_KIND_LOCAL; mu_f4_panel(&v, 1, 0, 0, "", 0); assert(count("doubles: partner door local\n") == 1);
    v.seats[1].kind = MU_LOBBY_KIND_HUMAN; mu_f4_panel(&v, 1, 0, 0, "", 0); assert(count("doubles: partner door friend\n") == 1);
    v.seats[1].kind = MU_LOBBY_KIND_OPEN; mu_f4_panel(&v, 1, 0, 0, "", 0); assert(count("doubles: partner door find\n") == 2);
    v.mode = 0; mu_f4_panel(&v, 0, 0, 0, "", 0); mu_f4_panel(NULL, 0, 0, 0, "", 0);
    assert(count("doubles: partner door") == 6);

    /* The partner door's tag and caption plate (mu_f4_partner_tag): the host's view of a seat that isn't a friend's. */
    { const char* cap = "x"; const char* tag;
      memset(&v, 0, sizeof v);
      v.flags = MU_LOBBY_F_ARMED | MU_LOBBY_F_KNOWN | MU_LOBBY_F_HOST; v.mode = MU_LOBBY_M_DOUBLES; v.my_seat = 0;
      v.seats[0].kind = MU_LOBBY_KIND_HUMAN; v.seats[1].kind = MU_LOBBY_KIND_OPEN;
      tag = mu_f4_partner_tag(&v, 0, MU_LOBBY_WANT_FIND, &cap); assert(!strcmp(tag, "FIND") && !strcmp(cap, "Find online"));
      tag = mu_f4_partner_tag(&v, 0, MU_LOBBY_WANT_INVITE, &cap);
      assert(!strcmp(tag, "FRIEND") && !strcmp(cap, "Invite a friend"));
      tag = mu_f4_partner_tag(&v, 0, MU_LOBBY_WANT_P2, &cap); assert(!strcmp(tag, "P2") && cap == NULL);
      tag = mu_f4_partner_tag(&v, 0, MU_LOBBY_WANT_CPU, &cap); assert(!strcmp(tag, "CPU") && cap == NULL);
      v.flags |= MU_LOBBY_F_INVITING;   /* an invite out: Invite's, whatever the tab last said */
      tag = mu_f4_partner_tag(&v, 0, MU_LOBBY_WANT_FIND, &cap);
      assert(!strcmp(tag, "FRIEND") && !strcmp(cap, "Invite a friend"));
      v.flags &= ~MU_LOBBY_F_INVITING;
      v.seats[1].kind = MU_LOBBY_KIND_LOCAL;
      tag = mu_f4_partner_tag(&v, 0, MU_LOBBY_WANT_FIND, &cap); assert(!strcmp(tag, "P2") && cap == NULL);
      v.seats[1].kind = MU_LOBBY_KIND_CPU;
      tag = mu_f4_partner_tag(&v, 0, MU_LOBBY_WANT_CPU, &cap); assert(!strcmp(tag, "CPU") && cap == NULL);
      /* CPU, then the tab to Find: the room still says CPU for a few frames; the tab's choice wins */
      tag = mu_f4_partner_tag(&v, 0, MU_LOBBY_WANT_FIND, &cap); assert(!strcmp(tag, "FIND") && !strcmp(cap, "Find online"));
      v.seats[1].kind = MU_LOBBY_KIND_HUMAN;   /* a friend has the seat: Melee's own tag */
      cap = "x"; assert(mu_f4_partner_tag(&v, 0, MU_LOBBY_WANT_INVITE, &cap) == NULL && cap == NULL);
      v.seats[1].kind = MU_LOBBY_KIND_OPEN;
      cap = "x"; assert(mu_f4_partner_tag(&v, 1, MU_LOBBY_WANT_FIND, &cap) == NULL && cap == NULL);   /* the guest */
      v.flags &= ~MU_LOBBY_F_KNOWN;
      cap = "x"; assert(mu_f4_partner_tag(&v, 0, MU_LOBBY_WANT_FIND, &cap) == NULL && cap == NULL);
      v.flags |= MU_LOBBY_F_KNOWN; v.mode = 0;   /* the Friends lobby */
      cap = "x"; assert(mu_f4_partner_tag(&v, 0, MU_LOBBY_WANT_FIND, &cap) == NULL && cap == NULL); }
    return 0;
}
"""


class HintsInTheMenuFont(unittest.TestCase):
    def test_doubles_hints_fit_the_label(self):
        wire = read(WIRE)
        text = body(wire, 'static inline const char* mu_lobby_hint_text(unsigned hint)')
        for name in ('SEARCHING', 'START_SEARCH', 'START_PARTNER', 'FIXED_RULES', 'UNRANKED_ONLY', 'TEAM_MISMATCH',
                     'SEARCH_STOPPED'):
            m = re.search(r'case MU_LOBBY_HINT_%s: return "([^"]*)";' % name, text)
            self.assertIsNotNone(m, name)
            self.assertRegex(m.group(1), r'^[A-Za-z0-9 .]{1,40}$', name)


if __name__ == '__main__':
    unittest.main()
