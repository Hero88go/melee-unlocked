"""Source regressions for the v0.9.65 merge; no game, network or Windows build.

Existing listing, results, loader, focus and agent-hook tests remain unchanged.
"""
from pathlib import Path
import re
import unittest

from relay_pads_wiring_test import code, function

ROOT = Path(__file__).resolve().parents[2]
CONFLICTED = (
    "port/CMakeLists.txt", "port/runtime/hle/slippi_online.cpp",
    "port/runtime/host/controller_profiles.cpp", "port/runtime/host/controller_profiles.h",
    "port/runtime/host/net_trace_file.cpp", "port/runtime/host/window.cpp",
)


def source(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


# Melee Unlocked 1.0: these sites follow yougame::session() (an app session, or a YouGame row
# picked under the launcher), not the app-only yougame::active().
class YouGame0965MergeTest(unittest.TestCase):
    def test_merge_boundaries_have_no_markers(self):
        for path in CONFLICTED:
            with self.subTest(path=path):
                self.assertIsNone(re.search(r"^(?:<{7}|={7}|>{7})(?: |$)", source(path), re.M))

    def test_rematch_version_check_preserves_yougame_and_local_peers(self):
        online = code(source("port/runtime/hle/slippi_online.cpp"))
        prepare = function(online, "prepare_online_match_state")
        gate = re.search(r"if\s*\(([^{};]*?)\)\s*\{\s*std::string version_error;", prepare)
        self.assertIsNotNone(gate)
        for condition in ("local_ready", "remote_ready", "!yougame::session()",
                          "!Matchmaking::local_peer.enabled"):
            self.assertIn(condition, gate.group(1))
        version = prepare[gate.end():].split("if (remote_count == 1)", 1)[0]
        self.assertIn("g_user->PollOnlineCompatibility(&version_error)", version)
        refusal = version.split("if (version < 0) {", 1)[1].split("}", 1)[0]
        for action in ("cleanup_connection();", "g_forced_error = version_error;",
                       "prepare_online_match_state(q);", "return;"):
            self.assertIn(action, refusal)
        self.assertIn("if (version == 0) remote_ready = 0;", version)
        self.assertIn("g_user->ResetOnlineCompatibility();", function(online, "cleanup_connection"))
        self.assertIn("g_user->ResetOnlineCompatibility();", function(online, "handle_online_inputs"))
        ranked = prepare.split("if (yougame::session() && g_last_search.mode == Matchmaking::RANKED)", 1)[1]
        self.assertLess(ranked.index("if (yougame::ranked_set_over())"),
                        ranked.index("g_user->PollOnlineCompatibility("))
        self.assertIn("cleanup_connection(yougame::Leave::Over);", ranked)
        self.assertIn("if (yougame::ranked_hold()) remote_ready = 0;", ranked)

    def test_initial_search_and_connection_skip_slippi_checks_in_yougame(self):
        net = code(source("port/runtime/hle/slippi_net.cpp"))
        for name, state in (("startMatchmaking", "INITIALIZING"),
                            ("handleConnecting", "OPPONENT_CONNECTING")):
            with self.subTest(name=name):
                body = function(net, name)
                self.assertRegex(body, r"^\s*if \(!yougame::session\(\) && !local_peer.enabled\) \{")
                check = body.split("if (!user_ || !user_->CheckSessionCompatibility(&error)) {", 1)[1]
                self.assertIn("fail(", check.split("}", 1)[0])
                self.assertIn("return;", check.split("}", 1)[0])
                self.assertIn("if (is_mm_terminated_ || state_ != " + state + ") return;", check)
        search = function(net, "startMatchmaking")
        self.assertIn("startYouGameMatchmaking();", search)

    def test_both_relay_providers_precede_native_xinput(self):
        window = code(source("port/runtime/host/window.cpp"))
        state = function(window, "xinput_state")
        bridge = state.index("if (yougame::relay_pads::active())")
        provider = state.index("if (controller_relay::active())")
        native = state.index("xinput_ask(")
        self.assertLess(bridge, provider)
        self.assertLess(provider, native)
        self.assertIn("return yougame::relay_pads::read(idx, state);", state[:provider])
        sample = state[provider:native]
        self.assertIn("*state={};", sample)
        self.assertIn("if (!controller_relay::sample(CaptureDevice::XInputPad,idx,s)) return false;", sample)
        for field in ("wButtons", "bLeftTrigger", "bRightTrigger", "sThumbLX", "sThumbLY", "sThumbRX", "sThumbRY"):
            self.assertIn(field, sample)
        self.assertIn("return true;", sample)
        probe = function(window, "xinput_probe_start")
        # Check both before creating the worker and in its loop, before each native probe.
        before_worker, worker = probe.split("std::thread", 1)
        before_probe = worker.split("xinput_ask(", 1)[0]
        for part in (before_worker, before_probe):
            for provider in ("yougame::relay_pads", "controller_relay"):
                self.assertIn("if (" + provider + "::active()) return;", part)
        self.assertIn("controller_relay::rumble(src.kind,src.index,on)", function(window, "input_rumble"))
        self.assertIn("controller_relay::rumble(src.kind,src.index,on)", function(window, "input_rumble_local"))

    def test_desync_trace_retention_keeps_agent_path_cleanup(self):
        trace = source("port/runtime/host/net_trace_file.cpp")
        record = trace.split("case Command::Row:", 1)[1].split("case Command::End:", 1)[0]
        self.assertRegex(record, r"if \(command.record.flags & \([^)]*\bkDesync\b[^)]*\)\) marked_ = true;")
        close = function(code(trace), "close_file")
        discard = close.split("if (rows_ == 0 || !marked_) {", 1)[1].split("} else if", 1)[0]
        self.assertRegex(discard, r"#ifdef YOUGAME_AGENT_TEST_HOOK\s+yougame::agent_paths::remove_output\(path_\);")
        self.assertRegex(discard, r"#else\s+std::remove\(path_\.c_str\(\)\);")
        self.assertIn("yougame::agent_paths::rename_output(path_, final_, ec);", close)

    def test_profiles_keep_all_actions_without_platform_headers(self):
        header = source("port/runtime/host/controller_profiles.h")
        profiles = source("port/runtime/host/controller_profiles.cpp")
        self.assertIn('#include "input_binding_types.h"', header)
        self.assertIn("kProfileActions = static_cast<int>(BindAction::Count)", header)
        self.assertIn("std::size(kActions) == static_cast<size_t>(BindAction::Count)", profiles)
        for text in (header, profiles):
            for include in ('"host.h"', '"input_bindings.h"', '<windows.h>'):
                self.assertNotIn("#include " + include, text)
        actions = profiles.split("constexpr const char* kActions[] = {", 1)[1].split("};", 1)[0]
        for action in ("ModX", "ModY", "LightShield", "MidShield", "StickMod"):
            self.assertEqual(actions.count('"' + action + '"'), 1)

    def test_cmake_retains_upstream_targets_and_shared_profile_tests(self):
        port = re.sub(r"#[^\n]*", "", source("port/CMakeLists.txt"))
        root = source("CMakeLists.txt")
        shared = source("port/tests/controller_profiles.cmake")
        for target in ("temporal_history", "controller_relay"):
            self.assertEqual(port.count("add_executable(port_" + target + "_test "), 1)
            self.assertEqual(port.count("add_test(NAME port_" + target + " "), 1)
        for target in ("controller_profiles", "dolphin_profile"):
            self.assertNotIn("add_executable(port_" + target + "_test", port)
            self.assertEqual(shared.count("add_executable(port_" + target + "_test"), 1)
        self.assertIn("include(port/tests/controller_profiles.cmake)", root)
        self.assertIn('option(MELEE_REQUIRE_STREAMLINE ', port)
        self.assertIn("if(MELEE_REQUIRE_STREAMLINE AND NOT", port)
        self.assertEqual(port.count("IMGUI_IMPL_WIN32_DISABLE_GAMEPAD"), 2)
        for target in ("stage_read_pin", "yougame_mode_policy", "yougame_agent_fight"):
            self.assertIn("add_executable(port_" + target + "_test", port)
        self.assertIn('option(YOUGAME_AGENT_TEST_HOOK ', port)
        self.assertIn("add_test(NAME port_yougame_0965_merge ", root)


if __name__ == "__main__":
    unittest.main()
