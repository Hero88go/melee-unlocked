"""Regression tests for Gecko caves that return past their original call site."""

import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "recomp"))
from analyze import _computed_return_delta  # noqa: E402
from emit import Emitter  # noqa: E402
from gekko import decode  # noqa: E402


class ComputedReturnTest(unittest.TestCase):
    def test_detects_lr_reload_with_positive_return_delta(self):
        # lwz r12,24(r1); addi r12,r12,8; mtlr r12; blr
        insns = [decode(0x80000000 + i * 4, word) for i, word in enumerate(
            (0x81810018, 0x398C0008, 0x7D8803A6, 0x4E800020))]
        self.assertEqual(_computed_return_delta(insns, 3), 8)

    def test_plain_saved_lr_return_has_no_delta(self):
        insns = [decode(0x80000000 + i * 4, word) for i, word in enumerate(
            (0x81810018, 0x7D8803A6, 0x4E800020))]
        self.assertIsNone(_computed_return_delta(insns, 2))

    def test_emitter_counts_eligible_check_and_emits_adjusted_resume(self):
        callee = SimpleNamespace(computed_returns={8})
        caller = SimpleNamespace(labels={0x80001008})
        emitter = Emitter(None, None, {0x80002000: callee}, set(), {0x80002000: "f_callee"})
        emitted = emitter._call(0x80002000, 0x80001000, caller)
        self.assertIn("++ppc::g_computed_return_checks", emitted)
        self.assertIn("if (c.lr == 0x80001008u)", emitted)
        self.assertIn("++ppc::g_resumed_returns; goto L_80001008", emitted)

    def test_emitter_does_not_count_unreachable_resume_label(self):
        callee = SimpleNamespace(computed_returns={8})
        caller = SimpleNamespace(labels=set())
        emitter = Emitter(None, None, {0x80002000: callee}, set(), {0x80002000: "f_callee"})
        emitted = emitter._call(0x80002000, 0x80001000, caller)
        self.assertNotIn("g_computed_return_checks", emitted)
        self.assertNotIn("g_resumed_returns", emitted)


if __name__ == "__main__":
    unittest.main()
