"""model.pt -> ghost_ai.bin, the file the game loads (port/runtime/hle/ghost_ai.cpp reads this layout).

    py -3.12 export.py [out path]

Little-endian: "MUGA", u32 version, u32 dims[12], then float32 arrays in the order below. The stick's
81 cells carry the mean real stick position the pros held in each, so a cell near the middle sends a
value the game's dead zone does not swallow.
"""
import struct
import os
import sys
from pathlib import Path

import numpy as np
import torch

import features as ft
import train

HERE = Path(os.environ.get('MELEE_GHOST_AI_HOME', Path(__file__).resolve().parent))


def stick_means():
    sums = np.zeros((ft.N_STICK, 2))
    counts = np.zeros(ft.N_STICK)
    for pro in ft.PROS:
        for f in sorted((HERE / "data" / pro / "frames").glob("*.npz"))[:150]:
            p = np.load(f)["s_pad"].astype(np.float32)
            c = ft.stick_class(p[:, 0], p[:, 1])
            np.add.at(sums, c, p[:, :2])
            np.add.at(counts, c, 1)
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


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "ghost_ai.bin"
    src = Path(sys.argv[2]) if len(sys.argv) > 2 else HERE / "model.pt"
    checkpoint = torch.load(src, map_location="cpu", weights_only=True)
    net = train.Net(n_pros=checkpoint["pro.weight"].shape[0])
    net.load_state_dict(checkpoint)
    sd = {k: v.detach().float().numpy() for k, v in net.state_dict().items()}
    dims = [train.CONT, ft.N_STATES, train.EMB_STATE, ft.N_CHARS, train.EMB_CHAR, 8, train.EMB_STAGE,
            sd["pro.weight"].shape[0], train.EMB_PRO, train.H_IN, train.H_RNN, train.H_OUT]
    arrays = [sd["state.weight"], sd["char.weight"], sd["stage.weight"], sd["pro.weight"],
              sd["inp.weight"], sd["inp.bias"],
              sd["rnn.weight_ih_l0"], sd["rnn.weight_hh_l0"], sd["rnn.bias_ih_l0"], sd["rnn.bias_hh_l0"],
              sd["mid.weight"], sd["mid.bias"],
              sd["stick.weight"], sd["stick.bias"], sd["cstick.weight"], sd["cstick.bias"],
              sd["btn.weight"], sd["btn.bias"], stick_means()]
    if not all(np.isfinite(a).all() for a in arrays):
        raise ValueError("Cannot export non-finite model weights or stick means")
    # Never leave a partial model at the path a game or packaging step can load.
    tmp = out.with_name(out.name + ".part")
    with open(tmp, "wb") as f:
        f.write(b"MUGA")
        f.write(struct.pack("<I", ft.VERSION))
        f.write(struct.pack("<12I", *dims))
        for a in arrays:
            f.write(np.ascontiguousarray(a, np.float32).tobytes())
    tmp.replace(out)
    n = sum(a.size for a in arrays)
    print(f"{out}: {n:,} numbers, {out.stat().st_size / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
