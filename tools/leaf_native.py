"""Turn the recompiler's translation of chosen leaf routines into code the native game compiles.

The routines listed here exist in the SDK only as paired-single assembly. Their translations in
port/generated* reproduce the original instruction by instruction, so compiling that same text
natively gives results that match the console by construction. Three things change on the way:
  - effective addresses are widened to 64 bits, because an argument may be a host pointer
  - constants the code reads through r2/r13 (the small data areas) come from a pool read out of the
    player's own main.dol at build time, stored in host byte order; nothing from it is committed
  - each routine gets a wrapper with its real C signature, which sets up registers and a scratch stack

Output is one C++ file for sourceport/game (compiled against sourceport/game/ppcleaf/ppc.h).
"""
import argparse
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "port/recomp"))
from dol import Dol  # noqa: E402

R2, R13 = 0x804DF9E0, 0x804DB6A0   # set by __init_registers in the 1.02 DOL

# name: (C return type, C parameters, [(register, expression)], how the result comes back)
P = "(uint64_t)(uintptr_t)"
ROUTINES = {
    "PSMTXIdentity":     ("void", "float m[3][4]", [("r3", P + "m")], None),
    "PSMTXCopy":         ("void", "const float src[3][4], float dst[3][4]", [("r3", P + "src"), ("r4", P + "dst")], None),
    "PSMTXConcat":       ("void", "const float a[3][4], const float b[3][4], float ab[3][4]", [("r3", P + "a"), ("r4", P + "b"), ("r5", P + "ab")], None),
    "PSMTXTranspose":    ("void", "const float src[3][4], float xpose[3][4]", [("r3", P + "src"), ("r4", P + "xpose")], None),
    "PSMTXInverse":      ("uint32_t", "const float src[3][4], float inv[3][4]", [("r3", P + "src"), ("r4", P + "inv")], "r3"),
    "PSMTXRotTrig":      ("void", "float m[3][4], char axis, float sin_a, float cos_a", [("r3", P + "m"), ("r4", "(uint64_t)(uint8_t)axis"), ("f1", "sin_a"), ("f2", "cos_a")], None),
    "PSMTXRotAxisRad":   ("void", "float m[3][4], const float* axis, float rad", [("r3", P + "m"), ("r4", P + "axis"), ("f1", "rad")], None),
    "PSMTXTrans":        ("void", "float m[3][4], float x, float y, float z", [("r3", P + "m"), ("f1", "x"), ("f2", "y"), ("f3", "z")], None),
    "PSMTXScale":        ("void", "float m[3][4], float x, float y, float z", [("r3", P + "m"), ("f1", "x"), ("f2", "y"), ("f3", "z")], None),
    "PSMTXQuat":         ("void", "float m[3][4], const float* q", [("r3", P + "m"), ("r4", P + "q")], None),
    "PSMTXMultVec":      ("void", "const float m[3][4], const float* src, float* dst", [("r3", P + "m"), ("r4", P + "src"), ("r5", P + "dst")], None),
    "PSMTXMultVecSR":    ("void", "const float m[3][4], const float* src, float* dst", [("r3", P + "m"), ("r4", P + "src"), ("r5", P + "dst")], None),
    "PSVECAdd":          ("void", "const float* a, const float* b, float* ab", [("r3", P + "a"), ("r4", P + "b"), ("r5", P + "ab")], None),
    "PSVECSubtract":     ("void", "const float* a, const float* b, float* ab", [("r3", P + "a"), ("r4", P + "b"), ("r5", P + "ab")], None),
    "PSVECScale":        ("void", "const float* src, float* dst, float scale", [("r3", P + "src"), ("r4", P + "dst"), ("f1", "scale")], None),
    "PSVECNormalize":    ("void", "const float* src, float* unit", [("r3", P + "src"), ("r4", P + "unit")], None),
    "PSVECMag":          ("float", "const float* v", [("r3", P + "v")], "f1"),
    "PSVECDotProduct":   ("float", "const float* a, const float* b", [("r3", P + "a"), ("r4", P + "b")], "f1"),
    "PSVECCrossProduct": ("void", "const float* a, const float* b, float* axb", [("r3", P + "a"), ("r4", P + "b"), ("r5", P + "axb")], None),
}
# Translated routines may call these; the native game has them as ordinary C.
BRIDGES = {"sinf": "float sinf(float)", "cosf": "float cosf(float)"}


