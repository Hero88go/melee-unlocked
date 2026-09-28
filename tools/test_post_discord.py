"""Release events must work even when the tag has no local release notes."""
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import post_discord


class ReleaseEventNotesTests(unittest.TestCase):
    def test_release_event_supplies_missing_notes(self):
        with tempfile.TemporaryDirectory() as folder:
            event = Path(folder) / 'event.json'
            event.write_text(json.dumps({'release': {'tag_name': 'v0.8.0', 'body': 'Published notes'}}))
            with patch.object(post_discord, 'ROOT', Path(folder)), patch.dict(os.environ, {
                'GITHUB_EVENT_PATH': str(event), 'GITHUB_EVENT_NAME': 'release'
            }):
                self.assertEqual(post_discord.notes_for_version('0.8.0'), 'Published notes')
                self.assertIsNone(post_discord.notes_for_version('0.7.1'))


if __name__ == '__main__':
    unittest.main()
