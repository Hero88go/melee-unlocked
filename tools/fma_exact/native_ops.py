#!/usr/bin/env python3
"""Native float operations of one function, grouped by source line (objdump -d -l on a -g build).

    python tools/fma_exact/native_ops.py <dll> <function>
"""
import collections
import re
import subprocess
import sys

OBJDUMP = r'C:/Users/Chandler/toolchains/winlibs-gcc-15.3/mingw64/bin/objdump.exe'
FLOAT = re.compile(r'^\s*[0-9a-f]+:\s+(v(?:mul|add|sub|div|sqrt|fmadd|fmsub|fnmadd|fnmsub|cvtss2sd|cvtsd2ss)[a-z0-9]*)\s')


def main():
    dll, fn = sys.argv[1], sys.argv[2]
    out = subprocess.run([OBJDUMP, '-d', '-l', '--no-show-raw-insn', dll, '--disassemble=' + fn],
                         capture_output=True, text=True, errors='replace').stdout
    line, ops = None, collections.OrderedDict()
    for l in out.splitlines():
        m = re.match(r'^(.*):(\d+)(?: \(discriminator \d+\))?$', l.strip())
        if m and m.group(1).endswith(('.c', '.h')):
            line = re.split(r'[\\/]', m.group(1))[-1] + ':' + m.group(2)
            continue
        m = FLOAT.match(l)
        if m and line:
            ops.setdefault(line, []).append(m.group(1))
    for k, v in sorted(ops.items(), key=lambda kv: (kv[0].rsplit(':', 1)[0], int(kv[0].rsplit(':', 1)[1]))):
        if any(x.startswith(('vf', 'vmul', 'vadd', 'vsub', 'vdiv', 'vsqrt')) for x in v):
            print('%-22s %s' % (k, ' '.join(v)))


if __name__ == '__main__':
    main()
