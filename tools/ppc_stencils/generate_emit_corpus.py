#!/usr/bin/env python3
"""Write a corpus of guest functions together with the C++ the recompiler emits for them.

Each function is decoded by port/recomp/gekko.py and emitted by port/recomp/emit.py, the same
code that writes the recompiled game, so the translator's native test can compare its output
with the recompiler's own text compiled by the same compiler. The corpus is deterministic.
Nothing here reads a game image: the guest words are generated.
"""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import hashlib
import random
import sys
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True  # Leave port/recomp exactly as it is.
sys.path.insert(0, str(ROOT / "port" / "recomp"))
import emit   # noqa: E402
import gekko  # noqa: E402

BASE = 0x80000000
BLR = 0x4E800020


def dform(op, first, second, immediate):
    return (op << 26) | (first << 21) | (second << 16) | (immediate & 0xFFFF)


def xform(first, second, third, xo, rc=0):
    return (31 << 26) | (first << 21) | (second << 16) | (third << 11) | (xo << 1) | rc


def mform(op, rs, ra, sh, mb, me, rc=0):
    return (op << 26) | (rs << 21) | (ra << 16) | (sh << 11) | (mb << 6) | (me << 1) | rc


def xlform(first, second, third, xo):
    return (19 << 26) | (first << 21) | (second << 16) | (third << 11) | (xo << 1)


def spr_field(spr):
    return ((spr & 31) << 5) | (spr >> 5)


def branch(offset):
    return (18 << 26) | (offset & 0x03FFFFFC)


def bc(bo, bi, offset):
    return (16 << 26) | (bo << 21) | (bi << 16) | (offset & 0xFFFC)


def bclr(bo, bi):
    return xlform(bo, bi, 0, 16)


