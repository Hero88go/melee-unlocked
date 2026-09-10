"""Per-function control-flow analysis: labels, calls, jump tables, extra entry points."""
from gekko import decode
from dol import RAM_BASE


class JumpTable:
    __slots__ = ("bctr_addr", "table_addr", "count", "targets")

    def __init__(self, bctr_addr, table_addr, count, targets):
        self.bctr_addr, self.table_addr, self.count, self.targets = bctr_addr, table_addr, count, targets


class FuncInfo:
    def __init__(self, func):
        self.func = func
        self.insns = []           # decoded instructions (None for undecodable words)
        self.labels = set()       # addresses inside this function that are branch targets
        self.calls = set()        # direct call targets (bl)
        self.tail_targets = set() # `b` targets outside the function
        self.jumptables = {}      # bctr addr -> JumpTable
        self.unresolved_bctr = [] # bctr addresses with no table
        self.bad = []             # undecodable words
        self.has_bctrl = False
        self.has_blrl = False


_WRITERS = ("addi", "addis", "or", "lwz", "lwzx", "lbz", "lhz", "add", "subf", "rlwinm", "mulli",
            "lha", "lfs", "mr", "ori", "oris", "xor", "and", "neg", "extsb", "extsh", "srawi", "slw",
            "srw", "lwzu", "lbzx", "lhzx", "mfspr", "subfic", "mullw", "divw", "divwu", "andi_rc",
            "cntlzw", "lwarx", "nor", "andc", "rlwimi", "lmw")


def _const_value(insns, reg, pos, depth=0):
    """Linear backward scan for the constant value of `reg` before instruction `pos`.
    Follows lis/addi/addis/mr chains (CodeWarrior hoists table bases this way)."""
    if depth > 6 or reg == 0 and depth > 0:
        return None
    m = pos - 1
    while m >= 0:
        ins = insns[m]
        if ins is None:
            m -= 1
            continue
        f = ins.f
        if ins.op == "addis" and f["rd"] == reg:
            if f["ra"] == 0:
                return (f["simm"] << 16) & 0xFFFFFFFF
            v = _const_value(insns, f["ra"], m, depth + 1)
            return None if v is None else (v + (f["simm"] << 16)) & 0xFFFFFFFF
        if ins.op == "addi" and f["rd"] == reg:
            if f["ra"] == 0:
                return f["simm"] & 0xFFFFFFFF
            v = _const_value(insns, f["ra"], m, depth + 1)
            return None if v is None else (v + f["simm"]) & 0xFFFFFFFF
        if ins.op == "or" and f["ra"] == reg and f["rs"] == f["rb"]:
            return _const_value(insns, f["rs"], m, depth + 1)
        if ins.op == "ori" and f["ra"] == reg:
            v = _const_value(insns, f["rs"], m, depth + 1)
            return None if v is None else v | f["uimm"]
        if ins.op in _WRITERS and (f.get("rd") == reg if ins.op not in ("or", "ori", "oris", "xor", "and",
                                   "andc", "nor", "rlwinm", "rlwimi", "extsb", "extsh", "srawi", "slw",
                                   "srw", "cntlzw", "andi_rc") else f.get("ra") == reg):
            return None
        if ins.op == "lmw" and f["rd"] <= reg:
            return None
        m -= 1
    return None


