"""Build the ProAI folder that Fight a pro reads: Phillip's published ONNX policies plus catalog.json.

Phillip (slippi-ai, Vlad Firoiu) publishes its trained policies under the MIT license on Hugging Face
(vladfi/phillip-models, index-v1.json). This tool fetches the chosen ones at the pinned commit, checks
each file's sha256 against the published index, reads each policy's own metadata (delay, the players
it can be conditioned on, the opponents it was trained against) and writes catalog.json for
port/runtime/hle/phillip_ai.cpp. Only policies whose inputs the game provides are accepted. The
--bundle kinds (imitation: one per pro) are copied into the folder; the game downloads the others
(one per matchup) from the catalog's pinned URL the first time that matchup is played.

    py -3.12 tools/ghost_ai/phillip_catalog.py --models <download dir> --out <ProAI dir> [--kinds imitation,rl]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import sys
import urllib.request
from pathlib import Path

INDEX_URL = "https://huggingface.co/vladfi/phillip-models/resolve/main/index-v1.json"
# Fight a pro's players (their index is ghost_ai's and the game's), the character each one plays and
# the connect code Phillip's policies know them by.
PROS = {1: ("zain", "MARTH", ["ZAIN#0", "Zain"]),
        3: ("cody", "FOX", ["IBDW#0", "Cody"]),
        4: ("amsa", "YOSHI", ["AMSA#0", "Amsa"])}
# libmelee character names -> the game's internal fighter kind.
KINDS = {"MARIO": 0, "FOX": 1, "CPTFALCON": 2, "DK": 3, "KIRBY": 4, "BOWSER": 5, "LINK": 6, "SHEIK": 7,
         "NESS": 8, "PEACH": 9, "POPO": 10, "NANA": 11, "PIKACHU": 12, "SAMUS": 13, "YOSHI": 14,
         "JIGGLYPUFF": 15, "MEWTWO": 16, "LUIGI": 17, "MARTH": 18, "ZELDA": 19, "YLINK": 20, "DOC": 21,
         "FALCO": 22, "PICHU": 23, "GAMEANDWATCH": 24, "GANONDORF": 25, "ROY": 26}


def supported(m: dict) -> str:
    """'' when the game supplies every input this policy reads (mirrors phillip_ai.cpp supported())."""
    if m.get("format_version") != 1 or m.get("batch_size") != 1:
        return "format"
    if m["controller"]["type"] not in ("default", "custom_v1"):
        return "controller encoding"
    cfg = m["config"]
    e = cfg["embed"]
    if e.get("items", {}).get("type", "skip") not in ("skip", "flat"):
        return "items"
    p = e["player"]
    if p.get("with_speeds") or p.get("with_controller"):
        return "player fields"
    o = cfg.get("observation", {})
    if o.get("frame_skip", {}).get("skip", 0):
        return "observation filter"
    if cfg["policy"].get("frame_skip", 1) != 1:
        return "frame skip"
    return ""


def version(name: str) -> tuple:
    """falco_d21_vs_fox_v6.2 -> (6, 2); a name without a version -> (0,)."""
    found = re.search(r"_v(\d+(?:\.\d+)*)$", name)
    return tuple(int(x) for x in found.group(1).split(".")) if found else (0,)


def latest(models: list) -> list:
    """Of the published versions of one policy (same character, opponents and delay), the newest."""
    best = {}
    for e in models:
        key = (tuple(e["characters"]), tuple(sorted(e["opponents"])), e["delay"], e["agent_type"])
        if key not in best or version(e["name"]) > version(best[key]["name"]):
            best[key] = e
    return [e for e in models if best.get((tuple(e["characters"]), tuple(sorted(e["opponents"])), e["delay"], e["agent_type"])) is e]


def metadata(path: Path) -> dict:
    import onnxruntime as ort
    so = ort.SessionOptions()
    so.log_severity_level = 3
    s = ort.InferenceSession(str(path), so, providers=["CPUExecutionProvider"])
    return json.loads(s.get_modelmeta().custom_metadata_map["slippi_ai"])


def fetch(entry: dict, folder: Path) -> Path:
    dest = folder / f"{entry['name']}.onnx"
    if dest.is_file() and hashlib.sha256(dest.read_bytes()).hexdigest() == entry["sha256"]:
        return dest
    req = urllib.request.Request(entry["url"], headers={"User-Agent": "melee-unlocked-pro-ai/1.0"})
    data = urllib.request.urlopen(req, timeout=600).read()
    if hashlib.sha256(data).hexdigest() != entry["sha256"]:
        raise SystemExit(f"{entry['name']}: sha256 does not match the published index")
    dest.write_bytes(data)
    return dest


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--models", type=Path, required=True, action="append",
                    help="folder(s) holding the .onnx files; downloads go to the first")
    ap.add_argument("--out", type=Path, required=True, help="the ProAI folder to write")
    ap.add_argument("--index", type=Path, help="a saved index-v1.json (default: fetch it)")
    ap.add_argument("--kinds", default="imitation,rl", help="the policies the catalog lists: imitation and/or rl")
    ap.add_argument("--bundle", default="imitation",
                    help="the kinds copied into the folder to ship; the others download in game on first use")
    ap.add_argument("--only", default="", help="comma list of model names to include (default: all that fit)")
    ap.add_argument("--fetch", action="store_true", help="download missing models")
    ap.add_argument("--max-mb", type=float, default=50.0,
                    help="leave out larger policies (Phillip's 90 MB ones need more than a frame a step on most PCs)")
    ap.add_argument("--delays", default="18", help="reaction delays to take, comma separated")
    args = ap.parse_args()

    if args.index:
        index = json.loads(args.index.read_text())
    else:
        index = json.loads(urllib.request.urlopen(INDEX_URL, timeout=60).read())
    kinds = set(args.kinds.split(","))
    bundle = set(args.bundle.split(","))
    only = set(filter(None, args.only.split(",")))
    args.models[0].mkdir(parents=True, exist_ok=True)
    args.out.mkdir(parents=True, exist_ok=True)
    wanted_chars = {char for _, char, _ in PROS.values()}
    models, skipped = [], []
    delays = {int(d) for d in args.delays.split(",")}
    allowed = [e for e in index["models"] if e["size"] <= args.max_mb * 1e6 and e["delay"] in delays]
    for e in latest(allowed):
        if len(e["characters"]) != 1 or e["characters"][0] not in wanted_chars:
            continue
        if only and e["name"] not in only:
            continue
        kind = "imitation" if e["agent_type"] == "IMITATION" else "rl"
        if kind not in kinds:
            continue
        path = next((d / f"{e['name']}.onnx" for d in args.models if (d / f"{e['name']}.onnx").is_file()),
                    args.models[0] / f"{e['name']}.onnx")
        if not path.is_file():
            if not args.fetch:
                skipped.append((e["name"], "not downloaded"))
                continue
            path = fetch(e, args.models[0])
        if hashlib.sha256(path.read_bytes()).hexdigest() != e["sha256"]:
            raise SystemExit(f"{e['name']}: sha256 does not match the published index")
        m = metadata(path)
        why = supported(m)
        if why:
            skipped.append((e["name"], why))
            continue
        character = KINDS[e["characters"][0]]
        names = {}
        for pro, (_, char, ids) in PROS.items():
            if KINDS[char] != character:
                continue
            code = next((m["name_map"][i] for i in ids if i in m["name_map"]), None)
            if code is not None:
                names[str(pro)] = int(code)
        if not names:
            skipped.append((e["name"], "no Fight a pro player in its names"))
            continue
        opponents = [] if kind == "imitation" else sorted(KINDS[o] for o in e["opponents"])
        # Which plays first: Phillip's newest self-play for the matchup (it beat the older matchup
        # policies 7 of 8 head to head), then the older matchup self-play, then its newest policy for
        # the whole character (even with the older matchup ones), then imitation. The newest are 90 MB
        # ("heavy": only on a PC that steps them fast enough).
        newest = m["controller"]["type"] == "custom_v1"
        if kind == "imitation":
            priority = 1
        elif newest:
            priority = 4 if len(opponents) == 1 else 2
        else:
            priority = 3
        if kind in bundle:
            shutil.copy2(path, args.out / path.name)
        elif (args.out / path.name).exists():
            (args.out / path.name).unlink()
        models.append({"file": path.name, "sha256": e["sha256"], "kind": kind, "character": character,
                       "opponents": opponents, "delay": int(m["config"]["policy"]["delay"]), "names": names,
                       "priority": priority, "heavy": path.stat().st_size > 50 * 1024 * 1024, "source": e["url"]})
    catalog = {"version": 1, "credit": "Phillip (slippi-ai) by Vlad Firoiu, MIT license; models from "
               "huggingface.co/vladfi/phillip-models", "models": models}
    (args.out / "catalog.json").write_text(json.dumps(catalog, indent=1) + "\n")
    for pro, (name, char, _) in PROS.items():
        have = [x["file"] for x in models if str(pro) in x["names"]]
        print(f"{name}: {len(have)} policies")
    for name, why in skipped:
        print(f"skipped {name}: {why}")
    shipped = [x for x in models if (args.out / x["file"]).is_file()]
    print(f"wrote {args.out / 'catalog.json'}: {len(models)} policies listed, {len(shipped)} in the folder "
          f"({sum((args.out / x['file']).stat().st_size for x in shipped) / 1e6:.0f} MB)")


if __name__ == "__main__":
    sys.exit(main())
