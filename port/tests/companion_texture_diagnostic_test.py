import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from companion_texture_diagnostic import selector_slots, xxh64


class CompanionTextureDiagnosticTest(unittest.TestCase):
    def test_sparse_selector_grid(self):
        slots = selector_slots()
        self.assertEqual(len(slots), 118)
        self.assertEqual(slots[0], {"frame": 0, "target": "PlCaNr.dat"})
        self.assertIn({"frame": 92, "target": "PlFxGr.dat"}, slots)
        self.assertIn({"frame": 167, "target": "PlYsAq.dat"}, slots)
        self.assertEqual(sum(slot["target"] == "PlGwNr.dat" for slot in slots), 4)

    def test_xxh64_matches_reference_vectors(self):
        self.assertEqual(xxh64(b""), 0xEF46DB3751D8E999)
        self.assertEqual(xxh64(b"test"), 0x4FDCCA5DDB678139)


if __name__ == "__main__":
    unittest.main()
