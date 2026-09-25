#!/usr/bin/env python3
"""Rasterizes Lab view draw data dumped by port_lab_view_test into PNGs.

    port_lab_view_test replay.slp Lab out 600 1200     # writes out_600.bin, out_1200.bin, out_atlas.rgba
    python tools/lab_render.py out_600.bin out_1200.bin

A plain software rasterizer for ImGui triangles (per-vertex color times the font atlas, alpha
blended in order), so the Lab view can be looked at on any machine, without a GPU or the game.
Needs numpy and Pillow.
"""
import argparse
import struct
from pathlib import Path

import numpy as np
from PIL import Image

VERTEX = np.dtype([("pos", "<f4", 2), ("uv", "<f4", 2), ("col", "<u4")])


def load_lists(path: Path):
    b = path.read_bytes()
    (count,), p, lists = struct.unpack_from("<i", b, 0), 4, []
    for _ in range(count):
        (nv,) = struct.unpack_from("<i", b, p); p += 4
        verts = np.frombuffer(b, dtype=VERTEX, count=nv, offset=p); p += nv * VERTEX.itemsize
        (ni,) = struct.unpack_from("<i", b, p); p += 4
        idx = np.frombuffer(b, dtype="<u2", count=ni, offset=p); p += ni * 2
        lists.append((verts, idx))
    return lists


def load_atlas(path: Path):
    b = path.read_bytes()
    w, h = struct.unpack_from("<ii", b, 0)
    return np.frombuffer(b, dtype=np.uint8, offset=8).reshape(h, w, 4) / 255.0


def render(lists, atlas, width, height):
    img = np.zeros((height, width, 3))
    ah, aw = atlas.shape[:2]
    for verts, idx in lists:
        pos, uv = verts["pos"], verts["uv"]
        col = np.stack([(verts["col"] >> s) & 255 for s in (0, 8, 16, 24)], 1) / 255.0
        for t in range(0, len(idx), 3):
            tri = idx[t:t + 3]
            P = pos[tri]
            x0, x1 = max(int(np.floor(P[:, 0].min())), 0), min(int(np.ceil(P[:, 0].max())), width - 1)
            y0, y1 = max(int(np.floor(P[:, 1].min())), 0), min(int(np.ceil(P[:, 1].max())), height - 1)
            if x1 < x0 or y1 < y0:
                continue
            xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            (ax, ay), (bx, by), (cx, cy) = P
            d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
            if abs(d) < 1e-9:
                continue
            l0 = ((by - cy) * (xs - cx) + (cx - bx) * (ys - cy)) / d
            l1 = ((cy - ay) * (xs - cx) + (ax - cx) * (ys - cy)) / d
            inside = (l0 >= -1e-6) & (l1 >= -1e-6) & (1 - l0 - l1 >= -1e-6)
            if not inside.any():
                continue
            L = np.stack([l0, l1, 1 - l0 - l1], -1)[inside]
            C = L @ col[tri]
            U = L @ uv[tri]
            C = C * atlas[np.clip((U[:, 1] * ah).astype(int), 0, ah - 1), np.clip((U[:, 0] * aw).astype(int), 0, aw - 1)]
            a = C[:, 3:4]
            region = img[y0:y1 + 1, x0:x1 + 1]
            region[inside] = region[inside] * (1 - a) + C[:, :3] * a
    return Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("dumps", nargs="+", type=Path, help="<prefix>_<frame>.bin files")
    ap.add_argument("--size", default="1280x720", help="the size the test drew at (default 1280x720)")
    args = ap.parse_args()
    width, height = (int(v) for v in args.size.split("x"))
    for dump in args.dumps:
        prefix = dump.name.rsplit("_", 1)[0]
        atlas = load_atlas(dump.with_name(prefix + "_atlas.rgba"))
        out = dump.with_suffix(".png")
        render(load_lists(dump), atlas, width, height).save(out)
        print(out)


if __name__ == "__main__":
    main()
