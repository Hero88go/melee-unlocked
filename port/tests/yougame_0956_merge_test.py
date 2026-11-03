"""Source regressions for the v0.9.56 window conflict and retained loader #9 fix.

Run with python3 port/tests/yougame_0956_merge_test.py. Windows behavior is
validated by the later build and runtime stages.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


class YouGame0956MergeTest(unittest.TestCase):
    def test_window_header_keeps_both_interfaces_once(self):
        header = source("port/runtime/host/window.h")
        self.assertIsNone(re.search(r"^(?:<{7}|={7}|>{7})(?: |$)", header, re.M))
        for declaration in ("bool window_take_activation();", "bool window_take_escape();",
                            "const char* window_menu_key();"):
            with self.subTest(declaration=declaration):
                self.assertEqual(header.count(declaration), 1)

    def test_upstream_activation_reaches_both_renderers(self):
        window = source("port/runtime/host/window.cpp")
        activated = window.split("case WM_ACTIVATEAPP:", 1)[1].split("break;", 1)[0]
        self.assertIn("if (w) g_activated.store(true);", activated)
        self.assertIn("bool window_take_activation() { return g_activated.exchange(false); }", window)
        for backend in ("gx_d3d11.cpp", "gx_d3d12.cpp"):
            with self.subTest(backend=backend):
                renderer = source("port/runtime/gx/" + backend)
                self.assertRegex(renderer, r"if \(host::window_take_activation\(\) && "
                                          r"opts_\.exclusive_fullscreen[^\n]*apply_fullscreen_mode\(\);")
        self.assertIn("bool window_take_activation() { return false; }",
                      source("port/tests/gpu_resource_test.cpp"))

    def test_yougame_menu_routing_survives_activation_merge(self):
        window = source("port/runtime/host/window.cpp")
        routing = window.split("if (yougame::stdio_bridge()) {", 1)[1].split("} else {", 1)[0]
        self.assertIn('yougame::relay_key("escape")', routing)
        self.assertIn("if (w == VK_F1 && !(l & (1 << 30))) g_escape_press.store(true);", routing)
        self.assertIn('const char* window_menu_key() { return yougame::stdio_bridge() ? yougame::parity::kMenuHint : "Esc"; }', window)
        self.assertIn("bool window_take_escape() { return g_escape_press.exchange(false); }", window)

    def test_stage_read_pin_tracks_online_state_at_call_site(self):
        # The behavioral stage_read_pin_test exercises transitions; this catches losing the
        # runtime connection to that policy, including the equal-length partial-read case.
        cosmetics = source("port/runtime/host/cosmetic_mods.cpp")
        self.assertIn("stage_read_pin::PinState<std::shared_ptr<const std::vector<uint8_t>>>", cosmetics)
        self.assertIn("form = stage_read_pin::pick(pin, form, online_now, file_offset, form->size());", cosmetics)
        pin = source("port/runtime/host/stage_read_pin.h")
        self.assertIn("offset == 0 || !pin.form || pin.online != online_now", pin)
        self.assertIn("pin = {current, online_now, current_size};", pin)


if __name__ == "__main__":
    unittest.main()
