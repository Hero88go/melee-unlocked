"""A modified executable image must be rejected before game boot."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

exe, dol = map(Path, sys.argv[1:])
data = bytearray(dol.read_bytes())
data[-1] ^= 1
header = bytearray(0x440)
header[:6] = b"GALE01"
struct.pack_into(">I", header, 0x420, len(header))
with tempfile.TemporaryDirectory() as directory:
    iso = Path(directory) / "mismatched-image.iso"
    iso.write_bytes(header + data)
    run = subprocess.run([str(exe), "--iso", str(iso), "--headless", "--frames", "1"],
                         capture_output=True, text=True, timeout=15,
                         creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    if run.returncode != 3 or "does not match vanilla Melee NTSC 1.02" not in run.stderr:
        raise SystemExit(f"mismatched image was not rejected: {run.returncode}\n{run.stdout}\n{run.stderr}")
print("modified game executable rejected before boot")
