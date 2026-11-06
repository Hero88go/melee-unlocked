"""How much a Phillip policy plays like the pro it is conditioned on, measured on the pro's own games.

Teacher forcing over a pro's held-out replays (ghost-ai data/<pro>/verified, split "test"): at every
frame the policy sees the recorded state of both fighters as the game would send it (the replay's
post-frame fields, mu_ghost_ai.h), its previous action is the controller the pro really held, and the
controller it samples is compared with the one the pro pressed for frame t + delay + 1. That is
slippi-ai's training pairing (tf/policies.py imitation_loss: states [0, U-1] predict actions
[D+1, U+D] with previous actions [D, U+D-1]) and the game's: a step sees the fighters as of the end of
the previous frame and its answer is the controller `delay` frames on.

Reported per temperature, in the policy's own controller space (33 buckets an axis and 8 buttons for
the d18 policies; custom_v1 labels for Phillip's newest):
  decision frames: the pro changed something that frame. stick = main stick agreed, buttons = the
  buttons agreed (custom_v1: the whole button label, c-stick included), both = everything agreed.
  "hold last input" (repeat the previous controller) is printed beside it as the floor.
  all frames: the same three over every frame.
Imitation and self-play policies can be compared on the same opponent with --opponent.

Not in the arrays: items (Phillip's newest policies see none here), the Fountain's platform heights
(those games are skipped for them), hurtbox state (every frame vulnerable). Ice Climbers games are
skipped (no Nana recorded). Relative numbers between policies on the same games are what matters.

    set PYTHONPATH=<folder with onnxruntime> (ghost-ai/teacher/deps)
    py -3.12 tools/ghost_ai/phillip_likeness.py --pro zain --model <ProAI dir>/marth_d18_imitation_v3.onnx
        [--opponent FOX] [--games 20] [--temperature 1.0 0.7] [--split test] [--data <ghost-ai root>]
        [--upstream ghost-ai/teacher/upstream] [--out result.json] [--seed 1]
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from phillip_catalog import KINDS, PROS  # noqa: E402
from phillip_parity import BUTTONS, PyPolicy, load_upstream  # noqa: E402

BUTTON_ORDER = ["A", "B", "X", "Y", "Z", "L", "R", "D_UP"]
POPO = 10


def pick_games(root: Path, pro: str, split: str, opponent: int | None, want: int, skip_fod: bool) -> list[Path]:
    """Accepted games of the split, oldest replay id first, on the opponent asked for."""
    folder = root / "data" / pro / "verified"
    rows = []
    for line in open(folder / "manifest.jsonl", encoding="utf-8"):
        r = json.loads(line)
        if r.get("status") == "accepted" and r.get("split") == split:
            rows.append(r["replay_id"])
    out = []
    for rid in sorted(rows):
        path = folder / "frames" / f"{rid}.npz"
        if not path.is_file():
            continue
        with np.load(path, allow_pickle=False) as z:
            o_kind = int(z["o_char"][0])
            stage = int(z["stage"])
            if o_kind == POPO or (skip_fod and stage == 2) or len(z["frame"]) < 200:
                continue
            if opponent is not None and o_kind != opponent:
                continue
        out.append(path)
        if len(out) >= want:
            break
    return out


class Space:
    """The policy's own discrete controller space: encode a recorded controller, read a sampled one."""

    def __init__(self, py: PyPolicy):
        self.py = py
        self.custom = py.custom
        if self.custom:
            stub = sys.modules["slippi_ai.types"]
            self.Stick, self.Buttons, self.Controller = stub.Stick, stub.Buttons, stub.Controller

    def encode(self, pad: np.ndarray, btn: int) -> tuple:
        """pad: main x, y, c x, c y in [-1, 1] and the trigger in [0, 1] as the replay records them."""
        lx, ly, cx, cy, trig = (float(v) for v in pad)
        bits = [bool(btn & BUTTONS[b]) for b in BUTTON_ORDER]
        shoulder_value = max(trig, 1.0 if (bits[5] or bits[6]) else 0.0)
        if self.custom:
            def arr(v):
                return np.array([v], np.float32)
            ctrl = self.Controller(main_stick=self.Stick(x=arr((lx + 1) / 2), y=arr((ly + 1) / 2)),
                                   c_stick=self.Stick(x=arr((cx + 1) / 2), y=arr((cy + 1) / 2)),
                                   shoulder=arr(shoulder_value),
                                   buttons=self.Buttons(*[np.array([b]) for b in bits]))
            labels = self.py.bucketer.bucket(ctrl)
            return (int(labels.buttons[0]), int(labels.main_stick[0]))
        axis, sh = self.py.axis, self.py.shoulder

        def q(v):   # slippi-ai DiscreteEmbedding.from_state: floor(x * n + 0.5), x in [0, 1]
            return int(np.clip(np.floor((v + 1) / 2 * axis + 0.5), 0, axis))
        shoulder = int(np.clip(np.floor(shoulder_value * sh + 0.5), 0, sh))
        return (q(lx), q(ly), q(cx), q(cy), shoulder, tuple(bits))

    def feed_prev(self, code: tuple):
        """The pro's real controller as the policy's previous action."""
        prev = self.py.prev
        if self.custom:
            prev["prev_actions.0.buttons"][...] = code[0]
            prev["prev_actions.0.main_stick"][...] = code[1]
            return
        for name, v in zip(("main_stick.x", "main_stick.y", "c_stick.x", "c_stick.y", "shoulder"), code[:5]):
            prev["prev_actions.0." + name][...] = v
        for b, on in zip(BUTTON_ORDER, code[5]):
            key = "prev_actions.0.buttons." + b
            if key in prev:
                prev[key][...] = on

    def sampled(self) -> tuple:
        out = self.py.last_out
        if self.custom:
            return (int(out["actions.0.buttons"].reshape(-1)[0]), int(out["actions.0.main_stick"].reshape(-1)[0]))

        def num(k):
            return int(out["actions.0." + k].reshape(-1)[0])
        return (num("main_stick.x"), num("main_stick.y"), num("c_stick.x"), num("c_stick.y"), num("shoulder"),
                tuple(bool(num("buttons." + b)) for b in BUTTON_ORDER))

    @staticmethod
    def stick(code: tuple) -> tuple:
        return (code[1],) if len(code) == 2 else code[:2]

    @staticmethod
    def buttons(code: tuple) -> tuple:
        return (code[0],) if len(code) == 2 else code[5]


