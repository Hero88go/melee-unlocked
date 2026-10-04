#!/usr/bin/env python3
"""How much of a piece of mod code the copy-and-patch planner accepts.

Read-only: it reads the inputs, asks the planner (the ppc_leaf_plan_probe program built in the
prototype build folder, which is leaf_translation_plan.h and nothing else) and prints a report.
It translates nothing, runs no guest code and writes only the report files named on the command
line.

Inputs, any number of each:
  --blob FILE[@ADDRESS]   raw big-endian PowerPC words, loaded at ADDRESS (default 0x80400000)
  --gecko FILE            a Gecko code list as text: an .ini with $Name lines, or bare
                          "XXXXXXXX YYYYYYYY" lines
  --gct FILE              a binary Gecko table (00D0C0DE 00D0C0DE ... F0000000 00000000)
  --mcm FILE_OR_FOLDER    a code library in the Melee Code Manager text format. Only bodies given
                          as hex are measured; a body given as assembly source would need an
                          assembler and is counted as "source text, not measured"

Reports name a code by its hook address only, never by its title or author.

What counts as a function:
  * a C2 (insert assembly) code: its instructions, with the branch back to the game that the
    code handler writes into its last word;
  * a C0 (execute) code;
  * in a raw blob, the words from one function start to the next. A function starts at the
    blob's first word, at the target of a `bl` inside the blob (unless that `bl` is the inline
    data idiom: a jump over data to an mflr of a register other than r0, or `bl +4`), and at a
    prologue (`mflr r0` followed within three words by `stwu r1` or `stw r0,4(r1)`, or a `stwu
    r1,-N(r1)` that no such mflr precedes).
Words the planner finds no path to from a function's entry are not counted as that function's
instructions. They are inline data, data pools after a return, dead code, or code that is only
reached through a pointer (a callback, a table of branches). To tell these apart each such run
of words is tried as a function of its own: a run that plans is counted as one more function and
its reached words as instructions; a run that does not plan is reported as words that are data or
unplanned code, with what refused it listed under the same headings.
04 and 06 codes that write into the game's own code are single patched instructions: they are
counted in the instruction totals only, because the function they belong to is the game's.
"""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import collections
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True  # Leave port/recomp exactly as it is.
sys.path.insert(0, str(ROOT / "port" / "recomp"))
import gekko  # noqa: E402

TEXT_LOW, TEXT_HIGH = 0x80003100, 0x803B7240   # the game's own code (NTSC 1.02)
CAVE_BASE = 0x80400000                          # where caves are imagined to sit, for branch math
BLR = 0x4E800020
WHOLE = {"InvalidRange", "FallsOffEnd"}


def mnemonic(address, word):
    insn = gekko.decode(address, word)
    if insn is None:
        return "(not an instruction: %08X)" % word if (word >> 26) in (0, 1, 2, 5, 6, 9, 22, 30, 58, 62) \
            else "(op %d, not decoded)" % (word >> 26)
    name = insn.op
    if name in ("b", "bc", "bclr", "bcctr"):
        name = {"b": "b", "bc": "bc", "bclr": "bclr", "bcctr": "bcctr"}[name] + ("l" if insn.lk else "") + \
               ("a" if getattr(insn, "aa", 0) else "")
    elif name in ("mfspr", "mtspr"):
        name += " %d" % insn.f["spr"]
    elif insn.rc and not name.endswith("_rc"):
        name += "."
    if getattr(insn, "oe", 0):
        name += " (OE)"
    return name


def branch_to(here, target):
    return 0x48000000 | ((target - here) & 0x03FFFFFC)


def unconditional(word):
    op = word >> 26
    if op == 18 and not word & 1:
        return True
    if op == 19:
        xo, bo = (word >> 1) & 0x3FF, (word >> 21) & 31
        return (xo in (16, 528) and not word & 1 and (bo & 20) == 20) or xo == 50
    return False


def forward_target(address, word):
    """The target of a relative forward branch without link, or None."""
    op = word >> 26
    if word & 3:
        return None
    if op == 18:
        li = word & 0x03FFFFFC
        li -= 0x04000000 if li & 0x02000000 else 0
        return address + li if li > 0 else None
    if op == 16:
        bd = word & 0xFFFC
        bd -= 0x10000 if bd & 0x8000 else 0
        return address + bd if bd > 0 else None
    return None


