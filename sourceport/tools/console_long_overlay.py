#!/usr/bin/env python3
"""Make the console's 32-bit long types explicit for the LP64 game build.

This is a private build input, never an edit to the decomp checkout. The original
console and Windows build use 32-bit long; Linux x86-64 uses 64-bit long. Changing
only u32/s32 would miss the SDK's audio buffers, delay lines, and other explicit
long fields. Tokenize C so comments, strings, character literals, long double,
and long long stay unchanged. A single L integer suffix also becomes int-width.
Host ABI shims are deliberately outside the transformed tree.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re


TOKEN = re.compile(
    rb'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    rb'|[A-Za-z_][A-Za-z_0-9]*|(?:\d|\.\d)(?:[A-Za-z_0-9.]|[eEpP][+-])*|\S',
    re.DOTALL,
)
INTEGER = re.compile(rb'(0[xX][0-9a-fA-F]+|0[bB][01]+|[0-9]+)([uUlL]*)\Z')
TYPE_WORDS = {b'long', b'int', b'unsigned', b'signed', b'const', b'volatile', b'double'}


def normalize(source: bytes) -> bytes:
    tokens = list(TOKEN.finditer(source))
    significant = [i for i, t in enumerate(tokens) if not t[0].startswith((b'//', b'/*'))]
    edits: list[tuple[int, int, bytes]] = []
    for pos, index in enumerate(significant):
        token = tokens[index]
        value = token[0]
        if value == b'long':
            lo, hi = pos, pos + 1
            while lo and tokens[significant[lo - 1]][0] in TYPE_WORDS:
                lo -= 1
            while hi < len(significant) and tokens[significant[hi]][0] in TYPE_WORDS:
                hi += 1
            words = [tokens[significant[i]][0] for i in range(lo, hi)]
            if words.count(b'long') != 1 or b'double' in words:
                continue
            edits.append((token.start(), token.end(), b'' if b'int' in words else b'int'))
        elif value[:1].isdigit():
            integer = INTEGER.fullmatch(value)
            if integer and integer[2].lower().count(b'l') == 1:
                suffix = integer[2].replace(b'l', b'').replace(b'L', b'')
                edits.append((token.start(), token.end(), integer[1] + suffix))
    for start, end, value in reversed(edits):
        source = source[:start] + value + source[end:]
    return source


def prepare(source: Path, output: Path) -> None:
    if source.resolve() == output.resolve() or source.resolve() in output.resolve().parents:
        raise ValueError('the overlay must be outside its source tree')
    for subdir in ('src', 'libs/dolphin'):
        for path in sorted((source / subdir).rglob('*')):
            if not path.is_file() or '.git' in path.parts:
                continue
            contents = path.read_bytes()
            if path.suffix.lower() in {'.c', '.h', '.inc'}:
                contents = normalize(contents)
            destination = output / path.relative_to(source)
            if not destination.exists() or destination.read_bytes() != contents:
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(contents)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    prepare(args.source, args.output)


if __name__ == '__main__':
    main()
