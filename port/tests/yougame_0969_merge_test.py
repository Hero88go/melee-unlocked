"""Regression coverage for the v0.9.69 CMake merge boundaries.

The shader test added upstream must coexist with the YouGame tests and the
production-disabled padBack hook. Runtime behavior is checked in later stages.
"""
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]


class YouGame0969MergeTest(unittest.TestCase):
    def test_shader_and_merge_regressions_are_registered_in_both_build_paths(self):
        for path, prefix in (("CMakeLists.txt", "port/"), ("port/CMakeLists.txt", "")):
            with self.subTest(path=path):
                cmake = (ROOT / path).read_text(encoding="utf-8")
                self.assertIsNone(re.search(r"^(?:<{7}|={7}|>{7})(?: |$)", cmake, re.M))
                for name, filename in (("shader_auto_fit", "shader_auto_fit_test.py"),
                                       ("yougame_0969_merge", "yougame_0969_merge_test.py")):
                    command = ('add_test(NAME port_' + name + ' COMMAND "${Python3_EXECUTABLE}" '
                               '"${CMAKE_CURRENT_SOURCE_DIR}/' + prefix + 'tests/' + filename + '")')
                    self.assertEqual(cmake.count(command), 1)

    def test_conflicted_section_keeps_policy_and_integration_tests(self):
        cmake = (ROOT / "port/CMakeLists.txt").read_text(encoding="utf-8")
        # Choosing the upstream side of the large conflict drops these targets.
        for name in ("lcancel_policy", "slippi_game_prep", "stage_read_pin",
                     "yougame_agent_fight", "yougame_agent_fight_off", "yougame_ranked",
                     "yougame_relay_pads", "yougame_ranked_rules", "yougame_mode_policy",
                     "host_stub_policy", "yougame_merge", "yougame_0956_merge",
                     "yougame_0962_merge", "yougame_0963_merge", "yougame_0968_merge",
                     "yougame_mod_online", "yougame_mod_online_noslippi"):
            with self.subTest(name=name):
                self.assertEqual(len(re.findall(r"add_test\(NAME port_" + name + r"\s", cmake)), 1)

    def test_agent_hook_stays_opt_in_and_imgui_keeps_gamepad_focus_fix(self):
        cmake = (ROOT / "port/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertRegex(cmake, r'option\(YOUGAME_AGENT_TEST_HOOK "[^"\n]+" OFF\)')
        hook = cmake.split("if(YOUGAME_AGENT_TEST_HOOK)", 1)[1].split("endif()", 1)[0]
        for target in ("melee_source", "melee_port", "runtime"):
            self.assertIn("target_compile_definitions(" + target + " PRIVATE YOUGAME_AGENT_TEST_HOOK)", hook)
        self.assertEqual(cmake.count("target_compile_definitions(port_imgui PRIVATE IMGUI_IMPL_WIN32_DISABLE_GAMEPAD)"), 2)


if __name__ == "__main__":
    unittest.main()