MFLR_R0, STW_R0_4_R1 = 0x7C0802A6, 0x90010004


def is_mflr(word):
    return word & 0xFC1FFFFF == 0x7C0802A6


def split_blob(words, base):
    """Function starts from `bl` targets and prologues; a function runs to the next start."""
    count = len(words)
    starts = {0}
    for i, word in enumerate(words):
        if word >> 26 == 18 and word & 3 == 1:                       # bl, relative
            li = word & 0x03FFFFFC
            li -= 0x04000000 if li & 0x02000000 else 0
            to = i + li // 4
            if not 0 <= to < count or to == i + 1:
                continue                                              # outside the blob, or bl +4
            if to > i and is_mflr(words[to]) and (words[to] >> 21) & 31 != 0:
                continue                                              # over inline data to mflr rX
            starts.add(to)
        elif word == MFLR_R0:
            if any(w >> 16 == 0x9421 or w == STW_R0_4_R1 for w in words[i + 1:i + 4]):
                starts.add(i)
        elif word >> 16 == 0x9421 and word & 0x8000:                  # stwu r1,-N(r1)
            if MFLR_R0 not in words[max(0, i - 3):i]:
                starts.add(i)
    ordered = sorted(starts)
    return [(base + 4 * a, words[a:b]) for a, b in zip(ordered, ordered[1:] + [count])]


def gecko_pairs_from_text(text):
    """[(name, [words...])] from an .ini style list."""
    codes, name, words = [], "(unnamed)", []
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("$"):
            if words:
                codes.append((name, words))
            name, words = line[1:].strip(), []
            continue
        parts = line.split()
        if len(parts) == 2 and all(len(p) == 8 for p in parts):
            try:
                words += [int(parts[0], 16), int(parts[1], 16)]
            except ValueError:
                pass
    if words:
        codes.append((name, words))
    return codes


