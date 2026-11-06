"""Fine-tune one of Phillip's d18 imitation policies on one pro's own verified games (PyTorch twin).

Imitation exactly as slippi-ai trains it (tf/policies.py imitation_loss): the policy sees the state at
the end of frame t, its previous action is the controller of frame t + D, and it must produce the
controller of frame t + D + 1 (D = the policy's delay, 18). Streams of consecutive 80-frame chunks per
batch slot carry the recurrent state across a game, as the trainer's data pipeline does; a new game
resets it. Only this pro's games are used (owner rule 2026-10-27: never mixed with ranked data or
another pro); the "test" split is never read here, selection uses "development".

The pro's name row: an existing one (--name-code, e.g. Zain's 5 in the Marth policy) or a new row
appended to the name table (--new-name Mang0) initialised to the mean of the existing rows.

    py -3.12 tools/ghost_ai/phillip_finetune.py --pro mang0 --base <falco_d18_imitation_v3.onnx> --new-name Mang0
        --out <folder> [--steps 20000] [--batch 128] [--unroll 80] [--lr 2e-5] [--eval-every 500] [--dev-games 24]
    py -3.12 tools/ghost_ai/phillip_finetune.py --pro zain --base <marth_d18_imitation_v3.onnx> --name-code 5 --out <folder> --evaluate-only
"""
from __future__ import annotations

import argparse
import json
import os
import random
import sys
import time
from pathlib import Path

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from phillip_catalog import KINDS  # noqa: E402
from phillip_torch import BUTTONS, PLAYER_FIELDS, Policy, from_onnx  # noqa: E402

BUTTON_BITS = {"A": 0x100, "B": 0x200, "X": 0x400, "Y": 0x800, "Z": 0x10, "L": 0x40, "R": 0x20, "D_UP": 0x8}
ACTION_KEYS = ["buttons." + b for b in BUTTONS] + ["main_stick.x", "main_stick.y", "c_stick.x", "c_stick.y", "shoulder"]
STAGES = {2: 0x08, 3: 0x12, 8: 0x06, 28: 0x1A, 31: 0x18, 32: 0x19}
POPO = 10
TECHS = (0xC7, 0xC8, 0xC9)
# The policy's opponent tech mask (config observation.animation.mask): its neutral, forward and backward tech starts
# look alike for this many frames, as the game sends them (phillip_ai.cpp observe, slippi-ai AnimationFilter).
MASK_WINDOW = 0


def mask_techs(action: np.ndarray, window: int) -> np.ndarray:
    out = action.copy()
    same, prev = 0, 0
    for t, a in enumerate(action):
        if a == prev:
            same += 1
        else:
            same, prev = 0, a
        if a in TECHS and same < window:
            out[t] = TECHS[0]
    return out


# ---- one game as the policy sees it ----------------------------------------------------------------------
class Game:
    """Per-frame state leaves of both fighters and the pro's discretised controller (default space)."""

    def __init__(self, path: Path, axis: int, shoulder: int):
        with np.load(path, allow_pickle=False) as z:
            s_num, o_num = z["s_num"].astype(np.float32), z["o_num"].astype(np.float32)
            self.n = len(z["frame"])
            self.state = {}
            for who, num, st, ch in (("p0", s_num, z["s_state"], z["s_char"]), ("p1", o_num, z["o_state"], z["o_char"])):
                self.state[who] = {
                    "percent": np.floor(np.maximum(num[:, 3], 0)).astype(np.int32),
                    "facing": (num[:, 2] > 0), "x": num[:, 0], "y": num[:, 1],
                    "action": st.astype(np.int32), "character": ch.astype(np.int32),
                    "invulnerable": np.zeros(self.n, bool),   # not in the arrays; rare (respawn, ledge)
                    "jumps_left": np.maximum(num[:, 9], 0).astype(np.int32),
                    "shield_strength": num[:, 4], "on_ground": (num[:, 8] == 0),
                }
            if MASK_WINDOW > 0:
                self.state["p1"]["action"] = mask_techs(self.state["p1"]["action"], MASK_WINDOW)
            self.stage = STAGES.get(int(z["stage"]), 0)
            pad, btn = z["s_pad"].astype(np.float32), z["s_btn"].astype(np.int64)
        bits = {b: (btn & BUTTON_BITS[b]) != 0 for b in BUTTONS}
        lr = bits["L"] | bits["R"]
        trig = np.maximum(pad[:, 4], lr.astype(np.float32))

        def q(v, n):   # slippi-ai DiscreteEmbedding.from_state: floor(x * n + 0.5), x in [0, 1]
            return np.clip(np.floor(v * n + 0.5), 0, n).astype(np.int32)
        self.action = {"buttons." + b: bits[b] for b in BUTTONS}
        for k, col in zip(("main_stick.x", "main_stick.y", "c_stick.x", "c_stick.y"), range(4)):
            self.action[k] = q((pad[:, col] + 1) / 2, axis)
        self.action["shoulder"] = q(trig, shoulder)


