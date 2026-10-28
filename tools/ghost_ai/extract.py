"""Turn downloaded .slp files into compact training arrays, then delete the .slp.

    py -3.12 extract.py            (loops until fetch.py is done and nothing is left)

Per game, data/<pro>/frames/<id>.npz holds, for the pro (s_) and the opponent (o_), every frame's
post-frame state and the pro's controller (the pre-frame of the same frame). features.py turns them
into the model's inputs; the game computes the same numbers from its fighter structs (mu_ghost_ai.c).
"""
import json
import hashlib
import os
import sys
import time
from pathlib import Path

import numpy as np
import peppi_py
from players import PLAYERS, verified_port

HERE = Path(os.environ.get('MELEE_GHOST_AI_HOME', Path(__file__).resolve().parent))
PROS = ["mang0", "zain", "hbox", "cody"]
CODES = {"mang0": "MANG#0", "zain": "ZAIN#0", "hbox": "HBOX#305", "cody": "IBDW#0"}


def a(x, dtype=np.float32):
    if hasattr(x, "to_numpy"):
        x = x.to_numpy(zero_copy_only=False)
    return np.asarray(x).astype(dtype)


def player_arrays(leader, prefix):
    po, pr = leader.post, leader.pre
    v = po.velocities
    out = {
        prefix + "state": a(po.state, np.int16),
        prefix + "char": a(po.character, np.int8),
        prefix + "num": np.stack([
            a(po.position.x), a(po.position.y), a(po.direction), a(po.percent), a(po.shield), a(po.stocks),
            a(po.state_age), a(po.misc_as), a(po.airborne), a(po.jumps),
            a(v.self_x_air), a(v.self_y), a(v.knockback_x), a(v.knockback_y), a(v.self_x_ground), a(po.hitlag),
        ], 1).astype(np.float16),
        prefix + "flags": np.stack([a(f, np.uint8) for f in po.state_flags], 1),
        prefix + "pad": np.stack([a(pr.joystick.x), a(pr.joystick.y), a(pr.cstick.x), a(pr.cstick.y), a(pr.triggers)], 1).astype(np.float16),
        prefix + "btn": a(pr.buttons_physical, np.uint16),
    }
    return out


def canonical_indices(frame_ids):
    """Final rollback revision of each frame, in game-time order.

    Peppi exposes speculative and repeated frame events. Treating their storage order as time
    teaches nonexistent transitions and pairs inputs with the wrong preceding state.
    """
    ids = np.asarray(frame_ids, dtype=np.int64)
    if not len(ids):
        return np.array([], dtype=np.int64)
    _, reverse_indices = np.unique(ids[::-1], return_index=True)
    last = len(ids) - 1 - reverse_indices
    last = last[(ids[last] >= -60) & (ids[last] <= ids[-1])]
    return last[np.argsort(ids[last])]


def extract(path, pro, port_hint):
    g = peppi_py.read_slippi(str(path))
    ports = [p for p in g.start.players]
    if len(ports) != 2:
        return None
    me = verified_port(ports, pro)
    if me is None:
        return None
    opp = 1 - me
    f = g.frames
    fids = a(f.id, np.int32)
    d = {"frame": fids, "stage": np.int16(g.start.stage)}
    d.update(player_arrays(f.ports[me].leader, "s_"))
    d.update(player_arrays(f.ports[opp].leader, "o_"))
    keep = canonical_indices(fids)
    for k, val in list(d.items()):
        if isinstance(val, np.ndarray) and val.shape and val.shape[0] == len(fids):
            d[k] = val[keep]
    if len(d["frame"]) < 1800 or not np.all(np.diff(d["frame"].astype(np.int64)) == 1):
        return None
    # Persist the attribution evidence in the training file. Legacy arrays without these fields
    # remain untouched and are explicitly unverified by the next training pipeline.
    d['identity_code'] = np.array(PLAYERS[pro]['code'])
    d['identity_method'] = np.array('exact_connect_code_v1')
    d['identity_port'] = np.int8(me)
    d['source_sha256'] = np.array(hashlib.sha256(Path(path).read_bytes()).hexdigest())
    d['source_replay_id'] = np.array(Path(path).stem)
    d['extract_version'] = np.int16(2)
    return d


def main():
    while True:
        did = 0
        for pro in PROS:
            base = HERE / "data" / pro
            if not (base / "ids.json").exists():
                continue
            hints = {g["id"]: g["port"] for g in json.loads((base / "ids.json").read_text())}
            (base / "frames").mkdir(exist_ok=True)
            for slp in sorted((base / "raw").glob("*.slp")):
                rid = slp.stem
                try:
                    d = extract(slp, pro, hints.get(rid, 0))
                    if d is not None:
                        np.savez_compressed(base / "frames" / f"{rid}.npz", **d)
                except BaseException as e:   # a damaged file (the parser can panic): skip it
                    if isinstance(e, KeyboardInterrupt):
                        raise
                    print(f"{pro} {rid}: {e}", flush=True)
                slp.unlink()
                with open(base / "done.txt", "a") as fh:
                    fh.write(rid + "\n")
                did += 1
        if did == 0:
            if all((HERE / f"fetch2-{p}.log").exists() and "all downloaded" in (HERE / f"fetch2-{p}.log").read_text() for p in PROS):
                break
            time.sleep(20)
    print("extract done", flush=True)


if __name__ == "__main__":
    if len(sys.argv) > 1:   # one file, for checking
        d = extract(Path(sys.argv[1]), "mang0", 0)
        for k, v in d.items():
            print(k, getattr(v, "shape", v), getattr(v, "dtype", ""))
    else:
        main()
