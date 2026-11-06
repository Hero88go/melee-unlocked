"""A PyTorch twin of Phillip's d18 policies (slippi-ai, MIT), loaded from the published ONNX weights.

The exported graph (format version 1, "default" controller) is: the game embedding (both players,
stage, the previous controller and the player's name, 1121 floats for the d18 policies) -> Linear 512
-> three times [LSTM 512 with a residual connection, LayerNorm (no epsilon) -> Linear 1024 -> GELU ->
Linear 512 with a residual connection] -> the autoregressive controller head: Linear 128, then one
component after another (buttons A B X Y Z L R D_UP, main stick x y, c-stick x y, shoulder), each an
MLP over [residual, previous value of that component] whose sample feeds a zero-initialised decoder
back into the residual. Sampling: buttons noise < sigmoid(logit / temperature), the rest
argmax(logits / temperature - log(-log(noise))). Weights are read straight from the ONNX initializers
in graph order; `check()` steps the twin and onnxruntime side by side on the same inputs and noise.

Training uses `distance()`: the per-component negative log likelihood of a target controller with the
target (not a sample) fed to the decoders, as slippi-ai's AutoRegressive.distance does.

    py -3.12 tools/ghost_ai/phillip_torch.py --check <model.onnx> [--steps 300]   (onnxruntime on PYTHONPATH)
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

BUTTONS = ["A", "B", "X", "Y", "Z", "L", "R", "D_UP"]
PLAYER_FIELDS = ["percent", "facing", "x", "y", "action", "character", "invulnerable", "jumps_left", "shield_strength", "on_ground"]
NUM_ACTIONS = 0x18F   # 399, clamped
NUM_CHARACTERS = 0x21  # 33, clamped
NUM_STAGES = 64        # clamped
TINY = 1.1754943508222875e-38


def one_hot_empty(x: torch.Tensor, n: int) -> torch.Tensor:
    """One-hot with out-of-range values as all zeros (slippi-ai OneHotPolicy.EMPTY / ONNX OneHot)."""
    valid = (x >= 0) & (x < n)
    return F.one_hot(torch.where(valid, x, torch.zeros_like(x)).long(), n).float() * valid.unsqueeze(-1).float()


class Config:
    def __init__(self, meta: dict):
        cfg = meta["config"]
        c = meta["controller"]["default"]
        self.axis = c["axis_spacing"]          # 32 -> 33 classes
        self.shoulder = c["shoulder_spacing"]  # 10 -> 11 classes
        self.num_names = cfg["max_names"]
        self.legacy_jumps = cfg["embed"]["player"].get("legacy_jumps_left", False)
        self.jumps = 6 if self.legacy_jumps else 7
        net = cfg["network"]
        assert net["name"] == "tx_like", net["name"]
        tx = net["tx_like"]
        self.hidden = tx["hidden_size"]
        self.layers = tx["num_layers"]
        self.ffw = tx["hidden_size"] * tx["ffw_multiplier"]
        assert tx["recurrent_layer"] == "lstm" and tx["activation"] == "gelu"
        head = cfg["controller_head"]
        assert head["name"] == "autoregressive"
        self.residual = head["autoregressive"]["residual_size"]
        self.depth = head["autoregressive"]["component_depth"]
        self.delay = cfg["policy"]["delay"]
        assert cfg["embed"]["items"].get("type", "skip") == "skip" and not cfg["embed"]["player"].get("with_nana")
        assert not cfg["embed"].get("with_randall") and not cfg["embed"].get("with_fod")
        player = 4 + NUM_ACTIONS + NUM_CHARACTERS + 1 + self.jumps + 2
        self.controller_width = len(BUTTONS) + 4 * (self.axis + 1) + (self.shoulder + 1)
        self.input_width = 2 * player + NUM_STAGES + self.controller_width + self.num_names
        # autoregressive components: (kind, classes)
        self.components = [("bool", 1)] * len(BUTTONS) + [("cat", self.axis + 1)] * 4 + [("cat", self.shoulder + 1)]


class Linear(nn.Module):
    """y = x @ W + b with W stored [in, out] as the ONNX Gemm (transB = 0) has it."""

    def __init__(self, inp: int, out: int, bias: bool = True):
        super().__init__()
        self.w = nn.Parameter(torch.zeros(inp, out))
        self.b = nn.Parameter(torch.zeros(out)) if bias else None

    def forward(self, x):
        y = x @ self.w
        return y + self.b if self.b is not None else y


class Layer(nn.Module):
    """Residual LSTM (gates i, f, g, o; the x Gemms have no bias) then LayerNorm -> Linear -> GELU -> Linear, residual."""

    def __init__(self, h: int, ffw: int):
        super().__init__()
        self.wx = nn.ParameterList([nn.Parameter(torch.zeros(h, h)) for _ in range(4)])
        self.wh = nn.ParameterList([nn.Parameter(torch.zeros(h, h)) for _ in range(4)])
        self.bh = nn.ParameterList([nn.Parameter(torch.zeros(h)) for _ in range(4)])
        self.ln_scale = nn.Parameter(torch.ones(h))
        self.ln_bias = nn.Parameter(torch.zeros(h))
        self.ffw1 = Linear(h, ffw)
        self.ffw2 = Linear(ffw, h)

    def forward(self, x, c, h):
        gates = [x @ self.wx[k] + h @ self.wh[k] + self.bh[k] for k in range(4)]
        i, f, g, o = torch.sigmoid(gates[0]), torch.sigmoid(gates[1]), torch.tanh(gates[2]), torch.sigmoid(gates[3])
        c2 = f * c + i * g
        h2 = o * torch.tanh(c2)
        y = x + h2
        z = y - y.mean(-1, keepdim=True)
        z = z / torch.sqrt((z * z).mean(-1, keepdim=True))
        z = z * self.ln_scale + self.ln_bias
        y = y + self.ffw2(F.gelu(self.ffw1(z)))
        return y, c2, h2


class Component(nn.Module):
    """One autoregressive controller component: MLP([residual, prev embedding]) -> logits; decoder of the chosen value."""

    def __init__(self, residual: int, classes: int, depth: int, last: bool):
        super().__init__()
        self.classes = classes
        self.kind = "bool" if classes == 1 else "cat"
        sizes = [residual + classes] + [residual] * depth + [classes]
        self.mlp = nn.ModuleList([Linear(a, b) for a, b in zip(sizes[:-1], sizes[1:])])
        self.decoder = Linear(classes, residual) if not last else None

    def embed(self, value: torch.Tensor) -> torch.Tensor:
        if self.kind == "bool":
            return value.float().unsqueeze(-1)
        return one_hot_empty(value, self.classes)

    def logits(self, residual: torch.Tensor, prev: torch.Tensor) -> torch.Tensor:
        x = torch.cat([residual, self.embed(prev)], -1)
        for k, lin in enumerate(self.mlp):
            x = lin(x)
            if k + 1 < len(self.mlp):
                x = torch.relu(x)
        return x.squeeze(-1) if self.kind == "bool" else x

    def sample(self, logits: torch.Tensor, temperature: torch.Tensor, noise: torch.Tensor) -> torch.Tensor:
        if self.kind == "bool":
            return noise.squeeze(-1) < torch.sigmoid(logits / temperature)
        gumbel = -torch.log(-torch.log(torch.clamp(noise, TINY, 1.0)))
        return torch.argmax(logits / temperature + gumbel, -1).to(torch.int32)

    def nll(self, logits: torch.Tensor, target: torch.Tensor) -> torch.Tensor:
        if self.kind == "bool":
            return F.binary_cross_entropy_with_logits(logits, target.float(), reduction="none")
        return F.cross_entropy(logits, target.long(), reduction="none")

    def feed(self, residual: torch.Tensor, value: torch.Tensor) -> torch.Tensor:
        return residual + self.decoder(self.embed(value)) if self.decoder is not None else residual


class Policy(nn.Module):
    def __init__(self, cfg: Config):
        super().__init__()
        self.cfg = cfg
        self.encoder = Linear(cfg.input_width, cfg.hidden)
        self.layers = nn.ModuleList([Layer(cfg.hidden, cfg.ffw) for _ in range(cfg.layers)])
        self.to_residual = Linear(cfg.hidden, cfg.residual)
        n = len(cfg.components)
        self.components = nn.ModuleList([Component(cfg.residual, classes, cfg.depth, k == n - 1)
                                         for k, (_, classes) in enumerate(cfg.components)])

    # ---- the game embedding, exactly as the graph builds it ----------------------------------------
    @staticmethod
    def _scaled(x: torch.Tensor, scale: float) -> torch.Tensor:
        return torch.clamp(x.float() * scale, -10.0, 10.0).unsqueeze(-1)

    def embed_player(self, p: dict) -> torch.Tensor:
        parts = [self._scaled(p["percent"], 0.01),
                 torch.where(p["facing"].bool(), 1.0, -1.0).unsqueeze(-1),
                 self._scaled(p["x"], 0.05), self._scaled(p["y"], 0.05),
                 F.one_hot(torch.clamp(p["action"].long(), 0, NUM_ACTIONS - 1), NUM_ACTIONS).float(),
                 F.one_hot(torch.clamp(p["character"].long(), 0, NUM_CHARACTERS - 1), NUM_CHARACTERS).float(),
                 torch.where(p["invulnerable"].bool(), 1.0, 0.0).unsqueeze(-1),
                 one_hot_empty(p["jumps_left"], self.cfg.jumps),
                 self._scaled(p["shield_strength"], 0.01),
                 torch.where(p["on_ground"].bool(), 1.0, 0.0).unsqueeze(-1)]
        return torch.cat(parts, -1)

    def embed_controller(self, a: dict) -> torch.Tensor:
        parts = [torch.where(a["buttons." + b].bool(), 1.0, 0.0).unsqueeze(-1) for b in BUTTONS]
        for k in ("main_stick.x", "main_stick.y", "c_stick.x", "c_stick.y"):
            parts.append(one_hot_empty(a[k], self.cfg.axis + 1))
        parts.append(one_hot_empty(a["shoulder"], self.cfg.shoulder + 1))
        return torch.cat(parts, -1)

    def embed(self, game: dict, prev_action: dict, name: torch.Tensor) -> torch.Tensor:
        return torch.cat([self.embed_player(game["p0"]), self.embed_player(game["p1"]),
                          F.one_hot(torch.clamp(game["stage"].long(), 0, NUM_STAGES - 1), NUM_STAGES).float(),
                          self.embed_controller(prev_action), one_hot_empty(name, self.cfg.num_names)], -1)

    # ---- the recurrent core ------------------------------------------------------------------------
    def initial_state(self, batch: int, device=None):
        z = torch.zeros(batch, self.cfg.hidden, device=device)
        return [z.clone() for _ in range(2 * self.cfg.layers)]   # c, h per layer

    def core(self, embedded: torch.Tensor, reset: torch.Tensor, state: list) -> tuple[torch.Tensor, list]:
        x = self.encoder(embedded)
        keep = (~reset.bool()).float().unsqueeze(-1)
        new_state = []
        for k, layer in enumerate(self.layers):
            c, h = state[2 * k] * keep, state[2 * k + 1] * keep
            x, c, h = layer(x, c, h)
            new_state += [c, h]
        return x, new_state

    # ---- the controller head -----------------------------------------------------------------------
    def action_values(self, a: dict) -> list:
        return [a["buttons." + b] for b in BUTTONS] + [a[k] for k in ("main_stick.x", "main_stick.y", "c_stick.x", "c_stick.y", "shoulder")]

    @staticmethod
    def action_dict(values: list) -> dict:
        out = {"buttons." + b: v for b, v in zip(BUTTONS, values[:8])}
        out.update(zip(("main_stick.x", "main_stick.y", "c_stick.x", "c_stick.y", "shoulder"), values[8:]))
        return out

    def sample(self, features: torch.Tensor, prev_action: dict, temperature: torch.Tensor, noise: list) -> dict:
        residual = self.to_residual(features)
        prev = self.action_values(prev_action)
        out = []
        for comp, p, u in zip(self.components, prev, noise):
            value = comp.sample(comp.logits(residual, p), temperature, u)
            residual = comp.feed(residual, value)
            out.append(value)
        return self.action_dict(out)

    def distance(self, features: torch.Tensor, prev_action: dict, target_action: dict) -> torch.Tensor:
        """Negative log likelihood of the target controller, summed over components: [batch]."""
        residual = self.to_residual(features)
        total = 0
        for comp, p, t in zip(self.components, self.action_values(prev_action), self.action_values(target_action)):
            total = total + comp.nll(comp.logits(residual, p), t)
            residual = comp.feed(residual, t)
        return total

    def step(self, game: dict, prev_action: dict, name: torch.Tensor, reset: torch.Tensor, state: list,
             temperature: torch.Tensor, noise: list) -> tuple[dict, list]:
        features, state = self.core(self.embed(game, prev_action, name), reset, state)
        return self.sample(features, prev_action, temperature, noise), state


# ---- loading the published weights -------------------------------------------------------------------
def load_metadata(path: Path) -> dict:
    import onnx
    model = onnx.load(str(path), load_external_data=False)
    for prop in model.metadata_props:
        if prop.key == "slippi_ai":
            return json.loads(prop.value)
    raise SystemExit(f"{path}: no slippi_ai metadata")


def from_onnx(path: Path) -> tuple[Policy, dict]:
    import onnx
    from onnx import numpy_helper
    model = onnx.load(str(path), load_external_data=False)
    meta = next(json.loads(p.value) for p in model.metadata_props if p.key == "slippi_ai")
    cfg = Config(meta)
    g = model.graph
    init = {t.name: numpy_helper.to_array(t) for t in g.initializer}
    # Cast nodes turn const_N_fp16 into const_N: resolve a Gemm input through them.
    cast = {n.output[0]: n.input[0] for n in g.node if n.op_type == "Cast" and n.input[0] in init}

    def tensor(name: str) -> torch.Tensor:
        a = init[cast.get(name, name)]
        return torch.from_numpy(np.ascontiguousarray(a.astype(np.float32)))

    gemms = [n for n in g.node if n.op_type == "Gemm"]
    lns = []   # (scale, bias) of each LayerNorm: the Mul and Add that follow a Div of a Sqrt
    for k, n in enumerate(g.node):
        if n.op_type == "Div" and k + 2 < len(g.node) and g.node[k + 1].op_type == "Mul" and g.node[k + 2].op_type == "Add" \
                and g.node[k + 1].input[1] in init and g.node[k + 2].input[1] in init and init[g.node[k + 1].input[1]].size == cfg.hidden:
            lns.append((tensor(g.node[k + 1].input[1]).reshape(-1), tensor(g.node[k + 2].input[1]).reshape(-1)))
    expected = 1 + cfg.layers * 10 + 1 + sum(3 + cfg.depth - 2 + (1 if k + 1 < len(cfg.components) else 0) for k in range(len(cfg.components)))
    if len(gemms) != expected or len(lns) != cfg.layers:
        raise SystemExit(f"{path.name}: unexpected graph ({len(gemms)} Gemm, {len(lns)} LayerNorm; expected {expected}, {cfg.layers})")
    pol = Policy(cfg)
    with torch.no_grad():
        def put(lin: Linear, node):
            lin.w.copy_(tensor(node.input[1]))
            if lin.b is not None:
                lin.b.copy_(tensor(node.input[2]).reshape(-1))
        put(pol.encoder, gemms[0])
        at = 1
        for k, layer in enumerate(pol.layers):
            for gate in range(4):
                layer.wx[gate].copy_(tensor(gemms[at].input[1]))
                layer.wh[gate].copy_(tensor(gemms[at + 1].input[1]))
                layer.bh[gate].copy_(tensor(gemms[at + 1].input[2]).reshape(-1))
                at += 2
            layer.ln_scale.copy_(lns[k][0])
            layer.ln_bias.copy_(lns[k][1])
            put(layer.ffw1, gemms[at]); put(layer.ffw2, gemms[at + 1])
            at += 2
        put(pol.to_residual, gemms[at]); at += 1
        for comp in pol.components:
            for lin in comp.mlp:
                put(lin, gemms[at]); at += 1
            if comp.decoder is not None:
                put(comp.decoder, gemms[at]); at += 1
        assert at == len(gemms)
    return pol, meta


# ---- parity against onnxruntime ------------------------------------------------------------------------
class Packer:
    """Pack logical leaves into the graph's inputs.<dtype> tensors and unpack its outputs."""

    def __init__(self, meta: dict):
        self.inputs, self.outputs = meta["inputs"], meta["outputs"]

    def pack(self, leaves: dict) -> dict:
        packed = {}
        for key, entries in self.inputs.items():
            dt = np.dtype(key.split(".", 1)[1])
            buf = np.zeros((1, max(e["offset"] + e["size"] for e in entries)), dt)
            for e in entries:
                if e["name"] in leaves:
                    buf[0, e["offset"]:e["offset"] + e["size"]] = np.asarray(leaves[e["name"]], dt).reshape(-1)
            packed[key] = buf
        return packed

    def unpack(self, outs: dict) -> dict:
        flat = {}
        for key, entries in self.outputs.items():
            for e in entries:
                flat[e["name"]] = outs[key][0, e["offset"]:e["offset"] + e["size"]].reshape(e["shape"] or ())
        return flat


