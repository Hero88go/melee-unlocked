"""Count build-machine paths embedded in release binaries (crash text and debug files quote them).

usage: binary_path_audit.py <file>... [--needle TEXT]... [--show N]
Exit 1 when any needle is found. Default needles: the current user name and "Users\\" / "Users/".
"""
import argparse
import os
import re
import sys
from pathlib import Path


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('files', nargs='+', type=Path)
    ap.add_argument('--needle', action='append', default=[])
    ap.add_argument('--show', type=int, default=3)
    args = ap.parse_args()
    needles = args.needle or [os.environ.get('USERNAME', ''), 'Users\\', 'Users/']
    needles = [n for n in needles if n]
    found = False
    for path in args.files:
        data = path.read_bytes()
        for needle in needles:
            hits = 0
            samples = []
            for encoded in (needle.encode('ascii', 'ignore'), needle.encode('utf-16-le')):
                if not encoded:
                    continue
                for m in re.finditer(re.escape(encoded), data, re.IGNORECASE):
                    hits += 1
                    if len(samples) < args.show:
                        start = max(0, m.start() - 40)
                        chunk = data[start:m.end() + 60]
                        text = chunk.replace(b'\0', b'')
                        samples.append(re.sub(rb'[^\x20-\x7e]', b'.', text).decode())
            print(f'{path.name}: {needle!r} {hits}')
            for sample in samples:
                print('   ', sample)
            found |= hits > 0
    return 1 if found else 0


if __name__ == '__main__':
    sys.exit(main())
