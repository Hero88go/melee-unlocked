"""Window-free source guards for the Windows-only adapter runtime.

The behavioral counter/lifecycle fixture is gc_adapter_reset_test.cpp (Windows).
These checks keep every hardware boundary guarded on macOS/Linux without opening
an adapter, starting the game, or substituting a different adapter implementation.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
ADAPTER = (ROOT / "port/runtime/host/gc_adapter.cpp").read_text()
SETTINGS = (ROOT / "port/runtime/gx/pc_settings.cpp").read_text()


def body(name):
    # Select multiline definitions, skipping the non-Windows open_hid no-op.
    match = re.search(r"^.*\b" + name + r"\([^;\n]*\) \{\n", ADAPTER, re.M)
    if not match:
        raise AssertionError(f"missing definition: {name}")
    start = match.end()
    depth = 1
    for pos in range(start, len(ADAPTER)):
        depth += (ADAPTER[pos] == "{") - (ADAPTER[pos] == "}")
        if not depth:
            return ADAPTER[start:pos].strip()
    raise AssertionError(f"unterminated definition: {name}")


class AdapterGuardTest(unittest.TestCase):
    def test_single_owner_policy_and_startup(self):
        self.assertIn("bool direct_adapter_allowed() { return !options.no_gc_adapter && !yougame::stdio_bridge(); }", ADAPTER)
        startup = body("ensure_scanner_started")
        self.assertTrue(startup.startswith("if (!direct_adapter_allowed()) return;"))
        self.assertIn("std::call_once(g_scanner_started", startup)
        self.assertIn("g_scanner = std::thread(scanner_thread);", startup)
        self.assertIn("++g_test_scanner_starts", startup)
        self.assertEqual(ADAPTER.count("std::thread(scanner_thread)"), 1)

    def test_every_open_route_and_worker_is_guarded(self):
        for name in ("open_adapter", "open_libusb", "open_hid"):
            with self.subTest(name=name):
                self.assertTrue(body(name).startswith("if (!direct_adapter_allowed()) return false;"))
        self.assertIn("return open_libusb() || open_hid();", body("open_adapter"))
        for name in ("reader_thread", "start_threads"):
            self.assertTrue(body(name).startswith("if (!direct_adapter_allowed()) return;"))
        self.assertTrue(body("scanner_thread").startswith("while (g_scanner_run.load() && direct_adapter_allowed())"))
        self.assertTrue(body("writer_thread").startswith("while (g_running.load() && direct_adapter_allowed())"))
        self.assertTrue(body("adapter_read").startswith("if (!direct_adapter_allowed()) { *got = 0; return LIBUSB_ERROR_NO_DEVICE; }"))
        self.assertTrue(body("adapter_write").startswith("if (!direct_adapter_allowed()) { *wrote = 0; return LIBUSB_ERROR_NO_DEVICE; }"))
        self.assertTrue(body("close_adapter").startswith("reset_transport = reset_transport && direct_adapter_allowed();"))

    def test_settings_queries_and_reset(self):
        problem = body("gcadapter_problem")
        self.assertTrue(problem.startswith("if (yougame::stdio_bridge()) {"))
        mirror, standalone = problem.split("ensure_scanner_started();")
        self.assertIn('return mirror_live(rep) ? "Controllers come from the YouGame app"', mirror)
        self.assertIn(': "No GameCube adapter from the YouGame app";', mirror)
        self.assertIn("return g_problem;", standalone)
        self.assertTrue(body("gcadapter_request_reset").startswith("if (!direct_adapter_allowed()) return false;"))
        rate = body("gcadapter_poll_rate_hz")
        self.assertIn("if (yougame::stdio_bridge()) {", rate)
        self.assertIn("attach_mirror();", rate)
        self.assertIn("if (!mirror_live(rep)) return 0.0;", rate)
        self.assertIn("bool mirror_live(uint8_t rep[37]) { return g_mirror && read_mirror(rep); }", ADAPTER)
        self.assertIn("const float rate = g_mirror->rate_hz;", body("read_mirror"))
        self.assertIn("g_poll_rate_hz.store(live ? rate : 0.0", body("read_mirror"))
        self.assertRegex(SETTINGS, r'if \(yougame::stdio_bridge\(\)\) \{\s*settings_hint\("Managed by the YouGame app"\);\s*\} else \{\s*const bool resetting = host::gcadapter_reset_pending\(\);')

    def test_poll_and_rumble_use_only_mirror_in_bridge_mode(self):
        poll = body("gcadapter_poll")
        mirror, standalone = poll.split("} else {", 1)
        self.assertIn("if (yougame::stdio_bridge()) {", mirror)
        self.assertIn("if (!g_mirror || !read_mirror(rep)) return 0;", mirror)
        self.assertNotIn("ensure_scanner_started", mirror)
        self.assertIn("ensure_scanner_started();", standalone)
        rumble = body("gcadapter_rumble")
        self.assertIn("if (options.no_gc_adapter || port < 0 || port > 3) return;", rumble)
        self.assertIn("g_mirror->rumble[port] = v;", rumble)
        self.assertLess(rumble.index("return;\n  }"), rumble.index("g_rumble_dirty = true"))

    def test_mirror_log_stays_at_attachment_only(self):
        message = "gc adapter: reading the YouGame app's adapter"
        self.assertEqual(ADAPTER.count(message), 1)
        self.assertIn(message, body("attach_mirror"))
        self.assertTrue(body("attach_mirror").startswith("const uintptr_t handle = yougame::pad_mirror_handle();\n  if (!handle || handle == g_mirror_handle) return;"))
        self.assertNotIn("log(", body("read_mirror"))


if __name__ == "__main__":
    unittest.main()
