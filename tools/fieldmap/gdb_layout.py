# Run inside gdb: gdb -batch -x gdb_layout.py melee_game.dbg
# For every union reachable from a named type, flatten each view to leaves with (console offset,
# console size, native offset, native size) and report leaf pairs whose aliasing differs between the
# 32-bit big-endian console layout and the native x64 layout.
import gdb, json, re, os, sys

OUT = os.environ.get('ALIAS_OUT', 'layout_out.json')
NAMES = os.environ.get('ALIAS_NAMES', 'info_types.txt')

C = gdb
SIZE_T_NAMES = {'size_t', 'uintptr_t', 'intptr_t', 'ptrdiff_t', 'ssize_t', 'mu_uptr', 'uptr', 'mu_sptr'}
memo_cl = {}
mismatch = set()


def rup(x, a):
    return (x + a - 1) // a * a


def typedef_names(t):
    names = []
    while t.code == C.TYPE_CODE_TYPEDEF:
        names.append(t.name)
        t = t.target()
    return names


def is_discptr(s):
    if s.code != C.TYPE_CODE_UNION:
        return False
    fn = [f.name for f in s.fields()]
    return fn == ['raw', 'type_']


def key(s):
    return str(s)


def cl(t):
    """console (size, align) in bytes"""
    names = typedef_names(t)
    s = t.strip_typedefs()
    code = s.code
    if code == C.TYPE_CODE_PTR:
        return (4, 4)
    if code in (C.TYPE_CODE_INT, C.TYPE_CODE_BOOL, C.TYPE_CODE_CHAR, C.TYPE_CODE_ENUM, C.TYPE_CODE_FLT):
        if s.sizeof == 8 and (set(names) & SIZE_T_NAMES or 'long' in str(s) and 'long long' not in str(s)):
            return (4, 4)
        return (s.sizeof, max(1, min(s.sizeof, 8)))
    if code == C.TYPE_CODE_ARRAY:
        lo, hi = s.range()
        n = hi - lo + 1
        es, ea = cl(s.target())
        return (es * max(n, 0), ea)
    if code == C.TYPE_CODE_STRUCT:
        r = struct_layout(s)
        return (r[0], r[1])
    if code == C.TYPE_CODE_UNION:
        if is_discptr(s):
            return (4, 4)
        k = key(s)
        if k in memo_cl and not k.startswith('union {'):
            return memo_cl[k]
        size, align = 0, 1
        for f in s.fields():
            fs, fa = cl(f.type)
            size, align = max(size, fs), max(align, fa)
        # explicit alignment on the native type (aligned(N)) beyond natural
        nat_native = 1
        for f in s.fields():
            nat_native = max(nat_native, natural_native_align(f.type))
        if s.alignof > nat_native:
            align = max(align, s.alignof)
        r = (rup(size, align), align)
        memo_cl[k] = r
        return r
    if code == C.TYPE_CODE_FUNC:
        return (0, 1)
    if code == C.TYPE_CODE_VOID:
        return (0, 1)
    return (s.sizeof, max(1, min(s.sizeof, 8)))


def natural_native_align(t):
    s = t.strip_typedefs()
    try:
        return s.alignof
    except Exception:
        return 1


memo_layout = {}


