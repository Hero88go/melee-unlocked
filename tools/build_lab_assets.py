#!/usr/bin/env python3
"""Converts Slippi Lab's character animations into the packs the Lab view reads.

Slippi Lab (https://github.com/frankborden/slippilab, MIT) draws each character as one flat SVG
silhouette per animation frame. Those silhouettes live in public/zips/<character>.zip (one JSON
array of SVG path strings per animation) and the mapping from Melee action state to animation
lives in src/viewer/characters/*.ts. This script reads both from a local checkout and writes one
binary pack per internal character ID, so the game never parses TypeScript or opens a zip.

    git clone https://github.com/frankborden/slippilab.git
    python tools/build_lab_assets.py --slippilab slippilab --out build/lab

Then run the game with --lab-dir build/lab (package_release.py copies it to Lab\\ when present).
Nothing from Slippi Lab is committed here: like the ISO, the packs are generated locally.

Pack layout (little endian), one file per internal character ID, named <id>.lab:
  char[4] "MLAB"   u32 version (1)
  f32 scale        f32 shield_offset_x   f32 shield_offset_y   f32 shield_size
  u16 action_count, then per action state ID: u16 animation index (0xFFFF = draw nothing)
  u16 animation_count, then per animation:
      u8 name_len, name bytes, u16 frame_count, then per frame: u16 path index (0xFFFF = none)
  u32 path_count, then per path: u16 byte_len, SVG path data bytes
"""
import argparse
import json
import re
import struct
import sys
import zipfile
from pathlib import Path

MISSING = 0xFFFF


def string_array(source: str, name: str) -> list:
    start = source.index(f"export const {name} = [")
    end = source.index("] as const", start)
    return re.findall(r'"((?:[^"\\]|\\.)*)"', source[start:end])


def arithmetic(expr: str) -> float:
    expr = expr.strip()
    if not re.fullmatch(r"[0-9.\s*+/()-]+", expr):
        raise ValueError(f"unexpected expression {expr!r}")
    return float(eval(expr, {"__builtins__": {}}))  # digits and operators only, checked above


def parse_character(path: Path) -> dict:
    src = path.read_text(encoding="utf-8")
    scale = arithmetic(re.search(r"\bscale:\s*([^,\n]+),", src).group(1))
    off = re.search(r"shieldOffset:\s*\[([^\]]+)\]", src).group(1).split(",")
    shield_size = arithmetic(re.search(r"shieldSize:\s*([^,\n]+),", src).group(1))

    def block(key: str) -> str:
        i = src.index(key)
        j = src.index("])", i)
        return src[i:j]

    animation_map = dict(re.findall(r'\[\s*"([^"]+)",\s*"([^"]*)"\s*\]', block("animationMap")))
    specials = {int(k): v for k, v in re.findall(r'\[\s*(\d+),\s*"([^"]*)"\s*\]', block("specialsMap"))}
    return {
        "scale": scale,
        "shield_offset": (arithmetic(off[0]), arithmetic(off[1])),
        "shield_size": shield_size,
        "animation_map": animation_map,
        "specials": specials,
    }


def load_zip(path: Path) -> dict:
    with zipfile.ZipFile(path) as z:
        return {Path(n).stem: json.loads(z.read(n)) for n in z.namelist() if n.endswith(".json")}


def lookup(character: dict, action_id: int, name) -> str:
    """Slippi Lab's computeRenderData lookup: the character's own map for the action's name, then
    its specials by ID, then the action name itself. "" means Slippi Lab draws nothing."""
    if name is not None and name in character["animation_map"]:
        return character["animation_map"][name]
    if action_id in character["specials"]:
        return character["specials"][action_id]
    return name or ""


def stand_ins(name: str) -> list:
    """Action names to try, in order, when an action has no animation of its own. Only ever the
    same move in another form, so a stand-in is the right shape, just not the exact variant:
      AppealL / AppealR -> Appeal, and Appeal -> AppealL / AppealR (which one a character's zip
        has differs: Captain Falcon's taunt was mapped to "Appeal" but only AppealL/R exist)
      AttackS4Hi / AttackS4LwS ... -> AttackS4S (angled forward smash drawn as the straight one)
      AttackS3Hi / AttackS3Lw ...  -> AttackS3S (angled forward tilt as the straight one)
    Anything else keeps no animation here; the game holds the last silhouette shown instead of
    drawing nothing (lab_view.cpp), so a character is never invisible mid-move."""
    out = []
    if name.endswith(("L", "R")) and len(name) > 1:
        out.append(name[:-1])
    else:
        out += [name + "L", name + "R"]
    m = re.fullmatch(r"AttackS([34])(Hi|Lw)S?", name)
    if m:
        out.append(f"AttackS{m.group(1)}S")
    return out


