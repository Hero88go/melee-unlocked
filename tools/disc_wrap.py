"""Wrap reads of disc pointer slots in DP() (see sourceport/game/include/mu_disc.h).

    python tools/disc_wrap.py [--field NAME ...] [--array NAME ...] FILE [FILE ...]

After a struct field becomes a DISC_PTR slot, every `expr->NAME` or `expr.NAME` that uses it as a
pointer must read it with DP(). This finds each occurrence, walks back over the whole operand
(identifiers, -> and . chains, calls, subscripts, parenthesised casts) and wraps it. Occurrences that
are already wrapped, that are assigned to, or whose address is taken are left alone and listed, so
they can be converted by hand (DISC_SET, DISC_NULL). On the console build DP(x) is x.
"""
import argparse
import re
from pathlib import Path


def operand_start(text, end):
    """Index where the postfix expression ending at `end` (exclusive, just before -> or .) begins."""
    i = end
    while True:
        # trailing ) or ] : skip the balanced group
        while i > 0 and text[i - 1] in ")]":
            close = text[i - 1]
            open_ = "(" if close == ")" else "["
            depth, j = 0, i - 1
            while j >= 0:
                if text[j] == close:
                    depth += 1
                elif text[j] == open_:
                    depth -= 1
                    if depth == 0:
                        break
                j -= 1
            i = j
            # a call or subscript: the callee / array name precedes it
            k = i
            while k > 0 and (text[k - 1].isalnum() or text[k - 1] == "_"):
                k -= 1
            if k == i:
                return i          # a parenthesised primary expression
            i = k
            break
        else:
            k = i
            while k > 0 and (text[k - 1].isalnum() or text[k - 1] == "_"):
                k -= 1
            i = k
        # continue over a preceding member access (which may start its own line)
        j = i
        while j > 0 and text[j - 1] in " \t\r\n":
            j -= 1
        if text[max(0, j - 2):j] == "->":
            i = j - 2
        elif j > 0 and text[j - 1] == "." and not (j > 1 and text[j - 2].isdigit()):
            i = j - 1
        else:
            return i
        while i > 0 and text[i - 1] in " \t\r\n":
            i -= 1


def wrap(text, fields, report, path):
    # Edits are made in place, left to right, so a chain of slots (a->slot1->slot2) nests:
    # the second operand walk sees the DP(...) the first one produced and wraps it again.
    pat = re.compile(r"\s*(->|\.)(" + "|".join(map(re.escape, fields)) + r")\b")
    pos = 0
    while True:
        m = pat.search(text, pos)
        if not m:
            return text
        start = operand_start(text, m.start())
        before = text[max(0, start - 3):start]
        line = text.count("\n", 0, m.start()) + 1
        head = text[max(0, start - 16):start].rstrip()
        continues = re.match(r"\s*(\[|->|\.)", text[m.end():m.end() + 4]) is not None
        if (start == m.start() or head.endswith(("DP(", "DISC_SET(", "DISC_NULL(", "DISC_RAW("))
                or (head.endswith("&") and not continues)):
            report.append(f"{path}:{line}: left alone (already wrapped, address taken or no operand)")
            pos = m.end()
            continue
        if re.match(r"\s*=[^=]", text[m.end():m.end() + 4]):
            report.append(f"{path}:{line}: left alone (assigned)")
            pos = m.end()
            continue
        text = text[:start] + "DP(" + text[start:m.end()] + ")" + text[m.end():]
        pos = m.end() + 4


def wrap_arrays(text, names, report, path):
    """NAME[i] for a global that is an array of slots: each element read becomes DP(NAME[i])."""
    pat = re.compile(r"(?<![\w.>])(" + "|".join(map(re.escape, names)) + r")\s*\[")
    pos = 0
    while True:
        m = pat.search(text, pos)
        if not m:
            return text
        depth, j = 0, m.end() - 1
        while j < len(text):
            depth += {"[": 1, "]": -1}.get(text[j], 0)
            if depth == 0:
                break
            j += 1
        end = j + 1
        head = text[max(0, m.start() - 16):m.start()].rstrip()
        line = text.count("\n", 0, m.start()) + 1
        continues = re.match(r"\s*(\[|->|\.)", text[end:end + 4]) is not None
        if head.endswith(("DP(", "DISC_SET(", "DISC_NULL(", "DISC_RAW(")) or (head.endswith("&") and not continues) \
                or re.match(r"\s*=[^=]", text[end:end + 4]):
            report.append(f"{path}:{line}: left alone (wrapped, address taken or assigned)")
            pos = end
            continue
        text = text[:m.start()] + "DP(" + text[m.start():end] + ")" + text[end:]
        pos = end + 4


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--field", action="append", default=[])
    ap.add_argument("--array", action="append", default=[])
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()
    report = []
    changed = 0
    for f in a.files:
        p = Path(f)
        text = open(p, encoding="utf-8", newline="").read()
        new = text
        if a.array:
            new = wrap_arrays(new, a.array, report, f)
        if a.field:
            new = wrap(new, a.field, report, f)
        if new != text:
            open(p, "w", encoding="utf-8", newline="").write(new)
            changed += 1
    print(f"{changed} files changed")
    for r in report:
        print(r)


if __name__ == "__main__":
    main()
