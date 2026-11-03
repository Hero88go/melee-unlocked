"""Window-free regressions for the full-host packaging provenance gate."""
from pathlib import Path
import hashlib
import json
import tempfile
import unittest
from unittest.mock import patch

import package_full


class HostProvenanceTest(unittest.TestCase):
    def test_rejects_missing_or_spike_provenance_before_copying(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'VERSION').write_text('test-version\n')
            (root / 'MeleeUnlocked').write_bytes(b'dev-spike-fixture')
            manifest = dict(version='test-version', agent_test_hook=False,
                            host_sha256=hashlib.sha256(b'dev-spike-fixture').hexdigest())
            for provenance in [{}, {'build_script': 'port/mac/build.sh',
                                    'host_entry': 'port/mac/main.cpp'},
                               {'build_script': 'port/mac/build_full.py',
                                'host_entry': 'port/mac/main.cpp'}]:
                with self.subTest(provenance=provenance):
                    (root / 'build-manifest.json').write_text(json.dumps(manifest | provenance))
                    with patch.object(package_full, 'ROOT', root), \
                         patch.object(package_full, 'build_dir', return_value=root), \
                         patch.object(package_full, 'copy') as copy, \
                         patch('sys.argv', ['package_full.py']):
                        with self.assertRaisesRegex(SystemExit, 'dev spike hosts cannot ship'):
                            package_full.main()
                        copy.assert_not_called()

    def test_full_provenance_still_requires_matching_binary(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'VERSION').write_text('test-version\n')
            (root / 'MeleeUnlocked').write_bytes(b'changed-host')
            (root / 'build-manifest.json').write_text(json.dumps(dict(
                version='test-version', build_script='port/mac/build_full.py',
                host_entry='port/app/main.cpp', host_sha256='stale-hash')))
            with patch.object(package_full, 'ROOT', root), \
                 patch.object(package_full, 'build_dir', return_value=root), \
                 patch.object(package_full, 'copy') as copy, \
                 patch('sys.argv', ['package_full.py']):
                with self.assertRaisesRegex(SystemExit, 'does not match'):
                    package_full.main()
                copy.assert_not_called()


if __name__ == '__main__':
    unittest.main()
