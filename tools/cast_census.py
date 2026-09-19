"""List (and optionally fix) every integer-to-pointer cast in the native game build whose integer is
narrower than a pointer.

    python tools/cast_census.py [--out reports/cast-census.txt] [--fix]

The game's addresses (MEM1 at 0x80000000, the game image at 0x82800000) all have bit 31 set, so a
pointer squeezed through a *signed* 32-bit integer comes back sign-extended to 0xFFFFFFFF8xxxxxxx
and faults. Casts from u32 zero-extend and are harmless, but the compiler's warning does not say
which kind a site is, and most of the game's pointer-holding words are declared s32 or int. --fix
wraps the operand of every flagged cast in MU_Z(), which zero-extends it natively and is the
identity on the console build (Runtime/platform.h), so every site behaves like a u32 slot.

Runs the real build's compile commands with -fsyntax-only (the build itself passes -w), in parallel.
"""
import argparse
import concurrent.futures
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build-sourceport-gcc"
NINJA = r"C:\Users\Chandler\toolchains\winlibs-gcc-15.3\mingw64\bin\ninja.exe"
WARN = re.compile(r"^(?P<file>[A-Za-z]:[^:]+|[^:]+):(?P<line>\d+):(?P<col>\d+): warning: cast to pointer from integer of different size")
SIGNED = re.compile(r"\(\s*(int|s32|long|signed|s16|intptr_t)\s*\)")
TYPE_CAST = re.compile(r"^\(\s*(?:const\s+|volatile\s+)*(?:struct\s+|union\s+|enum\s+|unsigned\s+|signed\s+)?[A-Za-z_]\w*(?:\s+(?:int|long|char|short))?(?:\s*\*)*\s*\)$")


def commands():
    out = subprocess.run([NINJA, "-C", str(BUILD), "-t", "commands", "melee_game"], capture_output=True, text=True).stdout
    for line in out.splitlines():
        if " -c " in line and line.rstrip().endswith((".c", ".c\"")):
            yield line


def run(cmd):
    cmd = re.sub(r" -o \S+", " ", cmd)
    cmd = re.sub(r" -MD -MT \S+ -MF \S+", " ", cmd)
    cmd = re.sub(r" -w(?= )", " ", cmd)   # the build silences warnings; this run needs this one
    cmd = cmd.replace(" -c ", " -fsyntax-only -Wint-to-pointer-cast -c ")
    r = subprocess.run(cmd, cwd=BUILD, shell=True, capture_output=True, text=True, errors="replace")
    hits = []
    for line in r.stderr.splitlines():
        m = WARN.match(line)
        if m:
            hits.append((m["file"], int(m["line"]), int(m["col"])))
    return hits


def group_end(text, i):
    """text[i] is an opening bracket; index just past its match."""
    pairs = {"(": ")", "[": "]"}
    close = pairs[text[i]]
    depth = 0
    for j in range(i, len(text)):
        if text[j] == text[i]:
            depth += 1
        elif text[j] == close:
            depth -= 1
            if depth == 0:
                return j + 1
    raise ValueError("unbalanced")


def skip_ws(text, i):
    while i < len(text) and text[i] in " \t\r\n":
        i += 1
    return i


def operand_end(text, i):
    """End of the cast-expression that starts at text[i] (unary operators, casts, postfix chains)."""
    i = skip_ws(text, i)
    if text[i] in "!~-+*&" and not text.startswith("->", i):
        return operand_end(text, i + 1)
    if text[i] == "(":
        end = group_end(text, i)
        if TYPE_CAST.match(re.sub(r"\s+", " ", text[i:end])):
            return operand_end(text, end)       # a cast: its operand follows
        i = end
    else:
        m = re.match(r"[A-Za-z_]\w*|0[xX][0-9A-Fa-f]+[uUlL]*|\d+[uUlL]*", text[i:])
        if not m:
            raise ValueError("no operand at " + text[i:i + 20])
        i += m.end()
    while True:                                # postfix: calls, subscripts, member access
        j = skip_ws(text, i)
        if j < len(text) and text[j] in "([":
            i = group_end(text, j)
        elif text.startswith("->", j) or (j < len(text) and text[j] == "."):
            j += 2 if text.startswith("->", j) else 1
            m = re.match(r"\s*[A-Za-z_]\w*", text[j:])
            i = j + m.end()
        else:
            return i


def fix_file(path, sites):
    """sites: (line, col) of each flagged cast. Wrap each cast's operand in MU_Z()."""
    text = open(path, encoding="utf-8", newline="").read()
    starts = [0]
    for m in re.finditer("\n", text):
        starts.append(m.end())
    edits = []
    for line, col in sites:
        pos = starts[line - 1] + col - 1
        if text[pos] != "(":
            print(f"  skip {path}:{line}:{col}: not at a cast: {text[pos:pos + 30]!r}")
            continue
        cast_end = group_end(text, pos)
        try:
            op_start = skip_ws(text, cast_end)
            op_end = operand_end(text, op_start)
        except ValueError as e:
            print(f"  skip {path}:{line}: {e}")
            continue
        if text[op_start:op_start + 5] == "MU_Z(":
            continue
        edits.append((op_start, op_end))
    for s, e in sorted(set(edits), reverse=True):
        text = text[:s] + "MU_Z(" + text[s:e] + ")" + text[e:]
    open(path, "w", encoding="utf-8", newline="").write(text)
    return len(edits)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "reports/cast-census.txt"))
    ap.add_argument("--fix", action="store_true")
    a = ap.parse_args()
    cmds = list(commands())
    sites = set()
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as ex:
        for hits in ex.map(run, cmds):
            sites.update(hits)
    rows = []
    for f, n, c in sorted(sites):
        try:
            text = Path(f).read_text(encoding="utf-8", errors="replace").splitlines()[n - 1].strip()
        except (OSError, IndexError):
            text = ""
        rel = f.replace("\\", "/").split("sourceport/extern/melee/")[-1]
        rows.append((0 if SIGNED.search(text) else 1, rel, n, text))
    rows.sort()
    with open(a.out, "w", encoding="utf-8") as fh:
        for signed, rel, n, text in rows:
            fh.write(f"{'SIGNED ' if signed == 0 else '       '}{rel}:{n}: {text}\n")
    print(f"{len(cmds)} files, {len(rows)} sites, {sum(1 for r in rows if r[0] == 0)} from a signed type -> {a.out}")
    if a.fix:
        by_file = {}
        for f, n, c in sites:
            by_file.setdefault(f, []).append((n, c))
        total = sum(fix_file(f, s) for f, s in sorted(by_file.items()))
        print(f"wrapped {total} casts in {len(by_file)} files")


if __name__ == "__main__":
    main()