class Generator:
    """Random canonical encodings. Inside a loop no register in `reserved` is written."""

    def __init__(self, seed):
        self.random = random.Random(seed)
        self.forms = self._forms()

    def register(self, reserved=()):
        while True:
            r = self.random.choice((0, 3, 31)) if self.random.randrange(3) == 0 else self.random.randrange(32)
            if r not in reserved:
                return r

    def immediate(self):
        if self.random.randrange(4) == 0:
            return self.random.choice((0, 1, 0x7FFF, 0x8000, 0xFFFF, 0x00FF, 0xFF00, 0x8001))
        return self.random.randrange(0x10000)

    def _forms(self):
        r, five = self.register, lambda: self.random.randrange(32)
        forms = {}
        # The destination is the first field for D-form and arithmetic words, else the second.
        for name, op in (("addi", 14), ("addis", 15), ("addic", 12), ("addic.", 13), ("subfic", 8), ("mulli", 7)):
            forms[name] = lambda w, op=op: dform(op, r(w), r(), self.immediate())
        for name, op in (("ori", 24), ("oris", 25), ("xori", 26), ("xoris", 27), ("andi.", 28), ("andis.", 29)):
            forms[name] = lambda w, op=op: dform(op, r(), r(w), self.immediate())
        for name, xo in (("add", 266), ("subf", 40), ("mullw", 235), ("mulhw", 75), ("mulhwu", 11), ("divw", 491),
                         ("divwu", 459), ("addc", 10), ("adde", 138), ("subfc", 8)):
            for rc in (0, 1):
                forms[name + "." * rc] = lambda w, xo=xo, rc=rc: xform(r(w), r(), r(), xo, rc)
        for name, xo in (("neg", 104), ("addze", 202), ("addme", 234), ("subfze", 200), ("subfme", 232)):
            for rc in (0, 1):
                forms[name + "." * rc] = lambda w, xo=xo, rc=rc: xform(r(w), r(), 0, xo, rc)
        for name, xo in (("and", 28), ("or", 444), ("xor", 316), ("nand", 476), ("nor", 124), ("eqv", 284),
                         ("andc", 60), ("orc", 412), ("slw", 24), ("srw", 536), ("sraw", 792)):
            for rc in (0, 1):
                forms[name + "." * rc] = lambda w, xo=xo, rc=rc: xform(r(), r(w), r(), xo, rc)
        for name, xo in (("extsb", 954), ("extsh", 922), ("cntlzw", 26)):
            for rc in (0, 1):
                forms[name + "." * rc] = lambda w, xo=xo, rc=rc: xform(r(), r(w), 0, xo, rc)
        for rc in (0, 1):
            forms["srawi" + "." * rc] = lambda w, rc=rc: xform(r(), r(w), five(), 824, rc)
            forms["rlwinm" + "." * rc] = lambda w, rc=rc: mform(21, r(), r(w), five(), five(), five(), rc)
            forms["rlwimi" + "." * rc] = lambda w, rc=rc: mform(20, r(), r(w), five(), five(), five(), rc)
            forms["rlwnm" + "." * rc] = lambda w, rc=rc: mform(23, r(), r(w), r(), five(), five(), rc)
        crf = lambda: self.random.randrange(8) << 2
        forms["cmpw"] = lambda w: xform(crf(), r(), r(), 0)
        forms["cmplw"] = lambda w: xform(crf(), r(), r(), 32)
        forms["cmpwi"] = lambda w: dform(11, crf(), r(), self.immediate())
        forms["cmplwi"] = lambda w: dform(10, crf(), r(), self.immediate())
        forms["mfcr"] = lambda w: xform(r(w), 0, 0, 19)
        forms["mtcrf"] = lambda w: (31 << 26) | (r() << 21) | (self.random.randrange(256) << 12) | (144 << 1)
        forms["mcrf"] = lambda w: xlform(crf(), crf(), 0, 0)
        for name, spr in (("mfxer", 1), ("mflr", 8), ("mfctr", 9)):
            forms[name] = lambda w, spr=spr: (31 << 26) | (r(w) << 21) | (spr_field(spr) << 11) | (339 << 1)
        for name, spr in (("mtxer", 1), ("mtlr", 8), ("mtctr", 9)):
            forms[name] = lambda w, spr=spr: (31 << 26) | (r() << 21) | (spr_field(spr) << 11) | (467 << 1)
        return forms

    def straight(self, count, reserved=(), exclude=()):
        names = [name for name in self.forms if name not in exclude]
        return [self.forms[self.random.choice(names)](reserved) for _ in range(count)]

    # ---- structured control flow: every loop has a bounded trip count by construction ----
    def condition(self):
        """A compare of two live values and the BO/BI of a branch on one of its CR bits."""
        field = self.random.randrange(8)
        compare = self.random.choice((
            lambda: xform(field << 2, self.register(), self.register(), 0),
            lambda: xform(field << 2, self.register(), self.register(), 32),
            lambda: dform(11, field << 2, self.register(), self.immediate()),
            lambda: dform(10, field << 2, self.register(), self.immediate())))()
        return compare, self.random.choice((4, 12)) | self.random.randrange(2), field * 4 + self.random.randrange(4)

    def block(self, depth, counters, in_ctr_loop):
        """A list of words that always falls through its end after a bounded number of steps."""
        rng = self.random
        reserved = tuple(counters)
        exclude = ("mtctr",) if in_ctr_loop else ()
        words = []
        for _ in range(rng.randrange(1, 4)):
            kind = rng.randrange(8 if depth < 3 else 2)
            if kind <= 1:
                words += self.straight(rng.randrange(1, 6), reserved, exclude)
            elif kind == 2:  # if: a forward conditional branch over a block
                compare, bo, bi = self.condition()
                body = self.block(depth + 1, counters, in_ctr_loop)
                words += [compare, bc(bo, bi, 4 * (len(body) + 1))] + body
            elif kind == 3:  # if/else: forward conditional branch, then a forward b over the else
                compare, bo, bi = self.condition()
                then = self.block(depth + 1, counters, in_ctr_loop)
                other = self.block(depth + 1, counters, in_ctr_loop)
                words += [compare, bc(bo, bi, 4 * (len(then) + 2))] + then + [branch(4 * (len(other) + 1))] + other
            elif kind == 4:  # early return on a condition
                compare, bo, bi = self.condition()
                words += [compare, bclr(bo, bi)]
            elif kind == 5 and not in_ctr_loop:  # CTR loop: li, mtctr, body, bdnz (or bdnz with a CR bit)
                counter = 28 + len(counters)
                if counter > 31:
                    continue
                body = self.block(depth + 1, counters + [counter], True)
                bo = rng.choice((16, 16, 16, 0, 8))
                tail = []
                if bo != 16:
                    compare, _, bi = self.condition()
                    tail = [compare]
                else:
                    bi = 0
                body += tail
                words += [dform(14, counter, 0, rng.randrange(1, 41)),
                          (31 << 26) | (counter << 21) | (spr_field(9) << 11) | (467 << 1)]
                words += body + [bc(bo, bi, -4 * len(body))]
            elif kind == 6:  # register-counted loop closed by a conditional backward branch
                counter = 28 + len(counters)
                if counter > 31:
                    continue
                body = self.block(depth + 1, counters + [counter], in_ctr_loop)
                if rng.randrange(2):
                    tail = [dform(13, counter, counter, 0xFFFF), bc(4, 2, 0)]            # addic. ; bne
                else:
                    field = rng.randrange(8)
                    tail = [dform(14, counter, counter, 0xFFFF), dform(11, field << 2, counter, 0),
                            bc(12, field * 4 + 1, 0)]                                    # addi ; cmpwi ; bgt
                body += tail[:-1]
                words += [dform(14, counter, 0, rng.randrange(1, 21))] + body
                words += [(tail[-1] & ~0xFFFC) | ((-4 * len(body)) & 0xFFFC)]
            else:  # loop closed by an unconditional backward b, left by a forward conditional branch
                counter = 28 + len(counters)
                if counter > 31:
                    continue
                body = self.block(depth + 1, counters + [counter], in_ctr_loop)
                body += [dform(13, counter, counter, 0xFFFF), bc(12, 2, 8)]              # addic. ; beq +8
                words += [dform(14, counter, 0, rng.randrange(1, 21))] + body + [branch(-4 * len(body))]
        return words

    def branching(self):
        while True:
            words = self.block(0, [], False)
            # At top level a forward branch may also decrement CTR (bdz, bdnz forward).
            if self.random.randrange(3) == 0:
                tail = self.straight(self.random.randrange(1, 4))
                words += [bc(self.random.choice((16, 18, 0, 2, 8, 10)), self.random.randrange(32), 4 * (len(tail) + 1))] + tail
            if self.random.randrange(4) == 0:  # unreachable words after an unconditional forward b
                junk = self.straight(self.random.randrange(1, 4))
                words += [branch(4 * (len(junk) + 1))] + junk
            words.append(BLR)
            if len(words) <= 400:
                return words


