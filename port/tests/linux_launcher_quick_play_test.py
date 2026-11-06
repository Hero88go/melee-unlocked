"""Exercise the built Linux launcher's window-free path with a harmless game stand-in."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

launcher = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='mu-quick-play-') as temporary:
    root = Path(temporary)
    exe = root / 'MeleeUnlocked'
    shutil.copyfile(launcher, exe)
    exe.chmod(0o755)
    disc = root / 'Melee disc with spaces.iso'
    with disc.open('wb') as file:
        file.write(b'GALE01\0\2')
        file.truncate(1_000_000_001)  # sparse test header; no game content is used
    output = root / 'game-args.txt'
    for name in ('melee_source', 'melee_source_compat'):
        path = root / name
        path.write_text('#!/bin/sh\nprintf \'%s\\n\' "$0" "$@" > "$MU_QUICK_PLAY_ARGS"\nexit 37\n')
        path.chmod(0o755)
    (root / 'Sys').mkdir()
    (root / 'Sys/codehandler.bin').write_bytes(b'test')
    (root / 'launcher.ini').write_text('iso=' + str(disc) + '\nsavereplays=1\nreplaydir=' + str(root/'Saved replays') + '\n')
    env = dict(os.environ, MU_QUICK_PLAY_ARGS=str(output), DISPLAY='', WAYLAND_DISPLAY='',
               SDL_VIDEODRIVER='invalid-for-test', SDL_AUDIODRIVER='dummy')
    result = subprocess.run([str(exe), '--play', '--frames', '1'], env=env, capture_output=True, text=True, timeout=20)
    assert result.returncode == 37, (result.returncode, result.stderr)
    args = output.read_text().splitlines()
    assert Path(args[0]).name in ('melee_source', 'melee_source_compat')
    assert args[args.index('--iso') + 1] == str(disc)
    assert args[args.index('--settings-path') + 1] == str(root/'port-settings.ini')
    assert args[args.index('--card-dir') + 1] == str(root/'User/GC/CardA')
    assert args[args.index('--replay-dir') + 1] == str(root/'Saved replays')
    assert args[-2:] == ['--frames', '1']
    result = subprocess.run([str(exe), '--play', '--iso', str(disc), '--version'], env=env,
                            capture_output=True, text=True, timeout=20)
    assert result.returncode == 37, result.stderr
    for tail in (['--iso'], ['--iso', str(root/'missing.iso')]):
        result = subprocess.run([str(exe), '--play', *tail], env=env, capture_output=True, text=True, timeout=20)
        assert result.returncode == 2 and result.stderr, (tail, result.returncode)
print('PASS: --play launches without SDL/window setup, preserves saved paths and game exit status, and rejects invalid discs')
