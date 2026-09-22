"""Gecko code support for the recompiler.

Slippi Dolphin boots Melee with two code tables: `bootloader.gct` (installed by Dolphin at
0x800028B8 and applied once by codehandler.bin) and the main table generated from the enabled
codes of GALE01r2.ini, which the game itself fetches over the Slippi EXI device (commands D3/D4)
and applies with its own in-game handler (types 04, 06, 08, C0, C2). The recompiled code cannot
execute instructions the guest writes into RAM, so this module expresses every code as
instruction-level patches the recompiler applies ahead of time:
  * 32/16/8-bit and string writes are applied to the DOL image;
  * C2 hooks replace the hooked instruction with the cave's instructions (spliced in place);
  * hooks and C0 caves outside any function become synthetic functions.
The guest still runs the real handler at boot, so RAM ends up exactly as under Dolphin.
"""
import re
import struct

CODEHANDLER_BASE = 0x80001800
BOOTLOADER_BASE = 0x800028B8   # codehandler.bin length (4288) - 8, per Gecko::InstallCodeHandler


# Codes the port compiles in but lets the player switch at run time (PC settings). Their table
# entries go last in the GCT so every other cave keeps its address whether they are on or off.
RUNTIME_OPTIONAL = {"Optional: Widescreen 16:9": "widescreen"}

# Codes the port adds to the table itself (they are not in Slippi's code list). Each is always in
# the table, at the end of the part that is never cut, so its cave and data sit in RAM at a fixed
# address; its hook is translated two ways and runs only while the named flag is on.
PORT_CODES = [
    # PAL stock icons: the stock row's root joint at 0.85 scale and y -21, as on PAL (UnclePunch).
    ("Port: PAL Stock Icons", "pal_stock_icons", [
        (0xC22F9A3C, 0x00000007), (0x48000021, 0x7C8802A6), (0x80640000, 0x907D002C),
        (0x907D0030, 0x80640004), (0x907D003C, 0x48000010), (0x4E800021, 0x3F59999A),
        (0xC1A80000, 0x801D0014), (0x60000000, 0x00000000)]),
    # With the row scaled down, a lost stock's icon ends its drop still on screen (its drop is part
    # of the scaled animation), so hide it once the drop reaches its last frame: at 802F84C8, in the
    # lost-stock branch of ifStock_802F8298 where r3 + 521 addresses that icon's drop counter and
    # r27 is the icon joint, call HSD_JObjSetFlagsAll(icon, JOBJ_HIDDEN) when the counter is 9 or
    # more (it becomes 10, the final frame, in this pass). A stock that comes back is unhidden by the
    # game's own loop, which clears the flag on every icon still in play each frame.
    ("Port: PAL Stock Icons, lost stocks leave the screen", "pal_stock_icons", [
        (0xC22F84C8, 0x0000000B),
        (0x38830209, 0x89840000),   # addi r4,r3,521 (the replaced instruction); lbz r12,0(r4)
        (0x280C0009, 0x41800044),   # cmplwi r12,9; blt skip
        (0x7C0802A6, 0x9421FFE0),   # mflr r0; stwu r1,-32(r1)
        (0x90010024, 0x90610008),   # stw r0,36(r1); stw r3,8(r1)
        (0x9081000C, 0x7F63DB78),   # stw r4,12(r1); mr r3,r27
        (0x38800010, 0x3D808037),   # li r4,16 (JOBJ_HIDDEN); lis r12,0x8037
        (0x618C1D9C, 0x7D8903A6),   # ori r12,r12,0x1D9C (HSD_JObjSetFlagsAll); mtctr r12
        (0x4E800421, 0x80610008),   # bctrl; lwz r3,8(r1)
        (0x8081000C, 0x80010024),   # lwz r4,12(r1); lwz r0,36(r1)
        (0x7C0803A6, 0x38210020),   # mtlr r0; addi r1,r1,32
        (0x60000000, 0x00000000)]), # skip: nop; (branch back)
    # No screen shake: Camera_ApplyQuake (cm/camera.c) moves the camera by quake_offset * quake_scale
    # and then zeroes quake_offset. At 8002A104, where r27 is game_camera (0x80452C68), zero
    # quake_offset.x/y (+0xA4/+0xA8) first, so the camera moves by nothing and the game's own
    # bookkeeping (counters, the clear at the end) runs unchanged. r0 is free until 8002A170.
    ("Port: No Screen Shake", "no_screen_shake", [
        (0xC202A104, 0x00000003),
        (0x38000000, 0x901B00A4),   # li r0,0; stw r0,164(r27)
        (0x901B00A8, 0xC07B00AC),   # stw r0,168(r27); lfs f3,172(r27) (the replaced instruction)
        (0x60000000, 0x00000000)]), # nop; (branch back)
]


class GeckoCode:
    def __init__(self, name):
        self.name = name
        self.codes = []      # (address_word, data_word)
        self.enabled = False
        self.optional = RUNTIME_OPTIONAL.get(name)   # flag name when switchable at run time
        self.port_flag = None                        # PORT_CODES: hook gated by this flag


