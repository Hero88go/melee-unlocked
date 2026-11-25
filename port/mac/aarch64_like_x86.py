#!/usr/bin/env python3
"""Compiler launcher for the AArch64 game library: make AArch64 GCC's output behave like x86-64 GCC's
where the game depends on the difference. Used as CMAKE_C_COMPILER_LAUNCHER
(sourceport/game/CMakeLists.txt). Two things differ.

Float to unsigned conversion. C leaves a negative or too large value undefined, and the game converts
them anyway: Nana's input buffer stores Popo's stick as (u8)(128.0F * x), which is negative for every
stick held left or down. The console (fctiwz, then the low bits) and x86-64 GCC (cvttss2si into a
wider signed register, then the low bits) keep the low bits of the signed result, so -1.0 gives
0x80. AArch64 GCC emits FCVTZU, which saturates to 0: Nana ignored Popo's left and down, and every
Ice Climbers game desynced against x86-64. The unit is compiled to assembly first, and each FCVTZU
becomes x86-64's conversion: signed and 64 bits wide, then the low bits.
  fcvtzu wD, sN|dN[, #f]  ->  fcvtzs xD, sN|dN[, #f]; mov wD, wD   (the W write zero-extends, as before)
  fcvtzu xD, sN|dN[, #f]  ->  fcvtzs xD, sN|dN[, #f]
  fcvtzu sD, sN           ->  fcvt dD, sN; fcvtzs dD, dD          (the low 32 bits land in sD)
These agree with x86-64 for every value from -2^63 to 2^63 (except that x86-64 converts to a u8 or
u16 through 32 bits, so the two differ beyond +-2^31 there, which no game value reaches) and set no
flags. A form not listed stops the build. Signed conversions (FCVTZS) still differ for NaN and for
values beyond the signed range: x86-64 gives the most negative value, AArch64 saturates.

Static data alignment. Optimising x86-64 GCC aligns every static array or struct of 32 bytes or more
to 32 bytes (ix86_data_alignment). The game has come to rely on that: buffers the console DMAs into
are asserted to be 32-byte aligned (HSD_DevComRequest) and addresses are rounded with & ~31, yet most
are declared without an alignment of their own. AArch64 GCC aligns them to 8, and its cc1 takes no
plugins. The AArch64 build compiles with -fdata-sections -fno-section-anchors, so every static
object sits in a section of its own and no code depends on where its neighbours are. After the
object is written, each such data section of 32 bytes or more gets alignment 32, which is where the
linker then places it. Merged constant and string sections hold many small items and stay as they are.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import os
import re
import struct
import subprocess
import sys

ALIGN = 32
SHF_ALLOC, SHF_MERGE, SHF_STRINGS = 0x2, 0x10, 0x20
PREFIXES = ('.data.', '.bss.', '.rodata.', '.data.rel.ro.', '.data.rel.')
SOURCES = ('.c', '.cc', '.cpp', '.cxx')

FCVTZU = re.compile(r'^(\s+)fcvtzu\t(.*)$')
TO_W = re.compile(r'w(\d+), ([sd]\d+)(, #\d+)?')
TO_X = re.compile(r'x(\d+), ([sd]\d+)(, #\d+)?')
TO_S = re.compile(r's(\d+), s(\d+)')


def x86_conversions(assembly):
    """Rewrites each fcvtzu in GCC's assembly text as above. Returns (text, count)."""
    lines, count = [], 0
    for line in assembly.split('\n'):
        m = FCVTZU.match(line)
        if not m:
            lines.append(line)
            continue
        indent, operands = m.groups()
        if w := TO_W.fullmatch(operands):
            d, n, fixed = w.group(1), w.group(2), w.group(3) or ''
            lines += [f'{indent}fcvtzs\tx{d}, {n}{fixed}', f'{indent}mov\tw{d}, w{d}']
        elif x := TO_X.fullmatch(operands):
            lines.append(f'{indent}fcvtzs\tx{x.group(1)}, {x.group(2)}{x.group(3) or ""}')
        elif s := TO_S.fullmatch(operands):
            d, n = s.groups()
            lines += [f'{indent}fcvt\td{d}, s{n}', f'{indent}fcvtzs\td{d}, d{d}']
        else:
            raise SystemExit(f'aarch64_like_x86: no x86-64 equivalent for "fcvtzu {operands}"')
        count += 1
    return '\n'.join(lines), count


def raise_alignment(path):
    with open(path, 'r+b') as f:
        data = bytearray(f.read())
        if data[:4] != b'\x7fELF' or data[4] != 2 or data[5] != 1:
            return 0
        shoff, = struct.unpack_from('<Q', data, 0x28)
        shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 0x3A)
        if not shoff or shentsize != 64:
            return 0
        names_off = struct.unpack_from('<Q', data, shoff + shstrndx * 64 + 0x18)[0]
        changed = 0
        for i in range(shnum):
            at = shoff + i * 64
            name, kind, flags, _, _, size, _, _, align = struct.unpack_from('<IIQQQQIIQ', data, at)
            end = data.index(0, names_off + name)
            section = data[names_off + name:end].decode('ascii', 'replace')
            if (flags & SHF_ALLOC) and not (flags & (SHF_MERGE | SHF_STRINGS)) and size >= ALIGN and align < ALIGN \
                    and section.startswith(PREFIXES):
                struct.pack_into("<Q", data, at + 0x30, ALIGN)   # sh_addralign
                changed += 1
        if changed:
            f.seek(0)
            f.write(data)
        return changed


def main():
    command = sys.argv[1:]
    out = command[command.index('-o') + 1] if '-o' in command else ''
    source = command[command.index('-c') + 1] if '-c' in command else ''
    if not out.endswith(('.o', '.obj')) or not source.endswith(SOURCES):
        return subprocess.run(command).returncode
    # Compile to assembly with the same options (the -MD/-MT/-MF dependency file keeps naming the
    # object), rewrite it, then assemble the object.
    assembly = out + '.s'
    to_assembly = list(command)
    to_assembly[command.index('-c')] = '-S'
    to_assembly[command.index('-o') + 1] = assembly
    result = subprocess.run(to_assembly)
    if result.returncode:
        return result.returncode
    with open(assembly) as f:
        text, _ = x86_conversions(f.read())
    with open(assembly, 'w') as f:
        f.write(text)
    result = subprocess.run([command[0], '-c', assembly, '-o', out])
    if result.returncode:
        return result.returncode
    os.remove(assembly)
    raise_alignment(out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