def random_leaves(rng: np.random.Generator, cfg: Config, meta: dict) -> dict:
    leaves = {}
    for who in ("p0", "p1"):
        leaves.update({f"game.{who}.percent": rng.integers(0, 200), f"game.{who}.action": rng.integers(0, 420),
                       f"game.{who}.character": rng.integers(0, 33), f"game.{who}.jumps_left": rng.integers(0, 7),
                       f"game.{who}.facing": rng.integers(0, 2), f"game.{who}.invulnerable": rng.integers(0, 2),
                       f"game.{who}.on_ground": rng.integers(0, 2), f"game.{who}.x": rng.normal(0, 60),
                       f"game.{who}.y": rng.normal(10, 30), f"game.{who}.shield_strength": rng.uniform(0, 60)})
    leaves["game.stage"] = rng.integers(0, 70)
    leaves["name"] = rng.integers(0, cfg.num_names + 2)
    leaves["temperature"] = rng.choice([1.0, 0.7, 0.4])
    leaves["rating"] = 0.0
    for b in BUTTONS:
        leaves["prev_actions.0.buttons." + b] = rng.integers(0, 2)
    for k in ("main_stick.x", "main_stick.y", "c_stick.x", "c_stick.y"):
        leaves["prev_actions.0." + k] = rng.integers(0, cfg.axis + 1)
    leaves["prev_actions.0.shoulder"] = rng.integers(0, cfg.shoulder + 1)
    for key, entries in meta["inputs"].items():
        for e in entries:
            if e["name"].startswith("noise."):
                leaves[e["name"]] = rng.random(e["size"], dtype=np.float32)
    return leaves


