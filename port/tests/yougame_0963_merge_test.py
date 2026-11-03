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

    def test_upstream_budget_uses_one_pacer_sample_per_retrace(self):
        online = source("port/runtime/hle/slippi_online.cpp")
        watch = online.split("static void budget_watch() {", 1)[1].split("void trace_close_tick()", 1)[0]
        self.assertLess(watch.index("if (retrace == g_budget_retrace) return;"),
                        watch.index("g_budget_retrace = retrace;"))
        self.assertLess(watch.index("g_budget_retrace = retrace;"),
                        watch.index("g_frame_budget.update("))
        self.assertIn("live_online_frame_protection().load(std::memory_order_relaxed)", watch)
        self.assertIn("g_budget_armed && is_online_match()", watch)
        self.assertIn("gx::subframes_active(), host::last_sim_frame_late()", watch)
        self.assertIn("FrameBudget::Change::Suspend", watch)
        self.assertIn("gx::set_subframe_suspended(true);", watch)
        self.assertIn("FrameBudget::Change::Resume", watch)
        self.assertIn("gx::set_subframe_suspended(false);", watch)
        trace = online.split("void trace_close_tick() {", 1)[1].split("void trace_end_match()", 1)[0]
        self.assertIn("budget_watch();", trace)
        for removed in ("g_budget_window", "g_budget_over", "MELEE_TEST_BUDGET_MS",
                        "budget_watch(double"):
            self.assertNotIn(removed, online)

    def test_budget_resets_after_match_and_results_disarm_it(self):
        online = source("port/runtime/hle/slippi_online.cpp")
        reset = online.split("static void budget_reset() {", 1)[1].split("static void budget_test_load()", 1)[0]
        self.assertIn("g_frame_budget.reset();", reset)
        self.assertIn("gx::set_subframe_suspended(false);", reset)
        end = online.split("void trace_end_match() {", 1)[1].split("\n}", 1)[0]
        self.assertLess(end.index("g_budget_armed = false;"), end.index("budget_reset();"))
        report = online.split("void handle_report_game(", 1)[1].split("\n}", 1)[0]
        self.assertIn("trace_end_match();", report)

    def test_new_diagnostic_loads_keep_yougame_and_parent_policy(self):
        online = source("port/runtime/hle/slippi_online.cpp")
        load = online.split("static void budget_test_load() {", 1)[1].split("static void budget_watch()", 1)[0]
        for name in ("MELEE_TEST_SIM_LOAD_MS", "MELEE_TEST_SUBFRAME_COST_MS"):
            with self.subTest(name=name):
                self.assertIn('host::test_env::getenv("' + name + '")', load)
                self.assertNotIn('std::getenv("' + name + '")', load)
        self.assertIn("if (ms <= 0.0 || !g_budget_armed) return;", load)
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
