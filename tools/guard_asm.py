"""Wrap every function written in PowerPC assembly in #ifndef MU_NATIVE, so the native build skips it.

One-shot transformation applied to SDK sources on the mu/native branch. A function counts as
assembly when its definition starts with `asm` or its body contains an `asm {` block. The console
build is untouched: the guard is only ever false there.
"""
import re
import sys
from pathlib import Path

SIG = re.compile(r"^(?:asm\s+)?(?:static\s+)?(?:asm\s+)?(?:const\s+)?[A-Za-z_][A-Za-z0-9_]*[\s*]+[A-Za-z_][A-Za-z0-9_]*\s*\(", re.M)


def guard(path):
    text = path.read_text(encoding="utf-8")
    out, pos, wrapped = [], 0, []
    for m in SIG.finditer(text):
        if m.start() < pos:
            continue
        # a definition: the parameter list is followed by an opening brace (possibly on the next line)
        depth, i = 0, m.end() - 1
        while i < len(text) and depth >= 0:
            if text[i] == "(": depth += 1
            elif text[i] == ")":
                depth -= 1
                if depth == 0: break
            i += 1
        j = i + 1
        while j < len(text) and text[j] in " \t\r\n": j += 1
        if j >= len(text) or text[j] != "{":
            continue
        depth, k = 0, j
        while k < len(text):
            if text[k] == "{": depth += 1
            elif text[k] == "}":
                depth -= 1
                if depth == 0: break
            k += 1
        body = text[m.start():k + 1]
        if re.match(r"\s*(?:static\s+)?asm\b", m.group(0)) or re.search(r"\basm\s*\{", body):
            out.append(text[pos:m.start()])
            out.append("#ifndef MU_NATIVE /* PowerPC assembly */\n" + body + "\n#endif")
            pos = k + 1
            wrapped.append(re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*\($", m.group(0)).group(1))
    out.append(text[pos:])
    if wrapped:
        path.write_text("".join(out), encoding="utf-8", newline="")
    return wrapped


if __name__ == "__main__":
    for arg in sys.argv[1:]:
        names = guard(Path(arg))
        print(f"{arg}: {len(names)} guarded: {' '.join(names)}")
