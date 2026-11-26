"""CPU bracket wiring and rule parity; no compiler, game, account or network required."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


class TournamentWiringTest(unittest.TestCase):
    def test_rules_equal_existing_online_rules(self):
        online = source("port/runtime/hle/slippi_online.cpp")
        original = online.split("static std::vector<uint8_t> block = {", 1)[1].split("};", 1)[0]
        original = [int(n, 16) for n in re.findall(r"0x([0-9a-fA-F]+)", original)]
        contract = source("port/runtime/abi/mu_tourney.h")
        rules = contract.split("rules[96] = {", 1)[1].split("};", 1)[0]
        values = [int(n.strip(), 0) for n in rules.split(",") if n.strip()]
        values += [0] * (96 - len(values))
        self.assertEqual(values, original[:96])
        self.assertIn("out[2] |= 0x08;", contract)
        self.assertIn("block[2] | 0x8", online)
        items = contract.split("items[8] = {", 1)[1].split("}", 1)[0]
        mask = bytes(int(n.strip(), 0) for n in items.split(",") if n.strip())
        self.assertEqual(int.from_bytes(mask, "big"), int(re.search(r"uint64_t items = (0x[0-9A-Fa-f]+)ull", online)[1], 16))
        player = contract.split("player[36] = {", 1)[1].split("};", 1)[0]
        values = [int(n.strip(), 0) for n in player.split(",") if n.strip()]
        # Vanilla online defaults for ratios, handicap and CPU type are retained.
        self.assertEqual(values[8:], original[96 + 8:96 + 36])

    def test_boot_is_gated_before_scene_replacement(self):
        game = source("sourceport/game/shim/mu_tourney.c")
        boot = game.split("int mu_tourney_boot(void)", 1)[1]
        self.assertLess(boot.index("got != 5 || reply[0] != 1) return 0;"), boot.index("mode->states = states;"))
        replay = source("sourceport/game/shim/mu_replay.c")
        self.assertIn("else if (mu_tourney_boot()) {\n        *mode = GM_VS;", replay)
        self.assertIn("mu_tourney_report(match_result);", replay)
        self.assertIn("mu_tourney_report(gmVs_GetSceneState()->match_result);", replay)
        self.assertNotIn("YOUGAME_AGENT_TEST_HOOK", game)
        self.assertNotIn("agent-test", game)
        self.assertNotIn("getenv", game)

    def test_css_is_unselected_and_only_human_can_choose(self):
        game = source("sourceport/game/shim/mu_tourney.c")
        self.assertIn("gm_801B06B0(css, 0xE, 33, 0, 0, 0x78, 0, 0);", game)
        self.assertIn("gm_801B0730(css, &human_character, NULL, &human_color, &human_nametag, NULL);", game)
        self.assertIn("human_nametag, cpu_character, cpu_level);", game)
        self.assertNotIn("GS_SSS", game)
        setup = game.split("static void vs_prep", 1)[1].split("static void vs_decide", 1)[0]
        self.assertLess(setup.index("mu_replay_apply_game_info(&vs->start, info);"), setup.index("gmVsMelee_EnterVs"))
        self.assertIn("mu_replay_apply_game_info(gm_GetGameModeStateEnterData(state), info);", setup)
        self.assertNotIn("mu_online_arm", setup)

    def test_report_reads_actual_match_state_once(self):
        game = source("sourceport/game/shim/mu_tourney.c")
        report = game.split("static void send_result", 1)[1]
        for token in ("gmVs_GetSceneState()->frame_count", "Player_GetStocks(0)", "Player_GetStocks(1)",
                      "mu_online_singles_winner(method)", "active && started && !completed && !in_flight", "send_result(7)"):
            self.assertIn(token, report)
        self.assertLess(report.index("in_flight = 1;"), report.index("command(p, sizeof p);"))
        self.assertNotIn("completed = 1;", report)
        online = source("sourceport/game/shim/mu_online.c")
        self.assertIn("winner = singles_winner(end_method);", online)
        self.assertIn("int mu_online_singles_winner(int end_method) { return singles_winner(end_method); }", online)
        self.assertIn("&& !mu_tourney_playing()", online)
        self.assertIn("fn_8016CF4C(0, OUTCOME_NO_CONTEST);", game)
        self.assertIn("(HSD_PadMasterStatus[0].button & lras) == lras", game)
        self.assertIn("int mu_online_singles_winner(int end_method)", online)
        self.assertIn("gmVsMelee_EnterResults(state);", game)
        self.assertIn("static void results_decide(GameModeState* state) { (void) state; return_menu(); }", game)
        self.assertNotIn("gmVsMelee_ExitResults", game)  # no challenger or rematch

    def test_existing_sdk_owns_delivery_and_cleanup(self):
        link = source("port/runtime/hle/yougame_link.cpp")
        self.assertIn("g_tourney.hello(hello);", link)
        self.assertIn("g_tourney.finish(p + 1, n - 1);", link)
        self.assertIn("tournament_bridge(body, deadline, 1500)", link)
        self.assertIn('tournament_bridge({{"type", "hello"}}, deadline, 1000)', link)
        self.assertIn("catch (const BridgeNoAnswer&)", link)
        host = source("port/app/source_host.cpp")
        # 0xEB is shared with CMD_STAGE_SONGS (0.9.216), which is always 16 bytes; the tournament never is.
        self.assertIn("if (c == MU_TOURNEY_COMMAND && !(c == CMD_STAGE_SONGS && n == 16)) return yougame::tournament_command(p, n, reply);", host)
        self.assertIn("g_game.practice(MU_TOURNEY_QUIT_OP, nullptr, 0);", host)
        self.assertIn("yougame::tournament_drain();", host)
        guarded = host.split("template <class F> void guarded", 1)[1].split("// ---- the table", 1)[0]
        self.assertLess(guarded.index("finish_tournament_before_exit();"), guarded.index("g_shutdown(stop.code)"))
        self.assertLess(host.index('#include "../runtime/abi/mu_tourney.h"'), host.index("#ifndef MELEE_NO_SLIPPI"))
        self.assertIn("case MU_TOURNEY_QUIT_OP:", source("sourceport/game/shim/mu_practice.c"))
        # Offline practice and gameplay modifiers cannot hijack a tournament or its CPU.
        self.assertIn("if (!yougame::tournament_active())\n    slippi::native_practice::apply_pads(pads);", host)
        self.assertIn("!yougame::tournament_active()) user_gecko::apply();", host)

    def test_boot_poll_never_waits_or_consumes_identity_budget(self):
        link = source("port/runtime/hle/yougame_link.cpp")
        launch = link.split("if (p[0] == MU_TOURNEY_LAUNCH && n == 1)", 1)[1].split("if (!tournament_active())", 1)[0]
        for forbidden in ("wait_identity", "identity()", "refresh_identity", "Sleep(", ".wait(", "g_tourney.hello"):
            self.assertNotIn(forbidden, launch)
        self.assertIn("g_tourney.launch()", launch)
        # Only the existing asynchronous hello receiver supplies launch intent.
        self.assertEqual(link.count("g_tourney.hello("), 1)
        self.assertIn("g_tourney.hello(hello);", link)
        practice = source("port/runtime/hle/native_practice.cpp")
        self.assertIn("g_bridge && major == 1 && safe && yougame::tournament_pending()", practice)
        self.assertIn("g_bridge(MU_TOURNEY_BOOT_OP, nullptr, 0);", practice)
        game = source("sourceport/game/shim/mu_practice.c")
        handoff = game.split("case MU_TOURNEY_BOOT_OP:", 1)[1].split("case MU_PR_PAGE_PAUSE:", 1)[0]
        self.assertIn("gm_GetCurrentGameMode() != GM_MENU || !mu_tourney_boot()", handoff)
        self.assertIn("mu_practice_bridge(MU_PR_REQUEST_MAJOR, &major, 1)", handoff)

    def test_tournament_lcancel_suppresses_injection_keeps_feedback(self):
        host = source("port/app/source_host.cpp")
        self.assertIn("lcancel::apply(pads, !yougame::tournament_active());", host)
        apply = source("port/runtime/host/lcancel.cpp").split("void apply(host::PadState pads[4], bool allow_automatic)", 1)[1]
        self.assertIn("allow_automatic && g_ports[p].cached)", apply)   # the cached injection, renamed
        self.assertIn("allow_automatic && policy::automatic_allowed", apply)
        self.assertIn("const bool indicate = want_indicator && !auto_allowed;", apply)
        self.assertIn("if (!auto_allowed || turned_off) {", apply)   # nothing is injected unless automatic is allowed
        self.assertIn("if (indicate || g_tints_set) publish_tints(retrace);", apply)

    def test_tournament_page_pause_uses_ranked_eligibility_and_advertisement(self):
        practice = source("port/runtime/hle/native_practice.cpp").split("void tick()", 1)[1]
        self.assertIn("const bool online = slippi::online::session_mode() >= 0 || yougame::tournament_active();", practice)
        self.assertIn("if (!online && !slippi::playback::enabled())", practice)
        self.assertIn("page_pause.tick(true, online,", practice)
        self.assertIn("yougame::pause_event(pause_result.pausable, pause_result.paused, pause_result.reason);", practice)

    def test_exact_timeout_tie_is_resolved_before_completion(self):
        game = source("sourceport/game/shim/mu_tourney.c").split("static void send_result", 1)[1]
        self.assertLess(game.index("mu_online_singles_winner(method)"), game.index("mu_tourney_result_winner(method, winner)"))
        self.assertLess(game.index("mu_tourney_result_winner(method, winner)"), game.index("in_flight = 1;"))
        contract = source("port/runtime/abi/mu_tourney.h")
        self.assertIn("return winner == 0 ? 1 : 2;", contract)

    def test_absolute_delivery_and_nonjoining_shutdown(self):
        link = source("port/runtime/hle/yougame_link.cpp")
        delivery = source("port/runtime/hle/yougame_tourney_delivery.h")
        command = link.split("bool tournament_command", 1)[1].split("void relay_key", 1)[0]
        self.assertNotIn("std::async", command)
        self.assertNotIn(".wait();", command)
        self.assertNotIn("request_exit", command)
        self.assertIn("delivery.wait_until(deadline)", command)
        self.assertIn("try_lock_until(deadline)", command)
        self.assertIn("std::chrono::seconds(5)", delivery)
        self.assertIn("std::packaged_task", delivery)
        self.assertIn("CancelSynchronousIo(cancel)", link)
        self.assertIn("tourney::lock_until(lock, deadline)", link)
        self.assertIn("std::mutex write_mutex;", link)
        self.assertIn("std::condition_variable cv;", link)
        self.assertIn("pipe_write_until(STDOUT_FILENO, data, deadline)", link)
        bridge = link.split("json bridge_stdio", 1)[1].split("// POST <bridge>", 1)[0]
        self.assertIn("tournament_write_line(line, absolute)", bridge)
        self.assertIn("std::min(absolute,", bridge)
        self.assertIn("if (absolute == tourney::Deadline::max()) write_line(line);", bridge)
        self.assertIn("site owns disconnect/crash expiry", source("sourceport/game/shim/mu_tourney.c"))

    def test_expiry_polls_css_and_restore_after_unload(self):
        game = source("sourceport/game/shim/mu_tourney.c")
        practice = source("sourceport/game/shim/mu_practice.c")
        host = source("port/runtime/hle/native_practice.cpp")
        self.assertIn("g_bridge(MU_TOURNEY_TICK_OP, nullptr, 0)", host)
        self.assertIn("mu_tourney_poll();", practice)
        self.assertIn("mu_tourney_enter_mode(mode);", practice)
        self.assertIn("mu_practice_bridge(MU_PR_REQUEST_MAJOR, &menu, 1)", game)
        self.assertIn("tournament_mode->states = original_states;", game)
        self.assertIn("if (returning) { return_menu(); return; }", game)
        self.assertNotIn("request_exit", game)

    def test_test_registration_and_noslippi_exclusion(self):
        cmake = source("CMakeLists.txt")
        self.assertLess(cmake.index("add_executable(port_yougame_tourney_test"), cmake.index("  return()"))
        self.assertIn("'port_yougame_tourney_test'", source("tools/build_linux.py"))
        game = source("sourceport/game/CMakeLists.txt")
        excluded = game.split("foreach(removed mu_slippi_menu.c", 1)[1].split("endforeach()", 1)[0]
        self.assertIn("mu_tourney.c", excluded)


if __name__ == "__main__":
    unittest.main()
