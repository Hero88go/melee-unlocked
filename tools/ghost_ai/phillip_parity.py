"""Check the game's Phillip runtime (port/runtime/hle/phillip_ai.cpp) against a Python stepper.

The Python side follows slippi-ai's onnx_policies.py contract: pack logical inputs by dtype, feed the
policy's sampled actions and recurrent state back as prev_actions/prev_state. With --upstream it decodes
custom_v1 controllers with slippi-ai's own action_space/custom_v1.py, assigns item slots with its
parsing_utils.ItemAssigner and places Randall with libmelee's stages.randall_position (all loaded from
that folder, nothing installed). Both sides step one policy over the same observations with every
noise input at 0.5, so the controllers they choose must agree frame for frame.

    py -3.12 tools/ghost_ai/phillip_parity.py --test <port_ghost_ai_test.exe> --model <ProAI dir>/<file.onnx>
        [--upstream <dir with gh_slippi_ai_action_space_custom_v1.py, parsing_utils.py, libmelee_stages.py>]
        [--frames 600] [--name-code 5] [--stage 31]   (onnxruntime must be importable)
"""
from __future__ import annotations

import argparse
import ast
import json
import os
import struct
import subprocess
import sys
import tempfile
import types as pytypes
import typing as tp
from pathlib import Path

import numpy as np

STAGES = {2: 0x08, 3: 0x12, 8: 0x06, 28: 0x1A, 31: 0x18, 32: 0x19}
BUTTONS = {"A": 0x100, "B": 0x200, "X": 0x400, "Y": 0x800, "Z": 0x10, "L": 0x40, "R": 0x20, "D_UP": 0x8}
TECHS = (0xC7, 0xC8, 0xC9)
MAX_ITEMS = 15


def load_upstream(folder: Path):
    """custom_v1, ItemAssigner and randall_position from the upstream sources, with stub modules."""
    class Stick(tp.NamedTuple):
        x: tp.Any
        y: tp.Any

    class Buttons(tp.NamedTuple):
        A: tp.Any; B: tp.Any; X: tp.Any; Y: tp.Any; Z: tp.Any; L: tp.Any; R: tp.Any; D_UP: tp.Any

    class Controller(tp.NamedTuple):
        main_stick: tp.Any
        c_stick: tp.Any
        shoulder: tp.Any
        buttons: tp.Any

    stub = pytypes.ModuleType("slippi_ai.types")
    for name in ("BoolArray", "UInt8Array", "UInt16Array", "FloatArray"):
        setattr(stub, name, _Any())
    stub.S = tp.TypeVar("S")
    stub.Stick, stub.Controller, stub.Buttons = Stick, Controller, Buttons
    stub.MAX_ITEMS = MAX_ITEMS
    pkg = pytypes.ModuleType("slippi_ai")
    pkg.types = stub
    sys.modules["slippi_ai"], sys.modules["slippi_ai.types"] = pkg, stub
    cv1 = pytypes.ModuleType("custom_v1")
    sys.modules["custom_v1"] = cv1   # its dataclasses look their module up
    exec(compile((folder / "gh_slippi_ai_action_space_custom_v1.py").read_text(encoding="utf-8"), "custom_v1.py", "exec"),
         cv1.__dict__)

    def extract(path: Path, names: set[str]) -> dict:
        tree = ast.parse(path.read_text(encoding="utf-8"))
        keep = [n for n in tree.body if (isinstance(n, (ast.FunctionDef, ast.ClassDef)) and n.name in names) or
                (isinstance(n, ast.Assign) and any(getattr(t, "id", "") in names for t in n.targets))]
        scope = {"np": np, "tp": tp, "types": stub}
        exec(compile(ast.Module(body=keep, type_ignores=[]), str(path), "exec"), scope)
        return scope

    assigner = extract(folder / "parsing_utils.py", {"ItemAssigner", "ItemId"})["ItemAssigner"]
    randall = extract(folder / "libmelee_stages.py", {"randall_position", "_RANDALL_CORNER_POSITIONS"})["randall_position"]
    return cv1, assigner, randall


class _Any:
    def __getitem__(self, item):
        return tp.Any


