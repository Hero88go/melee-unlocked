"""Link one Training Mode CE module into a single object the game DLL can take.

On the console each TM-CE module is its own .dat file: its globals are private to it, it exports a
fixed table (evFunction / tmFunction / cssFunction), and its calls into the game are resolved against
MexTK's melee.link addresses. Natively the same holds by construction:
  1. the module's objects are linked into one relocatable object (ld -r);
  2. every defined global except the exports becomes local, so two modules can both define
     Event_Init or a helper of the same name;
  3. the exports get the module's prefix (Event_Init -> tmce_lab_Event_Init), and each MexTK function
     name is renamed to the decomp function at the same console address (mextk_redefine.txt).

usage: link_module.py --ld LD --objcopy OBJCOPY --name NAME --exports FILE|- [--keep PATTERN ...]
                      --redefine FILE --out OUT.o OBJ...
"""
import argparse
import os
import subprocess
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ld', required=True)
    ap.add_argument('--objcopy', required=True)
    ap.add_argument('--name', required=True)
    ap.add_argument('--exports', default='-')
    ap.add_argument('--keep', action='append', default=[])
    ap.add_argument('--redefine', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('objs', nargs='+')
    a = ap.parse_args()

    work = a.out + '.work'
    os.makedirs(work, exist_ok=True)
    partial = os.path.join(work, 'partial.o')
    kept = os.path.join(work, 'kept.o')
    subprocess.run([a.ld, '-r', '-o', partial] + a.objs, check=True)

    exports = []
    if a.exports != '-':
        exports = [l.strip() for l in open(a.exports) if l.strip()]
    keep_file = os.path.join(work, 'keep.txt')
    with open(keep_file, 'w', newline='\n') as f:
        for e in exports:
            f.write(e + '\n')
        for k in a.keep:
            f.write(k + '\n')
    cmd = [a.objcopy]
    if a.keep:
        cmd.append('--wildcard')
    cmd += ['--keep-global-symbols=' + keep_file, partial, kept]
    subprocess.run(cmd, check=True)

    # Only the names this module uses (MexTK has several names for some functions), each target once
    # per pass: objcopy refuses two renames to one name in a single map.
    nm = os.path.join(os.path.dirname(a.ld), 'nm.exe')
    used = set()
    for line in subprocess.run([nm, kept], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split()
        if parts:
            used.add(parts[-1])
    passes = [[]]
    for e in exports:
        passes[0].append((e, 'tmce_%s_%s' % (a.name, e)))
    for line in open(a.redefine):
        p = line.split()
        if len(p) != 2 or p[0] not in used:
            continue
        for ps in passes:
            if all(t != p[1] for _, t in ps):
                ps.append((p[0], p[1]))
                break
        else:
            passes.append([(p[0], p[1])])
    src = kept
    for i, ps in enumerate(passes):
        ren = os.path.join(work, 'redefine%d.txt' % i)
        with open(ren, 'w', newline='\n') as f:
            for old, new in ps:
                f.write('%s %s\n' % (old, new))
        dst = a.out if i == len(passes) - 1 else os.path.join(work, 'pass%d.o' % i)
        subprocess.run([a.objcopy, '--redefine-syms=' + ren, src, dst], check=True)
        src = dst
    return 0


if __name__ == '__main__':
    sys.exit(main())
