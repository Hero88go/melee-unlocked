"""Apply the released native adaptations to the pinned public Melee submodule."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = '039c4bf4ca33338c35d21901ad19b7ede19d19ad'


def prepare(decomp):
    patch = ROOT / 'sourceport/patches/melee-native.patch'
    def git(*args):
        return subprocess.run(['git', '-C', str(decomp), *args], capture_output=True, text=True)
    if git('apply', '--reverse', '--check', str(patch)).returncode == 0:
        print('Native Melee sources are already prepared.')
        return
    head = git('rev-parse', 'HEAD')
    if head.returncode or head.stdout.strip() != BASE:
        raise SystemExit('Initialize the pinned sources first: git submodule update --init sourceport/extern/melee')
    status = git('status', '--porcelain', '--untracked-files=no')
    if status.returncode or status.stdout.strip():
        raise SystemExit('Melee sources have local changes. Use a clean checkout before applying the native patch.')
    check = git('apply', '--check', str(patch))
    if check.returncode:
        raise SystemExit(check.stderr)
    result = git('apply', str(patch))
    if result.returncode:
        raise SystemExit(result.stderr)
    print('Prepared the released native Melee sources.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp', type=Path, default=ROOT / 'sourceport/extern/melee')
    prepare(parser.parse_args().decomp)
