"""Bounded hidden functional checks of the current four-pro preview, never a strength benchmark."""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--iso', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    exe, iso, folder = args.exe.resolve(), args.iso.resolve(), args.out.resolve()
    folder.mkdir(parents=True, exist_ok=True)

    def run(item):
        pro, char, kind = item
        d = folder / str(pro)
        for sub in ('cards', 'user', 'appdata'):
            (d / sub).mkdir(parents=True, exist_ok=True)
        (d / 'user/user.json').write_text('{}')
        settings = d / 'settings.ini'
        settings.write_text('backend d3d12\nvolume 0\nfps 60\ngamelanguage 2\n')
        env = {k: v for k, v in os.environ.items() if not k.startswith(('MELEE_TEST_', 'MELEE_GHOST'))}
        env.update(APPDATA=str(d / 'appdata'), LOCALAPPDATA=str(d / 'appdata'), MELEE_NO_GC_ADAPTER='1',
                   MELEE_TEST_AFTER_SESSION='700', MELEE_TEST_TRAIN_AGAINST=f'950,{char}',
                   MELEE_TEST_PRACTICE_GHOST=str(pro))
        cmd = [str(exe), '--hidden', '--fast', '--iso', str(iso), '--volume', '0', '--frames', '1800',
               '--log-file', str(d / 'port.log'), '--user-dir', str(d / 'user'), '--card-dir', str(d / 'cards'),
               '--settings-path', str(settings)]
        try:
            proc = subprocess.run(cmd, cwd=exe.parent, env=env, creationflags=0x08000000, timeout=150,
                                  capture_output=True)
            log = (d / 'port.log').read_text(errors='replace') if (d / 'port.log').exists() else ''
            result = {'pro': pro, 'exit_code': proc.returncode,
                      'loaded_model': any('ghost ai:' in line and 'loaded' in line for line in log.splitlines()),
                      'drove_selected_fighter': f'driving pro {pro} as fighter {kind}' in log,
                      'training_entered': 'Training is running' in log,
                      'signals': [line for line in log.splitlines() if any(word in line for word in
                                  ('ghost ai:', 'practice test:', 'Training again', 'Training is running'))]}
        except subprocess.TimeoutExpired:
            result = {'pro': pro, 'timeout': True}
        print(json.dumps(result), flush=True)
        return result

    # The operator checks that the owner is not gaming before invoking this script.
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(run, [(0, 20, 22), (1, 9, 18), (2, 15, 15), (3, 2, 1)]))
    (folder / 'results.json').write_text(json.dumps(results, indent=2))
    return 0 if all(r.get('exit_code') == 0 and r.get('loaded_model') and
                    r.get('drove_selected_fighter') and r.get('training_entered') for r in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
