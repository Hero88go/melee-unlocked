"""Console (32-bit big-endian PowerPC EABI) layout of the MexTK headers, from pycparser.

layout(name) -> Layout(size, align, members) where members are (path, offset_bits, size_bits, kind,
type_text, is_bitfield). Paths use '.' for nested members and [i] for arrays of aggregates.
The MexTK headers carry the console offset of most fields in comments; check() compares them.
"""
import os
import re
import subprocess
from dataclasses import dataclass, field

from pycparser import c_ast, c_generator, c_parser

HERE = os.path.dirname(os.path.abspath(__file__))
GCC = 'C:/Users/Chandler/toolchains/winlibs-gcc-15.3/mingw64/bin/gcc.exe'
PRIMS = {
    'char': (1, 1), 'signed char': (1, 1), 'unsigned char': (1, 1),
    'short': (2, 2), 'short int': (2, 2), 'unsigned short': (2, 2), 'unsigned short int': (2, 2),
    'signed short': (2, 2),
    'int': (4, 4), 'signed int': (4, 4), 'unsigned int': (4, 4), 'unsigned': (4, 4), 'signed': (4, 4),
    'long': (4, 4), 'long int': (4, 4), 'unsigned long': (4, 4), 'unsigned long int': (4, 4),
    'signed long': (4, 4),
    'long long': (8, 8), 'unsigned long long': (8, 8), 'long long int': (8, 8),
    'unsigned long long int': (8, 8), 'signed long long': (8, 8),
    'float': (4, 4), 'double': (8, 8), 'long double': (8, 8), '_Bool': (1, 1), 'void': (0, 1),
}
gen = c_generator.CGenerator()


@dataclass
class Layout:
    size: int
    align: int
    members: list = field(default_factory=list)   # (path, off_bits, size_bits, kind, type_text, bitfield)


def rup(x, a):
    return (x + a - 1) // a * a


def preprocess(mextk_dir, fakeinc):
    out = subprocess.run([GCC, '-E', '-P', '-nostdinc', '-I', fakeinc, '-D__attribute__(x)=', '-D__asm__(x)=',
                          '-D__inline=', '-D__extension__=', '-Dinline=', os.path.join(mextk_dir, 'mex.h')],
                         capture_output=True, text=True, check=True)
    return out.stdout