def emit_function(index, kind, address, words):
    """The recompiler's C++ for these words, through the unmodified Emitter."""
    name = "emit_%d" % index
    insns = [gekko.decode(address + 4 * i, word) for i, word in enumerate(words)]
    if any(insn is None for insn in insns):
        raise RuntimeError("corpus word does not decode: %s" % [hex(w) for w in words])
    addrs = [address + 4 * i for i in range(len(words))]
    labels = {insn.branch_target for insn in insns if insn.branch_target is not None}
    if not labels <= set(addrs):
        raise RuntimeError("corpus branch leaves its function")
    func = SimpleNamespace(addr=address, name=name, size=4 * len(words))
    info = SimpleNamespace(
        func=func, insns=insns, addrs=addrs, addr_set=set(addrs), aliases={}, local_returns=set(),
        ctr_targets={}, ctr_calls={}, entries=set(), labels=labels, optional_hooks={}, setjmp_returns=set(),
        optional_text={}, jumptables={}, has_blrl=False, has_bctrl=False)
    emitter = emit.Emitter(None, None, {}, set(), {address: name})
    return name, emitter.emit_function(info)


def build(seed):
    generator = Generator(seed)
    rng = generator.random
    functions = []
    for name, make in generator.forms.items():
        for _ in range(24):
            functions.append((name, BASE, [make(()), BLR]))
    for _ in range(300):
        functions.append(("sequence", BASE + 4 * rng.randrange(64), generator.straight(rng.randrange(2, 41)) + [BLR]))
    for _ in range(500):
        functions.append(("branching", BASE + 4 * rng.randrange(64), generator.branching()))
    return functions


def render(functions):
    lines = ["// Generated by tools/ppc_stencils/generate_emit_corpus.py from port/recomp/emit.py; do not edit.",
             "// SPDX-License-Identifier: GPL-2.0-or-later", "#pragma once", '#include "ppc.h"',
             "namespace emit_corpus {"]
    rows = []
    for index, (kind, address, words) in enumerate(functions):
        name, text = emit_function(index, kind, address, words)
        lines.append(text.rstrip("\n"))
        lines.append("inline constexpr uint32_t words_%d[] = {%s};" % (index, ", ".join("0x%08Xu" % w for w in words)))
        rows.append('  {"%s", 0x%08Xu, words_%d, %d, %s},' % (kind, address, index, len(words), name))
    lines.append("struct Function { const char* kind; uint32_t address; const uint32_t* words; size_t count; ppc::Fn fn; };")
    lines.append("inline constexpr Function functions[] = {")
    lines += rows
    digest = hashlib.sha256(repr(functions).encode()).hexdigest()
    lines += ["};", 'inline constexpr char sha256[] = "%s";' % digest, "}", ""]
    return "\n".join(lines), digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--header", required=True, type=Path)
    parser.add_argument("--seed", type=int, default=20261001)
    args = parser.parse_args()
    functions = build(args.seed)
    text, digest = render(functions)
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text(text, encoding="utf-8")
    print("emit corpus: %d functions, %d guest words, sha256 %s" % (
        len(functions), sum(len(words) for _, _, words in functions), digest))


if __name__ == "__main__":
    main()
