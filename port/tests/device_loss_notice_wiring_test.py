"""A game that starts again after its graphics device was lost says so for a few seconds, and that the online match
ended when one was on, instead of coming back to the title screen with nothing said while the opponent sees
"Opponent left"."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


def body(text, start, end):
    a = text.index(start)
    return text[a:text.index(end, a)]


class DeviceLossNoticeWiringTest(unittest.TestCase):
    def setUp(self):
        self.settings = source("port/runtime/gx/pc_settings.cpp")

    def test_the_match_flag_is_kept_in_the_environment_the_new_process_inherits(self):
        track = body(self.settings, "static void track_online_match() {", "\n}\n")
        self.assertIn("slippi::online::is_online_match()", track)
        self.assertIn('set_env_flag("MELEE_IN_ONLINE_MATCH", now);', track)
        # Read once at the first frame, then cleared, so this process starts from its own state.
        self.assertLess(track.index('std::getenv("MELEE_IN_ONLINE_MATCH")'), track.index('set_env_flag("MELEE_IN_ONLINE_MATCH", false);'))
        # Only a restart made just now: MELEE_DEVICE_LOST is "<restarts>:<unix seconds>" (host.cpp).
        self.assertIn('std::sscanf(v, "%d:%lld", &restarts, &at);', track)
        self.assertIn("now - at < 120", track)
        host = source("port/runtime/host/host.cpp")
        self.assertIn('SetEnvironmentVariableA("MELEE_DEVICE_LOST", env);', host)

    def test_tracked_every_frame_and_drawn_with_the_practice_overlays(self):
        self.assertLess(self.settings.index("  track_online_match();\n"),
                        self.settings.index("    draw_native_practice(state, options, practice, pad, have_pad);"))
        practice = body(self.settings, "static void draw_native_practice(", "\n}\n")
        self.assertIn("draw_restart_notice(options);", practice)

    def test_the_notice_words(self):
        draw = body(self.settings, "static void draw_restart_notice(RenderOptions& options) {", "\n}\n")
        self.assertIn('"THE GAME RESTARTED"', draw)
        self.assertIn('"THE GRAPHICS DRIVER STOPPED ANSWERING, SO YOUR ONLINE MATCH ENDED"', draw)
        self.assertIn("now - r.since > 8.0", draw)


if __name__ == "__main__":
    unittest.main()
