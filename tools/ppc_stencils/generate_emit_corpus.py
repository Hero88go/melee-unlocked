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
BLRL = 0x4E800021
# Stand-in callees with a computed return (port/tests/ppc_leaf_host.h): they return to the
# caller's LR plus 8, or plus 4 when the address has bit 2 set.
RESUME_BASE, RESUME_COUNT = 0x80600000, 64


def resume_delta(address):
    return 4 if address & 4 else 8


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


def bcctr(bo, bi, lk=0):
    return xlform(bo, bi, 0, 528) | lk


def aform(op, d, a, b, c, xo5):
    return (op << 26) | (d << 21) | (a << 16) | (b << 11) | (c << 6) | (xo5 << 1)


def xoform(op, d, a, b, xo10):
    return (op << 26) | (d << 21) | (a << 16) | (b << 11) | (xo10 << 1)


# (name, kind, primary opcode, extended opcode). Kinds: D and X are displacement and indexed
# forms; U marks an update form (RA is never r0) and L an integer load (RA is never RD).
MEMORY_FORMS = [
    ("lwz", "D", 32, 0), ("lwzu", "DUL", 33, 0), ("lbz", "D", 34, 0), ("lbzu", "DUL", 35, 0),
    ("stw", "D", 36, 0), ("stwu", "DU", 37, 0), ("stb", "D", 38, 0), ("stbu", "DU", 39, 0),
    ("lhz", "D", 40, 0), ("lhzu", "DUL", 41, 0), ("lha", "D", 42, 0), ("lhau", "DUL", 43, 0),
    ("sth", "D", 44, 0), ("sthu", "DU", 45, 0), ("lmw", "LMW", 46, 0), ("stmw", "STMW", 47, 0),
    ("lwzx", "X", 31, 23), ("lwzux", "XUL", 31, 55), ("lbzx", "X", 31, 87), ("lbzux", "XUL", 31, 119),
    ("lhzx", "X", 31, 279), ("lhzux", "XUL", 31, 311), ("lhax", "X", 31, 343), ("lhaux", "XUL", 31, 375),
    ("stwx", "X", 31, 151), ("stwux", "XU", 31, 183), ("stbx", "X", 31, 215), ("stbux", "XU", 31, 247),
    ("sthx", "X", 31, 407), ("sthux", "XU", 31, 439),
    ("lwbrx", "X", 31, 534), ("lhbrx", "X", 31, 790), ("stwbrx", "X", 31, 662), ("sthbrx", "X", 31, 918),
    ("lfs", "D", 48, 0), ("lfsu", "DU", 49, 0), ("lfd", "D", 50, 0), ("lfdu", "DU", 51, 0),
    ("stfs", "D", 52, 0), ("stfsu", "DU", 53, 0), ("stfd", "D", 54, 0), ("stfdu", "DU", 55, 0),
    ("lfsx", "X", 31, 535), ("lfsux", "XU", 31, 567), ("lfdx", "X", 31, 599), ("lfdux", "XU", 31, 631),
    ("stfsx", "X", 31, 663), ("stfsux", "XU", 31, 695), ("stfdx", "X", 31, 727), ("stfdux", "XU", 31, 759),
    ("stfiwx", "X", 31, 983),
    ("psq_l", "PD", 56, 0), ("psq_lu", "PDU", 57, 0), ("psq_st", "PD", 60, 0), ("psq_stu", "PDU", 61, 0),
    ("psq_lx", "PX", 4, 6), ("psq_stx", "PX", 4, 7), ("psq_lux", "PXU", 4, 38), ("psq_stux", "PXU", 4, 39),
]
# Float kinds name the register fields the form uses: AB, AC, ACB, B (five-bit opcode), and
# U (unary), M (two sources) and CMP for the ten-bit ones.
FLOAT_FORMS = [
    ("fadd", "AB", 63, 21), ("fsub", "AB", 63, 20), ("fmul", "AC", 63, 25), ("fdiv", "AB", 63, 18),
    ("fmadd", "ACB", 63, 29), ("fmsub", "ACB", 63, 28), ("fnmadd", "ACB", 63, 31), ("fnmsub", "ACB", 63, 30),
    ("fadds", "AB", 59, 21), ("fsubs", "AB", 59, 20), ("fmuls", "AC", 59, 25), ("fdivs", "AB", 59, 18),
    ("fmadds", "ACB", 59, 29), ("fmsubs", "ACB", 59, 28), ("fnmadds", "ACB", 59, 31), ("fnmsubs", "ACB", 59, 30),
    ("fres", "B", 59, 24), ("frsqrte", "B", 63, 26), ("fsel", "ACB", 63, 23),
    ("frsp", "U", 63, 12), ("fmr", "U", 63, 72), ("fneg", "U", 63, 40), ("fabs", "U", 63, 264),
    ("fnabs", "U", 63, 136), ("fctiw", "U", 63, 14), ("fctiwz", "U", 63, 15),
    ("fcmpu", "CMP", 63, 0), ("fcmpo", "CMP", 63, 32),
    ("mffs", "MFFS", 63, 583), ("mtfsf", "MTFSF", 63, 711), ("mtfsb0", "MTFSB", 63, 70),
    ("mtfsb1", "MTFSB", 63, 38), ("mtfsfi", "MTFSFI", 63, 134), ("mcrfs", "MCRFS", 63, 64),
    ("ps_add", "AB", 4, 21), ("ps_sub", "AB", 4, 20), ("ps_mul", "AC", 4, 25), ("ps_div", "AB", 4, 18),
    ("ps_muls0", "AC", 4, 12), ("ps_muls1", "AC", 4, 13),
    ("ps_madd", "ACB", 4, 29), ("ps_msub", "ACB", 4, 28), ("ps_nmadd", "ACB", 4, 31), ("ps_nmsub", "ACB", 4, 30),
    ("ps_madds0", "ACB", 4, 14), ("ps_madds1", "ACB", 4, 15), ("ps_sum0", "ACB", 4, 10), ("ps_sum1", "ACB", 4, 11),
    ("ps_sel", "ACB", 4, 23), ("ps_res", "B", 4, 24), ("ps_rsqrte", "B", 4, 26),
    ("ps_mr", "U", 4, 72), ("ps_neg", "U", 4, 40), ("ps_abs", "U", 4, 264), ("ps_nabs", "U", 4, 136),
    ("ps_merge00", "M", 4, 528), ("ps_merge01", "M", 4, 560), ("ps_merge10", "M", 4, 592), ("ps_merge11", "M", 4, 624),
    ("ps_cmpu0", "CMP", 4, 0), ("ps_cmpo0", "CMP", 4, 32), ("ps_cmpu1", "CMP", 4, 64), ("ps_cmpo1", "CMP", 4, 96),
]