def check(path: Path, steps: int, seed: int) -> int:
    import onnxruntime as ort
    pol, meta = from_onnx(path)
    pol.eval()
    cfg = pol.cfg
    so = ort.SessionOptions(); so.log_severity_level = 3
    sess = ort.InferenceSession(str(path), so, providers=["CPUExecutionProvider"])
    fed = {i.name for i in sess.get_inputs()}
    outs = [o.name for o in sess.get_outputs()]
    packer = Packer(meta)
    rng = np.random.default_rng(seed)
    state_np = {k: np.asarray(v["value"], dtype=v["dtype"]) for k, v in meta["initial_state"].items()}
    state_t = pol.initial_state(1)
    worst_state, bad_actions, matches = 0.0, 0, 0
    for step in range(steps):
        leaves = random_leaves(rng, cfg, meta)
        leaves["needs_reset"] = 1 if step == 0 or rng.random() < 0.02 else 0
        leaves.update(state_np)
        packed = packer.pack(leaves)
        ref = packer.unpack(dict(zip(outs, sess.run(outs, {k: v for k, v in packed.items() if k in fed}))))
        t = lambda name, dt=torch.float32: torch.tensor([leaves[name]], dtype=dt)
        game = {who: {f: t(f"game.{who}.{f}") for f in PLAYER_FIELDS} for who in ("p0", "p1")}
        game["stage"] = t("game.stage")
        prev = {k: t("prev_actions.0." + k) for k in ["buttons." + b for b in BUTTONS] + ["main_stick.x", "main_stick.y", "c_stick.x", "c_stick.y", "shoulder"]}
        noise = [torch.tensor(np.asarray(leaves[f"noise.{k}"], np.float32)).reshape(1, -1) for k in range(len(cfg.components))]
        with torch.no_grad():
            action, state_t = pol.step(game, prev, t("name"), t("needs_reset"), state_t, torch.tensor(float(leaves["temperature"])), noise)
        for k, (c_or_h) in enumerate(state_t):
            ref_state = ref[f"state.{1 + 2 * (k // 2)}.{k % 2}"]
            worst_state = max(worst_state, float(np.abs(c_or_h.numpy().reshape(-1) - ref_state.reshape(-1)).max()))
        for name, v in action.items():
            got = int(v.reshape(-1)[0].item())
            want = int(np.asarray(ref["actions.0." + name]).reshape(-1)[0])
            if got != want:
                bad_actions += 1
            else:
                matches += 1
        # both sides continue from the reference state and the sampled action, as the game does
        for k in range(len(state_t)):
            state_np[f"prev_state.{1 + 2 * (k // 2)}.{k % 2}"] = ref[f"state.{1 + 2 * (k // 2)}.{k % 2}"].reshape(1, -1)
            state_t[k] = torch.from_numpy(np.array(state_np[f"prev_state.{1 + 2 * (k // 2)}.{k % 2}"], np.float32))
    print(f"{path.name}: {steps} steps, state max abs difference {worst_state:.2e}, controller values {matches} agree, {bad_actions} differ")
    return 0 if worst_state < 1e-4 and bad_actions == 0 else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", type=Path, required=True, help="a d18 policy .onnx to twin and compare")
    ap.add_argument("--steps", type=int, default=300)
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()
    return check(args.check, args.steps, args.seed)


if __name__ == "__main__":
    sys.exit(main())
