"""Train one network that plays like each pro, from their games (data/<pro>/frames/*.npz).

    py -3.12 train.py [minutes] [--resume]

Every frame it sees what the pro saw (both fighters' state after the previous frame, its own last two
controller frames) and learns the controller the pro held next. A GRU carries what happened before.
Writes model.pt and, for the game, ghost_ai.bin (export.py's format).
"""
import math
import os
import sys
import time
import zlib
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

import features as ft
from extract import canonical_indices
from players import PLAYERS

HERE = Path(os.environ.get('MELEE_GHOST_AI_HOME', Path(__file__).resolve().parent))
DEV = "cuda"
L, BURN, B = 96, 32, 256
EMB_STATE, EMB_CHAR, EMB_STAGE, EMB_PRO = 48, 8, 8, 8
H_IN, H_RNN, H_OUT = 512, 384, 384
DROP = 0.1


def zlib_crc(name):
    return zlib.crc32(name.encode())


def game_rows(d, pi):
    """One game from one player's side -> (cont, flags, cat, stick, cstick, btn), or None if too short.
    Row t: what was seen after frame t-1, the controller of t-1 and t-2; target: frame t."""
    T = len(d["frame"])
    ids = np.asarray(d["frame"])
    if np.any(np.diff(ids.astype(np.int64)) != 1):
        indices = canonical_indices(ids)
        d = {key: (value[indices] if isinstance(value, np.ndarray) and value.ndim and len(value) == T else value)
             for key, value in d.items()}
        T = len(d["frame"])
        if np.any(np.diff(d["frame"].astype(np.int64)) != 1):
            return None  # missing true frames must not be learned as consecutive controller steps
    if T < L + 3:
        return None
    sb = np.clip(np.nan_to_num(d["s_num"].astype(np.float32)) * ft.NUM_SCALE, -ft.NUM_CLIP, ft.NUM_CLIP)
    ob = np.clip(np.nan_to_num(d["o_num"].astype(np.float32)) * ft.NUM_SCALE, -ft.NUM_CLIP, ft.NUM_CLIP)
    sx, sy = d["s_num"][:, 0].astype(np.float32), d["s_num"][:, 1].astype(np.float32)
    ox, oy = d["o_num"][:, 0].astype(np.float32), d["o_num"][:, 1].astype(np.float32)
    rel = np.nan_to_num(np.stack([(ox - sx) / 100, (oy - sy) / 100, np.abs(ox - sx) / 100], 1))
    pads = np.nan_to_num(ft.pad_block(d["s_pad"], d["s_btn"]))          # (T, 10)
    c = np.concatenate([sb[1:-1], ob[1:-1], rel[1:-1], pads[1:-1], pads[:-2]], 1).astype(np.float16)
    fl = np.concatenate([d["s_flags"][1:-1], d["o_flags"][1:-1]], 1)
    stage = ft.STAGES.get(int(d["stage"]), 0)
    cat = np.stack([np.clip(d["s_state"][1:-1], 0, ft.N_STATES - 1), np.clip(d["o_state"][1:-1], 0, ft.N_STATES - 1),
                    np.clip(d["s_char"][1:-1], 0, ft.N_CHARS - 1), np.clip(d["o_char"][1:-1], 0, ft.N_CHARS - 1),
                    np.full(T - 2, stage), np.full(T - 2, pi)], 1).astype(np.int16)
    p = np.nan_to_num(d["s_pad"][2:].astype(np.float32))
    return (c, fl, cat, ft.stick_class(p[:, 0], p[:, 1]).astype(np.int8), ft.cstick_class(p[:, 2], p[:, 3]).astype(np.int8),
            ft.btn_bits(d["s_btn"][2:], p[:, 4]).astype(np.uint8))