def list_games(root: Path, pro: str, split: str, character: int | None, opponent: int | None = None) -> list[Path]:
    folder = root / "data" / pro / "verified"
    out = []
    for line in open(folder / "manifest.jsonl", encoding="utf-8"):
        r = json.loads(line)
        if r.get("status") == "accepted" and r.get("split") == split:
            p = folder / "frames" / f"{r['replay_id']}.npz"
            if p.is_file():
                out.append(p)
    out.sort()
    if character is not None:
        # Each game's character, opponent and length, read once and cached beside the manifest.
        cache_path = folder / "frames-index.json"
        cache = json.loads(cache_path.read_text()) if cache_path.is_file() else {}
        changed = False
        kept = []
        for p in out:
            info = cache.get(p.stem)
            if info is None:
                with np.load(p, allow_pickle=False) as z:
                    info = [int(z["s_char"][0]), int(z["o_char"][0]), int(len(z["frame"]))]
                cache[p.stem] = info
                changed = True
            if info[0] == character and info[1] != POPO and info[2] > 400 and (opponent is None or info[1] == opponent):
                kept.append(p)
        if changed:
            cache_path.write_text(json.dumps(cache))
        out = kept
    return out


class Streams:
    """B slots, each walking its games in order in chunks of U states; the caller carries the state."""

    def __init__(self, games: list[Path], batch: int, unroll: int, delay: int, axis: int, shoulder: int, seed: int):
        self.games, self.batch, self.unroll, self.delay = games, batch, unroll, delay
        self.axis, self.shoulder = axis, shoulder
        self.rng = random.Random(seed)
        self.order = list(range(len(games)))
        self.rng.shuffle(self.order)
        self.next_game = 0
        self.slots = [None] * batch   # (Game, position)
        self.epochs = 0

    def _fresh(self) -> Game:
        while True:
            if self.next_game >= len(self.order):
                self.rng.shuffle(self.order)
                self.next_game = 0
                self.epochs += 1
            g = Game(self.games[self.order[self.next_game]], self.axis, self.shoulder)
            self.next_game += 1
            if g.n > self.delay + self.unroll + 2:
                return g

    def batch_arrays(self) -> dict:
        U, D, B = self.unroll, self.delay, self.batch
        reset = np.zeros((U, B), bool)
        state = {who: {f: [] for f in PLAYER_FIELDS} for who in ("p0", "p1")}
        stage = np.zeros((U, B), np.int32)
        prev = {k: [] for k in ACTION_KEYS}
        target = {k: [] for k in ACTION_KEYS}
        for b in range(B):
            if self.slots[b] is None or self.slots[b][1] + U + D + 1 > self.slots[b][0].n:
                self.slots[b] = (self._fresh(), 0)
                reset[0, b] = True
            g, t0 = self.slots[b]
            sl = slice(t0, t0 + U)
            for who in ("p0", "p1"):
                for f in PLAYER_FIELDS:
                    state[who][f].append(g.state[who][f][sl])
            stage[:, b] = g.stage
            for k in ACTION_KEYS:
                prev[k].append(g.action[k][t0 + D:t0 + D + U])
                target[k].append(g.action[k][t0 + D + 1:t0 + D + U + 1])
            self.slots[b] = (g, t0 + U)
        stack = lambda lst: np.stack(lst, 1)   # [U, B]
        return {"reset": reset, "stage": stage,
                "state": {who: {f: stack(v) for f, v in fields.items()} for who, fields in state.items()},
                "prev": {k: stack(v) for k, v in prev.items()}, "target": {k: stack(v) for k, v in target.items()}}


