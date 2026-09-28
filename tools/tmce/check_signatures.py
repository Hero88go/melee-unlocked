"""Check every game function Training Mode CE calls: its MexTK prototype against the decomp's.

On the console integer and float arguments travel in separate register files, so a MexTK prototype
that lists them in a different order still works there. On x64 every argument has one positional
slot, so the classes must match position by position. Reports each function whose argument or
return classes differ, or whose MexTK prototype has fewer arguments than the function reads.

usage: python tools/tmce/check_signatures.py <decomp_info.json> <tmce module objects...>
"""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mextk_layout as M  # noqa: E402
from pycparser import c_ast  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(HERE))
MEXTK = os.path.join(ROOT, 'run-source', 'tmce-src', 'MexTK')
NM = 'C:/Users/Chandler/toolchains/winlibs-gcc-15.3/mingw64/bin/nm.exe'


def cls(model, t):
    while isinstance(t, c_ast.TypeDecl):
        t = t.type
    if isinstance(t, (c_ast.PtrDecl, c_ast.ArrayDecl)):
        return 'i'
    if isinstance(t, c_ast.IdentifierType):
        n = ' '.join(t.names)
        if n == 'void':
            return 'v'
        if n == 'float':
            return 'f'
        if n in ('double', 'long double'):
            return 'd'
        if n in M.PRIMS:
            return 'i'
        td = model.typedefs.get(n)
        return cls(model, td) if td is not None else '?'
    if isinstance(t, c_ast.Enum):
        return 'i'
    if isinstance(t, (c_ast.Struct, c_ast.Union)):
        return 's'
    return '?'


def mextk_protos(model):
    out = {}
    for ext in model.ast.ext:
        if isinstance(ext, c_ast.Decl) and isinstance(ext.type, c_ast.FuncDecl):
            fd = ext.type
            params, variadic = [], False
            for p in (fd.args.params if fd.args else []):
                if isinstance(p, c_ast.EllipsisParam):
                    variadic = True
                    continue
                c = cls(model, p.type)
                if c == 'v':
                    continue
                params.append(c)
            out[ext.name] = dict(ret=cls(model, fd.type), params=params, variadic=variadic)
    return out


def main():
    info = json.load(open(sys.argv[1]))['functions']
    redef = {}
    for line in open(os.path.join(ROOT, 'sourceport', 'game', 'tmce', 'MexTK', 'mextk_redefine.txt')):
        p = line.split()
        if len(p) == 2:
            redef.setdefault(p[1], []).append(p[0])
    used = set()
    for obj in sys.argv[2:]:
        for line in subprocess.run([NM, '-u', obj], capture_output=True, text=True).stdout.splitlines():
            used.add(line.split()[-1])
    model = M.Model(M.preprocess(MEXTK, os.path.join(HERE, 'fakeinc')))
    protos = mextk_protos(model)
    bad = 0
    checked = 0
    for dname in sorted(used):
        fi = info.get(dname)
        if not fi or 'params' not in fi:
            continue
        for mname in redef.get(dname, [dname]):
            mp = protos.get(mname)
            if mp is None:
                continue
            checked += 1
            dp = [('s' if c.startswith('s') else c) for c in fi['params']]
            mpar = mp['params']
            problems = []
            n = min(len(dp), len(mpar))
            for i in range(n):
                if dp[i] != mpar[i]:
                    problems.append('arg %d MexTK %s decomp %s' % (i + 1, mpar[i], dp[i]))
            if len(mpar) < len(dp):
                problems.append('MexTK passes %d of %d arguments' % (len(mpar), len(dp)))
            dr = 's' if fi['ret'].startswith('s') else fi['ret']
            if mp['ret'] not in ('v', dr) and not (mp['ret'] == 'i' and dr == 'i'):
                problems.append('returns MexTK %s decomp %s' % (mp['ret'], dr))
            if problems:
                bad += 1
                print('%-36s -> %-34s %s   [decomp %s]' % (mname, dname, '; '.join(problems), fi['text']))
    print('checked %d, differing %d' % (checked, bad))


if __name__ == '__main__':
    main()