def load_bodies(generated):
    names = {}
    for m in re.finditer(r'\{0x([0-9A-Fa-f]{8})u, "([^"]+)"\}', (generated / "function_names.cpp").read_text()):
        names.setdefault(m[2], int(m[1], 16))
    text = "".join(p.read_text() for p in sorted(generated.glob("guest_[0-9]*.cpp")))
    bodies = {}
    for m in re.finditer(r"^void f_([0-9A-F]{8})\(ppc::Context& __restrict c, uint8_t\* __restrict m\) \{\n.*?^\}\n", text, re.S | re.M):
        bodies[int(m[1], 16)] = m[0]
    return names, bodies


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--generated", type=Path, default=ROOT / "port/generated_vanilla")
    ap.add_argument("--dol", type=Path, default=ROOT / "melee/orig/GALE01/sys/main.dol")
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    names, bodies = load_bodies(args.generated)
    ram = Dol(str(args.dol)).ram
    address_of = {a: n for n, a in names.items()}
    chosen = {n: names[n] for n in ROUTINES}
    out, addresses = [], set()
    for name, addr in chosen.items():
        body = bodies[addr]
        for callee in set(re.findall(r"\bf_([0-9A-F]{8})\(c, m\)", body)):
            target = address_of.get(int(callee, 16))
            if target not in ROUTINES and target not in BRIDGES:
                raise SystemExit(f"{name} calls {target or callee}, which is neither translated here nor bridged")
        for banned in ("ppc::call", "ppc::ld8", "ppc::ld16", "ppc::st8", "ppc::st16", "ppc::backedge"):
            if banned in body:
                raise SystemExit(f"{name} uses {banned}: not a leaf maths routine")
        for reg, off in re.findall(r"\(c\.r\[(2|13)\] - (\d+)u\)", body):
            addresses.add((R2 if reg == "2" else R13) - int(off))
        if re.search(r"ppc::ld64\(c, m, \(c\.r\[(2|13)\]", body):
            raise SystemExit(f"{name} loads a double from small data; the pool is stored as 32-bit words")
        # Constants reached through an absolute address: lis (the upper half) then addi or ori. The
        # upper half becomes a host pointer into the pool, and an ori on that has to become an add.
        lines, upper_regs = [], set()
        for line in body.split("\n"):
            m_lis = re.match(r"  c\.r\[(\d+)\] = 0u \+ (0x8[0-9A-F]{3}0000)u;(.*)", line)
            m_or = re.match(r"  c\.r\[(\d+)\] = c\.r\[(\d+)\] \| (0x[0-9A-F]+)u;(.*)", line)
            m_add = re.match(r"  c\.r\[(\d+)\] = c\.r\[(\d+)\] \+ (\d+)u;(.*)", line)
            m_any = re.match(r"  c\.r\[(\d+)\] = ", line)
            if m_lis:
                upper_regs.add(m_lis[1])
                addresses.add(int(m_lis[2], 16))
                line = f"  c.r[{m_lis[1]}] = pool_address({m_lis[2]}u);{m_lis[3]}"
            elif m_or and m_or[1] == m_or[2] and m_or[1] in upper_regs:
                line = f"  c.r[{m_or[1]}] = c.r[{m_or[2]}] + {m_or[3]}u;{m_or[4]}"
                upper_regs.discard(m_or[1])
            elif m_add and m_add[1] == m_add[2] and m_add[1] in upper_regs:
                upper_regs.discard(m_add[1])
            elif m_any:
                upper_regs.discard(m_any[1])
            lines.append(line)
        body = "\n".join(lines)
        body = body.replace("uint32_t ea", "uint64_t ea").replace("ppc::Context& __restrict c, uint8_t* __restrict m", "ppc::Context& c, uint8_t* m")
        out.append("// " + name + "\nstatic " + body)

    # One pool of the DOL's own bytes covering every constant the routines reach, as host-order words.
    lo, hi = min(addresses) & ~0xFF, (max(addresses) + 64 + 0xFF) & ~0xFF
    if hi - lo > 1 << 20:
        raise SystemExit(f"constants span {hi - lo} bytes ({lo:08X}..{hi:08X}); that is not a constant pool")
    words = struct.unpack(f">{(hi - lo) // 4}I", bytes(ram[lo - 0x80000000:hi - 0x80000000]))
    pools = ["static const uint32_t pool_words[] = {" + ", ".join(f"0x{w:08X}u" for w in words) + "};",
             f"constexpr uint32_t POOL_LO = 0x{lo:08X}u;   // ..0x{hi:08X}",
             "inline uint64_t pool_address(uint32_t guest) { return (uint64_t)(uintptr_t)pool_words + (uint64_t)(guest - POOL_LO); }",
             f"static const uint64_t pool_r2 = pool_address(0x{R2:08X}u), pool_r13 = pool_address(0x{R13:08X}u);"]

    text = ['// GENERATED by tools/leaf_native.py from the recompiler\'s translation. Do not edit, do not commit.',
            '#include "ppc.h"', "#include <limits>", "namespace ppc {", '#include "ppc_estimates.inc"', "}", "",
            'extern "C" {'] + [decl + ";" for decl in BRIDGES.values()] + ["}", "", "namespace {"] + pools + [
            "// The translated code keeps its own stack frames. They live in the image, below 4 GB, because a",
            "// frame's back chain is stored as 32 bits. The game runs its simulation on one thread.",
            "alignas(16) unsigned char leaf_stack[4096];", ""]
    text += [f"void f_{a:08X}(ppc::Context& c, uint8_t* m);" for a in chosen.values()]
    for bridge in BRIDGES:
        text.append(f"void f_{names[bridge]:08X}(ppc::Context& c, uint8_t*) {{ c.f[1].ps0 = c.f[1].ps1 = (double){bridge}((float)c.f[1].ps0); }}")
    text += [""] + [b.replace("static void", "void", 1) for b in out] + ["}  // namespace", "", 'extern "C" {']
    for name, (ret, params, regs, result) in ROUTINES.items():
        lines = [f"{ret} {name}({params}) {{", "  ppc::Context c{};",
                 "  c.r[1] = (uint64_t)(uintptr_t)(leaf_stack + sizeof leaf_stack - 64); c.r[2] = pool_r2; c.r[13] = pool_r13;"]
        for reg, expr in regs:
            lines.append(f"  c.r[{reg[1:]}] = {expr};" if reg[0] == "r" else f"  c.f[{reg[1:]}].ps0 = c.f[{reg[1:]}].ps1 = (double)({expr});")
        lines.append(f"  f_{chosen[name]:08X}(c, nullptr);")
        if result:
            lines.append("  return (uint32_t)c.r[3];" if result == "r3" else "  return (float)c.f[1].ps0;")
        text += lines + ["}"]
    text.append("}")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(text) + "\n", encoding="utf-8")
    print(f"{len(ROUTINES)} routines, constant pool {lo:08X}..{hi:08X} ({hi - lo} bytes) -> {args.out}")


if __name__ == "__main__":
    main()
