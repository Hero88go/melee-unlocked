"""Host-only match presence and verbatim native header pins; no app or user profile access."""
import hashlib
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class MatchStateWiring(unittest.TestCase):
    def test_native_headers_are_the_supplied_0b5798f46_copies(self):
        for name, digest in {
            # 106: MU's copy adds Open YouGame and the administrator text to the start window.
            # 113: merged with YouGame 823c0f3ce (window to the front, taskbar flash, no "smaller helper").
            # 113: the administrator window offers only OK (YouGame is installed there, Get YouGame can't help).
            # 115: both headers verbatim from YouGame v2 a678bb7b (MU's Open YouGame and one-OK window upstreamed).
            # 114: YouGame v2 6db35802e raised rule (UAC off / Windows Sandbox: an elevated game whose shell is the same elevated user starts YouGame).
            # 120: yougame_start.h verbatim from YouGame v2 4b69a7a28 (o.ask: LATER at launch, ON_USE at the press; Play offline).
            # 120: then YouGame claude/c3-window-d258 063f1abcf (the drawn C3 window, o.name).
            # 121: then YouGame claude/v2 efdafa604 (PR 2413: /desktop?from=game&game=<slug>, long names wrap).
            # 122: then YouGame claude/start-dpi-b7d2 b2b8a304e (buttons placed while per-monitor aware, scaled screens).
            # 122: then YouGame claude/start-get-c91e 0544f1538 (Get YouGame installs YouGame in the start window, PR 2454).
            # 122.1: then YouGame claude/start-get-fix-d3a1 3def89e29 (PR 2456: no elevated installer from an admin game, stop races).
            # 122.2: then YouGame claude/start-get-label-f6c3 8f982f469 (PR 2462: progress label fits, Esc mid-download not saved).
            # 122.6: both from YouGame claude/v2 (3c9a9d020: staging's start installer, the mini app; e367c288d: a game
            # started before the app finds it on its own).
            # 122.19: then YouGame claude/v2 4198ed40e (PR 2506: the offer says sign-in comes after the install).
            # Then both verbatim from YouGame's public engines/native (YouGame 77c794d9e: mini retired; sheets may take the foreground): Get YouGame closes the
            # window and installs the full YouGame app in the background (YG_START_GETTING, yg_start_get_progress);
            # yg_config.staging: a build looks only for its own YouGame's channel.
            'yougame_start.h': 'f7f949033c1e939ab3325c20187ce1dca06b7c795eb24a0e8b0e7b6e62ec33ef',
            'yougame.h': 'a29b4b1fb8629439f2bde4f3f071bc0bafc81d6371720c4d02eb45a78b64e5d5',
        }.items():
            self.assertEqual(hashlib.sha256((ROOT / 'port/third_party/yougame_start' / name).read_bytes()).hexdigest(), digest)

    def test_online_start_uses_connected_frame_one_even_before_a_previous_report(self):
        online = (ROOT / 'port/runtime/hle/slippi_online.cpp').read_text()
        start = online.index('yougame::online_match_state(true);')
        self.assertIn('if (!is_disconnected()) ', online[start-30:start])
        self.assertIn('if (frame == 1)', online[start-500:start])
        self.assertLess(start, online.index('if (!g_summary.active && !is_disconnected())'))
        self.assertEqual(online.count('yougame::online_match_state(true);'), 1)
        self.assertIn('yougame::online_match_state(false);', online[online.index('static void finish_summary(const char* result) {'):])

    def test_only_worker_sends_the_hint_and_refusals_bypass_gameplay_policy(self):
        link = (ROOT / 'port/runtime/hle/yougame_link.cpp').read_text()
        self.assertIn('match_hint_worker().start([](int open) { yg_match_state(open, nullptr, nullptr); });', link)
        sender = link.split('void yg_match_state(int open, yg_callback, void*) {', 1)[1].split('\n}', 1)[0]
        self.assertIn('{"type", "matchState"}', sender)
        self.assertIn('{"open", open != 0}', sender)
        self.assertNotIn('bridge(', sender)
        hook = link.split('void online_match_state(bool open) {', 1)[1].split('\n}', 1)[0]
        self.assertNotIn('bridge', hook)
        self.assertNotIn('std::thread', hook)
        self.assertNotIn('available()', hook)
        self.assertIn('match_hint_worker().set(open);', hook)
        for fn in ['game_ended(const GameEnd& end)', 'end_session(Leave why)']:
            body = link.split('void ' + fn + ' {', 1)[1].split('\n}', 1)[0]
            self.assertIn('online_match_state(false);', body)

    def test_mac_hook_and_frameworks_are_enabled_without_enabling_linux_hook(self):
        link = (ROOT / 'port/runtime/hle/yougame_link.cpp').read_text()
        self.assertIn('#if defined(_WIN32) || defined(__APPLE__)\n#define YOUGAME_START_IMPLEMENTATION', link)
        self.assertIn('#if !defined(_WIN32) && !defined(__APPLE__)\nextern "C" yg_start_result yg_start', link)
        self.assertIn('return MELEE_YOUGAME_MAC_TRAY_FROM;', link)
        self.assertIn('return MELEE_YOUGAME_MAC_PROD_TRAY_FROM;', link)
        self.assertIn('o.tray_from = tray_from(staging);', link)
        build = (ROOT / 'port/mac/build_full.py').read_text()
        for framework in ['CoreServices', 'CoreFoundation']:
            self.assertIn("'-framework','" + framework + "'", build)


if __name__ == '__main__':
    unittest.main()
