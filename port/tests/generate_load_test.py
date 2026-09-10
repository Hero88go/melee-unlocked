"""Compile real decoded/emitted load instructions and check their executed results."""
from pathlib import Path
import sys
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "recomp"))
from gekko import decode
from emit import Emitter

emitter = Emitter(None, None, {}, set(), {})
info = SimpleNamespace(func=None)
out = [r'''
#include "ppc.h"
#include <cstdio>
#include <cstdlib>
namespace ppc {
uint8_t* locked_cache() { std::abort(); }
uint32_t mmio_read(Context&, uint32_t, int) { std::abort(); }
}
int main() {
  ppc::Context c{};
  uint8_t storage[64]{};
  uint8_t* m = storage;
  const uint32_t values[] = {0, 1, 0x7fff, 0x8000, 0xffff};
''']
for signed, dop, xop in [(True, 42, 343), (False, 40, 279)]:
    for indexed in (False, True):
        for update in (False, True):
            # r3 <- [r4 + 2] or [r4 + r5], with optional r4 update.
            word = ((31 << 26) | (3 << 21) | (4 << 16) | (5 << 11) |
                    ((xop + 32 * update) << 1)) if indexed else (
                    ((dop + update) << 26) | (3 << 21) | (4 << 16) | 2)
            ins = decode(0x80001000, word)
            code = emitter.emit_insn(info, 0, ins)
            expected = "(v & 0x8000u) ? (v | 0xffff0000u) : v" if signed else "v"
            out.append(f'''
  for (uint32_t v : values) {{
    c.r[4] = 0x80000010u; c.r[5] = 2; c.r[3] = 0xdeadbeefu;
    storage[18] = (uint8_t)(v >> 8); storage[19] = (uint8_t)v;
    {code}
    uint32_t expected = {expected};
    if (c.r[3] != expected || c.r[4] != {"0x80000012u" if update else "0x80000010u"}) {{
      std::fprintf(stderr, "{ins.op}: input=%04x result=%08x expected=%08x base=%08x\\n", v, c.r[3], expected, c.r[4]);
      return 1;
    }}
  }}
''')
out.append('std::puts("40 executed signed/unsigned halfword load cases passed"); return 0; }\n')
dest = Path(sys.argv[1])
dest.parent.mkdir(parents=True, exist_ok=True)
dest.write_text("".join(out), encoding="utf-8")
