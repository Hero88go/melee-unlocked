"""Prevent incomplete or input-divergent recordings from passing the cross-play gate."""
import copy
import unittest
from slp_diff import first_difference


class CoverageTests(unittest.TestCase):
    def setUp(self):
        self.a = {-123: {(0, 0): {"x": 1.0}, (1, 0): {"x": 2.0}},
                  -122: {(0, 0): {"x": 3.0}, (1, 0): {"x": 4.0}}}
        self.b = copy.deepcopy(self.a)

    def diff(self):
        return first_difference(self.a, self.b, ("x",))

    def test_equal(self):
        self.assertIsNone(self.diff())

    def test_truncated(self):
        del self.b[-122]
        self.assertEqual(self.diff(), (-122, None, ["missing frame in B"]))

    def test_extra_frame(self):
        self.b[-121] = copy.deepcopy(self.b[-122])
        self.assertIsNotNone(self.diff())

    def test_missing_player(self):
        del self.b[-122][(1, 0)]
        self.assertEqual(self.diff(), (-122, (1, 0), ["missing player in B"]))

    def test_extra_follower(self):
        self.b[-123][(0, 1)] = {"x": 1.0}
        self.assertIsNotNone(self.diff())

    def test_empty(self):
        self.assertIsNotNone(first_difference({}, {}, ("x",)))

    def test_missing_field(self):
        self.b[-123][(0, 0)] = {}
        self.assertIsNotNone(self.diff())

    def test_signed_zero(self):
        self.a[-123][(0, 0)]["x"] = 0.0
        self.b[-123][(0, 0)]["x"] = -0.0
        self.assertIsNotNone(self.diff())

    def test_changed_input(self):
        self.assertIsNotNone(first_difference({0: {(0, 0): {"buttons": 0}}},
                                             {0: {(0, 0): {"buttons": 256}}}, ("buttons",)))


if __name__ == "__main__":
    unittest.main()
