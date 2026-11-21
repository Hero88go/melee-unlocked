"""A player whose first frame is slow (a stage load on a slow GPU took 13.3 s) is not dropped as gone before the
match begins: before the other side's first frame the stall limit is 30 s, after it the usual 7 s."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


def body(text, start, end):
    a = text.index(start)
    return text[a:text.index(end, a)]


class SlowLoadStartWiringTest(unittest.TestCase):
    def test_the_limit_is_longer_only_before_the_first_frame(self):
        online = (ROOT / "port/runtime/hle/slippi_online.cpp").read_text(encoding="utf-8-sig")
        fn = body(online, "bool should_skip_online_frame(", "// A report of \"it froze, then felt delayed\"")
        self.assertIn("const int stall_limit = latest < 1 ? 60 * 30 : 60 * 7;", fn)
        self.assertIn("if (g_stall_frame_counts[i] > stall_limit) {", fn)
        self.assertNotIn("> 60 * 7)", fn)
        self.assertLess(fn.index("const int stall_limit"), fn.index("ForceDisconnectPlayer("))


if __name__ == "__main__":
    unittest.main()
