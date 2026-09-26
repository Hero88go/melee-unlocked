"""Capture representative non-Video settings pages in the review gallery."""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PAGES = {0: 'Customize', 1: 'Controls', 2: 'Audio', 3: 'Gecko Codes', 4: 'Customize'}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--iso', required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    for style, page in PAGES.items():
        command = [sys.executable, str(ROOT/'tools/capture_settings_review.py'),
                   '--iso', args.iso, '--out', str(args.out), '--styles', str(style),
                   '--pages', page, '--sizes', '800x600']
        subprocess.run(command, cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