def to_torch(arrays: dict, device) -> dict:
    def conv(a):
        if a.dtype == bool:
            return torch.from_numpy(a).to(device)
        if a.dtype.kind == "f":
            return torch.from_numpy(a).to(device)
        return torch.from_numpy(a.astype(np.int64)).to(device)
    return {"reset": conv(arrays["reset"]), "stage": conv(arrays["stage"]),
            "state": {who: {f: conv(v) for f, v in fields.items()} for who, fields in arrays["state"].items()},
            "prev": {k: conv(v) for k, v in arrays["prev"].items()}, "target": {k: conv(v) for k, v in arrays["target"].items()}}


# ---- the unrolled loss (embedding and head over all frames at once, the LSTMs over time) ------------------
def unroll_loss(pol: Policy, batch: dict, name: torch.Tensor, state: list, want_accuracy: bool = False):
    U, B = batch["reset"].shape
    flat = lambda x: x.reshape(U * B, *x.shape[2:])
    game = {who: {f: flat(v) for f, v in fields.items()} for who, fields in batch["state"].items()}
    game["stage"] = flat(batch["stage"])
    prev = {k: flat(v) for k, v in batch["prev"].items()}
    target = {k: flat(v) for k, v in batch["target"].items()}
    names = name.expand(U, B).reshape(U * B)
    x = pol.encoder(pol.embed(game, prev, names)).reshape(U, B, -1)
    keep = (~batch["reset"]).float().unsqueeze(-1)   # [U, B, 1]
    new_state = []
    for k, layer in enumerate(pol.layers):
        c, h = state[2 * k], state[2 * k + 1]
        gx = [x @ layer.wx[g] for g in range(4)]   # [U, B, H] for the whole chunk
        ys = []
        for t in range(U):
            c, h = c * keep[t], h * keep[t]
            gates = [gx[g][t] + h @ layer.wh[g] + layer.bh[g] for g in range(4)]
            i, f, g_, o = torch.sigmoid(gates[0]), torch.sigmoid(gates[1]), torch.tanh(gates[2]), torch.sigmoid(gates[3])
            c = f * c + i * g_
            h = o * torch.tanh(c)
            ys.append(x[t] + h)
        y = torch.stack(ys)
        z = y - y.mean(-1, keepdim=True)
        z = z / torch.sqrt((z * z).mean(-1, keepdim=True))
        z = z * layer.ln_scale + layer.ln_bias
        x = y + layer.ffw2(torch.nn.functional.gelu(layer.ffw1(z)))
        new_state += [c, h]
    features = x.reshape(U * B, -1)
    nll = pol.distance(features, prev, target)   # [U*B]
    out = {"loss": nll.mean(), "state": [s.detach() for s in new_state]}
    if want_accuracy:
        with torch.no_grad():
            residual = pol.to_residual(features)
            right_stick = torch.ones(U * B, dtype=torch.bool, device=nll.device)
            right_buttons = torch.ones_like(right_stick)
            right_all = torch.ones_like(right_stick)
            for k, (comp, key) in enumerate(zip(pol.components, ACTION_KEYS)):
                logits = comp.logits(residual, prev[key])
                guess = (logits > 0) if comp.kind == "bool" else logits.argmax(-1)
                ok = guess.long() == target[key].long()
                right_all &= ok
                if key.startswith("buttons."):
                    right_buttons &= ok
                if key in ("main_stick.x", "main_stick.y"):
                    right_stick &= ok
                residual = comp.feed(residual, target[key])
            changed = torch.zeros_like(right_all)
            for key in ACTION_KEYS:
                changed |= prev[key].long() != target[key].long()
            out["counts"] = {"frames": int(changed.numel()), "decisions": int(changed.sum()),
                             "stick": int((right_stick & changed).sum()), "buttons": int((right_buttons & changed).sum()),
                             "both": int((right_all & changed).sum()), "nll_sum": float(nll.sum())}
    return out


def evaluate(pol: Policy, games: list[Path], name: torch.Tensor, cfg, device, unroll: int, batch: int) -> dict:
    pol.eval()
    streams = Streams(games, batch, unroll, cfg.delay, cfg.axis, cfg.shoulder, seed=0)
    totals = {"frames": 0, "decisions": 0, "stick": 0, "buttons": 0, "both": 0, "nll_sum": 0.0}
    state = pol.initial_state(batch, device)
    seen = set()
    with torch.no_grad():
        while True:
            arrays = streams.batch_arrays()
            if streams.epochs > 0:
                break
            r = unroll_loss(pol, to_torch(arrays, device), name, state, want_accuracy=True)
            state = r["state"]
            for k, v in r["counts"].items():
                totals[k] += v
            seen |= {id(s[0]) for s in streams.slots}
    d = max(totals["decisions"], 1)
    pol.train()
    return {"nll": totals["nll_sum"] / max(totals["frames"], 1), "stick": 100.0 * totals["stick"] / d,
            "buttons": 100.0 * totals["buttons"] / d, "both": 100.0 * totals["both"] / d,
            "decisions": totals["decisions"], "frames": totals["frames"]}


