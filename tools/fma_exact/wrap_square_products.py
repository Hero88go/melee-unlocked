#!/usr/bin/env python3
"""Wrap squares of repeated expressions in MU_P(...) so GCC rounds them before any sum.

Measured against the retail code (tools/fma_exact/retail_ops.py, the function-level census): the
console compiler did not fuse a square whose operand is a value loaded from memory or a compound
expression (a dereferenced value it stored earlier in the function stays in a register and fuses), `v->x * v->x`, `m[i][j] * m[i][j]`, `SQ(px - ax)`, and did fuse squares of locals and
local struct members, `x * x`, `d1.x * d1.x`, `SQ(dx)`, like any other product (mpCheckFloor's
`SQ(px - ax) + SQ(py - ay)` is two rounded squares and an add on the console). GCC's
-ffp-contract=on fuses both. MU_P(x) (sourceport/game/include/mu_native.h) evaluates the square as
its own statement; outside the native build it is (x).

Only complete multiplicative terms are wrapped (nothing multiplies or divides them on either side),
so association never changes. Inside function bodies only; MSL is left alone (its routines are held
to the console by the parity harness).

    python tools/fma_exact/wrap_square_products.py [--dry-run] [--remove]
"""
import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TREE = ROOT / 'sourceport/extern/melee'
IDENT = re.compile(r'^[A-Za-z_]\w*$')
DEREF = re.compile(r'->|\[')
SIMPLE = re.compile(r'^[A-Za-z_]\w*(\.[A-Za-z_]\w*)*$')   # a local or a local struct member: the console fuses its square