def gecko_units(name, words):
    """(functions, patches, skipped lines) of one code's word stream."""
    functions, patches, skipped = [], [], 0
    i = 0
    while i + 1 < len(words):
        a, b = words[i], words[i + 1]
        i += 2
        kind = a & 0xFE000000
        address = 0x80000000 | (a & 0x01FFFFFF)
        if kind == 0xC2000000:
            body = words[i:i + 2 * b]
            i += 2 * b
            if not body:
                continue
            cave = CAVE_BASE + 4 * (sum(len(f[2]) for f in functions) + 16 * len(functions))
            # The code handler turns the last word into the branch back to the game.
            body = body[:-1] + [branch_to(cave + 4 * (len(body) - 1), address + 4)]
            functions.append(("cave @ %08X" % address, cave, body))
        elif kind == 0xC0000000:
            body = words[i:i + 2 * b]
            i += 2 * b
            while body and body[-1] == 0:
                body.pop()
            if body:
                functions.append(("execute code (C0)", CAVE_BASE + 0x100000, body))
        elif kind == 0x04000000:
            if TEXT_LOW <= address < TEXT_HIGH:
                patches.append((address, b))
            else:
                skipped += 1
        elif kind == 0x06000000:
            count = (b + 7) // 8
            data = words[i:i + 2 * count]
            i += 2 * count
            if TEXT_LOW <= address < TEXT_HIGH and b % 4 == 0 and address % 4 == 0:
                patches += [(address + 4 * k, w) for k, w in enumerate(data[:b // 4])]
            else:
                skipped += count + 1
        elif a == 0xF0000000 or a == 0x00D0C0DE:
            continue
        else:
            skipped += 1
    return functions, patches, skipped


MCM_HEAD = re.compile(r"^\s*(?:NTSC\s*1\.02|1\.02)\s*-+\s*(0x[0-9A-Fa-f]+)\s*-+\s*(.*)$")
MCM_FUNC = re.compile(r"^\s*<([^<>]+)>\s*(.*)$")
HEX_LINE = re.compile(r"^[0-9A-Fa-f\s]+$")


def mcm_units(text):
    """(functions, patches, bodies given as source) of one library file."""
    functions, patches, source = [], [], 0
    for mod in text.split("-==-"):
        lines = [line for line in mod.splitlines()]
        name = next((line.strip() for line in lines if line.strip()), "(unnamed)")
        blocks, block = [], None
        for line in lines:
            head = MCM_HEAD.match(line)
            func = None if head else MCM_FUNC.match(line)
            if head:
                block = {"address": int(head.group(1), 16), "rest": head.group(2).split("#")[0].strip(), "body": []}
                blocks.append(block)
            elif func and not line.strip().startswith("<<"):
                block = {"address": None, "rest": func.group(1), "body": []}
                blocks.append(block)
            elif block is not None:
                block["body"].append(line.split("#")[0].strip())
        for block in blocks:
            body = [line for line in block["body"] if line]
            hexed = all(HEX_LINE.match(line) for line in body)
            digits = "".join(line.replace(" ", "") for line in body) if hexed else ""
            words = [int(digits[i:i + 8], 16) for i in range(0, len(digits) - 7, 8)]
            address = block["address"]
            if address is not None and address < 0x80000000:
                address = TEXT_LOW + (address & 0x3FFFFC)  # A file offset: only the branch back uses it.
            if address is None:                          # a standalone <function>
                if not hexed or len(digits) % 8:
                    source += 1
                elif words:
                    functions.append(("standalone function", CAVE_BASE, words))
            elif "->" in block["rest"] and block["rest"].split("->", 1)[1].strip().lower().startswith("branch"):
                if not hexed or len(digits) % 8 or not words:
                    source += 1
                    continue
                # The last word is the placeholder the manager turns into the branch back.
                if words[-1] in (0x48000000, 0):
                    words[-1] = branch_to(CAVE_BASE + 4 * (len(words) - 1), address + 4)
                functions.append(("cave @ %08X" % address, CAVE_BASE, words))
            elif "->" in block["rest"]:
                new = re.sub(r"[^0-9A-Fa-f]", "", block["rest"].split("->", 1)[1]) + digits
                if TEXT_LOW <= address < TEXT_HIGH and len(new) % 8 == 0 and address % 4 == 0:
                    patches += [(address + i // 2, int(new[i:i + 8], 16)) for i in range(0, len(new), 8)]
    return functions, patches, source


def probe(path, functions):
    """[(ok, [(index, reason)])] for [(address, words)]."""
    if not functions:
        return []
    text = "".join("%08X %s\n" % (address, " ".join("%08X" % w for w in words)) for address, words in functions)
    out = subprocess.run([str(path)], input=text, capture_output=True, text=True, check=True).stdout.splitlines()
    if len(out) != len(functions):
        raise RuntimeError("the planner probe answered %d of %d functions" % (len(out), len(functions)))
    results = []
    for line in out:
        parts = line.split()
        ok = parts[0] == "ok"
        rest = parts[3:] if ok else parts[2:]
        runs = [tuple(int(x) for x in p[1:].split("-")) for p in rest if p.startswith("u")]
        refused = [(int(p.split(":")[0]), p.split(":")[1]) for p in rest if not p.startswith("u")]
        results.append((ok, refused, runs))
    return results


class Report:
    def __init__(self, label):
        self.label = label
        self.functions = self.accepted_functions = 0
        self.instructions = self.accepted_instructions = 0
        self.patches = self.accepted_patches = 0
        self.skipped = 0
        self.unreached = 0           # words in runs that do not plan as a function either
        self.pointer_functions = 0   # runs that plan as a function: reached only through a pointer
        self.source = 0
        self.forms = collections.Counter()           # (mnemonic, reason) of refused instructions
        self.reasons = collections.Counter()
        self.whole = collections.Counter()            # function-level refusals
        self.blockers = collections.Counter()         # functions refused for exactly one kind of form
        self.refused_functions = []

    def add_function(self, name, address, words, ok, refused, unreached):
        self.functions += 1
        self.instructions += len(words) - unreached
        bad = {index for index, reason in refused if reason not in WHOLE}
        self.accepted_instructions += len(words) - unreached - len(bad)
        if ok:
            self.accepted_functions += 1
            return
        kinds = set()
        for index, reason in refused:
            if reason in WHOLE:
                self.whole[reason] += 1
                kinds.add(reason)
                continue
            form = mnemonic(address + 4 * index, words[index])
            self.forms[(form, reason)] += 1
            self.reasons[reason] += 1
            kinds.add(form)
        if len(kinds) == 1:
            self.blockers[next(iter(kinds))] += 1
        self.refused_functions.append((name, len(words), sorted(kinds)))

    def add_patch(self, address, word, ok, refused):
        self.patches += 1
        self.instructions += 1
        if not any(reason not in WHOLE for _, reason in refused):
            self.accepted_patches += 1
            self.accepted_instructions += 1
        else:
            for _, reason in refused:
                if reason not in WHOLE:
                    self.forms[(mnemonic(address, word), reason)] += 1
                    self.reasons[reason] += 1

    def lines(self, top):
        def share(part, whole):
            return "%d of %d (%.1f%%)" % (part, whole, 100.0 * part / whole) if whole else "none"
        out = ["== %s ==" % self.label,
               "instructions accepted: " + share(self.accepted_instructions, self.instructions),
               "whole functions accepted: " + share(self.accepted_functions, self.functions)]
        if self.patches:
            out.append("  of the instructions, single patched instructions in game code: " +
                       share(self.accepted_patches, self.patches))
        if self.pointer_functions:
            out.append("  of the functions, runs no branch reaches that plan as functions of their own: %d"
                       % self.pointer_functions)
        if self.unreached:
            out.append("words no path reaches and that do not plan as a function (data, or unplanned code): %d"
                       % self.unreached)
        if self.skipped:
            out.append("code list lines that are not code (data writes, other code types): %d" % self.skipped)
        if self.source:
            out.append("code bodies given as assembly source, not measured: %d" % self.source)
        if self.whole:
            out.append("functions refused as a whole: " + ", ".join("%s %d" % kv for kv in self.whole.most_common()))
        if self.reasons:
            out.append("refused instructions by reason:")
            out += ["  %6d  %s" % (n, reason) for reason, n in self.reasons.most_common()]
            out.append("refused instructions by form (top %d):" % top)
            out += ["  %6d  %-28s %s" % (n, form, reason) for (form, reason), n in self.forms.most_common(top)]
        if self.blockers:
            out.append("functions that only one form keeps out (top %d):" % top)
            out += ["  %6d  %s" % (n, form) for form, n in self.blockers.most_common(top)]
        return out

    def as_json(self):
        return {"label": self.label, "functions": self.functions, "functions_accepted": self.accepted_functions,
                "instructions": self.instructions, "instructions_accepted": self.accepted_instructions,
                "patched_instructions": self.patches, "patched_instructions_accepted": self.accepted_patches,
                "unreached_words": self.unreached, "pointer_functions": self.pointer_functions, "not_code_lines": self.skipped, "source_bodies_not_measured": self.source, "whole_function_refusals": dict(self.whole),
                "refused_by_reason": dict(self.reasons),
                "refused_by_form": [{"form": f, "reason": r, "count": n} for (f, r), n in self.forms.most_common()],
                "single_blocker": dict(self.blockers),
                "refused_functions": [{"name": n, "words": w, "forms": k} for n, w, k in self.refused_functions]}


def measure(report, probe_path, functions, patches):
    results = probe(probe_path, [(address, words) for _, address, words in functions])
    pending = []
    for (name, address, words), (ok, refused, runs) in zip(functions, results):
        report.add_function(name, address, words, ok, refused, sum(b - a + 1 for a, b in runs))
        pending += [(address + 4 * a, words[a:b + 1]) for a, b in runs]
    # Each run of unreached words as a function of its own, and the runs it leaves in turn.
    for _ in range(64):
        if not pending:
            break
        results, following = probe(probe_path, pending), []
        for (address, words), (ok, refused, runs) in zip(pending, results):
            if ok:
                report.pointer_functions += 1
                report.add_function("run at %08X" % address, address, words, True, [], sum(b - a + 1 for a, b in runs))
                following += [(address + 4 * a, words[a:b + 1]) for a, b in runs]
                continue
            # It does not plan from its first word. A run of data often has code after it: try
            # again from the word after the first one that refused, and count what came before.
            bad = min((index for index, reason in refused if reason not in WHOLE), default=None)
            unreached = sum(b - a + 1 for a, b in runs)
            if bad is None or not mnemonic(address + 4 * bad, words[bad]).startswith("("):
                # Real instructions that the planner refuses, or code that runs off its end.
                report.add_function("run at %08X" % address, address, words, False, refused, unreached)
                following += [(address + 4 * a, words[a:b + 1]) for a, b in runs]
            elif bad + 1 >= len(words):
                report.unreached += len(words)
            else:
                report.unreached += bad + 1
                following.append((address + 4 * (bad + 1), words[bad + 1:]))
        pending = following
    else:
        report.unreached += sum(len(words) for _, words in pending)
    results = probe(probe_path, [(address, [word, BLR]) for address, word in patches])
    for (address, word), (ok, refused, _) in zip(patches, results):
        report.add_patch(address, word, ok, [(i, r) for i, r in refused if i == 0])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--blob", action="append", default=[], metavar="FILE[@ADDRESS]")
    parser.add_argument("--gecko", action="append", default=[], type=Path)
    parser.add_argument("--gct", action="append", default=[], type=Path)
    parser.add_argument("--mcm", action="append", default=[], type=Path)
    parser.add_argument("--probe", type=Path,
                        default=ROOT / "build-ppc-stencils" / "Release" / "ppc_leaf_plan_probe.exe")
    parser.add_argument("--top", type=int, default=25)
    parser.add_argument("--out", type=Path, help="write the text report here as well")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    if not args.probe.exists():
        sys.exit("planner probe not found: %s (build the prototype folder first)" % args.probe)
    reports = []
    for item in args.blob:
        name, _, at = item.partition("@")
        base = int(at, 16) if at else CAVE_BASE
        data = Path(name).read_bytes()
        words = list(struct.unpack(">%dI" % (len(data) // 4), data[:len(data) // 4 * 4]))
        report = Report("%s (raw blob at %08X, %d words)" % (Path(name).name, base, len(words)))
        measure(report, args.probe, [("%08X" % address, address, body) for address, body in split_blob(words, base)], [])
        reports.append(report)
    lists = [(path, gecko_pairs_from_text(path.read_text(encoding="utf-8", errors="replace"))) for path in args.gecko]
    for path in args.gct:
        data = path.read_bytes()
        words = list(struct.unpack(">%dI" % (len(data) // 4), data[:len(data) // 4 * 4]))
        lists.append((path, [(path.name, words[2:] if words[:2] == [0x00D0C0DE, 0x00D0C0DE] else words)]))
    for path, codes in lists:
        report = Report("%s (Gecko code list, %d codes)" % (path.name, len(codes)))
        functions, patches = [], []
        for name, words in codes:
            f, p, skipped = gecko_units(name, words)
            # Give every cave its own imagined address.
            for label, _, body in f:
                address = CAVE_BASE + 4 * sum(len(x[2]) + 4 for x in functions)
                if body and (body[-1] >> 26) == 18 and not body[-1] & 3 and "(C0)" not in label:
                    # Re-aim the branch back from the address it was first computed for.
                    hook = int(label.rsplit("@ ", 1)[1], 16)
                    body = body[:-1] + [branch_to(address + 4 * (len(body) - 1), hook + 4)]
                functions.append((label, address, body))
            patches += p
            report.skipped += skipped
        measure(report, args.probe, functions, patches)
        reports.append(report)
    for path in args.mcm:
        files = sorted(path.rglob("*.txt")) if path.is_dir() else [path]
        report = Report("%s (code library, %d files)" % (path.name, len(files)))
        functions, patches = [], []
        for file in files:
            f, p, source = mcm_units(file.read_text(encoding="utf-8", errors="replace"))
            functions += f
            patches += p
            report.source += source
        measure(report, args.probe, functions, patches)
        reports.append(report)
    lines = []
    for report in reports:
        lines += report.lines(args.top) + [""]
    if len(reports) > 1:
        total = Report("all inputs together")
        for report in reports:
            for field in ("functions", "accepted_functions", "instructions", "accepted_instructions", "patches",
                          "accepted_patches", "skipped", "source", "unreached", "pointer_functions"):
                setattr(total, field, getattr(total, field) + getattr(report, field))
            for field in ("forms", "reasons", "whole", "blockers"):
                getattr(total, field).update(getattr(report, field))
        lines += total.lines(args.top) + [""]
        reports.append(total)
    text = "\n".join(lines)
    print(text)
    if args.out:
        args.out.write_text(text, encoding="utf-8")
    if args.json:
        args.json.write_text(json.dumps([report.as_json() for report in reports], indent=1) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
