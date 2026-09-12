"""Regressions for false passes caused by incomplete playback recordings."""
import unittest
from replay_compare import compare_post_frames


class ReplayCoverageTest(unittest.TestCase):
    def setUp(self):
        self.player = dict(state=14, x=0., y=0., facing=1., percent=0., shield=60., stocks=4, char=0)
        self.frames = {f: {(0, 0): self.player.copy()} for f in range(-123, -120)}

    def test_complete(self):
        self.assertEqual(compare_post_frames(self.frames, self.frames), (3, 0, None))

    def test_missing_middle_or_tail_fails(self):
        for removed in (-122, -121):
            recorded = {f: p for f, p in self.frames.items() if f != removed}
            self.assertEqual(compare_post_frames(self.frames, recorded)[1], 1)

    def test_extra_follower_fails(self):
        recorded = {f: dict(p) for f, p in self.frames.items()}
        recorded[-122][(0, 1)] = self.player.copy()
        self.assertEqual(compare_post_frames(self.frames, recorded)[1], 1)

    def test_shield_divergence_fails(self):
        recorded = {f: {k: dict(p) for k, p in ps.items()} for f, ps in self.frames.items()}
        recorded[-122][(0, 0)]['shield'] = 50.
        self.assertEqual(compare_post_frames(self.frames, recorded)[1], 1)


if __name__ == '__main__':
    unittest.main()