class Data:
    """Games in memory: continuous inputs as float16, flags as bytes. Pro set by default: a random set of
    each pro's games (all would not fit); games whose name hashes to 0 mod 25 are held out. Or any list
    of (game dict, pro index, name) through `games` (the general set, train_general.py)."""

    def __init__(self, per_pro=900, seed=None, games=None):
        cols = [[] for _ in range(6)]
        starts, lens, val = [], [], []
        off = 0
        if games is None:
            pick = np.random.default_rng(seed)
            def pro_games():
                for pi, pro in enumerate(ft.PROS):
                    # Unverified legacy arrays cannot silently become a named pro's ground truth.
                    files = sorted((HERE / "data" / pro / "verified" / "frames").glob("*.npz"))
                    if len(files) > per_pro:
                        files = [files[i] for i in pick.choice(len(files), per_pro, replace=False)]
                    for f in files:
                        with np.load(f, allow_pickle=False) as source:
                            d = {k: source[k] for k in source.files}
                        if (str(d.get('identity_code', '')) != PLAYERS[pro]['code'] or
                                str(d.get('identity_method', '')) != 'exact_connect_code_v1' or
                                str(d.get('split', '')) not in ('train', 'development')):
                            continue
                        yield d, pi, f.stem
            games = pro_games()
        for d, pi, name in games:
            r = game_rows(d, pi)
            if r is None:
                continue
            for k in range(6):
                cols[k].append(r[k])
            n = len(r[0])
            starts.append(off)
            lens.append(n)
            val.append(str(d['split']) == 'development' if 'split' in d else zlib_crc(name) % 25 == 0)
            off += n
        if not starts:
            raise ValueError('No verified training/development games. Collect and audit named-pro data first; legacy arrays are not trusted automatically.')
        self.cont, self.flags, self.cat, self.stick, self.cstick, self.btn = (np.concatenate(c) for c in cols)
        self.starts, self.lens, self.val = np.array(starts), np.array(lens), np.array(val)
        print(f"{off:,} frames in {len(starts)} games", flush=True)

    def batch(self, rng, val=False):
        pool = np.where(self.val == val)[0]
        if not len(pool):
            raise ValueError('No games in requested split; collect independent sessions before training/evaluation.')
        # Each pro weighs the same whatever their game count (Hungrybox has far fewer games).
        pros = self.cat[self.starts[pool], 5]
        w = (self.lens[pool] - L).astype(np.float64)
        for p in np.unique(pros):
            w[pros == p] /= w[pros == p].sum()
        g = rng.choice(pool, size=B, p=w / w.sum())
        s = self.starts[g] + (rng.random(B) * (self.lens[g] - L)).astype(np.int64)
        idx = s[:, None] + np.arange(L)[None]
        mirror = rng.random(B) < 0.5
        return (torch.from_numpy(self.cont[idx]).to(DEV).float(), torch.from_numpy(self.flags[idx]).to(DEV),
                torch.from_numpy(self.cat[idx].astype(np.int64)).to(DEV), torch.from_numpy(self.stick[idx].astype(np.int64)).to(DEV),
                torch.from_numpy(self.cstick[idx].astype(np.int64)).to(DEV), torch.from_numpy(self.btn[idx]).to(DEV).float(),
                torch.from_numpy(mirror).to(DEV))


# Column layout of `cont`: self block 16, opp block 16, rel 3, pad(t-1) 10, pad(t-2) 10.
CONT = 2 * ft.N_NUM + 3 + ft.PREV * ft.PAD_IN
FLIP = [c for c in ft.MIRROR_NUM] + [ft.N_NUM + c for c in ft.MIRROR_NUM] + [2 * ft.N_NUM] + \
       [2 * ft.N_NUM + 3 + k * ft.PAD_IN + j for k in range(ft.PREV) for j in (0, 2)]


def mirror_batch(cont, stick, cstick, mirror):
    sign = torch.ones(CONT, device=DEV)
    sign[FLIP] = -1
    m = mirror[:, None, None]
    cont = torch.where(m, cont * sign, cont)
    qx, qy = stick // ft.STICK_STEPS, stick % ft.STICK_STEPS
    stick = torch.where(mirror[:, None], (ft.STICK_STEPS - 1 - qx) * ft.STICK_STEPS + qy, stick)
    swap = torch.tensor([0, 1, 2, 4, 3], device=DEV)
    cstick = torch.where(mirror[:, None], swap[cstick], cstick)
    return cont, stick, cstick


