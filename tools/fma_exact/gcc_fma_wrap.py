#!/usr/bin/env python3
"""Compiler launcher that makes single-precision fused multiply-adds round like the GameCube.

The console's single-precision fused ops (fmadds, fmsubs, fnmadds, fnmsubs) form the exact
product, add in double precision (one rounding) and then round to single. x86 FMA3 `ss`/`ps`
instructions round the exact result straight to single. The two disagree in rare double-rounding
cases, which is the last source of 1-ULP drift between the native build and the console.

Used as CMAKE_C_COMPILER_LAUNCHER for the game DLL: for a `-c x.c -o x.o` compile it runs the
compiler with `-S`, rewrites every scalar or 128-bit packed single-precision FMA3 instruction into
convert-to-double, exact multiply, one double add or subtract, round to single, and assembles the
result. xmm4/xmm5 are the scratch registers; the build reserves them with -ffixed-xmm4
-ffixed-xmm5 (both are caller-saved on Win64, so no calling-convention change). Double-precision
FMAs are already exact and are left alone. Any other compile passes straight through.

    python tools/fma_exact/gcc_fma_wrap.py <gcc> <args...>
"""
import os
import re
import subprocess
import sys
import tempfile

INSN = re.compile(r'^(\s*)(vf(n?)m(add|sub)(132|213|231)(ss|ps))\s+(.*?)\s*(#.*)?$')
MASK_LABEL = '.Lmu_fma_sign'


def split_operands(text):
    ops, depth, cur = [], 0, ''
    for ch in text:
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        if ch == ',' and depth == 0:
            ops.append(cur.strip())
            cur = ''
        else:
            cur += ch
    ops.append(cur.strip())
    return ops


def rewrite_line(m):
    indent, mnem, neg, kind, order, width, operands = m.group(1, 2, 3, 4, 5, 6, 7)
    ops = split_operands(operands)
    if len(ops) != 3:
        return None
    op3, op2, dst = ops                      # AT&T order: src3, src2, dst
    if any('xmm4' in o or 'xmm5' in o or 'ymm' in o for o in ops):
        return None                          # 256-bit packed or scratch in use: not handled
    if order == '132':
        a, b, c = dst, op3, op2
    elif order == '213':
        a, b, c = op2, dst, op3
    else:
        a, b, c = op2, op3, dst
    packed = width == 'ps'
    s1, s2 = ('%ymm4', '%ymm5') if packed else ('%xmm4', '%xmm5')
    cvt = 'vcvtps2pd' if packed else 'vcvtss2sd'
    mul, add, sub, xor = ('vmulpd', 'vaddpd', 'vsubpd', 'vxorpd') if packed else ('vmulsd', 'vaddsd', 'vsubsd', 'vxorpd')

    def load(op, reg):
        if packed:
            return f'{indent}{cvt}\t{op}, {reg}'
        return f'{indent}{cvt}\t{op}, {reg[:0] + reg.replace("y", "x")}, {reg.replace("y", "x")}'

    out = [f'{indent}# mu-fma-exact: {mnem} {operands}',
           load(a, s1), load(b, s2),
           f'{indent}{mul}\t{s2}, {s1}, {s1}',  # exact: 24x24-bit product fits a double
           load(c, s2)]
    if not neg and kind == 'add':
        out.append(f'{indent}{add}\t{s2}, {s1}, {s1}')       # a*b + c
    elif not neg and kind == 'sub':
        out.append(f'{indent}{sub}\t{s2}, {s1}, {s1}')       # a*b - c
    elif neg and kind == 'add':
        out.append(f'{indent}{sub}\t{s1}, {s2}, {s1}')       # c - a*b
    else:
        out.append(f'{indent}{add}\t{s2}, {s1}, {s1}')       # -(a*b) - c = -(a*b + c)
        out.append(f'{indent}{xor}\t{MASK_LABEL}{"4" if packed else "1"}(%rip), {s1}, {s1}')
    if packed:
        out.append(f'{indent}vcvtpd2ps\t{s1}, {dst}')
    else:
        out.append(f'{indent}vcvtsd2ss\t%xmm4, {dst}, {dst}')
    return out


def rewrite(asm):
    lines, count, uses_mask = [], 0, False
    for line in asm.splitlines():
        m = INSN.match(line)
        new = rewrite_line(m) if m else None
        if new is None:
            if m:
                sys.stderr.write(f'mu-fma-exact: left unchanged: {line.strip()}\n')
            lines.append(line)
            continue
        uses_mask |= any(MASK_LABEL in n for n in new)
        lines.extend(new)
        count += 1
    if uses_mask:
        lines += ['\t.section .rdata,"dr"', '\t.p2align 5',
                  f'{MASK_LABEL}1:', '\t.quad 0x8000000000000000, 0',
                  f'{MASK_LABEL}4:', '\t.quad 0x8000000000000000, 0x8000000000000000, 0x8000000000000000, 0x8000000000000000']
    return '\n'.join(lines) + '\n', count


def main():
    cc, args = sys.argv[1], sys.argv[2:]
    contract = os.environ.get('MU_FP_CONTRACT')   # experiments only: override -ffp-contract
    if contract:
        args = ['-ffp-contract=' + contract if a.startswith('-ffp-contract=') and a != '-ffp-contract=off' else a for a in args]
    srcs = [a for a in args if a.endswith('.c')]
    if '-c' not in args or len(srcs) != 1 or '-o' not in args:
        return subprocess.call([cc] + args)
    obj = args[args.index('-o') + 1]
    fd, tmp = tempfile.mkstemp(suffix='.s', dir=os.path.dirname(os.path.abspath(obj)) or None)
    os.close(fd)
    try:
        s_args = [a for a in args]
        s_args[s_args.index('-o') + 1] = tmp
        s_args[s_args.index('-c')] = '-S'
        rc = subprocess.call([cc] + s_args)
        if rc:
            return rc
        with open(tmp, encoding='utf-8', errors='surrogateescape') as f:
            text, _ = rewrite(f.read())
        with open(tmp, 'w', encoding='utf-8', errors='surrogateescape', newline='\n') as f:
            f.write(text)
        skip_next = False
        a_args = []
        for a in args:
            if skip_next:
                skip_next = False
                continue
            if a in ('-MF', '-MT', '-MQ'):
                skip_next = True
                continue
            if a in ('-MD', '-MMD', '-MP') or a == srcs[0]:
                continue
            a_args.append(a)
        return subprocess.call([cc] + a_args + ['-x', 'assembler', tmp])
    finally:
        try:
            os.remove(tmp)
        except OSError:
            pass


if __name__ == '__main__':
    sys.exit(main())