class Model:
    def __init__(self, text):
        self.ast = c_parser.CParser().parse(text)
        self.typedefs = {}
        self.structs = {}      # 'struct X' / 'union X' -> node with decls
        self.enums = set()
        self._collect(self.ast)
        self.memo = {}

    def _collect(self, node):
        for ext in node.ext:
            if isinstance(ext, c_ast.Typedef):
                self.typedefs[ext.name] = ext.type
                self._find_structs(ext.type)
            elif isinstance(ext, c_ast.Decl):
                self._find_structs(ext.type)

    def _find_structs(self, t):
        if isinstance(t, (c_ast.Struct, c_ast.Union)):
            kind = 'struct' if isinstance(t, c_ast.Struct) else 'union'
            if t.name and t.decls is not None:
                self.structs[kind + ' ' + t.name] = t
            for d in t.decls or []:
                self._find_structs(d.type)
        elif isinstance(t, (c_ast.TypeDecl, c_ast.PtrDecl, c_ast.ArrayDecl)):
            self._find_structs(t.type)
        elif isinstance(t, c_ast.Enum):
            if t.name:
                self.enums.add(t.name)

    def resolve(self, t):
        """struct/union node with members for a Struct/Union reference."""
        kind = 'struct' if isinstance(t, c_ast.Struct) else 'union'
        if t.decls is not None:
            return t
        return self.structs.get(kind + ' ' + (t.name or ''))

    def type_layout(self, t):
        """(size, align) of a pycparser type node (TypeDecl / PtrDecl / ArrayDecl / FuncDecl)."""
        if isinstance(t, c_ast.PtrDecl):
            return (4, 4)
        if isinstance(t, c_ast.ArrayDecl):
            es, ea = self.type_layout(t.type)
            n = eval_const(t.dim) if t.dim is not None else 0
            return (es * n, ea)
        if isinstance(t, c_ast.FuncDecl):
            return (0, 1)
        if isinstance(t, c_ast.TypeDecl):
            return self.type_layout(t.type)
        if isinstance(t, c_ast.IdentifierType):
            name = ' '.join(t.names)
            if name in PRIMS:
                return PRIMS[name]
            if name in self.typedefs:
                return self.type_layout(self.typedefs[name])
            raise KeyError('unknown type ' + name)
        if isinstance(t, (c_ast.Struct, c_ast.Union)):
            s = self.resolve(t)
            if s is None:
                return (0, 1)                 # incomplete
            lay = self.struct_layout(s)
            return (lay.size, lay.align)
        if isinstance(t, c_ast.Enum):
            return (4, 4)
        raise TypeError(type(t))

    def struct_layout(self, s):
        key = id(s)
        if key in self.memo:
            return self.memo[key]
        is_union = isinstance(s, c_ast.Union)
        members = []
        off = 0        # bits
        align = 1
        size = 0
        for d in s.decls or []:
            t = d.type
            fs, fa = self.type_layout(t)
            if d.bitsize is not None:
                width = eval_const(d.bitsize)
                unit = fs * 8
                if is_union:
                    pos = 0
                else:
                    if width == 0:
                        off = rup(off, unit)
                        continue
                    if (off % unit) + width > unit:
                        off = rup(off, unit)
                    pos = off
                    off += width
                align = max(align, fa)
                members.append((d.name, pos, width, 'bits', type_text(t), True, None))
                size = max(size, pos + width)
                continue
            pos = 0 if is_union else rup(off, fa * 8)
            members.append((d.name, pos, fs * 8, kind_of(self, t), type_text(t), False, t))
            if not is_union:
                off = pos + fs * 8
            size = max(size, pos + fs * 8)
            align = max(align, fa)
        total = rup((size + 7) // 8, align)
        lay = Layout(total, align, members)
        self.memo[key] = lay
        return lay

    def flatten(self, name):
        """All members of a named type (typedef name or 'struct X'), nested, console offsets in bits."""
        if name in self.typedefs:
            t = self.typedefs[name]
        else:
            t = self.structs[name]
        out = []
        self._flatten(t, 0, '', out, 0)
        return out

    def _flatten(self, t, base, path, out, depth):
        if depth > 12:
            return
        if isinstance(t, c_ast.TypeDecl):
            return self._flatten(t.type, base, path, out, depth)
        if isinstance(t, c_ast.IdentifierType):
            name = ' '.join(t.names)
            if name in self.typedefs:
                return self._flatten(self.typedefs[name], base, path, out, depth)
            return
        if isinstance(t, (c_ast.Struct, c_ast.Union)):
            s = self.resolve(t)
            if s is None:
                return
            lay = self.struct_layout(s)
            for (mname, pos, bits, kind, ttext, bitfield, mt) in lay.members:
                p = (path + '.' + mname) if (mname and path) else (mname or path)
                out.append(dict(path=p, cbit=base + pos, cbits=bits, kind=kind, type=ttext, bitfield=bitfield,
                                anon=mname is None))
                if mt is not None and kind == 'agg':
                    self._flatten(mt, base + pos, p, out, depth + 1)
                elif mt is not None and kind == 'arr':
                    at = mt
                    while not isinstance(at, c_ast.ArrayDecl):
                        if isinstance(at, c_ast.TypeDecl):
                            at = at.type
                        elif isinstance(at, c_ast.IdentifierType):
                            at = self.typedefs[' '.join(at.names)]
                        else:
                            break
                    if not isinstance(at, c_ast.ArrayDecl):
                        continue
                    et = at.type
                    if kind_of(self, et) == 'agg':
                        n = eval_const(at.dim) if at.dim is not None else 0
                        es = self.type_layout(et)[0] * 8
                        for i in range(min(n, 16)):
                            out.append(dict(path='%s[%d]' % (p, i), cbit=base + pos + i * es, cbits=es, kind='agg',
                                            type=type_text(et), bitfield=False, anon=False))
                            self._flatten(et, base + pos + i * es, '%s[%d]' % (p, i), out, depth + 1)


def kind_of(model, t):
    if isinstance(t, c_ast.PtrDecl):
        return 'ptr'
    if isinstance(t, c_ast.ArrayDecl):
        return 'arr'
    if isinstance(t, c_ast.TypeDecl):
        return kind_of(model, t.type)
    if isinstance(t, (c_ast.Struct, c_ast.Union)):
        return 'agg'
    if isinstance(t, c_ast.Enum):
        return 'int'
    if isinstance(t, c_ast.IdentifierType):
        name = ' '.join(t.names)
        if name in ('float', 'double'):
            return 'flt'
        if name in PRIMS:
            return 'int'
        if name in model.typedefs:
            return kind_of(model, model.typedefs[name])
    return 'other'


def type_text(t):
    """C declaration text of a type, with the name slot shown as %s."""
    import copy
    t2 = copy.deepcopy(t)
    _set_declname(t2, '%s')
    try:
        decl = c_ast.Decl(name=None, quals=[], align=[], storage=[], funcspec=[], type=t2, init=None, bitsize=None)
    except TypeError:
        decl = c_ast.Decl(name=None, quals=[], storage=[], funcspec=[], type=t2, init=None, bitsize=None)
    return gen.visit(decl)


def _set_declname(t, name):
    while not isinstance(t, c_ast.TypeDecl):
        if hasattr(t, 'type'):
            t = t.type
        else:
            return
    t.declname = name


def eval_const(n):
    if isinstance(n, c_ast.Constant):
        return int(n.value.rstrip('uUlL'), 0)
    if isinstance(n, c_ast.BinaryOp):
        a, b = eval_const(n.left), eval_const(n.right)
        return {'+': a + b, '-': a - b, '*': a * b, '/': a // b, '<<': a << b, '>>': a >> b}[n.op]
    if isinstance(n, c_ast.UnaryOp) and n.op == '-':
        return -eval_const(n.expr)
    if isinstance(n, c_ast.UnaryOp) and n.op == 'sizeof':
        raise ValueError('sizeof in dim')
    raise ValueError(gen.visit(n))


def comment_offsets(mextk_include):
    """{(struct name, field name): offset} from '// 0x..' comments on the field lines."""
    res = {}
    for fn in os.listdir(mextk_include):
        if not fn.endswith('.h'):
            continue
        cur = None
        depth = 0
        for line in open(os.path.join(mextk_include, fn), encoding='utf-8', errors='replace'):
            m = re.match(r'\s*struct\s+(\w+)\s*$', line)
            if m and depth == 0:
                cur = m.group(1)
            depth += line.count('{') - line.count('}')
            if depth == 0:
                continue
            m = re.match(r'\s*[\w\s\*]+?\b(\w+)\s*(\[[^\]]*\])*\s*;\s*//\s*(0x[0-9A-Fa-f]+)', line)
            if m and cur and depth == 1:
                res[(cur, m.group(1))] = int(m.group(3), 16)
    return res
