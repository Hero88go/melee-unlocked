"""Source regressions for the v0.9.63 online-budget and simulation-state merge.

The existing listing, loader, results, focus and bridge regressions remain intact.
Platform builds and runtime validation run in later release stages.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


class YouGame0963MergeTest(unittest.TestCase):
    def test_conflict_boundaries_have_no_markers(self):
        for path in ("port/runtime/hle/slippi_online.cpp", "port/runtime/host/host.cpp"):
            with self.subTest(path=path):
                self.assertIsNone(re.search(r"^(?:<{7}|={7}|>{7})(?: |$)", source(path), re.M))

    def test_frame_rate_is_never_lowered_for_the_player(self):
        # The online "protect match speed" pause and the offline hold to 60 drawn frames were
        # removed: the frame rate stays what the player chose, online and off.
        online = source("port/runtime/hle/slippi_online.cpp")
        backend = source("port/runtime/gx/threaded_backend.cpp")
        for removed in ("budget_watch", "budget_reset", "FrameBudget", "set_subframe_suspended",
                        "live_online_frame_protection"):
            with self.subTest(removed=removed):
                self.assertNotIn(removed, online)
        for removed in ("held_to_60", "subframe_suspended"):
            with self.subTest(removed=removed):
                self.assertNotIn(removed, backend)
        report = online.split("void handle_report_game(", 1)[1].split("\n}", 1)[0]
        self.assertIn("trace_end_match();", report)
        policy = source("port/runtime/host/test_env.h")
        self.assertIn("yougame_session || gameplay::vanilla_only() ? nullptr", policy)

    def test_upstream_deadline_signal_and_local_timing_report_coexist(self):
        host = source("port/runtime/host/host.cpp")
        header = source("port/runtime/host/host.h")
        for name in ("last_sim_frame_ms", "last_sim_frame_work_ms", "last_sim_frame_late",
                     "report_sim_work"):
            with self.subTest(name=name):
                self.assertEqual(header.count(name + "();"), 1)
                self.assertEqual(host.count(name + "() {"), 1)
        self.assertEqual(host.count("static FILE* g_sim_times = nullptr;"), 1)
        self.assertEqual(host.count("static bool g_last_sim_late = false;"), 1)
        pacing = host.split("g_next_frame += std::chrono::microseconds", 1)[1].split("perf::active->begin();", 1)[0]
        self.assertLess(pacing.index("g_last_sim_late = !(g_next_frame > now);"),
                        pacing.index("wait_for_tick(g_next_frame)"))
        self.assertIn("bool last_sim_frame_late() { return g_last_sim_late; }", host)
        report = host.split("void report_sim_work() {", 1)[1].split("\n}", 1)[0]
        self.assertIn("if (g_sim_times) std::fflush(g_sim_times);", report)
        self.assertIn("g_sim_times = f;", host)


if __name__ == "__main__":
    unittest.main()
