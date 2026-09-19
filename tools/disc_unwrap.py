"""Undo DP() wraps the compiler rejects because the operand is a real pointer, not a disc slot.

    python tools/disc_unwrap.py BUILD_LOG

tools/disc_wrap.py matches field names, and generic names (x8, x2C, x54) also exist in host
structs. GCC reports those as "'expr' is a pointer; did you mean to use '->'?" (or "has no member
named 'raw'/'type_'") inside mu_disc.h, followed by "note: in expansion of macro 'DP'" at the
call site. This removes the DP( ... ) at each such call site.
"""
import re
import sys
from collections import defaultdict

ROOT = "C:/Users/Chandler/NEW project/melee-sourceport/sourceport/extern/melee/"
BAD = re.compile(r"mu_disc\.h:\d+:\d+: error: (.* is a pointer; did you mean|.*has no member named '(raw|type_)'|request for member '(raw|type_)')")
NOTE = re.compile(r"^(?P<f>[A-Za-z]:[^:]+|[^:]+):(?P<l>\d+):(?P<c>\d+): note: in expansion of macro 'DP'")


def main():
    lines = open(sys.argv[1], encoding="utf-8", errors="replace").read().splitlines()
    sites = defaultdict(set)
    for i, line in enumerate(lines):
        if BAD.search(line):
            for j in range(i + 1, min(i + 8, len(lines))):
                m = NOTE.match(lines[j])
                if m:
                    sites[m["f"].replace("\\", "/")].add((int(m["l"]), int(m["c"])))
                    break
    for path, where in sites.items():
        text = open(path, encoding="utf-8", newline="").read()
        starts = [0] + [m.end() for m in re.finditer("\n", text)]
        for l, c in sorted(where, reverse=True):
            pos = starts[l - 1] + c - 1
            if text[pos:pos + 3] != "DP(":
                print(f"skip {path}:{l}:{c}: {text[pos:pos + 12]!r}")
                continue
            depth, k = 0, pos + 2
            while k < len(text):
                depth += {"(": 1, ")": -1}.get(text[k], 0)
                if depth == 0:
                    break
                k += 1
            text = text[:pos] + text[pos + 3:k] + text[k + 1:]
        open(path, "w", encoding="utf-8", newline="").write(text)
        print(f"{path.replace(ROOT, '')}: {len(where)} unwrapped")


if __name__ == "__main__":
    main()