def grow_name_table(pol: Policy, meta: dict, new_name: str) -> int:
    """Append one name row (the mean of the existing rows) to the encoder; returns the new code."""
    cfg = pol.cfg
    old = cfg.num_names
    with torch.no_grad():
        w = pol.encoder.w.data   # [input_width, hidden]; the name one-hot is the last `old` rows
        name_rows = w[-old:]
        new_row = name_rows.mean(0, keepdim=True)
        pol.encoder.w = torch.nn.Parameter(torch.cat([w, new_row], 0))
    cfg.num_names = old + 1
    cfg.input_width += 1
    meta["name_map"][new_name] = old
    meta["config"]["max_names"] = old + 1
    return old


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pro", required=True)
    ap.add_argument("--base", type=Path, required=True, help="the published imitation policy to start from")
    ap.add_argument("--name-code", type=int, help="the pro's existing row in the policy")
    ap.add_argument("--new-name", help="append a row for this name (e.g. Mang0) and train it")
    ap.add_argument("--opponent", help="only games against this character (libmelee name): a matchup fine-tune")
    ap.add_argument("--character", help="the pro's character (libmelee name); default from the policy's catalog name")
    ap.add_argument("--data", type=Path, default=Path(os.environ.get("MELEE_GHOST_AI_HOME", "C:/Users/Chandler/NEW project/ghost-ai")))
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--steps", type=int, default=20000)
    ap.add_argument("--batch", type=int, default=128)
    ap.add_argument("--unroll", type=int, default=80)
    ap.add_argument("--lr", type=float, default=2e-5)
    ap.add_argument("--lr-end", type=float, help="decay the learning rate linearly to this over --steps (default: constant)")
    ap.add_argument("--eval-every", type=int, default=500)
    ap.add_argument("--dev-games", type=int, default=24)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--evaluate-only", action="store_true", help="only measure the base policy on development games")
    ap.add_argument("--resume", type=Path, help="a last.pt or model.pt to continue from (keeps its step count)")
    ap.add_argument("--affinity", type=lambda v: int(v, 0), default=0x3000,
                    help="logical CPU mask (default 0x3000: two of sixteen); the PC stays usable while it trains")
    ap.add_argument("--threads", type=int, default=2, help="torch CPU threads")
    ap.add_argument("--priority", choices=("idle", "below", "normal"), default="idle",
                    help="Windows priority class (idle unless he is away)")
    args = ap.parse_args()

    # Owner 2026-11-05: never lag his PC. Idle priority, a couple of cores; the GPU does the work.
    torch.set_num_threads(args.threads)
    if sys.platform == "win32":
        import ctypes
        k32 = ctypes.windll.kernel32
        # Typed: an untyped GetCurrentProcess() truncates the pseudo handle and both calls fail silently.
        k32.GetCurrentProcess.restype = ctypes.c_void_p
        k32.SetPriorityClass.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        k32.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        me = k32.GetCurrentProcess()
        klass = {"idle": 0x40, "below": 0x4000, "normal": 0x20}[args.priority]
        if not (k32.SetPriorityClass(me, klass) and k32.SetProcessAffinityMask(me, args.affinity)):
            print("warning: could not lower this process", flush=True)

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    torch.manual_seed(args.seed)
    pol, meta = from_onnx(args.base)
    cfg = pol.cfg
    offset = 0
    resumed = None
    if args.resume:
        resumed = torch.load(args.resume, map_location="cpu")
        if resumed["num_names"] != cfg.num_names:
            grow_name_table(pol, meta, resumed["new_name"])
        pol.load_state_dict(resumed["state_dict"])
        meta["name_map"] = resumed["name_map"]
        code = resumed["name_code"]
        offset = int(resumed.get("step") or 0)
        args.new_name = args.new_name or resumed.get("new_name")
    elif args.new_name:
        code = grow_name_table(pol, meta, args.new_name)
    elif args.name_code is not None:
        code = args.name_code
    else:
        raise SystemExit("give --name-code or --new-name")
    global MASK_WINDOW
    anim = meta["config"].get("observation", {}).get("animation", {})
    MASK_WINDOW = int(anim.get("tech_mask_window", 7)) if anim.get("mask") else 0
    character = KINDS[args.character.upper()] if args.character else None
    if character is None:
        char_name = meta["config"]["dataset"].get("allowed_characters", "")
        character = KINDS.get(char_name.upper()) if isinstance(char_name, str) else None
    pol.to(device)
    name = torch.tensor([code], dtype=torch.long, device=device)

    opponent = KINDS[args.opponent.upper()] if args.opponent else None
    train_games = list_games(args.data, args.pro, "train", character, opponent)
    dev_games = list_games(args.data, args.pro, "development", character, opponent)[: args.dev_games]
    print(f"{args.pro}: {len(train_games)} training games, {len(dev_games)} development games, name row {code}, "
          f"character {character}, device {device}", flush=True)
    args.out.mkdir(parents=True, exist_ok=True)
    report = {"pro": args.pro, "base": args.base.name, "name_code": code, "new_name": args.new_name, "character": character,
              "train_games": len(train_games), "dev_games": len(dev_games), "evals": [], "args": vars(args) | {"base": str(args.base), "out": str(args.out), "data": str(args.data), "resume": str(args.resume)}}
    old_report = args.out / "report.json"
    if resumed is not None and old_report.is_file():
        # Keep the evaluations of the run this continues, up to the checkpoint's step.
        prev = json.loads(old_report.read_text())
        report["evals"] = [e for e in prev.get("evals", []) if e["step"] <= offset]
        if prev.get("best") and prev["best"]["step"] <= offset:
            report["best"] = prev["best"]
    dev = evaluate(pol, dev_games, name, cfg, device, args.unroll, min(args.batch, len(dev_games)))
    print(f"step {offset} dev nll {dev['nll']:.4f} stick {dev['stick']:.1f} buttons {dev['buttons']:.1f} both {dev['both']:.1f} "
          f"({dev['decisions']} decisions)", flush=True)
    if not report["evals"] or report["evals"][-1]["step"] != offset:
        report["evals"].append({"step": offset, **dev})
    (args.out / "report.json").write_text(json.dumps(report, indent=1))
    if args.evaluate_only:
        return 0

    opt = torch.optim.Adam(pol.parameters(), lr=args.lr)
    if resumed is not None and resumed.get("opt"):
        opt.load_state_dict(resumed["opt"])
    streams = Streams(train_games, args.batch, args.unroll, cfg.delay, cfg.axis, cfg.shoulder, args.seed + offset)
    state = pol.initial_state(args.batch, device)
    best = min(dev["nll"], report.get("best", {}).get("nll", float("inf")))
    t0 = time.time()
    running = 0.0
    for local in range(1, args.steps + 1):
        step = offset + local
        batch = to_torch(streams.batch_arrays(), device)
        r = unroll_loss(pol, batch, name, state)
        opt.zero_grad(set_to_none=True)
        r["loss"].backward()
        torch.nn.utils.clip_grad_norm_(pol.parameters(), 1.0)
        if args.lr_end is not None:
            for g in opt.param_groups:
                g["lr"] = args.lr + (args.lr_end - args.lr) * (local - 1) / max(args.steps - 1, 1)
        opt.step()
        state = r["state"]
        running += float(r["loss"].detach())
        if step % 50 == 0:
            print(f"step {step} loss {running / 50:.4f} epochs {streams.epochs} {(time.time() - t0) / local:.2f} s/step", flush=True)
            running = 0.0
        if step % args.eval_every == 0 or local == args.steps:
            dev = evaluate(pol, dev_games, name, cfg, device, args.unroll, min(args.batch, len(dev_games)))
            print(f"step {step} dev nll {dev['nll']:.4f} stick {dev['stick']:.1f} buttons {dev['buttons']:.1f} both {dev['both']:.1f}", flush=True)
            report["evals"].append({"step": step, **dev, "minutes": round((time.time() - t0) / 60, 1)})
            saved = {"state_dict": pol.state_dict(), "num_names": cfg.num_names, "new_name": args.new_name, "name_code": code,
                     "name_map": meta["name_map"], "base": args.base.name, "step": step, "dev": dev}
            torch.save(dict(saved, opt=opt.state_dict()), args.out / "last.pt")
            if dev["nll"] < best:
                best = dev["nll"]
                torch.save(saved, args.out / "model.pt")
                report["best"] = {"step": step, **dev}
            (args.out / "report.json").write_text(json.dumps(report, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
