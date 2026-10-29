"""Define the retail CPU hitlag branch without changing the decomp checkout."""
from pathlib import Path
import argparse


def repair(source: bytes) -> bytes:
    newline = b'\r\n' if b'\r\n' in source else b'\n'
    text = source.replace(b'\r\n', b'\n')
    old = b'''        } else {
            data2->xF9_b0 = false;
            if (0.1f * data2->level > HSD_Randf()) {'''
    new = b'''        } else {
            /* Retail keeps the caller's nonzero CPU pointer in r31 here.
             * The decomp's uninitialized switch_cmd loses that hitlag decision. */
            switch_cmd = 1;
            data2->xF9_b0 = false;
            if (0.1f * data2->level > HSD_Randf()) {'''
    if text.count(old) != 1:
        raise ValueError('CPU hitlag branch changed; review its retail register semantics')
    return text.replace(old, new).replace(b'\n', newline)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--hitlag-fixture', action='store_true')
    args = parser.parse_args()
    fixed = repair(args.source.read_bytes())
    if args.hitlag_fixture:
        start = fixed.index(b'    if (data->x18 != 0x12) {')
        end = fixed.index(b'    cur_cmd = data->x18;', start)
        fixed = fixed[start:end]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_bytes() != fixed:
        args.output.write_bytes(fixed)


if __name__ == '__main__':
    main()