def load_ini(path):
    """Parses [Gecko] and [Gecko_Enabled] like Dolphin's GeckoCodeConfig::LoadCodes."""
    codes, enabled, section, current = [], set(), None, None
    for raw in open(path, encoding="utf-8", errors="replace"):
        line = raw.strip()
        if not line:
            continue
        if line.startswith("["):
            section = line
            current = None
            continue
        if section == "[Gecko_Enabled]":
            if line.startswith("$"):
                enabled.add(line[1:].strip())
            continue
        if section != "[Gecko]":
            continue
        if line.startswith("$"):
            name = line[1:]
            bracket = name.find("[")
            if bracket >= 0:
                name = name[:bracket]
            current = GeckoCode(name.strip())
            codes.append(current)
            continue
        if line.startswith("*") or current is None:
            continue
        m = re.match(r"^([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})", line)
        if m:
            current.codes.append((int(m.group(1), 16), int(m.group(2), 16)))
    for code in codes:
        code.enabled = code.name in enabled or code.optional is not None
    for name, flag, lines in PORT_CODES:
        code = GeckoCode(name)
        code.codes = list(lines)
        code.enabled = True
        code.port_flag = flag
        codes.append(code)
    return codes


def generate_gct(codes, include_optional=True):
    """Generate the Slippi table, with host-gated port codes in a final private suffix."""
    out = bytearray(struct.pack(">II", 0x00D0C0DE, 0x00D0C0DE))
    for code in codes:
        if code.enabled and code.optional is None and code.port_flag is None:
            for a, d in code.codes:
                out += struct.pack(">II", a, d)
    offset = len(out)
    if include_optional:
        for code in codes:
            if code.enabled and code.optional is not None:
                for a, d in code.codes:
                    out += struct.pack(">II", a, d)
    port_offset = len(out)
    for code in codes:
        if code.enabled and code.port_flag is not None:
            for a, d in code.codes:
                out += struct.pack(">II", a, d)
    out += struct.pack(">II", 0xFF000000, 0)
    return bytes(out), offset, port_offset


class Hook:
    __slots__ = ("hook", "cave_addr", "words", "optional")

    def __init__(self, hook, cave_addr, words):
        self.hook, self.cave_addr, self.words = hook, cave_addr, words
        self.optional = None   # flag name when the hook belongs to a run-time optional code


class Cave:
    """A C0 (execute once) cave: a function at cave_addr ending in blr."""
    __slots__ = ("cave_addr", "words")

    def __init__(self, cave_addr, words):
        self.cave_addr, self.words = cave_addr, words


class GctPatches:
    def __init__(self):
        self.writes = []        # (addr, bytes)
        self.hooks = []         # Hook
        self.c0 = []            # Cave
        self.unsupported = []   # (line index, address word, data word)


def parse_gct(data, base_addr):
    """Decodes a GCT located at `base_addr` in guest RAM into patches. The cave address of a
    C2/C0 code is where its instructions live inside the table (the in-game handler branches
    there), so it depends on the table's address."""
    words = struct.unpack(">%dI" % (len(data) // 4), data)
    p = GctPatches()
    i = 0
    while i + 1 < len(words):
        a, b = words[i], words[i + 1]
        t = a >> 24
        addr = 0x80000000 | (a & 0x01FFFFFF)
        if a == 0x00D0C0DE and b == 0x00D0C0DE:
            i += 2
            continue
        if t in (0xF0, 0xFF, 0xE0):
            break
        if t == 0x00:
            count = (b >> 16) + 1
            p.writes.append((addr, bytes([b & 0xFF]) * count))
            i += 2
        elif t == 0x02:
            count = (b >> 16) + 1
            p.writes.append((addr, struct.pack(">H", b & 0xFFFF) * count))
            i += 2
        elif t == 0x04:
            p.writes.append((addr, struct.pack(">I", b)))
            i += 2
        elif t == 0x06:
            n = b
            lines = (n + 7) // 8
            blob = data[(i + 2) * 4:(i + 2) * 4 + n]
            p.writes.append((addr, blob))
            i += 2 + lines * 2
        elif t == 0x08:
            # 08XXXXXX YYYYYYYY TNNNZZZZ VVVVVVVV: T = size (0 byte, 1 half, 2 word), NNN = count - 1,
            # ZZZZ = address step, VVVVVVVV = value step.
            c, d = words[i + 2], words[i + 3]
            size = c >> 28
            count = ((c >> 16) & 0xFFF) + 1
            step = c & 0xFFFF
            vstep = d
            value = b
            blob_addr = addr
            for k in range(count):
                v = (value + k * vstep) & 0xFFFFFFFF
                if size == 0:
                    p.writes.append((blob_addr, bytes([v & 0xFF])))
                elif size == 1:
                    p.writes.append((blob_addr, struct.pack(">H", v & 0xFFFF)))
                else:
                    p.writes.append((blob_addr, struct.pack(">I", v)))
                blob_addr += step
            i += 4
        elif t == 0xC2:
            n = b
            cave_addr = base_addr + (i + 2) * 4
            cave = list(words[i + 2:i + 2 + n * 2])
            # The handler overwrites the last word of the cave with `b hook+4`.
            last_addr = cave_addr + (n * 2 - 1) * 4
            cave[-1] = 0x48000000 | (((addr + 4) - last_addr) & 0x03FFFFFC)
            p.hooks.append(Hook(addr, cave_addr, cave))
            i += 2 + n * 2
        elif t == 0xC0:
            n = b
            cave_addr = base_addr + (i + 2) * 4
            p.c0.append(Cave(cave_addr, list(words[i + 2:i + 2 + n * 2])))
            i += 2 + n * 2
        else:
            p.unsupported.append((i, a, b))
            i += 2
    return p