def struct_layout(s):
    """-> (csize, calign, [(field, coff_bits, noff_bits, cbits_size)] , native_model_ok)"""
    k = key(s)
    if k in memo_layout and not k.startswith('struct {'):
        return memo_layout[k]
    fields = []
    coff = 0
    calign = 1
    # native model to self-check our understanding of the native layout
    noff_model = 0
    ok = True
    packed = False
    nat_native = 1
    for f in s.fields():
        if f.is_base_class:
            continue
        nat_native = max(nat_native, natural_native_align(f.type))
    try:
        if s.alignof < nat_native:
            packed = True
    except Exception:
        pass
    for f in s.fields():
        if not hasattr(f, 'bitpos'):
            continue  # static
        noff = f.bitpos
        if f.bitsize:
            ft = f.type.strip_typedefs()
            unit = ft.sizeof * 8
            cu = cl(f.type)[0] * 8 or unit
            if f.bitsize == 0:
                coff = rup(coff, cu)
            elif (coff % cu) + f.bitsize > cu and not packed:
                coff = rup(coff, cu)
            fields.append((f, coff, noff, f.bitsize))
            coff += f.bitsize
            if not packed:
                calign = max(calign, cl(f.type)[1])
            continue
        fs, fa = cl(f.type)
        if packed:
            fa = 1
        coff = rup(coff, fa * 8)
        fields.append((f, coff, noff, fs * 8))
        coff += fs * 8
        calign = max(calign, fa)
    if packed:
        calign = max(calign, s.alignof)
    # explicit alignment attributes on the type
    if not packed and s.alignof > nat_native:
        calign = max(calign, s.alignof)
    csize = rup((coff + 7) // 8, calign)
    r = (csize, calign, fields, ok)
    memo_layout[k] = r
    return r


def contains_ptr(t, depth=0):
    s = t.strip_typedefs()
    if depth > 12:
        return False
    c = s.code
    if c == C.TYPE_CODE_PTR:
        return True
    if c == C.TYPE_CODE_ARRAY:
        return contains_ptr(s.target(), depth + 1)
    if c in (C.TYPE_CODE_STRUCT, C.TYPE_CODE_UNION):
        if is_discptr(s):
            return False
        return any(contains_ptr(f.type, depth + 1) for f in s.fields() if hasattr(f, 'bitpos'))
    names = typedef_names(t)
    if s.code == C.TYPE_CODE_INT and s.sizeof == 8:
        return True
    return False


def kind_of(t):
    s = t.strip_typedefs()
    c = s.code
    if c == C.TYPE_CODE_PTR:
        return 'ptr'
    if c == C.TYPE_CODE_FLT:
        return 'flt'
    if c == C.TYPE_CODE_ENUM:
        return 'int'
    if c in (C.TYPE_CODE_INT, C.TYPE_CODE_BOOL, C.TYPE_CODE_CHAR):
        return 'int'
    if c == C.TYPE_CODE_ARRAY:
        return 'arr'
    if c in (C.TYPE_CODE_STRUCT, C.TYPE_CODE_UNION):
        return 'agg'
    return 'other'


def flatten(t, cbase, nbase, path, out, depth=0):
    """leaves: (path, cbit, cbits, nbit, nbits, kind, typename)"""
    s = t.strip_typedefs()
    c = s.code
    if depth > 16:
        return
    if c == C.TYPE_CODE_UNION and is_discptr(s):
        out.append((path, cbase, 32, nbase, 32, 'discptr', str(t)))
        return
    if c == C.TYPE_CODE_STRUCT:
        r = struct_layout(s)
        for (f, coff, noff, cb) in r[2]:
            name = f.name or '<anon>'
            if f.bitsize:
                out.append((path + '.' + name, cbase + coff, f.bitsize, nbase + noff, f.bitsize, 'bits', str(f.type)))
            else:
                flatten(f.type, cbase + coff, nbase + noff, path + '.' + name, out, depth + 1)
        return
    if c == C.TYPE_CODE_UNION:
        for f in s.fields():
            flatten(f.type, cbase, nbase, path + '.' + (f.name or '<anon>'), out, depth + 1)
        return
    if c == C.TYPE_CODE_ARRAY:
        lo, hi = s.range()
        n = hi - lo + 1
        et = s.target()
        if n > 0 and contains_ptr(et) and n <= 64:
            ecs = cl(et)[0] * 8
            ens = et.strip_typedefs().sizeof * 8
            for i in range(n):
                flatten(et, cbase + i * ecs, nbase + i * ens, '%s[%d]' % (path, i), out, depth + 1)
            return
        cs = cl(t)[0] * 8
        out.append((path, cbase, cs, nbase, s.sizeof * 8, 'arr', str(et)))
        return
    k = kind_of(t)
    cs = cl(t)[0] * 8
    out.append((path, cbase, cs, nbase, s.sizeof * 8, k, str(t)))


def views_of(u, prefix, expand_unions=True):
    """Split a union into views. A member that is itself a union is expanded into its members."""
    res = []
    for f in u.strip_typedefs().fields():
        name = prefix + (f.name or '<anon>')
        ft = f.type.strip_typedefs()
        if expand_unions and ft.code == C.TYPE_CODE_UNION and not is_discptr(ft):
            res.extend(views_of(ft, name + '.'))
        else:
            leaves = []
            flatten(f.type, 0, 0, name, leaves)
            res.append((name, leaves))
    return res


def classify(a, b):
    ac0, ac1 = a[1], a[1] + a[2]
    bc0, bc1 = b[1], b[1] + b[2]
    an0, an1 = a[3], a[3] + a[4]
    bn0, bn1 = b[3], b[3] + b[4]
    cov = min(ac1, bc1) > max(ac0, bc0)
    nov = min(an1, bn1) > max(an0, bn0)
    if not cov and not nov:
        return None
    same_delta = (a[3] - a[1]) == (b[3] - b[1])
    aptr = a[5] == 'ptr'
    bptr = b[5] == 'ptr'
    if cov and not same_delta:
        return 'DELTA'
    if nov and not cov:
        return 'CLOBBER'
    # same delta, console overlap
    if aptr and bptr:
        return None
    if aptr or bptr:
        return 'PTRPUN'
    if a[2] == b[2] and a[1] == b[1]:
        return None
    if a[5] == 'arr' and b[5] == 'arr':
        return None
    if a[5] == 'bits' or b[5] == 'bits':
        return 'BITS'
    return 'ENDIAN'


def analyze_union(u, label, results):
    views = views_of(u, '')
    pairs = []
    for i in range(len(views)):
        for j in range(i + 1, len(views)):
            vi, li = views[i]
            vj, lj = views[j]
            for a in li:
                for b in lj:
                    cls = classify(a, b)
                    if cls:
                        pairs.append((cls, a, b))
    moved = []
    for v, leaves in views:
        for l in leaves:
            if l[1] != l[3] or l[2] != l[4]:
                moved.append(l)
    results.append({'label': label, 'union': str(u), 'views': [[v, l] for v, l in views], 'pairs': pairs,
                    'nmoved': len(moved)})


def find_unions(t, label, results, seen, depth=0):
    s = t.strip_typedefs()
    if depth > 10:
        return
    for f in s.fields():
        if not hasattr(f, 'bitpos'):
            continue
        ft = f.type.strip_typedefs()
        while ft.code == C.TYPE_CODE_ARRAY:
            ft = ft.target().strip_typedefs()
        name = label + '.' + (f.name or '<anon>')
        if ft.code == C.TYPE_CODE_UNION and not is_discptr(ft):
            k = str(ft)
            if k.startswith('union {'):
                kk = name
            else:
                kk = k
            if kk not in seen:
                seen.add(kk)
                analyze_union(ft, name, results)
        elif ft.code == C.TYPE_CODE_STRUCT and (ft.name is None or str(ft).startswith('struct {')):
            find_unions(ft, name, results, seen, depth + 1)


def main():
    names = []
    for line in open(NAMES, encoding='utf-8', errors='replace'):
        m = re.match(r'^\s*(\d+:)?\s*(struct|union)\s+(\w+);', line)
        if m:
            names.append(m.group(2) + ' ' + m.group(3))
            continue
        m = re.match(r'^\s*(\d+:)?\s*typedef\s+(struct|union)\s+\{\.\.\.\}\s+(\w+);', line)
        if m:
            names.append(m.group(3))
    names = sorted(set(names))
    results = []
    seen = set()
    errors = []
    for n in names:
        try:
            t = gdb.lookup_type(n.split(' ', 1)[1] if ' ' in n else n) if False else None
            if n.startswith('struct '):
                t = gdb.lookup_type(n[7:]) if False else gdb.parse_and_eval('(%s*)0' % n).type.target()
            elif n.startswith('union '):
                t = gdb.parse_and_eval('(%s*)0' % n).type.target()
            else:
                t = gdb.lookup_type(n)
            s = t.strip_typedefs()
            if s.code == C.TYPE_CODE_UNION:
                k = str(s)
                kk = k if not k.startswith('union {') else n
                if kk not in seen and not is_discptr(s):
                    seen.add(kk)
                    analyze_union(s, n, results)
            if s.code in (C.TYPE_CODE_STRUCT, C.TYPE_CODE_UNION):
                find_unions(s, n, results, seen)
        except Exception as e:
            errors.append((n, str(e)))
    json.dump({'results': results, 'errors': errors}, open(OUT, 'w'))
    print('unions', len(results), 'errors', len(errors), 'pairs', sum(len(r['pairs']) for r in results))


if os.environ.get('ALIAS_MAIN', '1') == '1':
    main()
