"""Rewrite writes to the GX write-gather pipe as calls, so the native build can redirect them.

`GXWGFifo.u32 = x;` is a store to a memory-mapped register: the hardware gathered the bytes into
the command FIFO. C offers no way to redirect that lvalue form and advance a cursor by the store's
size, so every site becomes `GXW_u32(x);`. On the console the macro is the same store, so the
matching build is unchanged; natively it appends the big-endian bytes to a staging buffer that is
handed to the host's FIFO decoder (sourceport/game/include/mu_gxpipe.h).

One-shot transformation over the decomp tree on the mu/native branch.
"""
import re
import sys
from pathlib import Path

STORE = re.compile(r"\bGXWGFifo\.(u8|u16|u32|u64|s8|s16|s32|s64|f32|f64)\s*=\s*(.+?);")
PASTE = re.compile(r"\bGXWGFifo\.T\s*=\s*(\w+);")   # inside the FUNC_ macros of GXVert.h/.c


def rewrite(path):
    text = path.read_text(encoding="utf-8")
    new = STORE.sub(lambda m: f"GXW_{m[1]}({m[2]});", text)
    new = PASTE.sub(lambda m: f"GXW_##T({m[1]});", new)
    n = len(STORE.findall(text)) + len(PASTE.findall(text))
    if new != text:
        path.write_text(new, encoding="utf-8", newline="")
    left = [l.strip() for l in new.splitlines() if "GXWGFifo" in l and "define GXWGFifo" not in l and "GXWGFifo :" not in l]
    return n, left


if __name__ == "__main__":
    root = Path(sys.argv[1])
    total = 0
    for path in sorted(list(root.rglob("*.c")) + list(root.rglob("*.h"))):
        if "GXWGFifo" not in path.read_text(encoding="utf-8", errors="replace"):
            continue
        n, left = rewrite(path)
        total += n
        print(f"{path.relative_to(root).as_posix()}: {n} rewritten" + (f"; still mentions the pipe: {left}" if left else ""))
    print(f"{total} pipe writes rewritten")
