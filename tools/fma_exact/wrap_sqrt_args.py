#!/usr/bin/env python3
"""Wrap every product term of a sqrtf / sqrtf_accurate argument in MU_P(...).

The console's sqrtf is an inline function: its argument becomes a variable before any arithmetic,
and the console compiler never fused a product into the sum there (MatToQuat, lbVector_Len,
HSD_MtxColMag, lbVector_CosAngle, lbVector_AngleXY, checked against the retail code). GCC's
-ffp-contract=on fuses them. MU_P(x) (sourceport/game/include/mu_native.h) evaluates a product as
its own statement, so it is rounded before the sum; outside the native build it is (x).

    python tools/fma_exact/wrap_sqrt_args.py [--dry-run]
"""
import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TREE = ROOT / 'sourceport/extern/melee'
CALL = re.compile(r'\b(sqrtf|sqrtf_accurate|sqrtf__Ff)\s*\(')
SKIP = {'math_ppc.h', 'math_native.h', 'math.h'}


def match_paren(s, i):
    """s[i] == '('; index of the matching ')'."""
    depth = 0
    while i < len(s):
        c = s[i]
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return i
        elif c in '"\'':
            q, i = c, i + 1
            while s[i] != q:
                i += 2 if s[i] == '\\' else 1
        i += 1
    raise ValueError('unbalanced')


def split_terms(arg):
    """Top-level + / - split (binary only). Returns [(sep, term)]."""
    out, depth, start, prev = [], 0, 0, ''
    i = 0
    while i < len(arg):
        c = arg[i]
        if c in '([':
            depth += 1
        elif c in ')]':
            depth -= 1
        elif c in '+-' and depth == 0:
            nxt = arg[i + 1] if i + 1 < len(arg) else ''
            j = i - 1
            while j >= 0 and arg[j].isspace():
                j -= 1
            pc = arg[j] if j >= 0 else ''
            exponent = pc in 'eE' and j > 0 and (arg[j - 1].isdigit() or arg[j - 1] == '.') and re.match(r'.*\d+\.?\d*[eE]$', arg[:j + 1])
            if (c == '-' and nxt == '>') or nxt == c or (i > 0 and arg[i - 1] == c) or exponent:
                i += 2 if (c == '-' and nxt == '>') or nxt == c else 1
                continue
            if pc and (pc.isalnum() or pc in ')]_.'):
                out.append((prev, arg[start:i]))
                prev, start = c, i + 1
        i += 1
    out.append((prev, arg[start:]))
    return out


def has_product(term):
    t = term.strip()
    return ('*' in re.sub(r'\(\s*\w+\s*\*\s*\)', '', t) and not t.startswith('*')) or re.search(r'\bSQ\s*\(', t)


def rewrite_arg(arg):
    terms = split_terms(arg)
    if len(terms) < 2 or not any(has_product(t) for _, t in terms):
        return arg, 0
    out, n = '', 0
    for sep, term in terms:
        if has_product(term) and 'MU_P(' not in term:
            lead = term[:len(term) - len(term.lstrip())]
            trail = term[len(term.rstrip()):]
            term = '%sMU_P(%s)%s' % (lead, term.strip(), trail)
            n += 1
        out += sep + term
    return out, n


def process(path, dry):
    s = path.read_text(encoding='utf-8', errors='surrogateescape') if False else open(path, encoding='utf-8', newline='').read()
    pos, total, out = 0, 0, []
    for m in CALL.finditer(s):
        if m.start() < pos:
            continue
        # skip definitions and declarations: preceded by a type on the same line
        line_start = s.rfind('\n', 0, m.start()) + 1
        before = s[line_start:m.start()]
        if re.search(r'\b(float|f32|double|inline|define)\b\s*$', before) or before.lstrip().startswith('#'):
            continue
        open_i = m.end() - 1
        close_i = match_paren(s, open_i)
        arg = s[open_i + 1:close_i]
        new, n = rewrite_arg(arg)
        if n:
            out.append(s[pos:open_i + 1])
            out.append(new)
            pos = close_i
            total += n
    if not total:
        return 0
    out.append(s[pos:])
    if not dry:
        with open(path, 'w', encoding='utf-8', newline='') as f:
            f.write(''.join(out))
    return total


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()
    files = terms = 0
    for p in sorted(list(TREE.glob('src/**/*.[ch]')) + list(TREE.glob('libs/**/*.[ch]'))):
        if p.name in SKIP:
            continue
        n = process(p, a.dry_run)
        if n:
            files += 1
            terms += n
            print('%3d  %s' % (n, p.relative_to(TREE)))
    print('%d product terms wrapped in %d files' % (terms, files))


if __name__ == '__main__':
    main()
