"""Export a fine-tuned PyTorch twin (phillip_torch.Policy) as an ONNX policy the game runs unchanged.

The graph is rebuilt the way slippi-ai's exporter lays it out (format version 1): the three packed
inputs (inputs.int32, inputs.bool, inputs.float32, leaf layout copied from the base policy's
metadata), the same game embedding, LSTM layers, controller head and sampling from the noise inputs,
and the three packed outputs. Weights are stored as float16 like the published files (21 MB). The
slippi_ai metadata is the base policy's with the name table of the fine-tune; phillip_ai.cpp reads
only that metadata, so the file drops into ProAI/ beside the published ones.

    py -3.12 tools/ghost_ai/phillip_export.py --base <falco_d18_imitation_v3.onnx> --model <out>/model.pt
        --out <ProAI>/falco_d18_mang0_v1.onnx [--verify 300]      (onnxruntime on PYTHONPATH for --verify)
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
import onnx
import torch
from onnx import TensorProto, helper, numpy_helper

sys.path.insert(0, str(Path(__file__).resolve().parent))
from phillip_torch import BUTTONS, NUM_ACTIONS, NUM_CHARACTERS, NUM_STAGES, PLAYER_FIELDS, TINY, Packer, Policy, from_onnx, random_leaves  # noqa: E402

STICKS = ["main_stick.x", "main_stick.y", "c_stick.x", "c_stick.y"]


class Builder:
    def __init__(self):
        self.nodes, self.inits = [], []
        self.n = 0

    def name(self, hint: str) -> str:
        self.n += 1
        return f"{hint}_{self.n}"

    def const(self, value, dtype=np.float32, hint="c") -> str:
        name = self.name(hint)
        self.inits.append(numpy_helper.from_array(np.asarray(value, dtype=dtype), name))
        return name

    def op(self, kind: str, inputs: list, hint: str = "", **attrs) -> str:
        out = self.name(hint or kind.lower())
        self.nodes.append(helper.make_node(kind, inputs, [out], **attrs))
        return out

    # weights: fp16 in the file, cast to float in the graph (as the published policies are)
    def weight(self, w: torch.Tensor, hint: str) -> str:
        half = self.const(w.detach().cpu().numpy().astype(np.float16), np.float16, hint + "_fp16")
        return self.op("Cast", [half], hint, to=TensorProto.FLOAT)

    def bias(self, b: torch.Tensor, hint: str) -> str:
        return self.const(b.detach().cpu().numpy().astype(np.float32), np.float32, hint)

    def gemm(self, x: str, lin, hint: str) -> str:
        ins = [x, self.weight(lin.w, hint + "_w")]
        if lin.b is not None:
            ins.append(self.bias(lin.b, hint + "_b"))
        return self.op("Gemm", ins, hint, alpha=1.0, beta=1.0 if lin.b is not None else 0.0, transB=0)

    def slice(self, pack: str, offset: int, size: int, hint: str) -> str:
        return self.op("Slice", [pack, self.const([offset], np.int64, "s"), self.const([offset + size], np.int64, "e"),
                                 self.const([1], np.int64, "ax")], hint)

    def column(self, x: str, hint: str) -> str:   # [1,1] -> [1]
        return self.op("Reshape", [x, self.const([1], np.int64, "shape")], hint)

    def one_hot(self, idx: str, n: int, hint: str) -> str:   # [1] int -> [1, n] float; out of range -> zeros
        i64 = self.op("Cast", [idx], hint + "_i64", to=TensorProto.INT64)
        return self.op("OneHot", [i64, self.const(n, np.int64, "depth"), self.const([0.0, 1.0], np.float32, "vals")], hint, axis=-1)

    def clamp_int(self, x: str, lo: int, hi: int, hint: str) -> str:
        return self.op("Min", [self.op("Max", [x, self.const(lo, np.int32, "lo")], hint + "_max"), self.const(hi, np.int32, "hi")], hint)

    def scaled(self, x: str, scale: float, hint: str) -> str:   # [1,1] float -> *scale, clamp [-10, 10]
        y = self.op("Mul", [x, self.const(scale, np.float32, "scale")], hint + "_mul")
        y = self.op("Max", [y, self.const(-10.0, np.float32, "lo")], hint + "_max")
        return self.op("Min", [y, self.const(10.0, np.float32, "hi")], hint)

    def where(self, cond: str, on: float, off: float, hint: str) -> str:
        return self.op("Where", [cond, self.const(on, np.float32, "on"), self.const(off, np.float32, "off")], hint)


def build(pol: Policy, meta: dict) -> onnx.ModelProto:
    cfg = pol.cfg
    b = Builder()
    layout = {key: {l["name"]: (l["offset"], l["size"]) for l in leaves} for key, leaves in meta["inputs"].items()}
    width = {key: max(l["offset"] + l["size"] for l in leaves) for key, leaves in meta["inputs"].items()}
    packs = {"int32": "inputs.int32", "bool": "inputs.bool", "float32": "inputs.float32"}

    def leaf(name: str) -> str:
        for key, leaves in layout.items():
            if name in leaves:
                off, size = leaves[name]
                return b.slice(packs[key.split(".", 1)[1]], off, size, name.replace(".", "_"))
        raise KeyError(name)

    # ---- the game embedding --------------------------------------------------------------------------
    def player(who: str) -> list[str]:
        g = f"game.{who}."
        percent = b.op("Cast", [leaf(g + "percent")], who + "_pct_f", to=TensorProto.FLOAT)
        action = b.one_hot(b.clamp_int(b.column(leaf(g + "action"), who + "_act"), 0, NUM_ACTIONS - 1, who + "_actc"), NUM_ACTIONS, who + "_action")
        char = b.one_hot(b.clamp_int(b.column(leaf(g + "character"), who + "_chr"), 0, NUM_CHARACTERS - 1, who + "_chrc"), NUM_CHARACTERS, who + "_char")
        jumps = b.one_hot(b.column(leaf(g + "jumps_left"), who + "_jmp"), cfg.jumps, who + "_jumps")
        return [b.scaled(percent, 0.01, who + "_percent"), b.where(leaf(g + "facing"), 1.0, -1.0, who + "_facing"),
                b.scaled(leaf(g + "x"), 0.05, who + "_x"), b.scaled(leaf(g + "y"), 0.05, who + "_y"), action, char,
                b.where(leaf(g + "invulnerable"), 1.0, 0.0, who + "_invul"), jumps,
                b.scaled(leaf(g + "shield_strength"), 0.01, who + "_shield"), b.where(leaf(g + "on_ground"), 1.0, 0.0, who + "_ground")]

    parts = player("p0") + player("p1")
    parts.append(b.one_hot(b.clamp_int(b.column(leaf("game.stage"), "stage"), 0, NUM_STAGES - 1, "stagec"), NUM_STAGES, "stage_oh"))
    prev = {}
    for name in BUTTONS:
        prev["buttons." + name] = leaf("prev_actions.0.buttons." + name)
        parts.append(b.where(prev["buttons." + name], 1.0, 0.0, "prev_" + name))
    for k in STICKS:
        prev[k] = b.column(leaf("prev_actions.0." + k), "prev_" + k.replace(".", "_"))
        parts.append(b.one_hot(prev[k], cfg.axis + 1, "prev_oh_" + k.replace(".", "_")))
    prev["shoulder"] = b.column(leaf("prev_actions.0.shoulder"), "prev_shoulder")
    parts.append(b.one_hot(prev["shoulder"], cfg.shoulder + 1, "prev_oh_shoulder"))
    parts.append(b.one_hot(b.column(leaf("name"), "name"), cfg.num_names, "name_oh"))
    embedded = b.op("Concat", parts, "embedded", axis=1)

    # ---- the recurrent core --------------------------------------------------------------------------
    x = b.gemm(embedded, pol.encoder, "encoder")
    reset = leaf("needs_reset")
    states_out = []
    for k, layer in enumerate(pol.layers):
        zero = b.const(np.zeros((1, cfg.hidden), np.float32), np.float32, f"zeros{k}")
        c = b.op("Where", [reset, zero, leaf(f"prev_state.{1 + 2 * k}.0")], f"c{k}")
        h = b.op("Where", [reset, zero, leaf(f"prev_state.{1 + 2 * k}.1")], f"h{k}")
        gates = []
        for gi in range(4):
            gx = b.op("Gemm", [x, b.weight(layer.wx[gi], f"l{k}_wx{gi}")], f"l{k}_gx{gi}", alpha=1.0, beta=0.0, transB=0)
            gh = b.op("Gemm", [h, b.weight(layer.wh[gi], f"l{k}_wh{gi}"), b.bias(layer.bh[gi], f"l{k}_bh{gi}")], f"l{k}_gh{gi}", alpha=1.0, beta=1.0, transB=0)
            gates.append(b.op("Add", [gx, gh], f"l{k}_gate{gi}"))
        i = b.op("Sigmoid", [gates[0]], f"l{k}_i")
        f = b.op("Sigmoid", [gates[1]], f"l{k}_f")
        g = b.op("Tanh", [gates[2]], f"l{k}_g")
        o = b.op("Sigmoid", [gates[3]], f"l{k}_o")
        c2 = b.op("Add", [b.op("Mul", [f, c], f"l{k}_fc"), b.op("Mul", [i, g], f"l{k}_ig")], f"l{k}_c2")
        h2 = b.op("Mul", [o, b.op("Tanh", [c2], f"l{k}_tc")], f"l{k}_h2")
        states_out += [c2, h2]
        y = b.op("Add", [x, h2], f"l{k}_res")
        mean = b.op("ReduceMean", [y, b.const([1], np.int64, "axes")], f"l{k}_mean", keepdims=1)
        z = b.op("Sub", [y, mean], f"l{k}_center")
        var = b.op("ReduceMean", [b.op("Mul", [z, z], f"l{k}_sq"), b.const([1], np.int64, "axes")], f"l{k}_var", keepdims=1)
        z = b.op("Div", [z, b.op("Sqrt", [var], f"l{k}_std")], f"l{k}_norm")
        z = b.op("Add", [b.op("Mul", [z, b.bias(layer.ln_scale.reshape(1, -1), f"l{k}_lns")], f"l{k}_lnm"), b.bias(layer.ln_bias.reshape(1, -1), f"l{k}_lnb")], f"l{k}_ln")
        z = b.op("Gelu", [b.gemm(z, layer.ffw1, f"l{k}_ffw1")], f"l{k}_gelu", approximate="none")
        x = b.op("Add", [y, b.gemm(z, layer.ffw2, f"l{k}_ffw2")], f"l{k}_out")

    # ---- the controller head -------------------------------------------------------------------------
    temperature = b.column(leaf("temperature"), "temperature")
    residual = b.gemm(x, pol.to_residual, "to_residual")
    keys = ["buttons." + n for n in BUTTONS] + STICKS + ["shoulder"]
    noise_names = [l["name"] for leaves in meta["inputs"].values() for l in leaves if l["name"].startswith("noise.")]
    noise_names.sort(key=lambda s: int(s.split(".")[1]))
    sampled = {}
    for ci, (comp, key) in enumerate(zip(pol.components, keys)):
        tag = key.replace(".", "_")
        if comp.kind == "bool":
            emb = b.where(prev[key], 1.0, 0.0, f"{tag}_prev_emb")
        else:
            emb = b.one_hot(prev[key], comp.classes, f"{tag}_prev_emb")
        h = b.op("Concat", [residual, emb], f"{tag}_in", axis=1)
        for li, lin in enumerate(comp.mlp):
            h = b.gemm(h, lin, f"{tag}_mlp{li}")
            if li + 1 < len(comp.mlp):
                h = b.op("Max", [h, b.const(0.0, np.float32, "zero")], f"{tag}_relu{li}")
        logits = b.op("Div", [h, temperature], f"{tag}_logits")
        u = leaf(noise_names[ci])
        if comp.kind == "bool":
            p = b.op("Sigmoid", [logits], f"{tag}_p")
            sample = b.op("Less", [u, p], f"{tag}_sample")          # [1,1] bool
            sample_emb = b.where(sample, 1.0, 0.0, f"{tag}_semb")
        else:
            gumbel = b.op("Neg", [b.op("Log", [b.op("Neg", [b.op("Log", [b.op("Clip", [u, b.const(TINY, np.float32, "tiny"), b.const(1.0, np.float32, "one")], f"{tag}_clip")], f"{tag}_log1")], f"{tag}_neg1")], f"{tag}_log2")], f"{tag}_gumbel")
            arg = b.op("ArgMax", [b.op("Add", [logits, gumbel], f"{tag}_pert")], f"{tag}_argmax", axis=1, keepdims=0)
            sample = b.op("Cast", [arg], f"{tag}_sample", to=TensorProto.INT32)   # [1] int32
            sample_emb = b.one_hot(sample, comp.classes, f"{tag}_semb")
        sampled[key] = sample
        if comp.decoder is not None:
            residual = b.op("Add", [residual, b.gemm(sample_emb, comp.decoder, f"{tag}_dec")], f"{tag}_res")

    # ---- the packed outputs, in the base policy's layout ----------------------------------------------
    def out_pack(key: str, make) -> None:
        leaves = sorted(meta["outputs"][key], key=lambda l: l["offset"])
        cols = [make(l["name"]) for l in leaves]
        b.nodes.append(helper.make_node("Concat", cols, [key], axis=1))

    ints = {"actions.0." + k: sampled[k] for k in STICKS + ["shoulder"]}
    out_pack("outputs.int32", lambda n: b.op("Reshape", [ints[n], b.const([1, 1], np.int64, "shape")], "o_" + n.replace(".", "_")))
    out_pack("outputs.bool", lambda n: sampled[n.split("actions.0.")[1]])
    state_by_name = {f"state.{1 + 2 * (k // 2)}.{k % 2}": s for k, s in enumerate(states_out)}
    out_pack("outputs.float32", lambda n: state_by_name[n])

    inputs = [helper.make_tensor_value_info("inputs.int32", TensorProto.INT32, [1, width["inputs.int32"]]),
              helper.make_tensor_value_info("inputs.bool", TensorProto.BOOL, [1, width["inputs.bool"]]),
              helper.make_tensor_value_info("inputs.float32", TensorProto.FLOAT, [1, width["inputs.float32"]])]
    out_width = {key: max(l["offset"] + l["size"] for l in leaves) for key, leaves in meta["outputs"].items()}
    outputs = [helper.make_tensor_value_info("outputs.int32", TensorProto.INT32, [1, out_width["outputs.int32"]]),
               helper.make_tensor_value_info("outputs.bool", TensorProto.BOOL, [1, out_width["outputs.bool"]]),
               helper.make_tensor_value_info("outputs.float32", TensorProto.FLOAT, [1, out_width["outputs.float32"]])]
    graph = helper.make_graph(b.nodes, "phillip_finetune", inputs, outputs, initializer=b.inits)
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 21)], producer_name="melee-unlocked phillip_export")
    model.ir_version = 10
    model.metadata_props.append(onnx.StringStringEntryProto(key="slippi_ai", value=json.dumps(meta)))
    onnx.checker.check_model(model)
    return model


def verify(pol: Policy, path: Path, meta: dict, steps: int, seed: int) -> bool:
    """The exported file against the twin on the same inputs, states and noise."""
    import onnxruntime as ort
    so = ort.SessionOptions(); so.log_severity_level = 3
    sess = ort.InferenceSession(str(path), so, providers=["CPUExecutionProvider"])
    outs = [o.name for o in sess.get_outputs()]
    packer = Packer(meta)
    rng = np.random.default_rng(seed)
    pol.eval()
    cfg = pol.cfg
    state_np = {k: np.asarray(v["value"], dtype=v["dtype"]) for k, v in meta["initial_state"].items()}
    worst, differ, agree = 0.0, 0, 0
    for step in range(steps):
        leaves = random_leaves(rng, cfg, meta)
        leaves["needs_reset"] = 1 if step == 0 or rng.random() < 0.02 else 0
        leaves.update(state_np)
        ref = packer.unpack(dict(zip(outs, sess.run(outs, packer.pack(leaves)))))
        t = lambda name, dt=torch.float32: torch.tensor([leaves[name]], dtype=dt)
        game = {who: {f: t(f"game.{who}.{f}") for f in PLAYER_FIELDS} for who in ("p0", "p1")}
        game["stage"] = t("game.stage")
        prev = {k: t("prev_actions.0." + k) for k in ["buttons." + n for n in BUTTONS] + STICKS + ["shoulder"]}
        noise = [torch.tensor(np.asarray(leaves[f"noise.{k}"], np.float32)).reshape(1, -1) for k in range(len(cfg.components))]
        state_t = [torch.from_numpy(np.array(state_np[f"prev_state.{1 + 2 * (k // 2)}.{k % 2}"], np.float32)) for k in range(2 * cfg.layers)]
        with torch.no_grad():
            action, state_t = pol.step(game, prev, t("name"), t("needs_reset"), state_t, torch.tensor(float(leaves["temperature"])), noise)
        for k, s in enumerate(state_t):
            key = f"state.{1 + 2 * (k // 2)}.{k % 2}"
            worst = max(worst, float(np.abs(s.numpy().reshape(-1) - ref[key].reshape(-1)).max()))
            state_np["prev_" + key] = ref[key].reshape(1, -1)
        for name, v in action.items():
            if int(v.reshape(-1)[0].item()) == int(np.asarray(ref["actions.0." + name]).reshape(-1)[0]):
                agree += 1
            else:
                differ += 1
    print(f"{path.name}: {steps} steps against the twin, state max abs difference {worst:.2e}, controller values {agree} agree, {differ} differ")
    return worst < 1e-3 and differ == 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", type=Path, required=True, help="the published policy the fine-tune started from")
    ap.add_argument("--model", type=Path, help="model.pt from phillip_finetune.py (default: export the base weights)")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--verify", type=int, default=0, help="steps to compare the file with the twin")
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()
    pol, meta = from_onnx(args.base)
    if args.model:
        saved = torch.load(args.model, map_location="cpu")
        if saved["num_names"] != pol.cfg.num_names:
            from phillip_finetune import grow_name_table
            grow_name_table(pol, meta, saved["new_name"])
        pol.load_state_dict(saved["state_dict"])
        meta["name_map"] = saved["name_map"]
        meta["config"]["max_names"] = pol.cfg.num_names
        meta["agent_config"] = {"finetune": {"base": saved["base"], "name_code": saved["name_code"], "step": saved.get("step"),
                                             "dev": saved.get("dev")}}
    # The file stores weight matrices in float16, as the published policies do (biases and layer norms stay float32).
    # Round the twin's matrices the same way first, so --verify checks the file against exactly what it holds; a
    # fine-tune's float32 weights otherwise differ from their float16 copies by up to about 1e-2 in the recurrent state.
    with torch.no_grad():
        from phillip_torch import Linear
        for mod in pol.modules():
            if isinstance(mod, Linear):
                mod.w.copy_(mod.w.half().float())
        for layer in pol.layers:
            for w in list(layer.wx) + list(layer.wh):
                w.copy_(w.half().float())
    model = build(pol, meta)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(model, str(args.out))
    print(f"wrote {args.out} ({args.out.stat().st_size / 1e6:.1f} MB), names {list(meta['name_map'])[-3:]}")
    if args.verify:
        return 0 if verify(pol, args.out, meta, args.verify, args.seed) else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
