"""The online menu's Ranked query reaches Slippi's handler, not the Hack Pack's (shared 0xFA)."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


class RankedMenuWiringTest(unittest.TestCase):
    def test_empty_0xfa_goes_to_slippi_before_any_other_0xfa_branch(self):
        host = source("port/app/source_host.cpp")
        route = host.index("if (c == slippi::online::CMD_YOUGAME_RANKED && n == 0) {")
        self.assertIn("return slippi::online::handle(c, p, n, reply);", host[route:route + 600])
        for branch in ("if (c == MU_HP_COMMAND && n >= 1 && p[0] == MU_HP_OP_CPU_WORD) {", "if (c == MU_HP_COMMAND) {"):
            self.assertLess(route, host.index(branch), branch)
        # The other 0xFA users always carry a payload, so they never take an empty 0xFA.
        self.assertIn("if (c == CMD_REPLAY_GATE && n == 5)", host)
        self.assertIn("if (c == CMD_VOICE_MATCH && n == 8)", host)

    def test_ids_still_collide_so_the_route_is_needed(self):
        bridge = source("port/runtime/hle/native_slippi_bridge.h")
        self.assertRegex(bridge, r"CMD_YOUGAME_RANKED = 0xFA")
        self.assertRegex(source("sourceport/game/shim/mu_hp.c"), r"#define MU_HP_COMMAND\s+0xFAu")

    def test_game_asks_with_an_empty_payload_and_hides_ranked_without_an_answer(self):
        menu = source("sourceport/game/shim/mu_slippi_menu.c")
        self.assertIn("mu_slippi_cmd(CMD_YOUGAME_RANKED, NULL, 0, r, sizeof r, &got)", menu)
        # YouGame Ranked: only while offered. Slippi Ranked: never in this build (owner 11-18).
        self.assertIn("sel != MU_YG_ROW_RANKED || ranked_offered", menu)
        self.assertIn("return sel >= MU_SLP_MODE_UNRANKED && sel <= MU_SLP_MODE_PARTY;", menu)
        self.assertNotIn("(sel == MU_SLP_MODE_RANKED && ranked_offered)", menu)

    def test_presentation_does_not_offer_the_hidden_row(self):
        policy = source("port/runtime/hle/yougame_mode_policy.h")
        self.assertIn("constexpr bool yougame_ranked_offered = false;", policy)
        online = source("port/runtime/hle/slippi_online.cpp")
        query = online.split("case CMD_YOUGAME_RANKED:", 1)[1].split("default:", 1)[0]
        self.assertIn("if (yougame::active())", query)
        self.assertIn("q[0] |= 4;", query)
        self.assertIn("if (yougame::mode_policy::yougame_ranked_offered) q[0] |= 1;", query)
        menu = source("sourceport/game/shim/mu_slippi_menu.c")
        self.assertIn("if (out == NULL) return (r[0] & 1) != 0;", menu)
        self.assertIn("int mu_slippi_ranked_available(void) { return ranked_supported; }", menu)

    def test_first_frame_queues_the_negotiated_id_without_socket_io(self):
        online = source("port/runtime/hle/slippi_online.cpp")
        self.assertIn("yougame::game_started(g_game_facts.rng, g_game_facts.stage)", online)
        link = source("port/runtime/hle/yougame_link.cpp")
        start = link.split("void game_started(", 1)[1].split("void game_ended(", 1)[0]
        self.assertIn("g_room->set.start_game(ranked::game_key(rng, stage))", start)
        self.assertNotIn("->send(", start)
        self.assertIn("std::lock_guard<std::mutex> lock(g_mutex)", start)

    def test_ranked_css_splash_and_opponent_byte_use_the_new_state(self):
        css = source("sourceport/game/shim/mu_slippi_css.c")
        self.assertEqual(css.count("mu_slippi_ranked_lines(rank_lines)"), 1)
        self.assertNotIn("mu_slippi_ranked_line(", css)
        self.assertIn("ranked_header(rank_lines[0])", css)
        self.assertIn('set_text(ST_RANK_OPPONENT + i, "")', css)
        panel = source("sourceport/game/shim/mu_ranked_rank.c")
        self.assertIn("mu_ranked_read_ranks(&ranks)", panel)
        self.assertIn("mu_rank_badge_create(", panel)
        self.assertIn("set = mu_ranked_set()", panel)
        self.assertIn('"Game %u - %u:%u"', panel)
        splash = source("sourceport/game/shim/mu_slippi_splash.c")
        self.assertIn("const MuRankedSet* set = mu_ranked_set()", splash)
        self.assertIn('"Game %u - %u:%u"', splash)
        self.assertNotIn("mu_slippi_ranked_line(2,", splash)
        self.assertNotIn("mu_slippi_ranked_line(3,", splash)  # no stale "comes next" note
        self.assertIn("set->score[me], (unsigned) set->score[!me]", splash)  # local first
        online = source("port/runtime/hle/slippi_online.cpp")
        self.assertIn("ranks[1 - g_local_player_index] = yougame::opponent_rank()", online)
        link = source("port/runtime/hle/yougame_link.cpp")
        self.assertNotIn("Stage striking comes next", link)
        self.assertIn("g_rank_score = g_room->set.score_text()", link)
        before_ticket = link.split("ranked::set_contract(request);", 1)[1].split("ticket = ticket_bridge(request, 30000)", 1)[0]
        self.assertIn("ranked_header();", before_ticket)
        self.assertIn("if (!g_rank_read.in_flight) break;", before_ticket)


if __name__ == "__main__":
    unittest.main()
