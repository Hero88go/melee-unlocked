"""Where the console compiler fused a multiply into an add, and where GCC does.

A fused multiply-add rounds once where separate instructions round twice, so the two compilers
have to agree site by site or the native game drifts from the console. Neither compiler documents
its choices, but both leave them in their output: the recompiler's translation shows every fused
operation the console binary performs, per function, and GCC's assembly shows its own. This
pairs the two by function name and reports where they differ.

  python tools/fma_census.py [--generated port/generated_vanilla] [--out reports/fma-census.json]

Counts are per function after inlining on each side, so a small difference can be an inlining
difference rather than a fusion one; a function where one side fused nothing and the other did
is the case to act on.
"""
import argparse
import collections
import concurrent.futures
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT / "sourceport/extern/melee"
GAME = ROOT / "sourceport/game"

SINGLE = re.compile(r"ppc::fs\(ppc::f(?:n?m(?:add|sub))\(")
DOUBLE = re.compile(r"= ppc::f(?:n?m(?:add|sub))\(")
PAIRED = re.compile(r"double x = ppc::f(?:n?m(?:add|sub))\(")
FUNC = re.compile(r"^void f_([0-9A-F]{8})\(ppc::Context& __restrict c, uint8_t\* __restrict m\) \{\n(.*?)^\}\n", re.S | re.M)


def units_by_address(splits):
    """(start, end, unit) for every .text range in the split file."""
    ranges, unit = [], None
    for line in splits.read_text(encoding="utf-8").splitlines():
        if line and not line[0].isspace() and line.endswith(":"):
            unit = line[:-1]
        m = re.match(r"\s+\.text\s+start:0x([0-9A-F]+) end:0x([0-9A-F]+)", line)
        if m and unit:
            ranges.append((int(m[1], 16), int(m[2], 16), unit))
    ranges.sort()
    return ranges


def console_counts(generated, ranges):
    names = {}
    for m in re.finditer(r'\{0x([0-9A-Fa-f]{8})u, "([^"]+)"\}', (generated / "function_names.cpp").read_text()):
        names.setdefault(int(m[1], 16), m[2])
    counts = {}
    for path in sorted(generated.glob("guest_[0-9]*.cpp")):
        for m in FUNC.finditer(path.read_text()):
            addr, body = int(m[1], 16), m[2]
            unit = next((u for s, e, u in ranges if s <= addr < e), None)
            if not unit or not (unit.startswith("melee/") or unit.startswith("sysdolphin/")):
                continue
            single, paired = len(SINGLE.findall(body)), len(PAIRED.findall(body))
            double = len(DOUBLE.findall(body)) - paired   # the paired form matches the double pattern too
            counts[names.get(addr, f"fn_{addr:08X}")] = {"unit": unit, "single": single, "double": double, "paired": paired}
    return counts


GCC_FUSED = re.compile(r"^\s+vf(?:n?m(?:add|sub))\d{3}(ss|sd|ps|pd)\b", re.M)
LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_.$]*):", re.M)


def gcc_counts_for(gcc, flags, path):
    run = subprocess.run([gcc, *flags, "-S", "-o", "-", str(path)], cwd=DECOMP, capture_output=True, text=True, errors="replace")
    if run.returncode:
        return path, None, run.stderr.strip().splitlines()[-1:] or ["failed"]
    counts, current = {}, None
    for line in run.stdout.splitlines():
        m = LABEL.match(line)
        if m and not m[1].startswith(".L"):
            current = m[1]
            counts.setdefault(current, {"single": 0, "double": 0})
            continue
        f = GCC_FUSED.match(line)
        if f and current:
            counts[current]["single" if f[1] in ("ss", "ps") else "double"] += 1
    return path, counts, None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--gcc", default=os.environ.get("MELEE_GCC", "gcc"))
    ap.add_argument("--generated", type=Path, default=ROOT / "port/generated_vanilla")
    ap.add_argument("--out", type=Path, default=ROOT / "reports/fma-census.json")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--contract", default="on", help="GCC -ffp-contract mode to measure: off, on or fast")
    args = ap.parse_args()

    ranges = units_by_address(DECOMP / "config/GALE01/splits.txt")
    console = console_counts(args.generated, ranges)

    flags = ["-std=gnu11", "-O2", "-w", "-mno-ms-bitfields", "-fno-strict-aliasing", "-fwrapv", "-fno-builtin", f"-ffp-contract={args.contract}",
             "-mavx2", "-mfma", "-fexec-charset=CP932", "-include", str(GAME / "include/mu_native.h"),
             "-DMU_NATIVE", "-DVERSION_GALE01", "-DBUILD_VERSION=0",
             f"-I{GAME}/include", "-Isrc", "-Isrc/MSL", "-Iextern/dolphin/include", f"-I{GAME}/include_stub", f"-I{ROOT}/port/runtime/abi"]
    files = sorted(p for d in ("src/melee", "src/sysdolphin") for p in (DECOMP / d).rglob("*.c"))
    gcc, failed = {}, []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for path, counts, error in pool.map(lambda p: gcc_counts_for(args.gcc, flags, p), files):
            if counts is None:
                failed.append((path.relative_to(DECOMP).as_posix(), error))
                continue
            gcc.update(counts)

    rows, summary = [], collections.Counter()
    for name, c in console.items():
        g = gcc.get(name)
        if g is None:
            summary["not compiled natively"] += 1
            continue
        dol = c["single"] + c["double"]
        nat = g["single"] + g["double"]
        if dol == 0 and nat == 0: kind = "none on either side"
        elif dol == nat: kind = "same count"
        elif dol == 0: kind = "GCC fuses, console did not"
        elif nat == 0: kind = "console fused, GCC does not"
        else: kind = "both fuse, counts differ"
        summary[kind] += 1
        if kind not in ("none on either side", "same count"):
            rows.append({"function": name, "unit": c["unit"], "console_single": c["single"], "console_double": c["double"],
                         "console_paired": c["paired"], "gcc_single": g["single"], "gcc_double": g["double"], "kind": kind})
    rows.sort(key=lambda r: (r["kind"], r["unit"], r["function"]))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    # Per file, where inlining differences cancel out: the console inlined static helpers that GCC
    # keeps as calls, and the other way round, so a function's count moves without a site changing.
    units = collections.defaultdict(lambda: {"console": 0, "gcc": 0, "functions": 0})
    for name, c in console.items():
        g = gcc.get(name)
        if g is None: continue
        u = units[c["unit"]]
        u["console"] += c["single"] + c["double"]; u["gcc"] += g["single"] + g["double"]; u["functions"] += 1
    unit_rows = sorted(({"unit": k, **v} for k, v in units.items() if v["console"] != v["gcc"]), key=lambda r: -abs(r["console"] - r["gcc"]))
    args.out.write_text(json.dumps({"summary": summary, "failed": failed, "differences": rows, "units": unit_rows}, indent=1))
    print(f"files whose fused-op totals differ: {len(unit_rows)} of {len(units)}; worst: " +
          ", ".join(f"{r['unit'].split('/')[-1]} {r['console']} vs {r['gcc']}" for r in unit_rows[:8]))
    print(f"{len(console)} game functions in the console binary, {len(gcc)} native symbols, {len(failed)} files failed to compile")
    for kind, n in summary.most_common():
        print(f"{n:6d}  {kind}")
    print(f"console fused ops in game code: single {sum(c['single'] for c in console.values())}, double {sum(c['double'] for c in console.values())}, paired {sum(c['paired'] for c in console.values())}")
    print(f"details: {args.out}")


if __name__ == "__main__":
    main()
