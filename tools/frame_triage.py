"""Judge captured frames from a scripted run.

An absolute "how much of this frame is not black" threshold is wrong for the
sweep: Brinstar Depths and Hyrule at night are legitimately dark, and a
correct frame there would score like a broken one on Onett. What actually
distinguishes a fault is that a frame disagrees with its own neighbours, so
that is what this measures.

    python tools/frame_triage.py <dir> [--glob 'k_*.ppm'] [--json out.json]

Reports the frames whose content differs sharply from the run's own baseline,
which is what a dropout, a blackout or a geometry fault looks like.
"""
import argparse
import glob
import json
import os
import re
import statistics
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("frame_triage needs Pillow: python -m pip install pillow")


def frame_number(path):
    m = re.search(r"_(\d+)\.ppm$", path)
    return int(m.group(1)) if m else -1


def measure(path):
    """Content of one frame, ignoring the HUD bands at top and bottom.

    The HUD keeps drawing when the 3D scene does not, so including it would
    mask exactly the failure this is looking for.
    """
    with Image.open(path) as im:
        im = im.convert("RGB")
        w, h = im.size
        band = im.crop((0, int(h * 0.15), w, int(h * 0.80)))
        # Downsample first: this runs over hundreds of frames in the sweep and
        # the judgement does not need every pixel.
        band = band.resize((w // 4, band.size[1] // 4), Image.BILINEAR)
        px = list(band.getdata())
    lit = sum(1 for r, g, b in px if r + g + b > 40)
    mean = sum(r + g + b for r, g, b in px) / (len(px) * 3.0)
    return {"lit": lit * 100.0 / len(px), "mean": mean, "pixels": len(px)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("directory")
    ap.add_argument("--glob", default="*.ppm")
    ap.add_argument("--json", help="write the full per-frame table here")
    ap.add_argument("--drop", type=float, default=0.45,
                    help="flag a frame below this fraction of the run's median content")
    args = ap.parse_args()

    paths = sorted(glob.glob(os.path.join(args.directory, args.glob)),
                   key=frame_number)
    if not paths:
        sys.exit("no frames matched %s in %s" % (args.glob, args.directory))

    rows = []
    for p in paths:
        m = measure(p)
        m["frame"] = frame_number(p)
        m["path"] = p
        rows.append(m)

    # The run judges itself. A stage that is dark everywhere has a low median
    # and a correct frame on it still sits at the median, so it does not flag.
    median = statistics.median(r["lit"] for r in rows)
    floor = median * args.drop

    # A run that is blank throughout is a different failure from a dropout, and
    # a median near zero would make every comparison meaningless.
    if median < 1.0:
        print("every frame is essentially blank (median %.2f%% lit)." % median)
        print("that is a whole-run failure, not a dropout: check the run itself.")
        return 2

    bad = [r for r in rows if r["lit"] < floor]
    for r in rows:
        r["ok"] = r["lit"] >= floor

    print("%d frames, median content %.2f%%, flagging below %.2f%%"
          % (len(rows), median, floor))
    if bad:
        print("\n%d frame(s) disagree with the run:" % len(bad))
        for r in bad:
            print("  frame %-6d %6.2f%% lit  %s"
                  % (r["frame"], r["lit"], os.path.basename(r["path"])))
    else:
        print("no frame disagrees with the run.")

    if args.json:
        with open(args.json, "w") as f:
            json.dump({"median": median, "floor": floor, "frames": rows}, f, indent=1)

    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