def fighter(num: np.ndarray, state: int, kind: int) -> dict:
    return dict(x=float(num[0]), y=float(num[1]), facing=float(num[2]), percent=float(num[3]), shield=float(num[4]),
                stocks=float(num[5]), air=float(num[8]), jumps=float(num[9]), motion=int(state), kind=int(kind), hurtbox=0)


def tally():
    return {"decisions": 0, "stick": 0, "buttons": 0, "both": 0, "hold_stick": 0, "hold_buttons": 0, "hold_both": 0,
            "frames": 0, "all_stick": 0, "all_buttons": 0, "all_both": 0}


def run_game(py: PyPolicy, space: Space, path: Path, temperature: float, seed: int, t: dict):
    with np.load(path, allow_pickle=False) as z:
        s_num, s_state, s_char = z["s_num"].astype(np.float32), z["s_state"], z["s_char"]
        o_num, o_state, o_char = z["o_num"].astype(np.float32), z["o_state"], z["o_char"]
        s_pad, s_btn, frames, stage = z["s_pad"].astype(np.float32), z["s_btn"], z["frame"], int(z["stage"])
    d = py.m["config"]["policy"]["delay"]
    n = len(frames)
    codes = [space.encode(s_pad[j], int(s_btn[j])) for j in range(n)]
    py.temperature = temperature
    py.noise_rng = np.random.default_rng(seed)
    py.state = {k: np.asarray(v["value"], dtype=v["dtype"]) for k, v in py.m["initial_state"].items()}
    for k in py.prev:
        py.prev[k][...] = 0
    py.prev_action, py.count = 0, 0
    for i in range(n - d - 1):
        target = codes[i + d + 1]
        previous = codes[i + d]
        space.feed_prev(previous)
        o = {"stage": stage, "frame": i, "match_frame": int(frames[i]),
             "p": [fighter(s_num[i], s_state[i], s_char[i]), fighter(o_num[i], o_state[i], o_char[i])],
             "opp_nana": None, "items": [], "fod": [0.0, 0.0]}
        py.step(o, i == 0)
        got = space.sampled()
        t["frames"] += 1
        st = space.stick(got) == space.stick(target)
        bt = space.buttons(got) == space.buttons(target)
        t["all_stick"] += st
        t["all_buttons"] += bt
        t["all_both"] += got == target
        if target != previous:
            t["decisions"] += 1
            t["stick"] += st
            t["buttons"] += bt
            t["both"] += got == target
            t["hold_stick"] += space.stick(previous) == space.stick(target)
            t["hold_buttons"] += space.buttons(previous) == space.buttons(target)
            t["hold_both"] += previous == target


