"""Function discovery for DOL images the decomp symbol map does not describe.

recomp.py translates the functions listed in GALE01_symbols.txt. A modded DOL -- an m-ex
export (ACE, Akaneia, any MexManager build), a Gecko-to-DOL injection, a custom decomp build --
carries code the map knows nothing about. Without this pass every one of those functions is
left out of the translation and ends up in the run-time interpreter (ppc::call falls back to
interp.cpp), which is correct but slow, and only works for code reached through a call.

This pass finds that code the way a disassembler would:

  seeds   the DOL entry point, every static branch out of known code that lands in text the
          symbol map does not cover, and (optionally) word-aligned pointers into that text,
          which is how m-ex hands fighter/stage callbacks to the game
  walk    follow the control flow of each candidate until it returns, recording the highest
          address reached; calls found on the way become new candidates (repeat to a fixpoint)
  extend  absorb a short tail after the last reached instruction, so jump-table arms and
          blocks only reachable through a computed branch stay inside the function

Discovered functions are registered in the SymbolMap as disc_<addr>, which is all analyze_all
needs to translate them. False positives are harmless: a function nothing calls is dead C++,
and a word that does not decode becomes a run-time fatal only if it is ever executed.
"""
import bisect

from gekko import decode
from symbols import Function

MAX_FUNC_SIZE = 0x10000   # a Melee function is far smaller; this only bounds a runaway walk
TAIL_EXTEND = 0x200       # bytes absorbed after the last reached instruction (jump-table arms)


def _uncovered(dol, starts, ends, addr):
    """True when `addr` is text that no known function already covers."""
    if not dol.in_text(addr) or addr & 3:
        return False
    i = bisect.bisect_right(starts, addr) - 1
    return not (i >= 0 and addr < ends[i])


def _section_end(dol, addr):
    for a, e in dol.text_ranges:
        if a <= addr < e:
            return e
    return addr


def _trace(dol, start, limit_end):
    """Walks control flow from `start`. Returns (end address, call targets, undecodable words)."""
    visited = set()
    stack = [start]
    high = start
    calls = set()
    bad = 0
    cap = min(limit_end, start + MAX_FUNC_SIZE)
    while stack:
        a = stack.pop()
        while True:
            if a in visited or a < start or a >= cap:
                break
            visited.add(a)
            if a > high:
                high = a
            ins = decode(a, dol.u32(a))
            if ins is None:
                bad += 1
                break
            op = ins.op
            if op == "b":
                target = ins.branch_target
                if ins.lk:                       # bl: call, execution continues after it
                    calls.add(target)
                    a += 4
                    continue
                if start <= target < cap:        # local jump
                    a = target
                    continue
                calls.add(target)                # tail call / branch into another function
                break
            if op == "bc":
                target = ins.branch_target
                if ins.lk:
                    calls.add(target)
                    a += 4
                    continue
                if start <= target < cap:
                    stack.append(target)
                else:
                    calls.add(target)
                if (ins.f["bo"] & 0x14) == 0x14:  # bc with no condition: an unconditional jump
                    break
                a += 4
                continue
            if op in ("bclr", "bcctr"):
                # blr / bctr end the path; the conditional forms and the linked forms (blrl,
                # bctrl -- a call through a register) fall through to the next instruction.
                if ins.lk or (ins.f["bo"] & 0x14) != 0x14:
                    a += 4
                    continue
                break
            if op in ("rfi", "sc"):
                break
            a += 4
    return high + 4, calls, bad


def _plausible(dol, addr):
    """A pointer seed is only believed when the first two words decode."""
    end = _section_end(dol, addr)
    if addr + 8 > end:
        return False
    return decode(addr, dol.u32(addr)) is not None and decode(addr + 4, dol.u32(addr + 4)) is not None


def discover(dol, symbols, scan_pointers=True, log=print):
    """Registers every function found outside the symbol map. Returns the list of new Functions."""
    known = [f for f in symbols.functions if dol.in_text(f.addr)]
    starts = [f.addr for f in known]
    ends = [f.end for f in known]
    covered_bytes = sum(e - s for s, e in zip(starts, ends))
    text_bytes = sum(e - a for a, e in dol.text_ranges)

    seeds = set()
    if _uncovered(dol, starts, ends, dol.entry):
        seeds.add(dol.entry)
    # Every static branch anywhere in text whose target is text the map does not cover: this is
    # how an m-ex build enters its own code, both from patched vanilla functions and internally.
    for section_start, section_end in dol.text_ranges:
        for a in range(section_start, section_end, 4):
            ins = decode(a, dol.u32(a))
            if ins is None or ins.op not in ("b", "bc"):
                continue
            target = ins.branch_target
            if target is not None and _uncovered(dol, starts, ends, target):
                seeds.add(target)
    branch_seeds = len(seeds)

    pointer_seeds = 0
    if scan_pointers:
        # Function pointers in data: m-ex fighter/stage tables, callback tables, vtables.
        for section in dol.sections:
            for a in range(section.addr, section.end, 4):
                value = dol.u32(a)
                if value in seeds or not _uncovered(dol, starts, ends, value):
                    continue
                if _plausible(dol, value):
                    seeds.add(value)
                    pointer_seeds += 1

    # Phase 1: the set of function starts, walking each candidate and following its calls.
    traced = {}      # start -> address just past the last instruction reached
    worklist = sorted(seeds)
    bad_words = 0
    while worklist:
        addr = worklist.pop()
        if addr in traced or not _uncovered(dol, starts, ends, addr):
            continue
        end, calls, bad = _trace(dol, addr, _section_end(dol, addr))
        bad_words += bad
        traced[addr] = max(end, addr + 4)
        for target in calls:
            if target not in traced and _uncovered(dol, starts, ends, target):
                worklist.append(target)

    # Phase 2: sizes. A function ends where the next one starts (known or discovered), at the
    # latest; the short tail past the last instruction reached keeps jump-table arms and blocks
    # only reachable through a computed branch inside the function that owns them.
    found = []
    ordered = sorted(traced)
    for i, addr in enumerate(ordered):
        limit = _section_end(dol, addr)
        if i + 1 < len(ordered) and ordered[i + 1] < limit:
            limit = ordered[i + 1]
        j = bisect.bisect_right(starts, addr)
        if j < len(starts) and starts[j] < limit:
            limit = starts[j]
        end = min(traced[addr] + TAIL_EXTEND, limit)
        func = Function("disc_%08X" % addr, addr, max(end - addr, 4), ".text", "global")
        symbols.add_function(func)
        found.append(func)

    if log:
        new_bytes = sum(f.size for f in found)
        log("discover: %d functions, %d bytes (%d branch seeds, %d pointer seeds, %d undecodable words)"
            % (len(found), new_bytes, branch_seeds, pointer_seeds, bad_words))
        log("discover: text %d bytes, symbol map covers %d, discovery covers %d, %d left for the interpreter"
            % (text_bytes, covered_bytes, new_bytes, text_bytes - covered_bytes - new_bytes))
    return found
