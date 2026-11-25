"""Apply the released native adaptations to the pinned public Melee submodule."""
import argparse
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BASE = '039c4bf4ca33338c35d21901ad19b7ede19d19ad'
# The tree the last preparation produced, kept in the submodule's own repository (local only).
PREPARED = 'refs/mu/prepared'


def prepare(decomp, archive=None):
    patch = ROOT / 'sourceport/patches/melee-native.patch'
    # The Apple silicon Mac's widening of 32-bit addresses (sourceport/game/include/mu_window.h),
    # kept apart from the released patch and applied on top of it.
    mac_patch = ROOT / 'sourceport/patches/melee-native-mac.patch'
    def git(*args, index=None, stdin=None):
        env = dict(os.environ, GIT_INDEX_FILE=index) if index else None
        return subprocess.run(['git', '-C', str(decomp), *args], capture_output=True, text=True, env=env,
                              input=stdin)
    def apply(p):
        check = git('apply', '--check', str(p))
        if check.returncode:
            raise SystemExit(check.stderr)
        result = git('apply', str(p))
        if result.returncode:
            raise SystemExit(result.stderr)
    if archive and git('rev-parse', '--show-toplevel').stdout.strip() != str(decomp.resolve()):
        subprocess.run(['git', '-C', str(decomp), 'init', '-q'], check=True)
    if archive:
        if git('apply', '--reverse', '--check', str(mac_patch)).returncode == 0:
            print('Native Melee sources are already prepared.')
            return
        if git('apply', '--reverse', '--check', str(patch)).returncode == 0:
            apply(mac_patch)
            print('Added the Mac changes to the prepared native Melee sources.')
            return
        # git archives lack .git. Verify both their embedded pinned commit and
        # every extracted file before applying; never accept an arbitrary tree.
        with archive.open('rb') as stream:
            commit = subprocess.run(['git', 'get-tar-commit-id'], stdin=stream,
                                    capture_output=True, text=True)
        if commit.returncode or commit.stdout.strip() != BASE:
            raise SystemExit('Archive is not the pinned Melee commit')
        with tarfile.open(archive) as tree:
            for member in tree.getmembers():
                if member.isdir():
                    continue
                if not member.isfile() or Path(member.name).is_absolute() or '..' in Path(member.name).parts:
                    raise SystemExit('Unsupported archive member')
                path = decomp / member.name
                if not path.is_file() or path.is_symlink() or path.read_bytes() != tree.extractfile(member).read():
                    raise SystemExit('Extracted Melee sources differ from the pinned archive')
        apply(patch)
        apply(mac_patch)
        print('Prepared the released native Melee sources.')
        return

    head = git('rev-parse', 'HEAD')
    if head.returncode or head.stdout.strip() != BASE:
        raise SystemExit('Initialize the pinned sources first: git submodule update --init sourceport/extern/melee')
    with tempfile.TemporaryDirectory() as tmp:
        # The tree both patches make from the pinned commit, built in a separate index so the
        # sources are not touched, and compared with them file by file.
        wanted = os.path.join(tmp, 'wanted')
        git('read-tree', BASE, index=wanted)
        for p in (patch, mac_patch):
            result = git('apply', '--cached', str(p), index=wanted)
            if result.returncode:
                raise SystemExit(f'{p.name} does not apply to the pinned sources:\n{result.stderr}')
        wanted_tree = git('write-tree', index=wanted).stdout.strip()

        def unchanged_from(index):
            git('update-index', '-q', '--refresh', index=index)
            return git('diff-files', '--quiet', index=index).returncode == 0

        def record():
            git('update-ref', PREPARED, wanted_tree)

        if unchanged_from(wanted):
            record()
            print('Native Melee sources are already prepared.')
            return
        status = git('status', '--porcelain', '--untracked-files=no')
        if status.returncode == 0 and not status.stdout.strip():
            apply(patch)
            apply(mac_patch)
            record()
            print('Prepared the released native Melee sources.')
            return
        # Prepared earlier with patches that have changed since (a merged upstream version, or an
        # edit to the Mac patch). When no file was edited after that preparation, rewrite only the
        # files whose patched text changed, so the build recompiles only those.
        last = git('rev-parse', '--verify', '-q', PREPARED + '^{tree}').stdout.strip()
        before = os.path.join(tmp, 'before')
        if last and git('read-tree', last, index=before).returncode == 0 and unchanged_from(before):
            changes = git('diff-tree', '-r', '-z', '--no-renames', '--name-status', last, wanted_tree).stdout
            fields = changes.split('\0')
            rewrite = []
            for kind, path in zip(fields[0::2], fields[1::2]):
                if kind == 'D':
                    (decomp / path).unlink(missing_ok=True)
                else:
                    rewrite.append(path)
            if rewrite:
                result = git('checkout-index', '-f', '-z', '--stdin', index=wanted, stdin='\0'.join(rewrite) + '\0')
                if result.returncode:
                    raise SystemExit(result.stderr)
            record()
            print(f'Updated {len(fields) // 2} prepared Melee source files for the changed patches.')
            return
    raise SystemExit('Melee sources have local changes, or were prepared with other patches. To start over '
                     '(this discards edits there): git -C sourceport/extern/melee checkout -- . && '
                     'git -C sourceport/extern/melee clean -fdx')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp', type=Path, default=ROOT / 'sourceport/extern/melee')
    parser.add_argument('--archive', type=Path, help='pinned git archive used to populate --decomp')
    args = parser.parse_args()
    prepare(args.decomp, args.archive)
