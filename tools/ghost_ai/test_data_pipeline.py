"""Regression checks for identity, final-frame selection and trainer/controller alignment."""
import unittest
from types import SimpleNamespace

import numpy as np
import extract
import features as ft
import train
from players import verified_port
from collect_verified import eligible, split_group, split_of


def player(code, character):
    return SimpleNamespace(netplay=SimpleNamespace(code=code) if code else None, character=character)


class DataPipelineTests(unittest.TestCase):
    def test_identity_never_inferred_from_character(self):
        self.assertIsNone(verified_port([player('', 20), player('', 2)], 'mang0'))
        self.assertIsNone(verified_port([player('BBB#960', 20), player('', 2)], 'mang0'))
        self.assertEqual(verified_port([player('', 20), player('MANG＃0', 20)], 'mang0'), 1)
        self.assertIsNone(verified_port([player('MANG#0', 2), player('', 20)], 'mang0'))
        self.assertIsNone(verified_port([player('MANG#0', 20), player('MANG#0', 20)], 'mang0'))

    def test_rollback_selects_last_revision_without_future_abandoned_frames(self):
        ids = np.array([-61, -60, -59, -58, -59, -58, -57, -56, -58, -57])
        selected = extract.canonical_indices(ids)
        np.testing.assert_array_equal(selected, [1, 4, 8, 9])
        np.testing.assert_array_equal(ids[selected], [-60, -59, -58, -57])

    def test_grouped_splits(self):
        a = {'startAt': '2025-01-03T00:01:00Z'}
        b = {'startAt': '2025-01-03T20:01:00Z'}
        self.assertEqual(split_group(a), split_group(b))
        self.assertEqual(split_of(split_group(a)), split_of(split_group(b)))
        self.assertIsNone(split_group({}))

    def test_training_alignment_after_rollback(self):
        frames = np.arange(120, dtype=np.int32)
        frames = np.insert(frames, 51, [49, 50])
        t = len(frames)
        data = {'frame': frames, 'stage': np.int16(31)}
        for prefix in ('s_', 'o_'):
            data[prefix+'num'] = np.zeros((t, 16), np.float32)
            data[prefix+'state'] = np.full(t, 14, np.int16)
            data[prefix+'char'] = np.zeros(t, np.int8)
            data[prefix+'flags'] = np.zeros((t, 5), np.uint8)
            data[prefix+'pad'] = np.zeros((t, 5), np.float32)
            data[prefix+'btn'] = np.zeros(t, np.uint16)
        data['s_num'][:, 0] = np.arange(t)
        selected = extract.canonical_indices(frames)
        data['s_btn'][selected[52]] = 0x100
        rows = train.game_rows(data, 0)
        self.assertEqual(len(rows[0]), 118)
        self.assertAlmostEqual(float(rows[0][50, 0]), float(selected[51]) / 100, places=3)
        self.assertEqual(rows[5][50, 0], 1)  # target frame 52, state frame 51
        data['frame'] = frames + (frames >= 80)
        self.assertIsNone(train.game_rows(data, 0))  # missing final frames are not contiguous samples

    def test_four_and_five_slot_checkpoints(self):
        for count in (4, 5):
            model = train.Net(n_pros=count)
            self.assertEqual(tuple(model.pro.weight.shape), (count, train.EMB_PRO))


if __name__ == '__main__':
    unittest.main()