def observations(n: int, rng: np.random.Generator, self_kind: int, opp_kind: int, stage: int, opp_nana: bool):
    """A plausible sequence: fighters wander, actions change, the opponent techs, items come and go."""
    x = np.array([-30.0, 30.0]); y = np.zeros(2); pct = np.zeros(2); act = np.array([14, 14])
    items: dict[int, list] = {}
    next_spawn = 100
    fod = [28.0, 20.0]
    for f in range(n):
        x += rng.normal(0, 1.5, 2); x = np.clip(x, -80, 80)
        y = np.maximum(0, y + rng.normal(0, 2, 2)); air = y > 1
        if rng.random() < 0.05: act = rng.integers(14, 250, 2)
        if rng.random() < 0.03: act[1] = TECHS[rng.integers(0, 3)]
        if rng.random() < 0.02: pct += rng.integers(0, 15, 2)
        if rng.random() < 0.05 and len(items) < 18:
            items[next_spawn] = [int(rng.integers(0x30, 0x60)), 0, float(rng.normal(0, 40)), float(rng.normal(20, 10))]
            next_spawn += 1
        for spawn in list(items):
            if rng.random() < 0.03: del items[spawn]
            else:
                items[spawn][1] = int(rng.integers(0, 4)); items[spawn][2] += float(rng.normal(0, 2))
        if rng.random() < 0.02: fod[int(rng.integers(0, 2))] = float(rng.uniform(15, 40))
        players = [dict(x=x[i], y=y[i], facing=1.0 if x[1 - i] > x[i] else -1.0, percent=pct[i], shield=60 - (f % 60) * 0.1,
                        stocks=4, air=float(air[i]), jumps=float(1 if air[i] else 2), motion=int(act[i]),
                        kind=self_kind if i == 0 else opp_kind, hurtbox=int(rng.random() < 0.05)) for i in range(2)]
        nana = dict(players[1], x=players[1]["x"] - 8, kind=11, motion=int(act[1]) ^ 1) if opp_nana else None
        yield {"stage": stage, "frame": f, "match_frame": f - 123, "p": players, "opp_nana": nana,
               "items": [(s, *v) for s, v in list(items.items())[:MAX_ITEMS]], "fod": list(fod)}


def fighter_bytes(p):
    num = [p["x"], p["y"], p["facing"], p["percent"], p["shield"], p["stocks"], 0, 0, p["air"], p["jumps"]] + [0.0] * 6
    return struct.pack("<16fHB5sB3x", *num, p["motion"], p["kind"], b"\0" * 5, p["hurtbox"])


ZERO_FIGHTER = struct.pack("<76x")


def pack_obs(o: dict, name_code: int) -> bytes:
    head = struct.pack("<BBBBhhI", 3, 1, 1, 1 if o["frame"] == 0 else 0, o["stage"], name_code, o["frame"])
    out = head + fighter_bytes(o["p"][0]) + fighter_bytes(o["p"][1]) + struct.pack("<6fI", *([0.0] * 6), 0)
    on_fod = o["stage"] == 2
    out += struct.pack("<iffBBBB", o["match_frame"], o["fod"][1] if on_fod else 0.0, o["fod"][0] if on_fod else 0.0,
                       0, 1 if o["opp_nana"] else 0, len(o["items"]), 0)
    out += ZERO_FIGHTER + (fighter_bytes(o["opp_nana"]) if o["opp_nana"] else ZERO_FIGHTER)
    for i in range(MAX_ITEMS):
        if i < len(o["items"]):
            spawn, kind, state, ix, iy = o["items"][i]
            out += struct.pack("<HBBIff", kind, state, 1, spawn, ix, iy)
        else:
            out += struct.pack("<16x")
    assert len(out) == 600, len(out)
    return out


