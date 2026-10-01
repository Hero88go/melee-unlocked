"""Generate the native MexTK headers Training Mode CE compiles against, plus the symbol map that links
its calls straight to the decomp's functions.

MexTK describes the game's structs at their console offsets. Natively every pointer is 8 bytes, so
each game-owned struct has a different layout. For every MexTK type paired with a decomp type
(type_pairs.json) the struct body is replaced by a twin: a union of padded anonymous structs, one
per MexTK member, each member at the native offset of the decomp member with the same console
offset (console offsets: mextk_layout.py over the MexTK headers; decomp: the game DLL's DWARF,
tools/tmce/gdb_decomp_info.py). A member that cannot be placed safely is left out, so a use of it
fails to compile and gets fixed by hand: no decomp member at that console offset, a different size,
or a pointer where the native struct keeps a 4-byte disc pointer.

Disc-resident types (loaded from .dat files, big-endian with 4-byte pointers) become opaque, so any
field access in TM-CE fails to compile and is rewritten with the disc accessors.

Fixed console addresses of the MexTK globals (`static T *stc_x = (void *)0x80...;`) are rewritten to
the decomp symbol at that address.

usage: python tools/tmce/gen_mextk.py <decomp_info.json> <out MexTK dir> <report>
       python tools/tmce/gen_mextk.py --request <request.json>
"""
import json
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mextk_layout as M  # noqa: E402
import mextk_symbols as MS  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(HERE))
MEXTK = os.path.join(ROOT, 'run-source', 'tmce-src', 'MexTK')
PAIRS = json.load(open(os.path.join(HERE, 'type_pairs.json')))
EXTRAS = json.load(open(os.path.join(HERE, 'twin_extras.json')))
COMMENTS = M.comment_offsets(os.path.join(MEXTK, 'include'))

# Loaded from .dat files: big-endian, 4-byte pointers. Opaque natively.
DISC_TYPES = [
    'JOBJDesc', 'MatAnimDesc', 'MatAnimJointDesc', 'AnimJointDesc', 'POBJDesc', 'COBJDesc', 'WOBJDesc',
    '_HSD_ImageDesc', '_HSD_Tlut', '_HSD_TObjTev', 'LObjDesc', '_HSD_LightPointDesc', '_HSD_LightSpotDesc',
    'HSD_FogDesc', 'HSD_SObjDesc', 'JOBJSet', 'MapHead', 'MapDesc', 'StageFile', 'grGroundParam', 'GrDesc',
    'MapGObjDesc', 'CollGroupDesc', 'CollLineDesc', 'ftCommonData', 'ftData', 'Figatree', 'itData',
    'itCommonAttr', 'EffectModelDesc', 'PtclDesc', 'TexGDesc', 'ColAnimDesc', 'ItemModelDesc',
    'HSD_ArchiveHeader', 'HSD_ArchiveRelocationInfo', 'HSD_ArchivePublicInfo', 'HSD_ArchiveExternInfo',
    'MapData', 'GeneralPoints', 'GeneralPointsInfo', 'LineHazardDesc', 'MapItemDesc', 'FtAction',
]

# Nested twin members that the decomp keeps as disc data (a DISC_STRUCT member: big-endian bytes in
# native memory, copied from a .dat file). Their twin must read big-endian too, on every level: the
# storage order does not reach into nested structs (mu_disc.h). (MexTK type, member path).
# FighterData.attr = Fighter.co_attrs (ftCo_DatAttrs, ft/types.h), filled by struct copy from ftData.
BE_NESTED = {('FighterData', 'attr')}

# Hand edits kept in the generated headers: (file, original text, replacement).
HEADER_EDITS = [
    ('hsd.h',
     'struct HSD_Material\n{\n    GXColor ambient;\n    GXColor diffuse;\n    GXColor specular;\n    float alpha;\n    float shininess;\n};',
     'struct HSD_Material\n{\n    GXColor ambient;\n    GXColor diffuse;\n    GXColor specular;\n    float alpha;\n    float shininess;\n}\n#ifdef MU_NATIVE\n/* disc data, read by the game as big-endian (sysdolphin mobj.h): TM-CE\'s alpha writes must match */\n__attribute__((scalar_storage_order("big-endian")))\n#endif\n;'),
]


# The C library: TM-CE's calls go to the host's, as the rest of the native game's do.
CRT = {'memcmp', 'memcpy', 'memmove', 'memset', 'sprintf', 'strchr', 'strcmp', 'strcpy', 'strlen', 'strncmp',
       'strncpy', 'strtoul', 'tolower', 'vsnprintf', 'fmodf', 'sqrtf', '_vsprintf', 'abs'}


