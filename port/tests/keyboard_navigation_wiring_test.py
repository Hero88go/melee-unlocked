"""The ImGui-only copy is published/consumed beside raw pads on Win32 and SDL hosts."""
from pathlib import Path
import unittest
import online_menu_test as menu
from yougame_console_wiring_test import body

ROOT = Path(__file__).resolve().parents[2]


class KeyboardNavigationWiringTest(unittest.TestCase):
    def test_snapshot_read_keeps_raw_callers_and_agent_acknowledgement(self):
        for path in ('port/runtime/host/window.cpp', 'port/linux/window_sdl.inl'):
            with self.subTest(path=path):
                source = (ROOT / path).read_text()
                getter = body(source, 'window_ui_pads')
                menu.OnlineMenuTest().compile_fixture('keyboard-ui-snapshot', r'''
#include <mutex>
#include <cstdlib>
#include <cstdio>
#define YOUGAME_AGENT_TEST_HOOK
struct PadState { int button=0, stick_x=0; };
std::mutex g_ui_pad_mutex;
PadState g_ui_pads[4], g_ui_navigation_pads[4];
bool g_ui_has_pad=true;
int g_ui_agent_step=0;
namespace yougame::agent_test {
struct Script { int calls=0; bool consume_ui(int) { return ++calls == 1; } } pad_back;
}
bool read(PadState pads[4], PadState navigation_pads[4]=nullptr) {
''' + getter + r'''
}
#define CHECK(x) do { if (!(x)) std::abort(); } while(0)
int main() {
  for(int i=0;i<4;++i) { g_ui_pads[i]={0x1000,127}; g_ui_navigation_pads[i]={0,0}; }
  PadState raw[4],nav[4]; CHECK(read(raw,nav));
  for(int i=0;i<4;++i) CHECK(raw[i].button==0x1000 && raw[i].stick_x==127 && nav[i].button==0 && nav[i].stick_x==0);
  CHECK(read(raw)); // Other callers keep the original API and values (apart from stale scripts).
''' + ('CHECK(raw[0].button==0 && yougame::agent_test::pad_back.calls==2);' if path.endswith('.cpp') else 'CHECK(raw[0].button==0x1000);') + r'''
  CHECK(raw[1].button==0x1000);
  puts("UI navigation snapshot: raw/filtered copies, optional destination and stale script handling");
}
''')

    def test_only_imgui_feed_uses_filtered_copy(self):
        source = (ROOT / 'port/runtime/host/window.cpp').read_text()
        self.assertIn('read_devices(out, debug, true, &ui.keyboard_navigation, false, g_extra_pads);', source)   # 1.0.34: pads 5 and 6 (Triples)
        self.assertIn('ui.keyboard_ports[i] = debug.feeding[i].kind == DeviceKind::Keyboard;', source)
        self.assertIn('g_ui_navigation_pads[i] = keyboard_ports[i] ? keyboard_navigation : pads[i];', source)
        self.assertIn('g_ui_navigation_pads[i].button &= pads[i].button;', source)
        self.assertIn('g_ui_navigation_pads[i] = pads[i];', source)  # Console-consumed input stays consumed.
        self.assertIn('!imgui_keyboard_navigation_key(vk, shift_only) && key(vk)', source)
        self.assertIn('g_box_keyboard_navigation, g_stick_modifier_percent', source)
        self.assertIn('if (shortcut_swallowed(vk)) return false;', source)
        self.assertIn('ui.keyboard_ports[takeover.first] = ui.keyboard_ports[takeover.second]', source)
        ui = (ROOT / 'port/runtime/gx/pc_settings.cpp').read_text()
        self.assertIn('host::window_ui_pads(pads, navigation_pads)', ui)
        self.assertIn('settings_input::gamepad(io, navigation_pads[nav_port]);', ui)
        self.assertIn('nav_pads[i] = {pads[i].err == 0, pads[i].button, pads[i].stick_x, pads[i].stick_y};', ui)
        self.assertNotIn('io.AddKeyEvent(ImGuiKey_GamepadStart, (pad.button', ui)
        self.assertIn('host::window_ui_pads(pads)', (ROOT / 'port/runtime/host/replay_control.cpp').read_text())
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertEqual(cmake.count('include(port/tests/keyboard_navigation.cmake)'), 2)
        self.assertLess(cmake.index('include(port/tests/keyboard_navigation.cmake)'), cmake.index('  return()'))


if __name__ == '__main__':
    unittest.main()