def resolve(character: dict, animations: dict, action_id: int, name, by_name: dict):
    anim = lookup(character, action_id, name)
    if anim in animations:
        return anim
    if name is None:
        return None
    for alt in [name] + stand_ins(name):
        for candidate in (lookup(character, by_name.get(alt, -1), alt), alt):
            if candidate and candidate in animations:
                return candidate
    # The mapped animation name itself may be the one needing an L/R form ("Appeal" -> "AppealL").
    for alt in stand_ins(anim) if anim else []:
        if alt in animations:
            return alt
    return None


def build_pack(character: dict, animations: dict, action_names: list) -> bytes:
    action_count = max(len(action_names), max(character["specials"], default=0) + 1)
    by_name = {n: i for i, n in enumerate(action_names)}
    wanted = []
    for action_id in range(action_count):
        name = action_names[action_id] if action_id < len(action_names) else None
        wanted.append(resolve(character, animations, action_id, name, by_name))

    names = sorted({a for a in wanted if a})
    index_of = {n: i for i, n in enumerate(names)}
    paths, path_index = [], {}

    def intern(d: str) -> int:
        if d not in path_index:
            path_index[d] = len(paths)
            paths.append(d)
        return path_index[d]

    out = bytearray(b"MLAB")
    out += struct.pack("<I4f", 1, character["scale"], *character["shield_offset"], character["shield_size"])
    out += struct.pack("<H", action_count)
    for anim in wanted:
        out += struct.pack("<H", index_of[anim] if anim else MISSING)
    out += struct.pack("<H", len(names))
    for name in names:
        frames = animations[name]
        encoded = name.encode("ascii")
        out += struct.pack("<B", len(encoded)) + encoded + struct.pack("<H", len(frames))
        for frame in frames:
            # Duplicate frames are stored as "frameN", a reference to an earlier frame.
            if isinstance(frame, str) and frame.startswith("frame"):
                ref = int(frame[len("frame"):])
                frame = frames[ref] if 0 <= ref < len(frames) else None
            out += struct.pack("<H", intern(frame) if isinstance(frame, str) and frame else MISSING)
    if len(paths) >= MISSING:
        raise SystemExit("too many unique frames for a u16 index")
    out += struct.pack("<I", len(paths))
    for d in paths:
        data = d.encode("ascii")
        out += struct.pack("<H", len(data)) + data
    return bytes(out)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--slippilab", type=Path, required=True, help="path to a slippilab checkout")
    ap.add_argument("--out", type=Path, required=True, help="output folder for the .lab packs")
    args = ap.parse_args()

    root = args.slippilab
    ids = (root / "src/common/ids.ts").read_text(encoding="utf-8")
    action_names = string_array(ids, "actionNameById")
    index = (root / "src/viewer/characters/index.ts").read_text(encoding="utf-8")
    order_src = index[index.index("actionMapByInternalId = ["):]
    order_src = order_src[: order_src.index("];")]
    order = re.findall(r"^\s*(\w+),", order_src, re.M)
    if len(order) < 27:
        raise SystemExit(f"expected 27 internal characters in index.ts, found {len(order)}")

    args.out.mkdir(parents=True, exist_ok=True)
    cache = {}
    total = 0
    for internal_id, name in enumerate(order):
        if name not in cache:
            character = parse_character(root / "src/viewer/characters" / f"{name}.ts")
            animations = load_zip(root / "public/zips" / f"{name}.zip")
            cache[name] = build_pack(character, animations, action_names)
        pack = cache[name]
        (args.out / f"{internal_id}.lab").write_bytes(pack)
        total += len(pack)
        print(f"{internal_id:2d} {name:16s} {len(pack) / 1e6:6.2f} MB")
    print(f"wrote {len(order)} packs, {total / 1e6:.1f} MB, to {args.out}")


if __name__ == "__main__":
    sys.exit(main())