def pct(a: int, b: int) -> str:
    return f"{100.0 * a / b:5.1f}" if b else "  n/a"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pro", required=True, choices=[v[0] for v in PROS.values()])
    ap.add_argument("--model", type=Path, required=True)
    ap.add_argument("--name-code", type=int, help="the pro's row in the policy (default: looked up in its name map)")
    ap.add_argument("--opponent", help="libmelee character name (FOX) or internal kind; default every opponent")
    ap.add_argument("--games", type=int, default=20)
    ap.add_argument("--temperature", type=float, nargs="+", default=[1.0])
    ap.add_argument("--split", default="test")
    ap.add_argument("--data", type=Path,
                    default=Path(os.environ.get("MELEE_GHOST_AI_HOME", "C:/Users/Chandler/NEW project/ghost-ai")))
    ap.add_argument("--upstream", type=Path, default=Path("C:/Users/Chandler/NEW project/ghost-ai/teacher/upstream"))
    ap.add_argument("--out", type=Path)
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()

    import onnxruntime as ort  # noqa: F401  (fails early with a clear message when PYTHONPATH lacks it)
    # Phillip's newest policies (custom_v1) decode with slippi-ai's own code, loaded from the upstream folder.
    upstream = load_upstream(args.upstream) if args.upstream.is_dir() else None
    py = PyPolicy(args.model, 0, upstream)
    ids = next(v[2] for v in PROS.values() if v[0] == args.pro)
    name_map = py.m.get("name_map") or {}
    code = args.name_code
    if code is None:
        code = next((name_map[i] for i in ids if i in name_map), None)
        if code is None:
            raise SystemExit(f"{args.model.name} has no row for {args.pro} ({', '.join(ids)}); pass --name-code")
    py.code = int(code)
    opponent = None
    if args.opponent:
        opponent = KINDS[args.opponent.upper()] if args.opponent.upper() in KINDS else int(args.opponent)
    games = pick_games(args.data, args.pro, args.split, opponent, args.games, skip_fod=py.custom)
    if not games:
        raise SystemExit("no games match")
    space = Space(py)
    result = {"model": args.model.name, "pro": args.pro, "name_code": py.code, "opponent": opponent,
              "delay": py.m["config"]["policy"]["delay"], "custom_v1": py.custom, "split": args.split,
              "games": [g.stem for g in games], "temperatures": {}}
    print(f"{args.model.name}: {args.pro} as name {py.code}, {len(games)} {args.split} games"
          + (f" against kind {opponent}" if opponent is not None else "") + f", delay {result['delay']}")
    print("temp   decisions  stick  buttons  both  | hold: stick buttons both | all frames: stick buttons both")
    for temperature in args.temperature:
        t = tally()
        t0 = time.time()
        for k, g in enumerate(games):
            run_game(py, space, g, temperature, args.seed * 1000003 + k, t)
        t["seconds"] = round(time.time() - t0, 1)
        result["temperatures"][str(temperature)] = t
        print(f"{temperature:4.2f}  {t['decisions']:9d}  {pct(t['stick'], t['decisions'])}  {pct(t['buttons'], t['decisions'])}"
              f"  {pct(t['both'], t['decisions'])}  |       {pct(t['hold_stick'], t['decisions'])} {pct(t['hold_buttons'], t['decisions'])}"
              f" {pct(t['hold_both'], t['decisions'])} |             {pct(t['all_stick'], t['frames'])} {pct(t['all_buttons'], t['frames'])}"
              f" {pct(t['all_both'], t['frames'])}   ({t['seconds']} s)")
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(result, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