class Net(nn.Module):
    def __init__(self, n_pros=None):
        super().__init__()
        self.state = nn.Embedding(ft.N_STATES, EMB_STATE)
        self.char = nn.Embedding(ft.N_CHARS, EMB_CHAR)
        self.stage = nn.Embedding(8, EMB_STAGE)
        # Existing pro checkpoints contain only named pros. General pretraining must request its
        # extra slot explicitly; silently adding one breaks checkpoint and binary compatibility.
        self.pro = nn.Embedding(len(ft.PROS) if n_pros is None else n_pros, EMB_PRO)
        n_in = CONT + 2 * ft.N_FLAGS + 2 * EMB_STATE + 2 * EMB_CHAR + EMB_STAGE + EMB_PRO
        self.inp = nn.Linear(n_in, H_IN)
        self.rnn = nn.GRU(H_IN, H_RNN, batch_first=True)
        self.mid = nn.Linear(H_RNN + H_IN, H_OUT)
        self.stick = nn.Linear(H_OUT, ft.N_STICK)
        self.cstick = nn.Linear(H_OUT, ft.N_CSTICK)
        self.btn = nn.Linear(H_OUT, ft.N_BTN)

    def features(self, cont, flags, cat):
        bits = ((flags[..., None].int() >> torch.arange(8, device=flags.device)) & 1).flatten(-2).float()
        return torch.cat([cont, bits, self.state(cat[..., 0]), self.state(cat[..., 1]), self.char(cat[..., 2]),
                          self.char(cat[..., 3]), self.stage(cat[..., 4]), self.pro(cat[..., 5])], -1)

    def forward(self, cont, flags, cat, h=None):
        x = F.dropout(F.relu(self.inp(self.features(cont, flags, cat))), DROP, self.training)
        r, h = self.rnn(x, h)
        y = F.dropout(F.relu(self.mid(torch.cat([r, x], -1))), DROP, self.training)
        return self.stick(y), self.cstick(y), self.btn(y), h


BTN_W = torch.tensor([1.0, 1.0, 1.0, 1.0, 1.0])


def losses(net, b):
    cont, flags, cat, stick, cstick, btn, mirror = b
    cont, stick, cstick = mirror_batch(cont, stick, cstick, mirror)
    ls, lc, lb, _ = net(cont, flags, cat)
    sl = slice(BURN, None)
    l_s = F.cross_entropy(ls[:, sl].reshape(-1, ft.N_STICK), stick[:, sl].reshape(-1))
    l_c = F.cross_entropy(lc[:, sl].reshape(-1, ft.N_CSTICK), cstick[:, sl].reshape(-1))
    l_b = F.binary_cross_entropy_with_logits(lb[:, sl], btn[:, sl], pos_weight=torch.full((ft.N_BTN,), 2.0, device=DEV))
    with torch.no_grad():
        acc_s = (ls[:, sl].argmax(-1) == stick[:, sl]).float().mean()
        pb, tb = lb[:, sl] > 0, btn[:, sl] > 0.5
        f1 = (2 * (pb & tb).sum() / (pb.sum() + tb.sum()).clamp(min=1)).float()
    return l_s + l_c + l_b, (l_s.item(), l_c.item(), l_b.item(), acc_s.item(), f1.item())


def main():
    minutes = float(sys.argv[1]) if len(sys.argv) > 1 and not sys.argv[1].startswith("--") else 60
    data = Data()
    RELOAD = 4000   # steps on one set of games before the next random set
    net = Net().to(DEV)
    opt = torch.optim.AdamW(net.parameters(), lr=1e-3, weight_decay=1e-4)
    if "--resume" in sys.argv and (HERE / "model.pt").exists():
        net.load_state_dict(torch.load(HERE / "model.pt"))
    rng = np.random.default_rng()
    t0, step = time.time(), 0
    best = float("inf")
    total = minutes * 60
    log = open(HERE / "train.log", "a")
    while time.time() - t0 < total:
        frac = (time.time() - t0) / total
        for g in opt.param_groups:
            g["lr"] = 1e-3 * (0.05 + 0.95 * 0.5 * (1 + math.cos(math.pi * frac)))
        if step and step % RELOAD == 0:
            del data
            data = Data()
        net.train()
        loss, m = losses(net, data.batch(rng))
        opt.zero_grad(set_to_none=True)
        loss.backward()
        nn.utils.clip_grad_norm_(net.parameters(), 1.0)
        opt.step()
        step += 1
        if step % 250 == 0:
            net.eval()
            with torch.no_grad():
                vm = np.mean([losses(net, data.batch(np.random.default_rng(7 + k), val=True))[1] for k in range(16)], 0)
            line = (f"{step} {time.time() - t0:.0f}s train stick {m[0]:.3f} c {m[1]:.3f} btn {m[2]:.3f} acc {m[3]:.3f} f1 {m[4]:.3f} | "
                    f"val stick {vm[0]:.3f} c {vm[1]:.3f} btn {vm[2]:.3f} acc {vm[3]:.3f} f1 {vm[4]:.3f}")
            print(line, flush=True)
            log.write(line + "\n")
            log.flush()
            vloss = vm[0] + vm[1] + vm[2]
            if vloss < best:   # the best on games it never trained on
                best = vloss
                torch.save(net.state_dict(), HERE / "model.pt")
    print("saved", flush=True)


if __name__ == "__main__":
    main()
