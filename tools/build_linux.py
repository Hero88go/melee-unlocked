#!/usr/bin/env python3
"""Build the native Linux ELF game and host from the same source tree."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def run(*arguments):
    command = [str(value) for value in arguments]
    print('+ ' + ' '.join(command), flush=True)
    subprocess.run(command, check=True, cwd=ROOT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument('--cpu', choices=('SSE2', 'AVX', 'AVX2'), default='AVX2')
    parser.add_argument('--headless-build', action='store_true')
    parser.add_argument('--agent-test-hook', action='store_true',
                        help='staging only: enable agent-test.json in the full Source Port')
    parser.add_argument('--host-only', action='store_true', help='use the existing game build')
    parser.add_argument('--test', action='store_true')
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build-linux')
    parser.add_argument('--game-build-dir', type=Path, default=ROOT / 'build-linux-game')
    parser.add_argument('--port-gen', type=Path, default=Path(os.environ.get('MELEE_PORT_GEN', ROOT.parent / 'mu-gen-090')),
                        help='generated hle_decls.h and guest_symbols.h, for the full Source Port')
    args = parser.parse_args()
    if sys.platform != 'linux':
        parser.error('run this script with Linux Python (including Ubuntu under WSL)')
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    full = not args.headless_build and (args.port_gen / 'hle_decls.h').exists()
    if args.agent_test_hook and (not full or not (args.port_gen / 'guest_symbols.h').exists()):
        parser.error('--agent-test-hook requires the full graphics host and --port-gen headers')
    for name in ('gcc-14', 'g++-14', 'cmake', 'ninja'):
        if not shutil.which(name):
            parser.error('missing build tool: ' + name)
    game_build = args.game_build_dir.resolve()
    host_build = args.build_dir.resolve()
    if not args.host_only:
        # The prepared game sources (tools/prepare_native_sources.py) when this tree has them, as the
        # Windows game build uses; otherwise the submodule.
        decomp = ROOT / 'build-native/decomp'
        extra = ['-DMELEE_DECOMP_DIR=' + str(decomp)] if (decomp / 'src/melee').is_dir() else []
        run('cmake', '-S', ROOT / 'sourceport/game', '-B', game_build, '-G', 'Ninja',
            '-DCMAKE_C_COMPILER=gcc-14', '-DCMAKE_CXX_COMPILER=g++-14',
            '-DCMAKE_BUILD_TYPE=Release', '-DMELEE_CPU_BASELINE=' + args.cpu, *extra)
        run('cmake', '--build', game_build, '--target',
            'mu_game_tests' if args.test else 'melee_game', '-j', args.jobs)
        if args.test:
            run('ctest', '--test-dir', game_build, '--output-on-failure')
    run('cmake', '-S', ROOT, '-B', host_build, '-G', 'Ninja',
        '-DCMAKE_CXX_COMPILER=g++-14', '-DCMAKE_BUILD_TYPE=Release',
        '-DMELEE_BUILD_NATIVE_LINUX=ON', '-DMELEE_CPU_BASELINE=' + args.cpu,
        '-DMELEE_LINUX_GAME_LIBRARY=' + str(game_build / 'melee_game.so'),
        '-DMELEE_LINUX_GRAPHICS=' + ('OFF' if args.headless_build else 'ON'),
        '-DYOUGAME_AGENT_TEST_HOOK=' + ('ON' if args.agent_test_hook else 'OFF'),
        '-DMELEE_PORT_GEN=' + (str(args.port_gen.resolve()) if (args.port_gen / 'hle_decls.h').exists() else ''))
    targets = ['melee_linux'] + (['melee_' 'source', 'melee_launcher'] if full else [])
    if full and args.cpu != 'AVX2':
        targets = ['melee_' 'source']   # the compatibility build: only the game, under its own names

    if args.test:
        targets += ['linux_gc_adapter_test', 'linux_input_script_test', 'port_yougame_sysgen_test',
                    'port_settings_mouse_test', 'port_controller_profiles_test',
                    'port_settings_profile_bindings_test', 'port_dolphin_profile_test',
                    'port_yougame_embed_view_test', 'port_yougame_embed_volume_test']
        if full:
            targets += ['linux_ws_frame_test']  # YouGame's WebSocket framing (port/linux/compat/tests)
            # The app-owned ring (fake ring, POSIX attach) and the transport routing tests.
            targets += ['yg_enet_socket_test', 'yg_enet_posix_ring_test', 'yougame_transport_test']
    run('cmake', '--build', host_build, '--target', *targets, '-j', args.jobs)
    if args.test:
        run('ctest', '--test-dir', host_build, '--output-on-failure')
    print('Native executable: ' + str(host_build / 'port/linux/melee_linux'))
    print('YOUGAME_AGENT_TEST_HOOK=' + ('ON (STAGING ONLY)' if args.agent_test_hook else 'OFF'))


if __name__ == '__main__':
    main()
