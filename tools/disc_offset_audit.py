import os
#!/usr/bin/env python3
"""Check every DISC_STRUCT field's native (x64 GCC) offset against the decomp's /* +XX */ annotation.

A disc struct is read straight from the console's data, so its host layout must match the console
layout byte for byte. The usual way it drifts is a pointer-typed field (UNK_T, T*): 8 bytes and
8-aligned on x64 against 4 on the console, which shifts every later field. The compiler says nothing,
the values are simply read from the wrong place. This compiles one probe per header with the game's
own flags and reports each annotated field whose host offset differs, and the first one per struct.

    python tools/disc_offset_audit.py [--json out.json]
"""
import argparse, json, os, re, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT / 'sourceport/extern/melee'
BUILD = ROOT / 'build-sourceport-gcc'
NINJA = os.environ.get('MELEE_NINJA', 'ninja')   # set MELEE_NINJA to a full path when ninja is not on PATH
TEMPLATE_OBJ = ('CMakeFiles/melee_game.dir/C_/Users/Chandler/NEW_project/melee-sourceport/'
                'sourceport/extern/melee/src/melee/ft/kinds/ftLink/ftlinkattackair.c.obj')

STRUCT = re.compile(r'\bstruct\s+(\w+)\s*\{', re.S)
FIELD = re.compile(r'/\*\s*(?:\w+)?\s*\+\s*([0-9A-Fa-f]+)\s*\*/\s*([^;{}]*?)\b(\w+)\s*(\[[^\]]*\])*\s*(?::\s*\d+)?\s*;')


def disc_structs(text):
    """Yield (name, [(offset, field)]) for top-level fields of structs closed with DISC_STRUCT."""
    for m in STRUCT.finditer(text):
        start = m.end()
        depth, i = 1, start
        while i < len(text) and depth:
            if text[i] == '{': depth += 1
            elif text[i] == '}': depth -= 1
            i += 1
        tail = text[i:i + 40]
        if not re.match(r'\s*DISC_STRUCT\b', tail):
            continue
        body = text[start:i - 1]
        # keep only depth-0 text of the body so nested struct members are not taken as ours
        flat, d = [], 0
        for ch in body:
            if ch == '{': d += 1; flat.append(' '); continue
            if ch == '}': d -= 1; flat.append(' '); continue
            flat.append(ch if d == 0 else ' ')
        flat = ''.join(flat)
        fields = []
        for f in FIELD.finditer(flat):
            decl = f.group(2)
            if ':' in f.group(0).split('*/', 1)[1]:   # bitfields have no address
                continue
            fields.append((int(f.group(1), 16), f.group(3), decl.strip()))
        if fields:
            yield m.group(1), fields


def compile_command():
    cmd = subprocess.run([NINJA, '-C', str(BUILD), '-t', 'commands', TEMPLATE_OBJ],
                         capture_output=True, text=True).stdout.strip().splitlines()[-1]
    cmd = cmd.replace('\\', '/')
    cmd = re.sub(r' -MD -MT \S+ -MF \S+', '', cmd)
    cmd = re.sub(r' -o \S+', ' -o NUL', cmd)
    src = re.search(r' -c ("[^"]+"|\S+)', cmd)   # quoted first: the path has a space
    return cmd, src.group(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--json', type=Path)
    a = ap.parse_args()
    cmd, src_token = compile_command()
    headers = sorted(p for p in (DECOMP / 'src').rglob('*.h') if 'DISC_STRUCT' in p.read_text(errors='replace'))
    report, probe_dir = [], Path(tempfile.mkdtemp(prefix='disc_probe_'))
    for header in headers:
        text = header.read_text(errors='replace')
        structs = list(disc_structs(text))
        if not structs:
            continue
        rel = header.relative_to(DECOMP / 'src').as_posix()
        lines = ['#include <stddef.h>', f'#include <{rel}>']
        probes = []
        for name, fields in structs:
            for off, field, decl in fields:
                probe = f'mu_probe_{len(probes)}'
                probes.append((name, field, off, decl))
                # A conflicting pair is a hard error even under -w; the note carries the real offset.
                lines.append(f'extern char {probe}[__builtin_offsetof(struct {name}, {field}) + 1];')
                lines.append(f'extern char {probe}[{off + 1}];')
        probe_file = probe_dir / (header.stem + '_probe.c')
        probe_file.write_text('\n'.join(lines) + '\n')
        run = subprocess.run(cmd.replace(src_token, f'"{probe_file.as_posix()}"'), shell=True, cwd=BUILD,
                             capture_output=True, text=True)
        out = run.stdout + run.stderr
        got = {}
        for m in re.finditer(r"conflicting types for .(mu_probe_\d+).; have .char\[(\d+)\]..*?\n(?:.*\n)*?.*previous declaration of .\1. with type .char\[(\d+)\]", out):
            got[m.group(1)] = int(m.group(3)) - 1   # the first declaration is the host offset
        failed_compile = 'error' in out and not got and 'conflicting' not in out
        # No conflict means the host offset equals the annotation. Some sub-structs are annotated
        # with their parent's absolute offsets, so compare relative to the struct's first field.
        base = {}
        for i, (name, field, off, decl) in enumerate(probes):
            host = got.get(f'mu_probe_{i}', off)
            if name not in base:
                base[name] = off - host
            if off - base[name] != host:
                report.append(dict(header=rel, struct=name, field=field, disc=off - base[name], host=host,
                                   annotation=off, decl=decl))
        if failed_compile:
            report.append(dict(header=rel, struct=None, field=None, error=out.strip().splitlines()[:3]))
    by_struct = {}
    for r in report:
        if r.get('struct'):
            by_struct.setdefault((r['header'], r['struct']), []).append(r)
    print(f'{len(headers)} headers, {sum(1 for r in report if r.get("struct"))} misplaced fields in {len(by_struct)} structs')
    for (h, s), rows in sorted(by_struct.items()):
        first = min(rows, key=lambda r: r['disc'])
        print(f'  {h}: struct {s}: {len(rows)} fields off; first +{first["disc"]:X} {first["field"]} at host +{first["host"]:X} ({first["decl"]})')
    for r in report:
        if not r.get('struct'):
            print(f'  {r["header"]}: probe did not compile: {r["error"]}')
    if a.json:
        a.json.write_text(json.dumps(report, indent=1))
    return 1 if report else 0


if __name__ == '__main__':
    sys.exit(main())
