#!/usr/bin/env python3
"""Package the verified native ELF host/game for private Linux testing."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
COEF_SHA256 = 'c41e7d9f763da40983e83a5291cbecec1f536fd8b08ac6dda05eab33f57501ea'


def checked_elf(path):
    if not path.is_file():
        raise SystemExit(f'Missing native ELF build: {path}')
    with path.open('rb') as stream:
        header = stream.read(20)
    if (header[:6] != b'\x7fELF\x02\x01' or len(header) != 20
            or int.from_bytes(header[18:20], 'little') != 62):
        raise SystemExit(f'Expected an x86-64 native ELF build: {path}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build-linux/port/linux')
    parser.add_argument('--game-build-dir', type=Path, default=ROOT / 'build-linux-game')
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    version = (ROOT / 'VERSION').read_text().strip()
    output = args.out or ROOT / 'release' / f'MeleeUnlocked-{version}-linux-x86_64-preview'
    archive = Path(str(output) + '.tar.gz')
    if output.exists() or archive.exists():
        raise SystemExit('Output already exists; choose a new --out directory.')
    executable = args.build_dir / 'melee_linux'
    game = args.build_dir / 'melee_game.so'
    checked_elf(executable)
    checked_elf(game)
    actual_version = subprocess.check_output([str(executable.resolve()), '--version'], text=True).strip()
    if actual_version != version:
        raise SystemExit(f'Host version {actual_version!r} does not match {version!r}')
    exclusions = args.game_build_dir / 'melee_game.snapexcl'
    if not exclusions.is_file():
        raise SystemExit('The matching game snapshot exclusions are required.')
    if (args.game_build_dir / 'melee_game.so').read_bytes() != game.read_bytes():
        raise SystemExit('The packaged host output and game library build do not match.')
    coefficient = ROOT / 'port/slippi_sys/GC/dsp_coef.bin'
    if hashlib.sha256(coefficient.read_bytes()).hexdigest() != COEF_SHA256:
        raise SystemExit('The free DSP coefficient table does not match the release asset.')
    readme = ROOT / 'port/linux/README.txt'
    if not readme.is_file():
        raise SystemExit('Player installation and compatibility instructions are required.')
    output.mkdir(parents=True)
    for source, name in ((executable, 'melee_linux'), (game, 'melee_game.so'),
                         (exclusions, 'melee_game.snapexcl'), (readme, 'README.txt'),
                         (ROOT / 'port/linux/MeleeUnlocked.sh', 'MeleeUnlocked.sh')):
        shutil.copy2(source, output / name)
    (output / 'melee_linux').chmod(0o755)
    (output / 'MeleeUnlocked.sh').chmod(0o755)
    (output / 'Sys/GC').mkdir(parents=True)
    shutil.copy2(coefficient, output / 'Sys/GC/dsp_coef.bin')
    shutil.copy2(ROOT / 'port/linux/51-melee-unlocked-gc-adapter.rules', output / '51-melee-unlocked-gc-adapter.rules')
    licenses = output / 'licenses'
    licenses.mkdir()
    shutil.copy2(ROOT / 'LICENSE', licenses / 'COPYING.txt')
    shutil.copy2(ROOT / 'NOTICE', licenses / 'NOTICE.txt')
    shutil.copy2(ROOT / 'port/third_party/libusb/COPYING', licenses / 'libusb-LGPL-2.1.txt')
    for source in (ROOT / 'docs/licenses').glob('*.txt'):
        shutil.copy2(source, licenses / source.name)
    training_notice = ROOT / 'docs/third-party-training-mode-ce.txt'
    if training_notice.is_file():
        shutil.copy2(training_notice, licenses / training_notice.name)
    for library in ('libsdl2-2.0-0', 'libglew2.2'):
        source = Path('/usr/share/doc') / library / 'copyright'
        if source.is_file():
            shutil.copy2(source, licenses / (library + '.txt'))
    # A worktree made on Windows has a .git file Linux git cannot follow: pass the commit in.
    source_commit = os.environ.get('MELEE_SOURCE_COMMIT') or subprocess.check_output(
        ['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
    manifest = {'version': version, 'source_base': source_commit, 'platform': 'linux-x86_64',
                'runtime': 'native ELF', 'status': 'offline preview', 'files': {}}
    for path in sorted(output.rglob('*')):
        if path.is_file():
            manifest['files'][str(path.relative_to(output))] = hashlib.sha256(path.read_bytes()).hexdigest()
    (output / 'build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    with tarfile.open(archive, 'w:gz') as packed:
        packed.add(output, arcname=output.name)
    print(archive)


if __name__ == '__main__':
    main()
