"""model.pt -> ghost_ai.bin, the file the game loads (port/runtime/hle/ghost_ai.cpp reads this layout).

    py -3.12 export.py [out path]

Little-endian: "MUGA", u32 version, u32 dims[12], then float32 arrays in the order below. The stick's
81 cells carry the mean real stick position the pros held in each, so a cell near the middle sends a
value the game's dead zone does not swallow.
"""
import argparse
import struct
import os
import sys
from pathlib import Path

import numpy as np
import torch

import features as ft
import train

HERE = Path(os.environ.get('MELEE_GHOST_AI_HOME', Path(__file__).resolve().parent))


def stick_means(pro=None):
    sums = np.zeros((ft.N_STICK, 2))
    counts = np.zeros(ft.N_STICK)
    for player in [pro] if pro else ft.PROS:
        folder = HERE / "data" / player / ("verified/frames" if pro else "frames")
        accepted = 0
        for f in sorted(folder.glob("*.npz")):
            with np.load(f, allow_pickle=False) as source:
                if pro and str(source.get('split', '')) != 'train':
                    continue
                p = source["s_pad"].astype(np.float32)
            c = ft.stick_class(p[:, 0], p[:, 1])
            np.add.at(sums, c, p[:, :2])
            np.add.at(counts, c, 1)
            accepted += 1
            if accepted >= 150:
                break
    means = np.zeros((ft.N_STICK, 2), np.float32)
    for c in range(ft.N_STICK):
        gx, gy = ft.stick_value(c)
        means[c] = sums[c] / counts[c] if counts[c] > 20 else (gx, gy)
    # Mirror-symmetric: the network is trained on mirrored pictures too.
    for c in range(ft.N_STICK):
        qx, qy = c // ft.STICK_STEPS, c % ft.STICK_STEPS
        m = (ft.STICK_STEPS - 1 - qx) * ft.STICK_STEPS + qy
        if m > c:
            x = (means[c, 0] - means[m, 0]) / 2
            y = (means[c, 1] + means[m, 1]) / 2
            means[c], means[m] = (x, y), (-x, y)
    return means


def quantize_rows(array):
    """Symmetric int8 with a separate scale for every matrix row."""
    array = np.ascontiguousarray(array, np.float32)
    maximum = np.max(np.abs(array), axis=1)
    scales = np.where(maximum > 0, maximum / 127.0, 1.0).astype(np.float32)
    # Subnormal weights can underflow the scale; they quantize exactly to zero at float precision.
    scales = np.maximum(scales, np.finfo(np.float32).tiny)
    values = np.clip(np.rint(array / scales[:, None]), -127, 127).astype(np.int8)
    return scales, values


def model_arrays(sd, means):
    names = ["state.weight", "char.weight", "stage.weight", "pro.weight", "inp.weight", "inp.bias",
             "rnn.weight_ih_l0", "rnn.weight_hh_l0", "rnn.bias_ih_l0", "rnn.bias_hh_l0",
             "mid.weight", "mid.bias", "stick.weight", "stick.bias", "cstick.weight", "cstick.bias",
             "btn.weight", "btn.bias"]
    return [(name, sd[name], sd[name].ndim == 2) for name in names] + [("stick_mean", means, False)]


def write_model(out, checkpoint, quantized=False, means=None):
    """Return the exact dequantized weights used by the native runtime for independent checks."""
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    net = train.Net(n_pros=checkpoint["pro.weight"].shape[0])
    net.load_state_dict(checkpoint)
    sd = {k: v.detach().float().numpy() for k, v in net.state_dict().items()}
    dims = [train.CONT, ft.N_STATES, train.EMB_STATE, ft.N_CHARS, train.EMB_CHAR, 8, train.EMB_STAGE,
            sd["pro.weight"].shape[0], train.EMB_PRO, train.H_IN, train.H_RNN, train.H_OUT]
    arrays = model_arrays(sd, stick_means() if means is None else means)
    if not all(np.isfinite(a).all() for _, a, _ in arrays):
        raise ValueError("Cannot export non-finite model weights or stick means")
    if np.any(np.abs(arrays[-1][1]) > 1):
        raise ValueError("Stick means must be inside the controller range")
    tmp = out.with_name(out.name + ".part")
    reconstructed = {}
    with open(tmp, "wb") as f:
        f.write(b"MUGA")
        f.write(struct.pack("<I", 2 if quantized else 1))
        f.write(struct.pack("<12I", *dims))
        for name, a, matrix in arrays:
            if quantized and matrix:
                scales, q = quantize_rows(a)
                for scale, row in zip(scales, q):
                    f.write(struct.pack("<f", float(scale)))
                    f.write(row.tobytes())
                reconstructed[name] = torch.from_numpy(q.astype(np.float32) * scales[:, None])
            else:
                f.write(np.ascontiguousarray(a, dtype='<f4').tobytes())
                if name != 'stick_mean':
                    reconstructed[name] = torch.from_numpy(a.copy())
    tmp.replace(out)
    return reconstructed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('out', nargs='?', type=Path, default=HERE / 'ghost_ai.bin')
    parser.add_argument('checkpoint', nargs='?', type=Path, default=HERE / 'model.pt')
    parser.add_argument('--quantize', action='store_true', help='format 2: per-row int8 matrix weights')
    parser.add_argument('--pro', help='calibrate stick positions only from this pro verified corpus')
    parser.add_argument('--dequantized-checkpoint', type=Path, help='save native-equivalent weights for parity tests')
    args = parser.parse_args()
    out, src = args.out, args.checkpoint
    checkpoint = torch.load(src, map_location="cpu", weights_only=True)
    reconstructed = write_model(out, checkpoint, args.quantize, stick_means(args.pro))
    if args.dequantized_checkpoint:
        torch.save(reconstructed, args.dequantized_checkpoint)
    print(f"{out}: {out.stat().st_size:,} bytes, format {2 if args.quantize else 1}; inference uses float32 weights")


if __name__ == "__main__":
    main()