class PyPolicy:
    def __init__(self, path: Path, name_code: int, upstream):
        import onnxruntime as ort
        so = ort.SessionOptions(); so.log_severity_level = 3
        so.intra_op_num_threads = 1
        self.s = ort.InferenceSession(str(path), so, providers=["CPUExecutionProvider"])
        self.m = json.loads(self.s.get_modelmeta().custom_metadata_map["slippi_ai"])
        self.inputs = {i.name for i in self.s.get_inputs()}
        self.outs = [o.name for o in self.s.get_outputs()]
        self.state = {k: np.asarray(v["value"], dtype=v["dtype"]) for k, v in self.m["initial_state"].items()}
        self.prev = {}
        for key, leaves in self.m["inputs"].items():
            for l in leaves:
                if l["name"].startswith("prev_actions."):
                    self.prev[l["name"]] = np.zeros((1,) + tuple(l["shape"]), key.split(".", 1)[1])
        self.code = name_code
        c = self.m["controller"]["default"]
        self.axis, self.shoulder = c["axis_spacing"], c["shoulder_spacing"]
        self.custom = self.m["controller"]["type"] == "custom_v1"
        anim = self.m["config"].get("observation", {}).get("animation", {})
        self.mask_window = anim.get("tech_mask_window", 7) if anim.get("mask") else 0
        self.rating = (self.m.get("agent_config") or {}).get("rating", 0.0)
        self.prev_action, self.count = 0, 0
        if upstream:
            cv1, assigner, self.randall = upstream
            cfg = self.m["controller"]["custom_v1"]
            self.bucketer = cv1.Config(c_stick_config=cv1.PolarStickConfig(**cfg["c_stick_config"]),
                                       main_stick_config=cv1.PolarStickConfig(**cfg["main_stick_config"])).create_bucketer()
            self.cv1 = cv1
            self.assigner = assigner()
        elif self.custom:
            raise SystemExit("this policy uses custom_v1: pass --upstream")

    def step(self, o: dict, reset: bool):
        leaves = {}
        opp_action = o["p"][1]["motion"]
        if self.mask_window:   # observations.AnimationFilter.update, fast mode
            if opp_action == self.prev_action: self.count += 1
            else: self.count, self.prev_action = 0, opp_action
            if opp_action in TECHS and self.count < self.mask_window: opp_action = TECHS[0]

        def fighter(pre, p, action):
            leaves.update({pre + "x": p["x"], pre + "y": p["y"], pre + "facing": p["facing"] > 0,
                           pre + "percent": int(np.floor(p["percent"])), pre + "shield_strength": p["shield"],
                           pre + "on_ground": not p["air"], pre + "jumps_left": int(p["jumps"]),
                           pre + "action": action, pre + "character": p["kind"], pre + "invulnerable": p["hurtbox"] != 0})
        fighter("game.p0.", o["p"][0], o["p"][0]["motion"])
        fighter("game.p1.", o["p"][1], opp_action)
        if o["opp_nana"]:
            fighter("game.p1.nana.", o["opp_nana"], o["opp_nana"]["motion"]); leaves["game.p1.nana.exists"] = True
        stage = STAGES.get(o["stage"], 0)
        leaves["game.stage"] = stage
        if stage == 0x06 and hasattr(self, "randall"):
            h, left, right = self.randall((o["match_frame"] + 1200) % 1200)
            leaves["game.randall.x"], leaves["game.randall.y"] = (left + right) / 2, h
        if stage == 0x08:
            leaves["game.fod_platforms.left"], leaves["game.fod_platforms.right"] = o["fod"][1], o["fod"][0]
        if hasattr(self, "assigner"):
            slots = self.assigner.assign([s for s, *_ in o["items"]])
            for slot, (spawn, kind, state, ix, iy) in zip(slots, o["items"]):
                pre = f"game.items.item_{slot}."
                leaves.update({pre + "exists": True, pre + "type": kind, pre + "state": state, pre + "x": ix, pre + "y": iy})
        leaves.update({"name": self.code, "needs_reset": reset, "temperature": 1.0, "rating": self.rating})
        leaves.update(self.state)
        leaves.update(self.prev)
        packed = {}
        for key, entries in self.m["inputs"].items():
            dt = np.dtype(key.split(".", 1)[1])
            width = max(e["offset"] + e["size"] for e in entries)
            buf = np.zeros((1, width), dt)
            for e in entries:
                v = leaves.get(e["name"])
                if e["name"].startswith("noise."):
                    v = np.full(e["size"], 0.5, np.float32)
                if v is not None:
                    buf[0, e["offset"]:e["offset"] + e["size"]] = np.asarray(v, dt).reshape(-1)
            packed[key] = buf
        vals = dict(zip(self.outs, self.s.run(self.outs, {k: v for k, v in packed.items() if k in self.inputs})))
        out = {}
        for key, entries in self.m["outputs"].items():
            for e in entries:
                out[e["name"]] = vals[key][:, e["offset"]:e["offset"] + e["size"]].reshape((1,) + tuple(e["shape"]))
        for k, v in out.items():
            if k.startswith("state."):
                self.state["prev_" + k] = v
            elif k.startswith("actions."):
                self.prev["prev_" + k] = v
        if self.custom:   # slippi-ai's own decoder
            ctrl = self.bucketer.decode(self.cv1.ControllerV1(buttons=out["actions.0.buttons"].reshape(1).astype(np.uint16),
                                                               main_stick=out["actions.0.main_stick"].reshape(1).astype(np.uint16)))
            axes = [float(np.clip(float(v[0]) * 2 - 1, -1, 1)) for v in
                    (ctrl.main_stick.x, ctrl.main_stick.y, ctrl.c_stick.x, ctrl.c_stick.y)]
            buttons = sum(m for b, m in BUTTONS.items() if bool(getattr(ctrl.buttons, b)[0]))
            shoulder = float(ctrl.shoulder[0])
        else:
            num = lambda k: float(out["actions.0." + k].reshape(-1)[0])
            axes = [float(np.clip(num(k) / self.axis * 2 - 1, -1, 1))
                    for k in ("main_stick.x", "main_stick.y", "c_stick.x", "c_stick.y")]
            buttons = sum(m for b, m in BUTTONS.items() if num("buttons." + b))
            shoulder = num("shoulder") / self.shoulder
        if buttons & 0x40:
            shoulder = 1.0
        return axes + [shoulder, float(buttons)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--test", type=Path, required=True)
    ap.add_argument("--model", type=Path, required=True)
    ap.add_argument("--upstream", type=Path)
    ap.add_argument("--frames", type=int, default=600)
    ap.add_argument("--name-code", type=int, default=0)
    ap.add_argument("--self-kind", type=int, default=18)
    ap.add_argument("--opp-kind", type=int, default=1)
    ap.add_argument("--stage", type=int, default=31)
    ap.add_argument("--opp-nana", action="store_true")
    args = ap.parse_args()
    upstream = load_upstream(args.upstream) if args.upstream else None
    obs = list(observations(args.frames, np.random.default_rng(7), args.self_kind, args.opp_kind, args.stage, args.opp_nana))
    with tempfile.TemporaryDirectory() as tmp:
        ob, outp = Path(tmp) / "obs.bin", Path(tmp) / "act.bin"
        ob.write_bytes(b"".join(pack_obs(o, args.name_code) for o in obs))
        env = dict(os.environ, MELEE_PRO_AI_FIXED_NOISE="0.5")
        r = subprocess.run([str(args.test), "--phillip", str(args.model.parent), args.model.name, str(ob), str(outp)], env=env)
        if r.returncode:
            raise SystemExit(f"C++ runtime failed with exit {r.returncode}")
        cpp = np.frombuffer(outp.read_bytes(), np.float32).reshape(-1, 6)
    py = PyPolicy(args.model, args.name_code, upstream)
    ref = np.array([py.step(o, i == 0) for i, o in enumerate(obs)], np.float32)
    assert len(cpp) == len(ref), (len(cpp), len(ref))
    stick_err = np.abs(cpp[:, :5] - ref[:, :5]).max()
    button_bad = int((cpp[:, 5] != ref[:, 5]).sum())
    moving = int((np.abs(ref[:, :2]).max(axis=1) > 0.3).sum())
    pressed = int((ref[:, 5] != 0).sum())
    print(f"{len(ref)} frames: max stick/shoulder difference {stick_err:.6f}, button mismatches {button_bad}; "
          f"reference moved the stick on {moving} frames and pressed buttons on {pressed}")
    return 0 if stick_err < 1e-4 and button_bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