class Generator:
    """Random canonical encodings. Inside a loop no register in `reserved` is written."""

    def __init__(self, seed):
        self.random = random.Random(seed)
        self.forms = self._forms()
        # Forms that touch memory or float registers: never used inside the generated loops,
        # whose counters they could overwrite.
        self.wide = {name: (lambda kind=kind, op=op, xo=xo: self.wide_word(kind, op, xo))
                     for name, kind, op, xo in MEMORY_FORMS + FLOAT_FORMS}

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
        for rc in (0, 1):
            forms["subfe" + "." * rc] = lambda w, rc=rc: xform(r(w), r(), r(), 136, rc)
        for name, xo in (("crand", 257), ("cror", 449), ("crxor", 193), ("crnand", 225), ("crnor", 33),
                         ("creqv", 289), ("crandc", 129), ("crorc", 417)):
            forms[name] = lambda w, xo=xo: xlform(five(), five(), five(), xo)
        forms["mcrxr"] = lambda w: xform(self.random.randrange(8) << 2, 0, 0, 512)
        forms["sync"] = lambda w: xform(0, 0, 0, 598)
        forms["eieio"] = lambda w: xform(0, 0, 0, 854)
        forms["isync"] = lambda w: xlform(0, 0, 0, 150)
        for name, xo in (("dcbst", 54), ("dcbf", 86), ("dcbtst", 246), ("dcbt", 278), ("dcbi", 470), ("icbi", 982)):
            forms[name] = lambda w, xo=xo: xform(0, r(), r(), xo)
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

    def wide_word(self, kind, op, xo):
        """One canonical word of a memory or float form."""
        rng, r = self.random, self.register
        a, b, c, k = r(), r(), r(), r()
        d16 = self.immediate() if rng.randrange(4) else rng.choice((0, 4, 8, 0xFFFC, 0xFFF8))
        if "U" in kind and kind[0] in "DXP":
            while b == 0:
                b = rng.randrange(1, 32)
            if kind.endswith("L"):
                while a == b:
                    a = rng.randrange(32)
        if kind == "LMW":
            a = rng.randrange(1, 32)
            b = rng.randrange(a)
            return dform(op, a, b, d16)
        if kind == "STMW":
            if rng.randrange(2):
                a = rng.randrange(24, 32)
            return dform(op, a, b, d16)
        if kind[0] == "D":
            return dform(op, a, b, d16)
        if kind[0] == "X":
            return xform(a, b, c, xo)
        if kind.startswith("PD"):
            return (op << 26) | (a << 21) | (b << 16) | (rng.randrange(2) << 15) | (rng.randrange(8) << 12) | (d16 & 0xFFF)
        if kind.startswith("PX"):
            return (4 << 26) | (a << 21) | (b << 16) | (c << 11) | (rng.randrange(2) << 10) | (rng.randrange(8) << 7) | (xo << 1)
        if kind == "AB":
            return aform(op, a, b, c, 0, xo)
        if kind == "AC":
            return aform(op, a, b, 0, k, xo)
        if kind == "ACB":
            return aform(op, a, b, c, k, xo)
        if kind == "B":
            return aform(op, a, 0, c, 0, xo)
        if kind == "U":
            return xoform(op, a, 0, c, xo)
        if kind == "M":
            return xoform(op, a, b, c, xo)
        if kind == "CMP":
            return xoform(op, rng.randrange(8) << 2, b, c, xo)
        if kind in ("MFFS", "MTFSB"):
            return xoform(63, a, 0, 0, xo)
        if kind == "MTFSF":
            return (63 << 26) | (rng.randrange(256) << 17) | (c << 11) | (xo << 1)
        if kind == "MTFSFI":
            return xoform(63, rng.randrange(8) << 2, 0, rng.randrange(16) << 1, xo)
        if kind == "MCRFS":
            return xoform(63, rng.randrange(8) << 2, rng.randrange(8) << 2, 0, xo)
        raise ValueError(kind)

    def mixed(self, count):
        """Straight-line words from every form the translator accepts."""
        rng = self.random
        narrow, wide = list(self.forms), list(self.wide)
        return [self.wide[rng.choice(wide)]() if rng.randrange(2) else self.forms[rng.choice(narrow)](())
                for _ in range(count)]

    def calling(self, address):
        """Every form, with calls and tail calls through the dispatch; no loops, so it ends."""
        rng = self.random
        host = BASE + 0x400000
        words = []
        for _ in range(rng.randrange(2, 30)):
            here = address + 4 * len(words)
            pick = rng.randrange(12)
            if pick == 0:
                words.append(branch(host + 4 * rng.randrange(64) - here) | 1)                      # bl
            elif pick == 1:                                                                        # bcl, near
                words.append(bc(rng.choice((4, 12)), rng.randrange(32), address + 0x1000 + 4 * rng.randrange(64) - here) | 1)
            elif pick == 2:                                                                        # bctrl, bcctrl
                words.append(bcctr(rng.choice((20, 20, 4, 12)), rng.randrange(32), 1))
            else:
                words += self.mixed(1)
        here = address + 4 * len(words)
        ending = rng.randrange(6)
        if ending == 0:
            words.append(branch(host + 4 * rng.randrange(64) - here))                              # tail call
        elif ending == 1:
            words.append(bcctr(20, 0))                                                             # bctr
        elif ending == 2:
            words += [bc(rng.choice((4, 12)), rng.randrange(32), 0x2000), BLR]                     # conditional tail
        elif ending == 3:
            words += [bcctr(rng.choice((4, 12)), rng.randrange(32)), BLR]                          # conditional bctr
        else:
            words.append(BLR)
        return words

    def plain(self, count):
        """Straight-line words that leave LR, CTR and r24..r27 (saved return addresses) alone."""
        rng, saved, out = self.random, (24, 25, 26, 27), []
        narrow = [name for name in self.forms if not name.startswith("mt") or name == "mtcrf"]
        wide = [name for name, kind, _, _ in MEMORY_FORMS + FLOAT_FORMS if kind != "LMW"]
        while len(out) < count:
            if rng.randrange(3):
                out.append(self.forms[rng.choice(narrow)](saved))
                continue
            word = self.wide[rng.choice(wide)]()
            if (word >> 21) & 31 in saved or (word >> 16) & 31 in saved:
                continue
            out.append(word)
        return out

    def data_word(self):
        rng = self.random
        if rng.randrange(2):
            return rng.choice((0x00000000, 0x00000001, 0x3F800000, 0xC0000000, 0xFFFFFFFF, 0x00FF00FF))
        return rng.randrange(1 << 26)  # primary opcode 0: never an instruction

    def local(self, address):
        """Local subroutines, inline data, blrl and callees with computed returns.

        The main body saves LR in r27 and the three subroutines in r26, r25 and r24. The first
        subroutine may call the other two. Every path ends.
        """
        rng = self.random
        host = BASE + 0x400000
        words, patches = [], []
        mflr = lambda r: (31 << 26) | (r << 21) | (spr_field(8) << 11) | (339 << 1)
        mtlr = lambda r: (31 << 26) | (r << 21) | (spr_field(8) << 11) | (467 << 1)
        lis_ori = lambda target: [dform(15, 12, 0, target >> 16), dform(24, 12, 12, target & 0xFFFF)]

        def piece(first_sub):
            here = address + 4 * len(words)
            pick = rng.randrange(10)
            if pick <= 1:
                if first_sub < 3:
                    patches.append((len(words), rng.randrange(first_sub, 3), rng.randrange(4) == 0))
                    words.append(0)
            elif pick == 2:  # bl over inline data, mflr, and usually a read of the data
                count, reg = rng.randrange(1, 5), rng.randrange(3, 11)
                words.append(branch(4 * (count + 1)) | 1)
                words.extend(self.data_word() for _ in range(count))
                words.append(mflr(reg))
                if rng.randrange(3):
                    words.append(dform(32, rng.randrange(3, 11), reg, 4 * rng.randrange(count)))
            elif pick == 3:
                words.append(branch(4) | 1 if rng.randrange(2) else bc(20, 31, 4) | 1)
                words.append(mflr(rng.randrange(3, 11)))
            elif pick == 4:
                words.append(branch(host + 4 * rng.randrange(64) - here) | 1)
            elif pick == 5:  # a callee with a computed return; the words after the call can be skipped
                words.append(branch(RESUME_BASE + 4 * rng.randrange(RESUME_COUNT) - here) | 1)
                words.extend(self.plain(3))
            elif pick == 6:  # blrl to a host function
                words.extend(lis_ori(host + 4 * rng.randrange(64)))
                words.append(mtlr(12))
                words.append(BLRL if rng.randrange(3) else bclr(rng.choice((4, 12)), rng.randrange(32)) | 1)
            else:
                words.extend(self.plain(rng.randrange(1, 5)))

        words.append(mflr(27))
        for _ in range(rng.randrange(1, 9)):
            piece(0)
        words.append(mtlr(27))
        ending = rng.randrange(6)
        if ending == 0:
            words.append(branch(host + 4 * rng.randrange(64) - (address + 4 * len(words))))
        elif ending == 1:
            words.extend(lis_ori(host + 4 * rng.randrange(64)))
            words += [(31 << 26) | (12 << 21) | (spr_field(9) << 11) | (467 << 1), bcctr(20, 0)]
        elif ending == 2:
            words += [BLRL, BLR]  # blrl with the entry LR: a return
        else:
            words.append(BLR)
        if rng.randrange(3) == 0:
            words.extend(self.data_word() for _ in range(rng.randrange(1, 4)))  # a data pool
        subs = []
        for sub in range(3):
            subs.append(len(words))
            nests = sub == 0 and rng.randrange(2)
            words.append(mflr(26 - sub))
            for _ in range(rng.randrange(4)):
                piece(1 if nests else 3)
            words.extend(self.plain(rng.randrange(1, 4)))
            words.append(mtlr(26 - sub))
            if rng.randrange(4) == 0:
                # Some of these callees move LR, which must not change where the subroutine returns.
                target = RESUME_BASE + 4 * rng.randrange(RESUME_COUNT) if rng.randrange(2) else host + 4 * rng.randrange(64)
                words.append(branch(target - (address + 4 * len(words))))
            else:
                words.append(BLR)
        for at, sub, conditional in patches:
            offset = 4 * (subs[sub] - at)
            words[at] = (bc(rng.choice((4, 12)), rng.randrange(32), offset) | 1) if conditional else (branch(offset) | 1)
        return words

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
    # A word that does not decode is inline data in a "local" function: the emitter writes a
    # ppc::fatal for it, which nothing reaches. Anywhere else it is a mistake of this generator.
    if kind != "local" and any(insn is None for insn in insns):
        raise RuntimeError("corpus word does not decode: %s" % [hex(w) for w in words])
    addrs = [address + 4 * i for i in range(len(words))]
    # What analyze.py finds for cave code. A branch inside the function is a label. A `bl` to one
    # of the function's own addresses is a local call: its target and its return address are
    # labels and the return address a local return. Any other branch with LK is a call and a
    # branch that leaves the function a tail call, both through ppc::call: with no function
    # table the emitter has no callee to call directly. The stand-in callees with a computed
    # return are known to it only by their deltas, as the callees of a real cave would be.
    inside = set(addrs)
    labels, local_returns = set(), set()
    infos = {RESUME_BASE + 4 * i: SimpleNamespace(computed_returns={resume_delta(RESUME_BASE + 4 * i)})
             for i in range(RESUME_COUNT)}
    for insn in insns:
        target = insn.branch_target if insn is not None and insn.op in ("b", "bc") else None
        if target is None:
            continue
        if insn.lk and target in inside:
            labels |= {target, insn.addr + 4}
            local_returns.add(insn.addr + 4)
        elif insn.lk and target in infos:
            labels |= {insn.addr + 4 + k for k in infos[target].computed_returns if insn.addr + 4 + k in inside}
        elif not insn.lk and target in inside:
            labels.add(target)
    func = SimpleNamespace(addr=address, name=name, size=4 * len(words))
    info = SimpleNamespace(
        func=func, insns=insns, addrs=addrs, addr_set=set(addrs), aliases={}, local_returns=local_returns,
        ctr_targets={}, ctr_calls={}, entries=set(), labels=labels, optional_hooks={}, setjmp_returns=set(),
        optional_text={}, jumptables={},
        has_blrl=any(insn is not None and insn.op == "bclr" and insn.lk for insn in insns), has_bctrl=False)
    emitter = emit.Emitter(None, None, infos, set(), {address: name})
    return name, emitter.emit_function(info)


def build(seed):
    generator = Generator(seed)
    rng = generator.random
    functions = []
    for name, make in generator.forms.items():
        for _ in range(24):
            functions.append((name, BASE, [make(()), BLR]))
    for name, make in generator.wide.items():
        for _ in range(16):
            functions.append((name, BASE + 0x3000, [make(), BLR]))
    for _ in range(300):
        functions.append(("sequence", BASE + 4 * rng.randrange(64), generator.straight(rng.randrange(2, 41)) + [BLR]))
    for _ in range(300):
        functions.append(("mixed", BASE + 0x3000 + 4 * rng.randrange(64), generator.mixed(rng.randrange(2, 41)) + [BLR]))
    for _ in range(300):
        address = BASE + 0x3000 + 4 * rng.randrange(64)
        functions.append(("calling", address, generator.calling(address)))
    for _ in range(600):
        address = BASE + 0x3000 + 4 * rng.randrange(64)
        functions.append(("local", address, generator.local(address)))
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