def _match_jumptable(dol, symbols, info, idx):
    """CodeWarrior switch: cmplwi rX, N ; bgt default ; [lis/addi hoisted anywhere above]
    rlwinm rI, rX, 2, 0, 29 ; lwzx rT2, rT, rI ; mtctr rT2 ; bctr."""
    insns = info.insns
    bctr = insns[idx]
    lo = max(0, idx - 24)
    j = idx - 1
    ctr_src = None
    while j >= lo:
        ins = insns[j]
        if ins is not None and ins.op == "mtspr" and ins.f["spr"] == 9:
            ctr_src = ins.f["rs"]
            break
        j -= 1
    if ctr_src is None:
        return None
    k = j - 1
    table_regs = None
    table_off = 0
    while k >= lo:
        ins = insns[k]
        if ins is None:
            k -= 1
            continue
        if ins.op == "lwzx" and ins.f["rd"] == ctr_src:
            table_regs = (ins.f["ra"], ins.f["rb"])
            break
        if ins.op == "lwz" and ins.f["rd"] == ctr_src:
            table_regs = (ins.f["ra"],)
            table_off = ins.f["simm"]
            break
        k -= 1
    if table_regs is None:
        return None
    base = None
    for reg in table_regs:
        v = _const_value(insns, reg, k)
        if v is not None and dol.in_ram((v + table_off) & 0xFFFFFFFF) and not dol.in_text(v):
            base = (v + table_off) & 0xFFFFFFFF
            break
    if base is None:
        return None
    count = None
    m = idx - 1
    while m >= max(0, idx - 40):
        ins = insns[m]
        if ins is not None and ins.op in ("cmpli", "cmpi"):
            count = ins.f["uimm"] + 1 if ins.op == "cmpli" else ins.f["simm"] + 1
            break
        m -= 1
    func = info.func
    targets = []
    limit = count if count and count > 0 else 4096
    for n in range(limit):
        a = base + n * 4
        if not dol.in_ram(a):
            break
        t = dol.u32(a)
        if not (func.addr <= t < func.end) or (t & 3):
            break
        targets.append(t)
    if not targets:
        return None
    return JumpTable(bctr.addr, base, len(targets), targets)


def _data_scan_tables(dol, func, claimed):
    """Fallback: find runs of >=2 consecutive .data words that point into `func`.
    Because the emitter switches on the actual CTR value, over-approximating the
    target set only adds labels; it never changes behaviour."""
    targets = set()
    for s in dol.sections:
        if s.kind != "data":
            continue
        run = []
        for a in range(s.addr, s.end - 3, 4):
            t = dol.u32(a)
            if func.addr <= t < func.end and not (t & 3):
                run.append((a, t))
            else:
                if len(run) >= 2 and run[0][0] not in claimed:
                    targets.update(t for _, t in run)
                run = []
        if len(run) >= 2 and run[0][0] not in claimed:
            targets.update(t for _, t in run)
    return sorted(targets)


def analyze_function(dol, symbols, func):
    info = FuncInfo(func)
    for a in range(func.addr, func.end, 4):
        w = dol.u32(a)
        ins = decode(a, w)
        if ins is None:
            info.bad.append((a, w))
        info.insns.append(ins)
    for idx, ins in enumerate(info.insns):
        if ins is None:
            continue
        if ins.op == "b":
            t = ins.branch_target
            if ins.lk:
                info.calls.add(t)
            elif func.addr <= t < func.end:
                info.labels.add(t)
            else:
                info.tail_targets.add(t)
        elif ins.op == "bc":
            t = ins.branch_target
            if ins.lk:
                info.calls.add(t)
            elif func.addr <= t < func.end:
                info.labels.add(t)
            else:
                info.tail_targets.add(t)
        elif ins.op == "bcctr":
            if ins.lk:
                info.has_bctrl = True
            else:
                jt = _match_jumptable(dol, symbols, info, idx)
                if jt is None:
                    claimed = {t.table_addr for t in info.jumptables.values()}
                    targets = _data_scan_tables(dol, func, claimed)
                    if targets:
                        jt = JumpTable(ins.addr, 0, len(targets), targets)
                if jt:
                    info.jumptables[ins.addr] = jt
                    info.labels.update(jt.targets)
                else:
                    info.unresolved_bctr.append(ins.addr)
        elif ins.op == "bclr" and ins.lk:
            info.has_blrl = True
    return info


def analyze_all(dol, symbols):
    infos = {}
    for func in symbols.functions:
        if not dol.in_text(func.addr):
            continue
        infos[func.addr] = analyze_function(dol, symbols, func)
    # Extra entry points: targets of calls/tail branches that land mid-function.
    extra_entries = {}  # containing function addr -> set(entry addrs)
    for info in infos.values():
        for t in list(info.calls) + list(info.tail_targets):
            if t in infos:
                continue
            owner = symbols.containing(t)
            if owner is None:
                extra_entries.setdefault(None, set()).add(t)
            else:
                extra_entries.setdefault(owner.addr, set()).add(t)
    return infos, extra_entries
