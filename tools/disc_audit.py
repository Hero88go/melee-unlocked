"""Audit DISC_STRUCT types for fields that compile but have the wrong layout.

    python tools/disc_audit.py [ROOT]      (default: sourceport/extern/melee)

A DISC_STRUCT maps bytes from the disc. Three kinds of field compile silently and are wrong there:
  - a raw pointer (8 bytes natively; the file has a 4-byte slot)  -> DISC_PTR(T)
  - a pointer-sized typedef (UNK_T, uintptr_t, HSD_IDKey, MtxPtr) -> DISC_PTR / u32
  - a nested struct, union or maths type that is not itself big-endian (Vec3, a host struct) -> its
    _BE twin, or mark that type DISC_STRUCT
Run it after every decomp merge: upstream retypes fields and a merge can put a raw pointer back.
"""
import re
import sys
from pathlib import Path

ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / "sourceport/extern/melee"
SCALARS = {"u8", "s8", "u16", "s16", "u32", "s32", "u64", "s64", "f32", "f64", "int", "char", "float", "double",
           "bool", "BOOL", "unsigned", "signed", "short", "long", "GXColor", "enum_t", "be_f32", "be_s32", "be_u32",
           "be_u16", "be_s16", "Vec3_BE", "Vec2_BE", "Vec4_BE", "Quaternion_BE", "Mtx_BE", "Mtx44_BE", "size_t"}
POINTER_SIZED = {"UNK_T", "uintptr_t", "intptr_t", "HSD_IDKey", "MtxPtr", "ssize_t"}
HOST_MATH = {"Vec3", "Vec2", "Vec4", "Quaternion", "Mtx", "Mtx44", "Vec"}


def bodies(text):
    """(name, body, line) for each struct/union marked DISC_STRUCT."""
    for m in re.finditer(r"(?:typedef\s+)?(?:struct|union)\s+(\w+)?\s*(?:__attribute__\(\([^)]*\)\)\s*)?\{", text):
        i, depth = m.end(), 1
        while depth and i < len(text):
            depth += {"{": 1, "}": -1}.get(text[i], 0)
            i += 1
        tail = text[i:i + 80]
        t = re.match(r"\s*DISC_STRUCT\s*\**\s*(\w+)?", tail)
        if t:
            yield m.group(1) or t.group(1) or "?", text[m.end():i - 1], text.count("\n", 0, m.start()) + 1


def disc_type_names(files):
    names = set()
    for text in files.values():
        for name, _, _ in bodies(text):
            names.add(name)
        # the typedef name after DISC_STRUCT: "typedef struct _x {...} DISC_STRUCT x;"
        for m in re.finditer(r"\}\s*DISC_STRUCT\s+(\w+)\s*;", text):
            names.add(m.group(1))
    return names


def scalar_typedefs(files):
    """Enum typedefs and typedefs of plain scalars are scalars in a disc struct."""
    names = set()
    for text in files.values():
        for m in re.finditer(r"typedef\s+enum\s*\w*\s*\{[^}]*\}\s*(\w+)\s*;", text):
            names.add(m.group(1))
        for m in re.finditer(r"typedef\s+enum\s+\w+\s+(\w+)\s*;", text):
            names.add(m.group(1))
        for m in re.finditer(r"typedef\s+(?:unsigned\s+|signed\s+)?(u8|s8|u16|s16|u32|s32|int|char|short|float|f32|long|enum_t)\s+(\w+)\s*;", text):
            names.add(m.group(2))
    return names


def main():
    files = {}
    for p in list(ROOT.glob("src/**/*.h")) + list(ROOT.glob("libs/**/*.h")) + list(ROOT.glob("src/**/*.c")):
        try:
            files[p] = p.read_text(encoding="utf-8", errors="replace")
        except OSError:
            pass
    disc = disc_type_names(files)
    SCALARS.update(scalar_typedefs(files))
    # typedef aliases of disc types count as disc too
    for text in files.values():
        for m in re.finditer(r"typedef\s+(?:struct|union)\s+(\w+)\s+(\w+)\s*;", text):
            if m.group(1) in disc:
                disc.add(m.group(2))
        for m in re.finditer(r"typedef\s+DISC_PTR\([^;]*\)\s+(\w+)\s*(\[[^\]]*\])?\s*;", text):
            disc.add(m.group(1))
    problems = 0
    for p, text in files.items():
        for name, body, line in bodies(text):
            depth = 0
            for raw in body.split("\n"):
                code = re.sub(r"/\*.*?\*/|//.*", "", raw).strip()
                depth += code.count("{") - code.count("}")
                if not code.endswith(";") or "(" in code and "DISC_PTR" not in code and "(*" not in code:
                    if "(*" not in code:
                        continue
                if "DISC_PTR" in code or code.startswith(("#", "}")):
                    continue
                why = None
                if re.search(r"\(\s*\*", code):
                    why = "function or array pointer"
                elif "*" in code:
                    why = "raw pointer"
                else:
                    m = re.match(r"(?:const\s+|volatile\s+)?(?:struct\s+|union\s+|enum\s+)?(\w+)(?:\s+\w+)?\s+\w+", code)
                    if m:
                        t = m.group(1)
                        if t in POINTER_SIZED:
                            why = f"pointer-sized {t}"
                        elif t in HOST_MATH:
                            why = f"host-order {t}"
                        elif t not in SCALARS and t not in disc and not t.startswith(("enum", "Ft", "Fighter_Part")) \
                                and not code.startswith("enum"):
                            why = f"nested {t} (not DISC)"
                if why:
                    problems += 1
                    rel = p.relative_to(ROOT).as_posix()
                    print(f"{rel}:{line}: {name}: {why}: {code}")
    print(f"{problems} fields to check")


if __name__ == "__main__":
    main()