def request(path):
    pairs = sorted(set(PAIRS.values()))
    sym = MS.Symbols()
    f = MS.functions(sym)
    g = MS.globals_(sym)
    req = {'types': pairs, 'functions': sorted({v[1] for v in f.values() if v[1]}),
           'globals': sorted({v['symbol'] for v in g.values() if v['symbol']})}
    json.dump(req, open(path, 'w'))


def typename_of(ttext):
    """'ColorOverlay %s[3]' -> ('ColorOverlay', '[3]'); None for pointers, inline structs, bitfields."""
    m = re.match(r'^\s*(?:(?:struct|union)\s+)?(\w+)\s+%s((?:\[\d+\])*)\s*$', ttext)
    if not m:
        return None, None
    return m.group(1), m.group(2)


class Gen:
    def __init__(self, model, info):
        self.model = model
        self.types = info['types']
        self.report = []
        self._invariant = {}
        self.pads = 0
        self.accessors = []

    # ---- which MexTK types have the same layout natively ----
    def invariant(self, name):
        """True when a MexTK type has no pointers and no bitfields: identical console and native layout."""
        if name in self._invariant:
            return self._invariant[name]
        self._invariant[name] = False          # recursion guard
        if name in PAIRS:
            ok = False
        elif name in M.PRIMS:
            ok = True
        elif name in self.model.typedefs:
            ok = self.node_invariant(self.model.typedefs[name], 0)
        else:
            ok = False
        self._invariant[name] = ok
        return ok

    def node_invariant(self, t, depth):
        from pycparser import c_ast
        if depth > 20:
            return False
        if isinstance(t, (c_ast.PtrDecl, c_ast.FuncDecl)):
            return False
        if isinstance(t, (c_ast.ArrayDecl, c_ast.TypeDecl)):
            return self.node_invariant(t.type, depth + 1)
        if isinstance(t, c_ast.Enum):
            return True
        if isinstance(t, c_ast.IdentifierType):
            n = ' '.join(t.names)
            if n in M.PRIMS:
                return True
            return self.invariant(n)
        if isinstance(t, (c_ast.Struct, c_ast.Union)):
            if t.name and t.name in PAIRS:
                return False
            s = self.model.resolve(t)
            if s is None:
                return False
            for d in s.decls or []:
                if d.bitsize is not None:
                    return False
                if not self.node_invariant(d.type, depth + 1):
                    return False
            return True
        return False

    def decomp_rows(self, dtype):
        rows = self.types[dtype]['members']
        by_cbit = {}
        for r in rows:
            by_cbit.setdefault(r['cbit'], []).append(r)
        return rows, by_cbit

    def region_same(self, rows, cbit, cbits):
        """The decomp region [cbit, cbit+cbits) has the same bytes at the same relative offsets natively
        (no member inside changes size). Returns the native start bit or None."""
        start = None
        for r in rows:
            if r['cbit'] < cbit + cbits and r['cbit'] + r['cbits'] > cbit:
                inside = r['cbit'] >= cbit and r['cbit'] + r['cbits'] <= cbit + cbits
                if r['kind'] != 'bits' and r['cbits'] != r['nbits'] and inside:
                    return self.union_linear(rows, cbit, cbits)
                if r['cbit'] == cbit and r['kind'] != 'bits':
                    cand = r['nbit']
                    if start is None or cand < start:
                        start = cand
        if start is None:
            # inside a larger same-size region (an array of scalars, a padding array)
            for r in rows:
                if r['cbit'] <= cbit and cbit + cbits <= r['cbit'] + r['cbits'] and r['cbits'] == r['nbits'] \
                        and r['kind'] in ('arr', 'int', 'flt', 'agg'):
                    return r['nbit'] + (cbit - r['cbit'])
            return self.union_linear(rows, cbit, cbits)
        # the region itself must not contain a size-changing member (checked above) and must map linearly
        for r in rows:
            if cbit <= r['cbit'] < cbit + cbits and r['kind'] != 'bits':
                if r['nbit'] - start != r['cbit'] - cbit:
                    return self.union_linear(rows, cbit, cbits)
        return start

    def union_linear(self, rows, cbit, cbits):
        """Inside a decomp union (not a disc pointer): the offset from the union start, unchanged."""
        best = None
        for r in rows:
            if r.get('union') and not r.get('discptr') and r['cbit'] <= cbit and cbit + cbits <= r['cbit'] + r['cbits']:
                if best is None or r['cbits'] < best['cbits']:
                    best = r
        if best is None:
            return None
        return best['nbit'] + (cbit - best['cbit'])

    def place(self, rows, by_cbit, mm):
        """(native bit offset, None) for a MexTK scalar/pointer/array member, or (None, reason)."""
        cands = by_cbit.get(mm['cbit'], [])
        if mm['bitfield']:
            return self.place_bits(rows, mm)
        if mm['kind'] == 'ptr':
            for c in cands:
                if c['kind'] in ('ptr', 'int') and c['cbits'] == 32 and c['nbits'] == 64 and not c.get('discptr'):
                    return c['nbit'], None          # a pointer, or a uintptr_t the decomp keeps one in
            for r in rows:
                if r['kind'] == 'arr' and ptr_array(r) and r['cbit'] <= mm['cbit'] < r['cbit'] + r['cbits'] \
                        and (mm['cbit'] - r['cbit']) % 32 == 0 and r['nbits'] == 2 * r['cbits']:
                    return r['nbit'] + 2 * (mm['cbit'] - r['cbit']), None
            if any(c.get('discptr') or (c['kind'] in ('ptr', 'int', 'agg') and c['nbits'] == 32) for c in cands):
                return None, 'native keeps 4 bytes here (disc pointer or integer)'
            return None, 'no decomp pointer at this console offset'
        if mm['kind'] == 'arr' and '*' in mm['type'].split('%s')[0]:
            for c in cands:
                if c['kind'] == 'arr' and ptr_array(c) and c['cbits'] == mm['cbits'] and c['nbits'] == 2 * c['cbits']:
                    return c['nbit'], None
            return None, 'no decomp pointer array of that length here'
        nb = self.region_same(rows, mm['cbit'], mm['cbits'])
        if nb is not None:
            return nb, None
        return None, 'no same-size decomp member (%s)' % ', '.join(
            '%s %s %d/%d' % (c['path'], c['kind'], c['cbits'], c['nbits']) for c in cands)

    def place_bits(self, rows, mm):
        """A MexTK bitfield (console bits counted from the most significant bit) inside a decomp bitfield or
        plain integer: the same value bits, at their native little-endian position."""
        b, w = mm['cbit'], mm['cbits']
        best = None
        for r in rows:
            if r['kind'] not in ('bits', 'int'):
                if not (r['kind'] == 'arr' and r['cbits'] == r['nbits'] and byte_array(r)):
                    continue
            if not (r['cbit'] <= b and b + w <= r['cbit'] + r['cbits']):
                continue
            if r['kind'] == 'int' and r['cbits'] != r['nbits']:
                continue
            if best is None or r['cbits'] < best['cbits']:
                best = r
        if best is None:
            return None, 'no decomp bitfield or integer holds these bits'
        r = best
        if r['kind'] == 'arr':
            byte = (b - r['cbit']) // 8
            if (b % 8) + w > 8:
                return None, 'bitfield crosses bytes of a byte array'
            return r['nbit'] + byte * 8 + (8 - (b % 8) - w), None
        return r['nbit'] + r['cbits'] - (b - r['cbit']) - w, None

    # ---- twin emission ----
    def twin_body(self, mex_name, dtype):
        mem = self.model.flatten(mex_name)
        tag = getattr(self.model.typedefs[mex_name].type, 'name', None) or mex_name
        for m in mem:
            if '.' in m['path'] or '[' in m['path']:
                continue
            c = COMMENTS.get((tag, m['path']))
            if c is not None and c != m['cbit'] // 8:
                self.report.append('OFFSET %s.%s: header comment says 0x%x, the declarations give 0x%x' % (
                    mex_name, m['path'], c, m['cbit'] // 8))
        rows, by_cbit = self.decomp_rows(dtype)
        size = self.types[dtype]['native_size']
        lines = []
        self.emit_members(mem, '', rows, by_cbit, 0, lines, mex_name, '        ')
        # members MexTK folds into a neighbouring struct (tmce twin_extras.json): name, C type, console offset
        for name, ctype, coff in EXTRAS.get(mex_name, []):
            if ctype.endswith('*'):
                mm = dict(path=name, cbit=int(coff, 16) * 8, cbits=32, kind='ptr', type=ctype + '%s',
                          bitfield=False, anon=False)
            else:
                size = {'int': 32, 's32': 32, 'u32': 32, 'float': 32, 's16': 16, 'u16': 16, 'u8': 8, 's8': 8}[ctype]
                mm = dict(path=name, cbit=int(coff, 16) * 8, cbits=size, kind='flt' if ctype == 'float' else 'int',
                          type=ctype + ' %s', bitfield=False, anon=False)
            nb, why = self.place(rows, by_cbit, mm)
            if nb is None:
                self.report.append('%s.%s (extra): %s' % (mex_name, name, why))
                continue
            lines.append(self.at(nb, M_decl(mm['type'], name) + ';', '        '))
        body = '{\n    union {\n        char _mex_native_size[%d];\n%s    };\n}' % (size, ''.join(lines))
        return body

    def emit_members(self, mem, prefix, rows, by_cbit, base_nbit, lines, label, ind):
        """Emit every direct member of `prefix` (anonymous members flattened) at native offsets
        relative to base_nbit."""
        n = [0]
        direct = [m for m in mem if parent(m['path']) == prefix and not m['anon'] and m['path'] != prefix]
        seen = set()
        for m in direct:
            name = m['path'].rsplit('.', 1)[-1]
            if '[' in name or name in seen:
                continue
            seen.add(name)
            decl = self.member_decl(m, name, mem, rows, by_cbit, base_nbit, label, ind)
            if decl is None:
                continue
            lines.append(decl)

    def member_decl(self, m, name, mem, rows, by_cbit, base_nbit, label, ind):
        tname, dims = typename_of(m['type'])
        where = '%s.%s' % (label, m['path'])
        agg_array = m['kind'] == 'arr' and '*' not in m['type'].split('%s')[0] and \
            (('{' in m['type']) or (tname is not None and not self.scalar(tname)))
        if m['kind'] == 'agg' or agg_array:
            # nested aggregate (or array of aggregates)
            count = 1
            for d in re.findall(r'\[(\d+)\]', dims or ''):
                count *= int(d)
            if tname and tname in PAIRS and PAIRS[tname] in self.types:
                dt = PAIRS[tname]
                cands = [c for c in by_cbit.get(m['cbit'], []) if c['kind'] in ('agg', 'arr')]
                want = self.types[dt]['native_size']
                for c in cands:
                    same_type = re.sub(r'^(struct|union)\s+', '', c['type'].split('[')[0].strip()) == dt
                    if c['nbits'] == want * 8 * count and (c['cbits'] == m['cbits'] or same_type):
                        return self.at(c['nbit'] - base_nbit, '%s %s%s;' % (tname, name, dims or ''), ind)
                if count > 1:
                    esz = m['cbits'] // count
                    starts = []
                    for i in range(count):
                        hit = [c for c in by_cbit.get(m['cbit'] + i * esz, []) if c['kind'] == 'agg'
                               and re.sub(r'^(struct|union)\s+', '', c['type'].strip()) == dt]
                        if not hit:
                            break
                        starts.append(hit[0]['nbit'])
                    if len(starts) == count and all(starts[i] == starts[0] + i * want * 8 for i in range(count)):
                        return self.at(starts[0] - base_nbit, '%s %s%s;' % (tname, name, dims or ''), ind)
            if tname and self.invariant(tname):
                nb = self.region_same(rows, m['cbit'], m['cbits'])
                if nb is not None:
                    return self.at(nb - base_nbit, '%s %s%s;' % (tname, name, dims or ''), ind)
                self.report.append('%s: value type %s over a region that changes natively' % (where, tname))
                return None
            if agg_array:
                return self.agg_array_decl(m, name, count, mem, rows, by_cbit, base_nbit, label, ind, where)
            # inline or unpaired aggregate: lay its members out in place
            start, _ = self.first_native(m, mem, rows, by_cbit)
            if start is None:
                self.report.append('%s: no member of the nested struct maps' % where)
                return None
            inner = []
            sub = [x for x in mem if x['path'].startswith(m['path'] + '.')]
            self.emit_members(mem, m['path'], rows, by_cbit, start, inner, label, ind + '        ')
            if not inner:
                return None
            end = self.native_end(sub, rows, by_cbit)
            span = max(1, (end - start + 7) // 8) if end else 1
            body = self.wrap(m['type'], span, inner, ind)
            if (label, m['path']) in BE_NESTED:
                body = be_levels(body)
            if tname and tname in PAIRS:
                self.report.append('%s: %s laid out in place (decomp splits it differently)' % (where, tname))
            return self.at(start - base_nbit, '%s %s;' % (body, name), ind)
        nbit, why = self.place(rows, by_cbit, m)
        if nbit is None:
            self.report.append('%s: left out (%s)' % (where, why))
            self.bit_access(m, rows, label)
            return None
        rel = nbit - base_nbit
        if rel < 0:
            self.report.append('%s: left out (native offset before its parent)' % where)
            return None
        if m['bitfield']:
            ttext = m['type'].replace('%s', '').strip()
            unit = self.console_size(ttext) * 8 or 32
            ustart = rel // unit * unit
            shift = rel - ustart
            if shift + m['cbits'] > unit:
                self.report.append('%s: left out (bitfield crosses its unit natively)' % where)
                return None
            self.pads += 1
            pre = 'char _p%d[%d]; ' % (self.pads, ustart // 8) if ustart else ''
            gap = '%s : %d; ' % (ttext, shift) if shift else ''
            return '%sstruct { %s%s%s %s : %d; };\n' % (ind, pre, gap, ttext, name, m['cbits'])
        if rel % 8:
            self.report.append('%s: left out (not byte aligned natively)' % where)
            return None
        return self.at(rel, M_decl(m['type'], name) + ';', ind)

    def bit_access(self, m, rows, label):
        """An integer or bitfield MexTK reads as one value where the decomp has separate bitfields (their
        bit order within a byte is reversed natively): get/set functions over the native bits."""
        if m['kind'] not in ('int', 'bits') or '[' in m['path'] or m['cbits'] > 32:
            return
        nbits = []
        for i in range(m['cbits']):
            nb, _ = self.place_bits(rows, dict(cbit=m['cbit'] + i, cbits=1))
            if nb is None:
                return
            nbits.append(nb)
        self.accessors.append((label, m['path'], m['cbits'], nbits))

    def agg_array_decl(self, m, name, count, mem, rows, by_cbit, base_nbit, label, ind, where):
        """An array of structs with pointers inside: an array of an anonymous element twin whose stride is the
        decomp array's native element size."""
        arr = [c for c in by_cbit.get(m['cbit'], []) if c['kind'] == 'arr' and c['cbits'] == m['cbits']]
        if not arr:
            a = self.joined_arrays(m, by_cbit)
            if a is None:
                self.report.append('%s: array left out (no decomp array of the same console size)' % where)
                return None
            arr = [a]
        a = arr[0]
        if a['nbits'] % count:
            self.report.append('%s: array left out (native size %d not a multiple of %d)' % (where, a['nbits'], count))
            return None
        estride = a['nbits'] // count
        first = m['path'] + '[0]'
        inner = []
        self.emit_members(mem, first, rows, by_cbit, a['nbit'], inner, label, ind + '        ')
        if not inner:
            self.report.append('%s: array left out (no element member maps)' % where)
            return None
        body = self.wrap(m['type'], estride // 8, inner, ind)
        return self.at(a['nbit'] - base_nbit, '%s %s[%d];' % (body, name, count), ind)

    def wrap(self, ttext, span, inner, ind):
        """The in-place twin of a nested struct or union: its MexTK tag kept when it has one."""
        tag = re.match(r'^\s*(struct|union)\s+(\w+)\s*\{', ttext)
        if tag and tag.group(1) == 'union':
            return 'union %s {\n%s    char _mex_span[%d];\n%s%s}' % (tag.group(2), ind, span, ''.join(inner), ind)
        head = 'struct %s' % tag.group(2) if tag else 'struct'
        return '%s {\n%s    union {\n%s        char _mex_span[%d];\n%s%s    };\n%s}' % (
            head, ind, ind, span, ''.join(inner), ind, ind)

    def joined_arrays(self, m, by_cbit):
        """Consecutive decomp arrays of one element type that together cover the MexTK array, laid out back
        to back natively too: seen as one array."""
        cbit, nbit, total, etype = m['cbit'], None, 0, None
        first = None
        while total < m['cbits']:
            cands = [c for c in by_cbit.get(cbit, []) if c['kind'] == 'arr']
            if not cands:
                return None
            c = cands[0]
            et = c['type'].split('[')[0].strip()
            if etype is None:
                etype, first, nbit = et, c, c['nbit']
            elif et != etype or c['nbit'] != nbit:
                return None
            total += c['cbits']
            cbit += c['cbits']
            nbit = c['nbit'] + c['nbits']
        if total != m['cbits']:
            return None
        return dict(first, cbits=total, nbits=nbit - first['nbit'])

    def scalar(self, tname):
        if tname in M.PRIMS:
            return True
        t = self.model.typedefs.get(tname)
        return t is not None and M.kind_of(self.model, t) in ('int', 'flt', 'ptr')

    def console_size(self, tname):
        t = self.model.typedefs.get(tname)
        if t is None:
            return M.PRIMS.get(tname, (0, 1))[0]
        return self.model.type_layout(t)[0]

    def first_native(self, m, mem, rows, by_cbit):
        """Native bit where a nested aggregate starts: the native start of the decomp member at its console
        offset, or of its first placeable member minus that member's console distance when that is 0."""
        cands = [c for c in by_cbit.get(m['cbit'], []) if c['kind'] != 'bits']
        if cands:
            return min(c['nbit'] for c in cands), None
        nb = self.region_same(rows, m['cbit'], 8)
        return nb, None

    def native_end(self, sub, rows, by_cbit):
        end = 0
        for x in sub:
            if x['kind'] == 'agg' or x['anon']:
                continue
            if x['kind'] == 'ptr':
                nb, _ = self.place(rows, by_cbit, x)
                if nb is not None:
                    end = max(end, nb + 64)
                continue
            if x['bitfield']:
                nb, _ = self.place(rows, by_cbit, x)
                if nb is not None:
                    end = max(end, nb + x['cbits'])
                continue
            nb = self.region_same(rows, x['cbit'], x['cbits'])
            if nb is not None:
                end = max(end, nb + x['cbits'])
        return end

    def at(self, rel_bits, decl, ind):
        if rel_bits % 8:
            return None
        if rel_bits == 0:
            return '%sstruct { %s };\n' % (ind, decl)
        self.pads += 1
        return '%sstruct { char _p%d[%d]; %s };\n' % (ind, self.pads, rel_bits // 8, decl)


def be_levels(body):
    """Big-endian storage order on every struct/union level of a generated twin body (the attribute
    goes between the keyword and the tag). Members of a named value type (Vec3...) keep that type's
    own order: list them in the report if the decomp stores them big-endian."""
    return re.sub(r'\b(struct|union)(\s+\w+)?\s*\{',
                  lambda mo: '%s %s%s {' % (mo.group(1), BE, mo.group(2) or ''), body)


def ptr_array(row):
    return re.search(r'\*\s*(\[\d+\])+$', row['type']) is not None


def byte_array(row):
    return re.match(r'^(const\s+)?(u8|s8|char|unsigned char|signed char|bool|_Bool|uint8_t|int8_t)\s*\[', row['type']) is not None


BE = '__attribute__((scalar_storage_order("big-endian")))'


def disc_body(model, t, depth=0):
    """Body text ('{ ... }') of a struct/union node laid out as on the disc."""
    from pycparser import c_ast
    s = model.resolve(t)
    lines = []
    for d in s.decls or []:
        lines.append('    ' * (depth + 1) + disc_decl(model, d.type, d.name or '', d.bitsize, depth + 1) + ';')
    return '{\n' + '\n'.join(lines) + '\n' + '    ' * depth + '}'


def disc_decl(model, t, name, bitsize, depth):
    from pycparser import c_ast
    dims = ''
    while isinstance(t, c_ast.ArrayDecl):
        dims += '[%s]' % (M.eval_const(t.dim) if t.dim is not None else '')
        t = t.type
    bits = ' : %d' % M.eval_const(bitsize) if bitsize is not None else ''
    if isinstance(t, c_ast.PtrDecl):
        return 'unsigned int %s%s' % (name, dims)          # disc pointer: MEX_DP() to follow it
    inner = t.type if isinstance(t, c_ast.TypeDecl) else t
    if isinstance(inner, c_ast.IdentifierType):
        tn = ' '.join(inner.names)
        if tn in M.PRIMS:
            return '%s %s%s%s' % (tn, name, dims, bits)
        td = model.typedefs.get(tn)
        if td is None:
            return '%s %s%s%s' % (tn, name, dims, bits)
        base = td
        while isinstance(base, c_ast.TypeDecl):
            base = base.type
        if isinstance(base, c_ast.IdentifierType) or isinstance(base, c_ast.Enum):
            if isinstance(base, c_ast.Enum):
                return 'int %s%s%s' % (name, dims, bits)
            return disc_decl(model, td, name, bitsize, depth)
        if isinstance(td, c_ast.PtrDecl):
            return 'unsigned int %s%s' % (name, dims)
        if isinstance(td, c_ast.ArrayDecl):
            # a typedef'd array (Mtx): expand it
            return disc_decl(model, td, name + dims, bitsize, depth)
        if isinstance(base, (c_ast.Struct, c_ast.Union)):
            kw = 'struct' if isinstance(base, c_ast.Struct) else 'union'
            return '%s %s %s %s%s' % (kw, BE, disc_body(model, base, depth), name, dims)
        return '%s %s%s%s' % (tn, name, dims, bits)
    if isinstance(inner, (c_ast.Struct, c_ast.Union)):
        kw = 'struct' if isinstance(inner, c_ast.Struct) else 'union'
        return '%s %s %s %s%s' % (kw, BE, disc_body(model, inner, depth), name, dims)
    if isinstance(inner, c_ast.Enum):
        return 'int %s%s%s' % (name, dims, bits)
    return 'int %s%s /* ? */' % (name, dims)


def M_decl(ttext, name):
    return ttext.replace('%s', name)


def parent(path):
    # 'a.b[2].c' -> 'a.b[2]'
    if '.' not in path:
        return ''
    return path.rsplit('.', 1)[0]


# ---- header rewriting ----
STRUCT_START = re.compile(r'^(struct|union)\s+(\w+)\s*(//.*)?$')


def replace_struct_blocks(text, bodies):
    """Replace the body of each `struct NAME\\n{ ... };` block at column 0 whose NAME is in bodies
    (NAME -> body text starting with '{', or None for an opaque type: the block is removed)."""
    out = []
    lines = text.split('\n')
    i = 0
    while i < len(lines):
        m = STRUCT_START.match(lines[i])
        if m and m.group(2) in bodies and i + 1 < len(lines) and lines[i + 1].strip().startswith('{'):
            name = m.group(2)
            depth = 0
            j = i + 1
            while j < len(lines):
                depth += lines[j].count('{') - lines[j].count('}')
                if depth == 0 and lines[j].rstrip().endswith(';'):
                    break
                j += 1
            body = bodies[name]
            if isinstance(body, tuple):
                out.append('%s %s %s /* disc data: big-endian, pointers are 4-byte slots (MEX_DP) */' % (m.group(1), BE, name))
                out.append(body[1] + ';')
            elif body is None:
                out.append('/* %s: disc data: opaque natively */' % name)
            else:
                out.append('%s %s /* native twin, generated */' % (m.group(1), name))
                out.append(body + ';')
            i = j + 1
            continue
        out.append(lines[i])
        i += 1
    return '\n'.join(out)


def rewrite_globals(text, fname, gl, gl_info, report, locals_needed):
    def repl(line):
        m = MS.GLOBAL_RE.search(line)
        if not m:
            return line
        name = m.group(2)
        g = gl.get(name)
        if not g or g['file'] != fname:
            return line
        if g['symbol'] is None and 0x80000000 <= g['addr'] < 0x80003100:
            return line          # the OS globals at the bottom of MEM1: MEM1 sits at 0x80000000 natively too
        if g['symbol'] is None:
            report.append('global %s at %08X: no decomp symbol, left as a console address' % (name, g['addr']))
            return '/* native: no object at %08X */ // %s' % (g['addr'], line.strip())
        sym = g['symbol']
        local = bool((gl_info.get(sym) or {}).get('local')) or g['local']
        if local:
            # file-private in the decomp: reached through a pointer its file publishes
            # (void *mu_tmce_ref_<name> = &object;), so the name becomes a macro
            locals_needed.add('%s %s %08X' % (name, sym, g['addr']))
            return '%sextern void *mu_tmce_ref_%s;\n#define %s ((%s *)((char *)mu_tmce_ref_%s + %d))' % (
                '// ' + line.strip() + '\n', name, name, m.group(1).strip(), name, g['offset'] or 0)
        target = sym
        if g['offset']:
            report.append('global %s = %s+0x%x: console offset used as is, check' % (name, sym, g['offset']))
        cast = line[m.start():m.end()]
        head = cast[:cast.index('=')]
        alias = 'mu_mx_' + re.sub(r'\W', '_', target)
        new = '%s= (void *)(%s + %d);' % (head, alias, g['offset'] or 0)
        return 'extern char %s[] __asm__("%s");\n%s%s%s' % (alias, target, line[:m.start()], new, line[m.end():])
    return '\n'.join(repl(l) for l in text.split('\n'))


def main():
    if sys.argv[1] == '--request':
        request(sys.argv[2])
        return
    info_path, out_dir, report_path = sys.argv[1:4]
    info = json.load(open(info_path))
    model = M.Model(M.preprocess(MEXTK, os.path.join(HERE, 'fakeinc')))
    g = Gen(model, info)
    bodies = {}
    for mex, dt in PAIRS.items():
        if dt not in info['types']:
            g.report.append('%s: decomp type %s missing' % (mex, dt))
            continue
        tag = getattr(model.typedefs[mex].type, 'name', None) or mex
        bodies[tag] = g.twin_body(mex, dt)
    for d in DISC_TYPES:
        t = model.typedefs.get(d)
        tag = getattr(t.type, 'name', None) if t is not None else d
        body = None
        if t is not None:
            base = t
            while not isinstance(base, (type(None),)) and hasattr(base, 'type') and base.__class__.__name__ == 'TypeDecl':
                base = base.type
            node = model.resolve(base) if base.__class__.__name__ in ('Struct', 'Union') else None
            if node is not None:
                body = ('disc', disc_body(model, node))
        bodies.setdefault(tag or d, body)
    sym = MS.Symbols()
    gl = MS.globals_(sym)
    locals_needed = set()
    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir)
    os.makedirs(os.path.join(out_dir, 'include'))
    mex_h = open(os.path.join(MEXTK, 'mex.h'), encoding='utf-8').read()
    mex_h = mex_h.replace('#define MEX_H\n', '#define MEX_H\n\n#include "mex_native_prelude.h"\n', 1)
    tail = mex_h.rindex('#endif')
    mex_h = mex_h[:tail] + '#include "mex_bit_access.h"\n#include "../native/tmce_compat.h"\n\n' + mex_h[tail:]
    open(os.path.join(out_dir, 'mex.h'), 'w', encoding='utf-8', newline='\n').write(mex_h)
    open(os.path.join(out_dir, 'mex_native_prelude.h'), 'w', newline='\n').write(
        '/* Generated by tools/tmce/gen_mextk.py: what the native MexTK headers add. */\n'
        '#pragma once\n#include <stdint.h>\n\n'
        '/* A pointer slot in disc data (a .dat file): 4 bytes holding the real address, which sits below\n'
        ' * 4 GB natively (MEM1 and the game image). */\n'
        '#define MEX_DP(slot) ((void *)(uintptr_t)(unsigned int)(slot))\n\n'
        '/* C library functions MexTK declares with the console int sizes: natively they go to wrappers\n'
        ' * (native/tmce_runtime.c), so a 32-bit size never lands in a 64-bit size_t parameter. */\n'
        '#define memcpy mex_memcpy\n#define memmove mex_memmove\n#define memset mex_memset\n'
        '#define strncmp mex_strncmp\n#define strcpy mex_strcpy\n#define strlen mex_strlen\n'
        '#define calloc mex_calloc\n#define _vsprintf mex__vsprintf\n')
    for fn in os.listdir(MEXTK):
        if fn.endswith('Function.txt'):
            shutil.copy(os.path.join(MEXTK, fn), os.path.join(out_dir, fn))
    inc = os.path.join(MEXTK, 'include')
    for fn in sorted(os.listdir(inc)):
        text = open(os.path.join(inc, fn), encoding='utf-8', errors='replace').read()
        if fn.endswith('.h'):
            text = replace_struct_blocks(text, bodies)
            text = rewrite_globals(text, fn, gl, info['globals'], g.report, locals_needed)
            for efn, orig, repl in HEADER_EDITS:
                if efn == fn:
                    if orig not in text:
                        g.report.append('HEADER EDIT %s: original text not found' % fn)
                    text = text.replace(orig, repl, 1)
        open(os.path.join(out_dir, 'include', fn), 'w', encoding='utf-8', newline='\n').write(text)
    # bit accessors for values the decomp keeps as separate bitfields
    acc = ['/* Generated by tools/tmce/gen_mextk.py: MexTK values that the decomp keeps as separate bitfields.',
           ' * Console bit order within a byte is reversed natively, so these read and write each bit where the',
           ' * native build keeps it. mexbits_get_<Type>__<path>(object), mexbits_set_<Type>__<path>(object, value). */',
           '#pragma once']
    for label, path, width, nbits in g.accessors:
        fn = '%s__%s' % (label, path.replace('.', '_'))
        get = ['static inline unsigned int mexbits_get_%s(const void *o)' % fn, '{',
               '    const unsigned char *b = (const unsigned char *)o;', '    unsigned int v = 0;']
        put = ['static inline void mexbits_set_%s(void *o, unsigned int v)' % fn, '{',
               '    unsigned char *b = (unsigned char *)o;']
        for i, nb in enumerate(nbits):
            vb = width - 1 - i
            get.append('    v |= (unsigned int)((b[%d] >> %d) & 1) << %d;' % (nb // 8, nb % 8, vb))
            put.append('    b[%d] = (unsigned char)((b[%d] & ~(1u << %d)) | (((v >> %d) & 1u) << %d));' % (
                nb // 8, nb // 8, nb % 8, vb, nb % 8))
        acc += get + ['    return v;', '}'] + put + ['}']
    open(os.path.join(out_dir, 'mex_bit_access.h'), 'w', newline='\n').write('\n'.join(acc) + '\n')
    # sizes of the twins, checked at compile time
    checks = ['/* Generated by tools/tmce/gen_mextk.py. */', '#pragma once', '#include "mex.h"']
    for mex, dt in PAIRS.items():
        if dt in info['types']:
            checks.append('_Static_assert(sizeof(%s) == %d, "%s twin size");' % (mex, info['types'][dt]['native_size'], mex))
    open(os.path.join(out_dir, 'mex_twin_checks.h'), 'w', newline='\n').write('\n'.join(checks) + '\n')
    # MexTK function name -> decomp symbol, for objcopy --redefine-syms on the TM-CE objects
    funcs = MS.functions(sym)
    redefs = []
    # MexTK functions the decomp keeps file-private: exported wrappers in the decomp file (mu_tmce_fn_*)
    overrides = json.load(open(os.path.join(HERE, 'function_overrides.json')))
    for name, target in sorted(overrides.items()):
        redefs.append('%s %s' % (name, target))
    for name, (addr, dname, hit) in sorted(funcs.items()):
        if name in overrides:
            continue
        fi = info['functions'].get(dname) if dname else None
        if name in CRT:
            continue
        if dname is None or fi is None:
            g.report.append('function %s at %08X: no native function' % (name, addr))
            continue
        if 'not_function' in fi:
            g.report.append('function %s at %08X: decomp %s is data (%s)' % (name, addr, dname, fi['not_function']))
            continue
        if dname != name:
            redefs.append('%s %s' % (name, dname))
    open(os.path.join(out_dir, 'mextk_redefine.txt'), 'w', newline='\n').write('\n'.join(redefs) + '\n')
    open(os.path.join(out_dir, 'locals_needed.txt'), 'w', newline='\n').write('\n'.join(sorted(locals_needed)) + '\n')
    open(report_path, 'w', encoding='utf-8').write('\n'.join(g.report) + '\n')
    print('twins', len([b for b in bodies.values() if isinstance(b, str)]), 'disc', len([b for b in bodies.values() if isinstance(b, tuple)]),
          'redefs', len(redefs), 'report lines', len(g.report), 'locals', len(locals_needed))


if __name__ == '__main__':
    main()
