"""Offline checks for Linux hook configuration and package classification."""
import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


def load_tool(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


build = load_tool('build_linux')
package = load_tool('package_linux_release')


class LinuxAgentHookTest(unittest.TestCase):
    def test_configure_enables_and_resets_cached_hook(self):
        with tempfile.TemporaryDirectory() as scratch:
            gen = Path(scratch)
            (gen / 'hle_decls.h').touch()
            (gen / 'guest_symbols.h').touch()
            for enabled in (True, False):
                calls = []
                argv = ['build_linux.py', '--host-only', '--port-gen', str(gen), '--jobs', '2']
                if enabled:
                    argv.append('--agent-test-hook')
                output = io.StringIO()
                with patch.object(build.sys, 'platform', 'linux'), patch.object(build.sys, 'argv', argv), \
                        patch.object(build.shutil, 'which', return_value='/unused/tool'), \
                        patch.object(build, 'run', side_effect=lambda *args: calls.append(args)), \
                        contextlib.redirect_stdout(output):
                    build.main()
                self.assertIn('-DYOUGAME_AGENT_TEST_HOOK=' + ('ON' if enabled else 'OFF'), calls[0])
                self.assertIn('melee_source', calls[1])
                self.assertIn('YOUGAME_AGENT_TEST_HOOK=' + ('ON (STAGING ONLY)' if enabled else 'OFF'),
                              output.getvalue())

    def test_hook_requires_full_host(self):
        with tempfile.TemporaryDirectory() as scratch:
            for flags, headers in (([], ()), ([], ('hle_decls.h',)),
                                   (['--headless-build'], ('hle_decls.h', 'guest_symbols.h'))):
                for header in headers:
                    (Path(scratch) / header).touch()
                argv = ['build_linux.py', '--agent-test-hook', '--port-gen', scratch] + flags
                with patch.object(build.sys, 'platform', 'linux'), patch.object(build.sys, 'argv', argv), \
                        patch.object(build, 'run') as run, contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit) as error:
                        build.main()
                    self.assertEqual(error.exception.code, 2)
                    run.assert_not_called()

    def test_package_uses_binary_not_cache(self):
        with tempfile.TemporaryDirectory() as scratch:
            exe = Path(scratch) / 'melee_source'
            for data, expected in ((b'\x7fELF\0clean\0', False),
                                   (b'\x7fELF\0agent-test.json\0', True)):
                exe.write_bytes(data)
                self.assertEqual(package.agent_test_hook_enabled(exe), expected)


if __name__ == '__main__':
    unittest.main()
