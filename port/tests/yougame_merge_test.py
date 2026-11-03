"""Source regressions for the v0.9.50 merge boundaries; no Windows process required.

Run with python3 port/tests/yougame_merge_test.py. Behavioral policy, pad UI,
ranked-result and exit-reason tests remain separate and unchanged.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


class YouGameMergeTest(unittest.TestCase):
    def test_conflicted_files_have_both_interfaces_and_no_markers(self):
        for path in ("port/app/source_host.cpp", "port/runtime/hle/slippi_net.cpp",
                     "port/runtime/hle/slippi_online.cpp", "port/runtime/hle/slippi_online.h",
                     "port/runtime/host/host.h", "port/runtime/host/window.cpp"):
            with self.subTest(path=path):
                self.assertIsNone(re.search(r"^(?:<{7}|={7}|>{7})(?: |$)", source(path), re.M))
        host = source("port/runtime/host/host.h")
        self.assertIn("void input_read_devices(PadState out[4]);", host)
        self.assertIn("int lras = -1, const uint8_t* placements = nullptr", host)
        online = source("port/runtime/hle/slippi_online.h")
        for declaration in ("bool early_inputs_accept();", "GameFacts last_game_facts();",
                            "bool native_ranked_next_game("):
            self.assertIn(declaration, online)

    def test_padback_remains_at_game_poll_boundary(self):
        window = source("port/runtime/host/window.cpp")
        self.assertEqual(window.count("void input_poll(PadState out[4]) {"), 1)
        devices, poll = window.split("void input_poll(PadState out[4]) {", 1)
        poll, sampler = poll.split("void input_read_devices(PadState out[4]) {", 1)
        # The upstream sampler must neither consume the UI script nor inject its chord.
        self.assertNotIn("pad_back.sample_for_ui", devices)
        self.assertNotIn("pad_back.sample_for_ui", sampler)
        hook = poll.index("pad_back.sample_for_ui")
        self.assertLess(poll.index("~UiSnapshot()"), hook)
        self.assertLess(poll.index("#ifdef YOUGAME_AGENT_TEST_HOOK"), hook)
        self.assertLess(hook, poll.index("settings_shortcut::poll_z_start"))
        self.assertLess(hook, poll.index("g_ui_capture.load()"))
        self.assertIn("g_ui_agent_step = scripted.step;", poll)
        # The fight sample now also passes the live input policy state and its log (agent-only rooms, 10-25).
        self.assertIn("apply_fight(pads, yougame::agent_test::current_fight_sample,", poll)
        self.assertIn("yougame::agent_test::live_input_state(slippi::online::session_mode())", poll)
        # Upstream still observes the poll before reading scripts or physical devices.
        self.assertLess(poll.index("early_input::note_poll();"), poll.index("script_pads("))
        self.assertLess(poll.index("early_input::note_poll();"), poll.index("read_devices("))

    def test_effective_delay_and_yougame_ranked_coexist(self):
        online = source("port/runtime/hle/slippi_online.cpp")
        delay = online.split("case CMD_GET_DELAY:", 1)[1].split("case CMD_GET_RANK:", 1)[0]
        self.assertIn("effective_delay()", delay)
        self.assertNotIn("g_config.delay", delay)
        self.assertIn("q.push_back((uint8_t)effective_delay());", online)
        for bridge in ("yougame::rank_response()", "yougame::rank_visibility()",
                       "case CMD_YOUGAME_RANKED:", "yougame::ranked_header()",
                       "yougame::ranked_set_complete(payload[0])"):
            self.assertIn(bridge, online)

    def test_listing_profile_and_overlay_keep_the_yougame_check(self):
        # Upstream's precedence (vanilla-only, explicit layers, no-mod-autodetect) plus YouGame's own
        # check at both sites (defense in depth, loader finding #8).
        main = source("port/app/main.cpp")
        profile = main.split("if (gx::RenderOptions::mod_features_available() && !source_port::has_mod_layers()", 1)[1]
        profile = profile.split("#ifdef MELEE_NO_SLIPPI", 1)[0]
        self.assertIn("set_mod_profile(gfx.mod_profile.c_str(), false)", profile)
        self.assertIn("mod_sources(yougame::active()).folders", profile)
        self.assertIn("saved profile ignored in YouGame mode", profile)
        host = source("port/app/source_host.cpp")
        overlay = host.split("void load_mod_overlay() {", 1)[1].split("if (g_mod_layers.empty()) return;", 1)[0]
        self.assertIn("if (host::gameplay::vanilla_only()) return;", overlay)
        self.assertIn("!host::gameplay::mod_autodetect_disabled && !g_mod_root_explicit", overlay)
        self.assertIn("mod_sources(yougame::active()).folders", overlay)
        self.assertIn("implicit Mods/Files overlay ignored in YouGame mode", overlay)
        scan = source("port/runtime/host/mod_scan.cpp")
        self.assertIn("!host::gameplay::vanilla_only() && !host::gameplay::mod_autodetect_disabled && store().auto_detect", scan)

    def test_game_end_keeps_placements_and_casual_winner(self):
        host = source("port/app/source_host.cpp")
        result = host.split("host::publish_lobby_result(winner,", 1)[1].split(";", 1)[0]
        self.assertIn("(int)(int8_t)payload[1]", result)
        self.assertIn("size >= 6 ? payload + 2 : nullptr", result)
        bridge = source("port/runtime/host/host.cpp")
        self.assertIn("end.placements[i] = placements[i]", bridge)
        self.assertIn("yougame::game_ended(end);", bridge)
        link = source("port/runtime/hle/yougame_link.cpp")
        self.assertIn("ranked::casual_winner(end.end_method, end.lras, end.winner,", link)
        self.assertIn("end.has_placements ? end.placements : nullptr, g_room->size", link)
        # A duel's TIME! from the record waits for the game's report, which Source publishes too.
        ended = link.split("void game_ended(const GameEnd& end) {", 1)[1]
        wait = ended.index("ranked::time_out_waits_for_report(end.end_method, end.has_placements, g_room->size)")
        self.assertLess(wait, ended.index("g_room->in_game = false;"))
        report = source("port/runtime/hle/slippi_online.cpp").split("void handle_report_game(", 1)[1]
        self.assertIn("if ((!host::game_image || end_method == 1 || end_method == 7) &&", report)

    def test_remix_keeps_auxiliary_source_gates(self):
        main = source("port/app/main.cpp")
        self.assertIn("user_gecko::set_sources_allowed(yougame::mode_policy::mod_sources(yougame::active()).folders)", main)
        self.assertIn("hle::card::file_filter =", main)
        self.assertIn("card_save_allowed(true, source_port::mods::check_te_file(file).te)", main)
        host = source("port/app/source_host.cpp")
        card = host.split("void card_mount_files() {", 1)[1].split("FILE* in =", 1)[0]
        self.assertIn("mods::check_te_file(entry.path()).te", card)
        self.assertIn("card_save_allowed(true, true, explicit_import)", card)
        self.assertRegex(host, r"mod_sources\(yougame::active\(\)\)\.folders\s*\? std::getenv\(\"MELEE_GAME_DLL\"\) : nullptr")
        self.assertRegex(host, r"if \(yougame::mode_policy::mod_sources\(yougame::active\(\)\)\.folders\)\s*if \(const char\* dll = std::getenv\(\"MELEE_GAME_DLL\"\)\)")
        ai = source("port/runtime/host/hackpack_ai.cpp").split("void look_for_disc() {", 1)[1]
        self.assertLess(ai.index("if (!yougame::mode_policy::mod_sources(yougame::active()).folders)"),
                        ai.index("mods::panel_view()"))
        self.assertIn("return;", ai.split("Status s;", 1)[0])
        spectator = source("port/runtime/hle/slippi_spectate.cpp").split("void start() {", 1)[1]
        self.assertLess(spectator.index("spectator_allowed(yougame_mode,"),
                        spectator.index("std::thread(server_thread"))

    def test_refusals_and_confirmation_focus_survive(self):
        host = source("port/app/source_host.cpp")
        self.assertIn("yougame::notify_mod_refused(explicit_layer,", host)
        self.assertIn("yougame::mode_policy::ModRefusal::ModdedDol", host)
        settings = source("port/runtime/gx/pc_settings.cpp")
        focus = settings.split("if (state.menu_quit && index == 0 && state.menu_quit_focus_pending)", 1)[1]
        focus = focus.split("state.menu_quit_focus_pending = false;", 1)[0]
        self.assertIn("ImGui::SetFocusID(", focus)
        self.assertIn("ImGui::SetNavCursorVisible(true);", focus)


if __name__ == "__main__":
    unittest.main()
