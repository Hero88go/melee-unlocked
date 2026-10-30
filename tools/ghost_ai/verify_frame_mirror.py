"""Hash-compare per-player verified frame arrays before switching to an archive mirror."""
import argparse
import hashlib
import json
from pathlib import Path


PROS = ('mang0', 'zain', 'hbox', 'cody', 'amsa')


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        while chunk := source.read(1 << 20):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True,
                        help='ghost-ai/data directory containing <pro>/verified/frames')
    parser.add_argument('--mirror-root', type=Path, required=True,
                        help='archive directory containing <pro> frame folders')
    parser.add_argument('--pros', nargs='*', choices=PROS, default=list(PROS))
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    result = {'source_root': str(args.source_root.resolve()),
              'mirror_root': str(args.mirror_root.resolve()), 'players': {}, 'all_match': True}
    for pro in args.pros:
        source = args.source_root / pro / 'verified' / 'frames'
        mirror = args.mirror_root / pro
        left = {path.relative_to(source).as_posix(): path for path in source.rglob('*.npz')}
        right = {path.relative_to(mirror).as_posix(): path for path in mirror.rglob('*.npz')}
        missing = sorted(left.keys() - right.keys())
        extra = sorted(right.keys() - left.keys())
        mismatched = []
        if not missing and not extra:
            for name, original in left.items():
                copied = right[name]
                if original.stat().st_size != copied.stat().st_size or sha256(original) != sha256(copied):
                    mismatched.append(name)
        row = {'files': len(left), 'mirror_files': len(right), 'missing': missing[:20],
               'extra': extra[:20], 'mismatched': mismatched[:20],
               'all_match': not missing and not extra and not mismatched}
        result['players'][pro] = row
        result['all_match'] = result['all_match'] and row['all_match']
        print(pro, json.dumps(row), flush=True)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2), encoding='utf-8')
    if not result['all_match']:
        raise SystemExit('Frame mirror check failed; preserve all original frame arrays')
    print(json.dumps({'all_match': True, 'report': str(args.out.resolve())}, indent=2))


if __name__ == '__main__':
    main()
