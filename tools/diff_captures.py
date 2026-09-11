"""Report how many pixels differ between consecutive PPM captures (proof of distinct presented frames)."""
import sys
from pathlib import Path

def read_ppm(path):
    data = Path(path).read_bytes()
    parts = data.split(maxsplit=4)
    assert parts[0] == b"P6", path
    w, h = int(parts[1]), int(parts[2])
    pixels = parts[4][: w * h * 3]
    return w, h, pixels

def main():
    files = sorted(Path(a) for a in sys.argv[1:])
    prev = None
    for f in files:
        w, h, px = read_ppm(f)
        if prev is not None:
            pw, ph, ppx = prev
            assert (pw, ph) == (w, h)
            diff = sum(1 for i in range(0, len(px), 3) if px[i:i+3] != ppx[i:i+3])
            print(f"{f.name}: {diff} of {w*h} pixels differ from previous ({100.0*diff/(w*h):.2f}%)")
        else:
            print(f"{f.name}: baseline {w}x{h}")
        prev = (w, h, px)

if __name__ == "__main__":
    main()
