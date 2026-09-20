"""Offline tests. No credentials, network, or game assets required."""
import contextlib
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import jev_review as review


class ReviewTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        root = Path(self.directory.name)
        self.state, self.questions = root / "state.json", root / "questions.json"
        self.state.write_text('{"evidence": "fixture only"}', encoding="utf-8")
        self.rubric = {"gate": {"type": "choice", "instructions": "Is the claim supported?",
                               "criteria": {"supported": "Evidence supports it",
                                            "unknown": "Insufficient evidence"}}}
        self.questions.write_text(json.dumps(self.rubric), encoding="utf-8")
        self.args = ["--state", str(self.state), "--questions", str(self.questions),
                     "--cache-dir", str(root / "cache")]
        self.response = {"model": "jev-1.13.0", "answers": {
            "gate": {"type": "choice", "choice": "unknown"}}}

    def run_main(self, extra=()):
        with contextlib.redirect_stdout(io.StringIO()) as output:
            review.main(self.args + list(extra))
        return json.loads(output.getvalue())

    def test_dry_run_never_calls_network_or_needs_key(self):
        with patch.dict(os.environ, {}, clear=True), patch.object(review, "evaluate") as network:
            self.assertEqual(self.run_main(["--dry-run"])["questions"], ["gate"])
            network.assert_not_called()

    def test_missing_key_fails_without_network(self):
        with patch.dict(os.environ, {}, clear=True), patch.object(review, "evaluate") as network:
            with self.assertRaisesRegex(RuntimeError, "not set"):
                self.run_main()
            network.assert_not_called()

    def test_cache_and_changed_evidence(self):
        with patch.dict(os.environ, {"TYPESAFE_API_KEY": "test-only"}), patch.object(
                review, "evaluate", return_value=self.response) as network:
            self.assertFalse(self.run_main()["cached"])
            self.assertTrue(self.run_main()["cached"])
            self.state.write_text('{"evidence": "changed"}', encoding="utf-8")
            self.assertFalse(self.run_main()["cached"])
            self.assertEqual(network.call_count, 2)

    def test_alias_bypasses_cache(self):
        with patch.dict(os.environ, {"TYPESAFE_API_KEY": "test-only"}), patch.object(
                review, "evaluate", return_value=self.response) as network:
            self.run_main(["--model", "jev-latest"])
            self.run_main(["--model", "jev-latest"])
            self.assertEqual(network.call_count, 2)

    def test_bad_answers_rejected(self):
        for response in ({}, {"model": "jev-other", "answers": {}},
                         {"model": "jev-1.13.0", "answers": {}},
                         {"model": "jev-1.13.0", "answers": {
                             "gate": {"type": "choice", "choice": "invented"}}}):
            with self.subTest(response=response), self.assertRaises(ValueError):
                review.validate(response, self.rubric, "jev-1.13.0")

    def test_request_preserves_structure(self):
        payload, digest, _ = review.prepare(self.state, self.questions, "jev-1.13.0")
        self.assertIsInstance(json.loads(payload)["state"], dict)
        self.assertEqual(len(digest), 64)


if __name__ == "__main__":
    unittest.main()
