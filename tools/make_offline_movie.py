"""Generate deterministic controller input for offline visual smoke testing.

Uses Dolphin's DTM format from Source/Core/Core/Movie.h. No global keyboard input,
so other independently running Dolphin instances cannot receive these controls.
Menu navigation is provisional until checked against captured output.
"""
from pathlib import Path
import struct

root = Path(__file__).resolve().parents[1]
frames = []
def hold(count, buttons=0, x=128, y=128):
    frames.extend([struct.pack('<H6B', buttons, 0, 0, x, y, 128, 128)] * count)
def tap(button, wait=60):
    hold(2, button)
    hold(wait)

hold(240)
tap(4)       # B: leave login submenu.
tap(1 << 6)  # Up: select menu entry above Online Play.
tap(2, 120)  # A: enter it.
tap(2)       # A: select character under cursor.
tap(1, 120)  # Start: proceed.
tap(2, 180)  # A: choose stage.
for cycle in range(100):
    hold(40, x=230 if cycle % 2 else 26)
    tap(16, 12)  # Y jump
    tap(2, 12)   # A attack
    tap(4, 12)   # B special
hold(300)
header = bytearray(256)
header[:4] = b'DTM\x1a'
header[4:10] = b'GALE01'
header[11] = 1
struct.pack_into('<QQ', header, 13, len(frames), len(frames))
struct.pack_into('<Q', header, 129, 1700000000)
struct.pack_into('<Q', header, 237, (1 << 63) - 1)
path = root / 'reports/offline-smoke.dtm'
path.write_bytes(header + b''.join(frames))
print(path, len(frames), 'controller polls')
