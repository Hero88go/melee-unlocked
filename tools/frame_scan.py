"""Measures smoothness and flicker from a burst of consecutive presented frames.

The port can dump every presented frame over a short window (`--capture <prefix> --capture-sim-frame N
--capture-burst K`). At an unlocked frame rate those frames are sub-frame renders of the same or
adjacent simulation frames, so motion between them should be small and even. This reports:

  step      mean absolute pixel change from the previous frame, as a share of full scale
  frozen    frames whose change is under 15% of the median step (an object held instead of moving)
  snap      frames whose change is over 250% of the median step (a held object catching up)
  flicker   frames whose mean luminance differs from both neighbours by more than the threshold

Judder shows up as frozen/snap pairs; a whole-screen flash shows up as flicker. Usage:

    python tools/frame_scan.py <directory or prefix> [--flicker 0.15]
"""
import argparse
import sys
from pathlib import Path


def read_ppm(path):
    data = path.read_bytes()
    if not data.startswith(b"P6"):
        raise ValueError(f"{path}: not a binary PPM")
    fields, offset = [], 2
    while len(fields) < 3:
        while offset < len(data) and data[offset : offset + 1].isspace():
            offset += 1
        if data[offset : offset + 1] == b"#":
            while offset < len(data) and data[offset] != 0x0A:
                offset += 1
            continue
        start = offset
        while offset < len(data) and not data[offset : offset + 1].isspace():
            offset += 1
        fields.append(int(data[start:offset]))
    width, height, _ = fields
    return width, height, data[offset + 1 :]


def frame_stats(pixels, width, height, step=7):
    """Mean luminance and a subsampled copy for differencing (every `step` pixels)."""
    total, sample = 0, bytearray()
    for i in range(0, width * height * 3 - 3, step * 3):
        r, g, b = pixels[i], pixels[i + 1], pixels[i + 2]
        total += r + g + b
        sample.append((r * 77 + g * 151 + b * 28) >> 8)
    return total / max(1, len(sample) * 3 * 255), sample


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path", help="directory holding the .ppm burst, or a filename prefix")
    ap.add_argument("--flicker", type=float, default=0.15, help="relative luminance jump counted as a flash")
    args = ap.parse_args()

    target = Path(args.path)
    if target.is_dir():
        files = sorted(target.glob("*.ppm"))
    else:
        files = sorted(target.parent.glob(target.name + "*.ppm"))
    if len(files) < 3:
        sys.exit(f"need at least 3 captured frames, found {len(files)}")

    luma, samples, sizes = [], [], set()
    for f in files:
        width, height, pixels = read_ppm(f)
        sizes.add((width, height))
        mean, sample = frame_stats(pixels, width, height)
        luma.append(mean)
        samples.append(sample)
    if len(sizes) != 1:
        sys.exit(f"frames differ in size: {sorted(sizes)}")

    steps = []
    for a, b in zip(samples, samples[1:]):
        n = min(len(a), len(b))
        steps.append(sum(abs(a[i] - b[i]) for i in range(n)) / (n * 255))

    ordered = sorted(steps)
    median = ordered[len(ordered) // 2]
    frozen = [i for i, s in enumerate(steps) if median > 0 and s < 0.15 * median]
    snap = [i for i, s in enumerate(steps) if median > 0 and s > 2.5 * median]
    flicker = [
        i
        for i in range(1, len(luma) - 1)
        if abs(luma[i] - luma[i - 1]) > args.flicker * max(luma[i - 1], 1e-6)
        and abs(luma[i] - luma[i + 1]) > args.flicker * max(luma[i + 1], 1e-6)
    ]

    print(f"{len(files)} frames at {sizes.pop()}")
    print(f"step: median {median * 100:.3f}% of full scale, min {ordered[0] * 100:.3f}%, max {ordered[-1] * 100:.3f}%")
    print(f"frozen frames (held): {len(frozen)}{' ' + str([files[i + 1].name for i in frozen[:6]]) if frozen else ''}")
    print(f"snap frames (catch-up): {len(snap)}{' ' + str([files[i + 1].name for i in snap[:6]]) if snap else ''}")
    print(f"flicker frames: {len(flicker)}{' ' + str([files[i].name for i in flicker[:6]]) if flicker else ''}")
    ok = not frozen and not snap and not flicker
    print("PASS: motion is even and no frame flashes" if ok else "FAIL: see the frames listed above")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
