"""List the struct types that map disc data, starting from what the game loads out of archives.

    python tools/disc_types.py [--decomp sourceport/extern/melee] [--json out.json]

Roots are the variables handed to lbArchive_LoadSymbols / lbArchive_80016DBC (and friends) as
symbol targets, plus HSD_ArchiveGetPublicAddress results cast to a type. From those it follows every
pointer or nested struct field to other structs (a regex reading of the sources, good enough to make
the worklist; the compiler has the final word). Types already marked DISC_STRUCT are reported as done.
"""
import argparse
import json
import re
from collections import defaultdict
from pathlib import Path

LOADERS = r"(?:lbArchive_LoadSymbols|lbArchive_80016DBC|lbArchive_80016C64|lbArchive_80016EFC|lbArchive_LoadSections)"
SCALAR = {"u8", "s8", "u16", "s16", "u32", "s32", "f32", "f64", "int", "char", "float", "double", "bool", "BOOL", "void",
          "unsigned", "signed", "short", "long", "GXColor", "uintptr_t", "intptr_t", "size_t", "UNK_T", "Vec3", "Vec2",
          "Vec4", "Quaternion", "Mtx", "Mtx44", "MtxPtr", "Vec3_BE", "Vec2_BE", "Quaternion_BE", "Mtx_BE", "Mtx44_BE",
          "be_f32", "be_u32", "be_s32", "be_u16", "be_s16", "HSD_GObj", "HSD_JObj", "HSD_Archive"}


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--decomp", default="sourceport/extern/melee")
    ap.add_argument("--json")
    args = ap.parse_args()
    root = Path(args.decomp) / "src"
    files = list(root.rglob("*.h")) + list(root.rglob("*.c"))
    texts = {f: strip_comments(f.read_text(encoding="utf-8", errors="replace")) for f in files}

    # struct bodies, by struct tag and by typedef name
    bodies, where, disc = {}, {}, set()
    alias = {}
    for f, t in texts.items():
        for m in re.finditer(r"typedef\s+struct\s+(\w+)\s+(\w+)\s*;", t):
            alias[m.group(2)] = m.group(1)
        for m in re.finditer(r"(typedef\s+)?(struct|union)\s+(\w*)\s*\{", t):
            start, depth, i = m.end(), 1, m.end()
            while depth and i < len(t):
                depth += {"{": 1, "}": -1}.get(t[i], 0)
                i += 1
            body = t[start:i - 1]
            tail = re.match(r"\s*(DISC_STRUCT\s*)?(\w*)", t[i:])
            names = [n for n in (m.group(3), tail.group(2) if m.group(1) else "") if n]
            for n in names:
                if n not in bodies or len(body) > len(bodies[n]):
                    bodies[n], where[n] = body, f
                if tail.group(1):
                    disc.add(n)
    def canon(n):
        return alias.get(n, n) if alias.get(n, n) in bodies else n

    # variable declarations: name -> type
    decl_re = re.compile(r"(?:static\s+)?(?:struct\s+)?(\w+)\s*\*+\s*(\w+)\s*(?:\[[^\]]*\])?\s*[;=,)]")
    file_decl = {f: defaultdict(set) for f in texts}
    for f, t in texts.items():
        for m in decl_re.finditer(t):
            file_decl[f][m.group(2)].add(m.group(1))
    def decl_for(f, name):
        # the load site's own file first, then its header of the same name, then nothing
        found = file_decl[f].get(name)
        if found:
            return found
        h = f.with_suffix(".h")
        return file_decl.get(h, {}).get(name, set())
    roots = defaultdict(set)
    for f, t in texts.items():
        for m in re.finditer(LOADERS + r"\s*\(([^;]*?)\)\s*;", t, flags=re.S):
            for v in re.findall(r"&\s*([\w.\->\[\]]+)", m.group(1)):
                name = re.split(r"[.\->\[]", v)[-1] if "->" not in v else v.split("->")[-1]
                name = re.sub(r"\W", "", name)
                for ty in decl_for(f, name):
                    if ty not in SCALAR:
                        roots[canon(ty)].add(f"{f.relative_to(root)}")
    # closure over fields
    seen, order, parent = set(), [], {}
    stack = list(roots)
    while stack:
        ty = stack.pop()
        if ty in seen or ty in SCALAR or ty not in bodies:
            continue
        seen.add(ty)
        if re.search(r"(HSD_GObj|HSD_JObj|HSD_DObj|HSD_CObj|HSD_LObj|HSD_Archive)\s*\*", bodies[ty]):
            continue   # holds runtime objects: not disc data, and not a path to it
        order.append(ty)
        for fm in re.finditer(r"(?:struct\s+|union\s+)?(\w+)\s*(\**)\s*\w+\s*(?:\[[^\]]*\])*\s*;", bodies[ty]):
            child = canon(fm.group(1))
            if child not in seen and child in bodies and child not in SCALAR:
                parent.setdefault(child, ty)
                stack.append(child)
    todo = [t for t in order if t not in disc and not t.startswith("HSD_") and not t.startswith("_HSD_")]
    report = {t: {"file": str(where[t].relative_to(root)), "via": parent.get(t, "(root)"),
                  "roots_from": sorted(roots.get(t, ()))[:3]} for t in todo}
    print(f"{len(roots)} root types, {len(order)} reachable, {len(todo)} to convert (game types, not yet DISC_STRUCT)")
    by_file = defaultdict(list)
    for t, r in report.items():
        by_file[r["file"]].append(t)
    for f in sorted(by_file):
        print(f"{f}: {' '.join(sorted(by_file[f]))}")
    if args.json:
        Path(args.json).write_text(json.dumps(report, indent=1))


if __name__ == "__main__":
    main()