def mask(s):
    """Same length as s, comments and string/char literals blanked, preprocessor lines blanked."""
    out, i, n = list(s), 0, len(s)
    line_start = True
    while i < n:
        c = s[i]
        if line_start and c in ' \t':
            i += 1
            continue
        if line_start and c == '#':
            while i < n and s[i] != '\n':
                if s[i] == '\\' and i + 1 < n and s[i + 1] == '\n':
                    out[i] = ' '
                    i += 2
                    continue
                out[i] = ' '
                i += 1
            continue
        line_start = c == '\n'
        if s.startswith('//', i):
            while i < n and s[i] != '\n':
                out[i] = ' '
                i += 1
            continue
        if s.startswith('/*', i):
            j = s.find('*/', i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if s[k] != '\n':
                    out[k] = ' '
            i = j
            continue
        if c in '"\'':
            j = i + 1
            while j < n and s[j] != c:
                j += 2 if s[j] == '\\' else 1
            for k in range(i + 1, min(j, n)):
                out[k] = ' '
            i = j + 1
            continue
        i += 1
    return ''.join(out)


def group_back(m, j):
    """m[j] == ')' or ']': index of its opening bracket."""
    close = m[j]
    open_ = '(' if close == ')' else '['
    depth = 0
    while j >= 0:
        if m[j] == close:
            depth += 1
        elif m[j] == open_:
            depth -= 1
            if depth == 0:
                return j
        j -= 1
    return -1


def group_fwd(m, i):
    open_ = m[i]
    close = ')' if open_ == '(' else ']'
    depth = 0
    while i < len(m):
        if m[i] == open_:
            depth += 1
        elif m[i] == close:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def operand_back(m, j):
    """Operand ending at m[j] (inclusive): identifier chain with -> . [] or a parenthesised group.
    Returns its start index or -1."""
    end = j
    while True:
        if j < 0:
            return -1
        if m[j] == ']':
            j = group_back(m, j)
            if j < 0:
                return -1
            j -= 1
            continue
        if m[j] == ')':
            k = group_back(m, j)
            if k < 0:
                return -1
            # a call `f(...)` is not an operand we wrap; a plain group is
            p = k - 1
            while p >= 0 and m[p] in ' \t\n':
                p -= 1
            if p >= 0 and (m[p].isalnum() or m[p] == '_'):
                return -1
            return k
        if m[j].isalnum() or m[j] == '_':
            while j >= 0 and (m[j].isalnum() or m[j] == '_'):
                j -= 1
            start = j + 1
            if m[start].isdigit():
                return -1
            # member access continues to the left
            if j >= 1 and m[j - 1:j + 1] == '->':
                j -= 2
                continue
            if j >= 0 and m[j] == '.':
                j -= 1
                continue
            return start
        return -1


def operand_fwd(m, i):
    """Operand starting at m[i]. Returns its end index (inclusive) or -1."""
    n = len(m)
    if m[i] == '(':
        e = group_fwd(m, i)
        return e
    if not (m[i].isalpha() or m[i] == '_'):
        return -1
    j = i
    while True:
        while j < n and (m[j].isalnum() or m[j] == '_'):
            j += 1
        if j < n and m[j] == '[':
            e = group_fwd(m, j)
            if e < 0:
                return -1
            j = e + 1
            while j < n and m[j] == '[':
                e = group_fwd(m, j)
                j = e + 1
        if m.startswith('->', j):
            j += 2
            continue
        if j < n and m[j] == '.' and j + 1 < n and (m[j + 1].isalpha() or m[j + 1] == '_'):
            j += 1
            continue
        if j < n and m[j] == '(':
            return -1          # a call
        return j - 1


def norm(t):
    return re.sub(r'\s+', '', t)


def prev_char(m, i):
    i -= 1
    while i >= 0 and m[i] in ' \t\n':
        i -= 1
    return (m[i], i) if i >= 0 else ('', -1)


def next_char(m, i):
    i += 1
    while i < len(m) and m[i] in ' \t\n':
        i += 1
    return (m[i], i) if i < len(m) else ('', len(m))


def in_body(m):
    """depth[i] > 0 when m[i] is inside a function body (brace depth at file level)."""
    depth, d = [0] * len(m), 0
    for i, c in enumerate(m):
        if c == '{':
            d += 1
        elif c == '}':
            d -= 1
        depth[i] = d
    return depth


def assigned_before(m, depth, pos, expr):
    """True when `expr = ...` appears earlier in the same function body (the console compiler keeps a
    value it just stored in a register and fuses its square like a local's)."""
    start = pos
    while start > 0 and depth[start - 1] > 0:
        start -= 1
    body = m[start:pos]
    pat = re.escape(expr).replace('\-\>', r'\s*->\s*').replace('\.', r'\s*\.\s*')
    return re.search(r'(?<![\w.>])' + pat + r'\s*(?:[-+*/]?=)(?!=)', body) is not None


def find_squares(s):
    m = mask(s)
    depth = in_body(m)
    spans = []
    # a * a
    for star in [i for i, c in enumerate(m) if c == '*']:
        if depth[star] <= 0 or m[star + 1:star + 2] in ('=', '/') or m[star - 1:star] in ('/',):
            continue
        pc, pj = prev_char(m, star)
        if not (pc.isalnum() or pc in ')]_'):
            continue                                   # unary or pointer declarator
        ls = operand_back(m, pj)
        if ls < 0:
            continue
        nc, ni = next_char(m, star)
        if not nc:
            continue
        re_ = operand_fwd(m, ni)
        if re_ < 0:
            continue
        left, right = s[ls:pj + 1], s[ni:re_ + 1]
        if norm(left) != norm(right) or SIMPLE.match(norm(left)):
            continue
        if assigned_before(m, depth, ls, norm(left)):
            continue                                   # stored just before: still in a register
        bc, _ = prev_char(m, ls)
        ac, _ = next_char(m, re_)
        if bc in ('*', '/', '%', '&', '!', '~') or ac in ('*', '/', '%', '[', '(', '.', '-') and not (ac == '-' and m[re_ + 1:re_ + 3].strip()[:1] != '>'):
            continue
        # declarations like `Type* a * a` are impossible; `x->y * x->y` is a square of a member
        spans.append((ls, re_))
    # SQ(e)
    for mm in re.finditer(r'\bSQ\s*\(', m):
        i = mm.start()
        if depth[i] <= 0:
            continue
        o = mm.end() - 1
        e = group_fwd(m, o)
        if e < 0:
            continue
        arg = s[o + 1:e]
        if SIMPLE.match(norm(arg)) or assigned_before(m, depth, i, norm(arg)):
            continue
        bc, _ = prev_char(m, i)
        ac, _ = next_char(m, e)
        if bc in ('*', '/', '%') or ac in ('*', '/', '%'):
            continue
        spans.append((i, e))
    # drop spans nested in other spans, and ones already wrapped
    spans.sort()
    out, last_end = [], -1
    for a, b in spans:
        if a <= last_end:
            continue
        if s[max(0, a - 5):a] == 'MU_P(':
            continue
        out.append((a, b))
        last_end = b
    return out


def remove(s):
    """Undo MU_P wraps exactly (each inserted ')' is the match of its 'MU_P(')."""
    m = mask(s)
    while True:
        i = s.find('MU_P(')
        if i < 0 or m[i] == ' ':
            return s
        j = group_fwd(s, i + 4)
        s = s[:i] + s[i + 5:j] + s[j + 1:]
        m = mask(s)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--remove', action='store_true', help='undo every MU_P wrap in the tree')
    ap.add_argument('--show', type=int, default=0, help='print this many wrapped sites')
    a = ap.parse_args()
    total = files = shown = 0
    for p in sorted(list(TREE.glob('src/**/*.c')) + list(TREE.glob('src/**/*.h')) +
                    list(TREE.glob('libs/**/*.c')) + list(TREE.glob('libs/**/*.h'))):
        if 'MSL' in p.parts or p.name in ('math_ppc.h',):
            continue
        with open(p, encoding='utf-8', newline='') as f:
            s = f.read()
        if a.remove:
            if 'MU_P(' in s and p.name != 'math.h':
                t = remove(s)
                if t != s and not a.dry_run:
                    with open(p, 'w', encoding='utf-8', newline='') as f:
                        f.write(t)
                files += 1
            continue
        spans = find_squares(s)
        if not spans:
            continue
        for a0, b0 in spans:
            if shown < a.show:
                print('   %s: %s' % (p.relative_to(TREE), ' '.join(s[a0:b0 + 1].split())))
                shown += 1
        t = s
        for a0, b0 in reversed(spans):
            t = t[:a0] + 'MU_P(' + t[a0:b0 + 1] + ')' + t[b0 + 1:]
        if not a.dry_run:
            with open(p, 'w', encoding='utf-8', newline='') as f:
                f.write(t)
        total += len(spans)
        files += 1
    print(('%d files unwrapped' % files) if a.remove else ('%d squares wrapped in %d files' % (total, files)))


if __name__ == '__main__':
    main()
