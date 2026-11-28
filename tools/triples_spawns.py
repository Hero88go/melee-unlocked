"""gen_spawns.py: the Triples neutral spawn table (digicannon/ssbm-triples-asm asm/neutral_spawn.asm, GPL-3.0)
converted 1:1 to a C header. Reads the .asm, writes mu_triples_spawns.h. Run: py -3.12 gen_spawns.py <asm> <out.h>"""
import re
import sys

src, out = sys.argv[1], sys.argv[2]
text = open(src, encoding="utf-8").read()
table = text[text.index("NeutralSpawnTable:"):]
rows, cur, name = [], None, ""
for line in table.splitlines():
    s = line.strip()
    m = re.match(r"#\s*(.+)$", s)
    if m and not s.startswith("#Singles") and not s.startswith("# Singles") and "Teams" not in s and "Only here" not in s \
            and "identified" not in s:
        name = m.group(1).strip()
        continue
    m = re.match(r"\.long\s+(-?0x[0-9A-Fa-f]+|-?\d+)$", s)
    if m:
        v = int(m.group(1), 0)
        if v == -1:
            break
        cur = {"id": v, "name": name, "pts": []}
        rows.append(cur)
        continue
    m = re.match(r"\.float\s+(-?[\d.]+)\s*,\s*(-?[\d.]+)$", s)
    if m and cur is not None:
        cur["pts"].append((m.group(1), m.group(2)))

lines = [
    "/* Triples neutral spawns: digicannon/ssbm-triples-asm asm/neutral_spawn.asm (GPL-3.0), converted 1:1 by",
    " * tri_tools/gen_spawns.py. Per stage (external id): 6 singles points, then 6 teams points (x, y). */",
    "#pragma once",
    "typedef struct MuTriplesSpawnRow { int stage; float singles[6][2]; float teams[6][2]; } MuTriplesSpawnRow;",
    "static const MuTriplesSpawnRow mu_triples_spawns[] = {",
]
for r in rows:
    if len(r["pts"]) != 12:
        sys.exit(f"stage {r['id']:#x} ({r['name']}): {len(r['pts'])} points, expected 12")
    f = lambda p: "{" + ", ".join(x + ("f" if "." in x else ".0f") for x in p) + "}"
    lines.append(f"    {{0x{r['id']:02X}, /* {r['name']} */")
    lines.append("     {" + ", ".join(f(p) for p in r["pts"][:6]) + "},")
    lines.append("     {" + ", ".join(f(p) for p in r["pts"][6:]) + "}},")
lines.append("};")
open(out, "w", newline="\n").write("\n".join(lines) + "\n")
print(f"{len(rows)} stages -> {out}")
