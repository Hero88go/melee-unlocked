"""Mark structs that map disc data as disc layout (see sourceport/game/include/mu_disc.h).

    python tools/disc_convert.py HEADER STRUCT [STRUCT ...]

For each named struct defined in HEADER: the closing brace gains DISC_STRUCT, every pointer field
becomes a DISC_PTR slot (T** becomes a slot to an array of slots), and Vec3/Vec2/Quaternion/Mtx
fields become their big-endian twins. On the console build all of these expand to what was there,
so the edit is layout-neutral there. Fields it cannot judge (function pointers, nested structs of
other types) are listed so they can be checked by hand. Use sites are then fixed from the compiler's
errors: DP(x->field) reads a slot as a typed pointer.
"""
import re
import sys
from pathlib import Path

SCALAR_TWINS = {"f32": "be_f32", "float": "be_f32", "u16": "be_u16", "s16": "be_s16", "u32": "be_u32", "s32": "be_s32"}
BE_TWINS = {"Vec3": "Vec3_BE", "Vec2": "Vec2_BE", "Quaternion": "Quaternion_BE", "Mtx": "Mtx_BE"}
SCALARS = {"Vec3_BE", "Vec2_BE", "Quaternion_BE", "Mtx_BE", "u8", "s8", "u16", "s16", "u32", "s32", "f32", "f64", "int", "char", "float", "double", "bool", "BOOL",
           "unsigned", "signed", "short", "long", "GXColor", "uintptr_t", "intptr_t", "size_t", "UNK_T", "void"}
FIELD = re.compile(r"^(\s*(?:/\*[^*]*\*/\s*)?)((?:const\s+)?(?:struct\s+|union\s+|enum\s+)?[A-Za-z_]\w*(?:\s+[A-Za-z_]\w*)?)\s*(\*+)\s*([A-Za-z_]\w*)\s*(\[[^\]]*\])*\s*;(.*)$")
PLAIN = re.compile(r"^(\s*(?:/\*[^*]*\*/\s*)?)((?:struct\s+)?[A-Za-z_]\w*)\s+([A-Za-z_]\w*)\s*((?:\[[^\]]*\])*)\s*;(.*)$")


def find_struct(text, name):
    for pattern in (rf"struct\s+{name}\s*\{{", rf"typedef\s+struct\s+\w*\s*\{{(?=[^{{}}]*\}}\s*(?:DISC_STRUCT\s+)?{name}\s*;)"):
        m = re.search(pattern, text)
        if m:
            start = m.end()
            depth, i = 1, start
            while depth:
                depth += {"{": 1, "}": -1}.get(text[i], 0)
                i += 1
            return start, i - 1
    return None


def convert_body(body, report):
    out = []
    for line in body.split("\n"):
        m = FIELD.match(line)
        if m and "(" not in line:
            indent, base, stars, field, arrays, rest = m.group(1), m.group(2), m.group(3), m.group(4), m.group(5) or "", m.group(6)
            arrays = re.search(r"(\[[^\]]*\])+", line[m.start(4):line.index(";")])
            arrays = arrays.group(0) if arrays else ""
            inner = base.strip()
            inner = SCALAR_TWINS.get(inner, BE_TWINS.get(inner, inner))
            slot = f"DISC_PTR({inner})"
            for _ in range(len(stars) - 1):
                slot = f"DISC_PTR({slot})"
            out.append(f"{indent}{slot} {field}{arrays};{rest}")
            continue
        m = PLAIN.match(line)
        if m and m.group(2) in ("UNK_T", "MtxPtr"):   # pointer typedefs: 8 bytes natively, 4 on disc
            out.append(f"{m.group(1)}DISC_PTR({'void' if m.group(2) == 'UNK_T' else 'Mtx_BE'}) {m.group(3)}{m.group(4)};{m.group(5)}")
            continue
        if m and m.group(2) in BE_TWINS:
            out.append(f"{m.group(1)}{BE_TWINS[m.group(2)]} {m.group(3)}{m.group(4)};{m.group(5)}")
            continue
        if m and m.group(2).split()[-1] not in SCALARS and not m.group(2).startswith("enum"):
            report.append(f"check nested field: {line.strip()}")
        elif "(" in line and "*" in line:
            report.append(f"check function pointer: {line.strip()}")
        out.append(line)
    return "\n".join(out)


def main():
    header = Path(sys.argv[1])
    text = header.read_text(encoding="utf-8")
    for name in sys.argv[2:]:
        span = find_struct(text, name)
        if not span:
            print(f"{name}: not found in {header}")
            continue
        start, end = span
        report = []
        body = convert_body(text[start:end], report)
        tail = text[end:]
        if not tail.startswith("} DISC_STRUCT") and not re.match(r"\}\s*DISC_STRUCT", tail):
            tail = "} DISC_STRUCT" + tail[1:]
        text = text[:start] + body + tail
        print(f"{name}: converted" + "".join(f"\n  {r}" for r in report))
    header.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
