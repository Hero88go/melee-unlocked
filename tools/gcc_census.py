"""Syntax-check every game source file with GCC and summarise what stops it compiling.

Nothing is linked or run. The output is a census: how many files pass, and the most common
diagnostics with one example location each, so the work can be sized and ordered.
"""
import argparse
import collections
import concurrent.futures
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT / "sourceport/extern/melee"
DIAG = re.compile(r"^(?P<file>[^:\n]+(?::[^:\n]+)?):(?P<line>\d+):(?P<col>\d+): (?P<kind>fatal error|error): (?P<msg>.*)$")
QUOTED = re.compile(r"'[^']*'")


def check(gcc, flags, path):
    run = subprocess.run([gcc, *flags, "-fsyntax-only", str(path)], cwd=DECOMP, capture_output=True, text=True, errors="replace")
    found = []
    for line in run.stderr.splitlines():
        m = DIAG.match(line)
        if m:
            found.append((m["msg"], f'{m["file"]}:{m["line"]}'))
    return path, run.returncode, found


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--gcc", default=os.environ.get("MELEE_GCC", "gcc"))
    ap.add_argument("--dirs", nargs="+", default=["src/melee", "src/sysdolphin"])
    ap.add_argument("--include", nargs="*", default=["src", "src/MSL", "extern/dolphin/include"], help="include path, in order, relative to the decomp")
    ap.add_argument("--define", nargs="*", default=["VERSION_GALE01", "BUILD_VERSION=0"])
    ap.add_argument("--flag", nargs="*", default=[], help="extra compiler flags, written without the leading dash pair issue: pass as =-flag")
    ap.add_argument("--werror", nargs="*", default=[], help="warnings to count as errors, e.g. pointer-to-int-cast int-to-pointer-cast")
    ap.add_argument("--out", type=Path, default=ROOT / "reports/gcc-census.json")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()

    flags = ["-std=gnu11", "-fmax-errors=0", *([f"-Werror={w}" for w in args.werror] or ["-w"]), "-mno-ms-bitfields", "-fno-builtin", "-fno-strict-aliasing"]
    flags += [f"-I{i}" for i in args.include] + [f"-D{d}" for d in args.define] + [f.lstrip("=") for f in args.flag]
    files = sorted(p for d in args.dirs for p in (DECOMP / d).rglob("*.c"))
    by_message, per_file, clean = collections.Counter(), {}, 0
    example, files_with = {}, collections.defaultdict(set)
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for path, code, found in pool.map(lambda p: check(args.gcc, flags, p), files):
            rel = path.relative_to(DECOMP).as_posix()
            per_file[rel] = len(found)
            clean += code == 0
            for msg, where in found:
                key = QUOTED.sub("'_'", msg)
                by_message[key] += 1
                example.setdefault(key, (msg, where))
                files_with[key].add(rel)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({"flags": flags, "files": len(files), "clean": clean, "per_file": per_file,
                                    "messages": [{"message": k, "count": n, "files": len(files_with[k]), "example": example[k][0], "at": example[k][1]}
                                                 for k, n in by_message.most_common()]}, indent=1))
    print(f"{clean} of {len(files)} files pass; {sum(by_message.values())} errors in {len(by_message)} kinds")
    for key, n in by_message.most_common(25):
        print(f"{n:7d} in {len(files_with[key]):4d} files  {example[key][0][:110]}   [{example[key][1]}]")


if __name__ == "__main__":
    main()
