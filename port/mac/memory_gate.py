"""Compiler launcher for the M3 direct-build exception (no more than -j2 externally)."""
import subprocess, sys
sys.dont_write_bytecode = True
from build_full import memory_gate
if __name__ == '__main__':
    memory_gate()
    raise SystemExit(subprocess.call(sys.argv[1:]))
