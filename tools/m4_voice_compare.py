#!/usr/bin/env python3
"""M4 per-voice audio comparison (the "true GameCube audio" standard).

Both runs of a lockstep pair must be traced with MELEE_TRACE_AX_VOICES:
  - host (port/runtime/hle/ax_ucode.h): one line per AX frame, and per frame one line for every
    parameter block the ucode rendered, with FNV-1a hashes of that voice's own 160 samples before the
    final mix (after its volume envelope, and every mixer contribution) and, whenever the CPU changed
    the block, its words as the frame began;
  - game: every HSD_AudioSFXStartParam request, disc-stream start and first stream data, and every
    SFX sample-file load with its ARAM base ("[ax-request-*]", "[ax-stream-*]", "[ax-bank-*]"), all
    with the timebase.

Voices are rebuilt from the blocks, paired between the engines inside each M4 window (anchored on the
state digest's GM_MENU and match-frame-1 retraces) by the file and offset their sample came from, and
checked:
  1. identical parameter block at the voice's first frame: every field except the block's own
     pointers and the ucode's depop history, with sample addresses compared relative to the ARAM base
     of the sample file (or disc-stream buffer) they belong to;
  2. identical per-voice hashes frame by frame from each voice's own start frame, for as long as both
     engines play it inside the window;
  3. start at the AX frame the AXOut model gives: __AXOutNewFrame syncs parameter blocks, runs the DSP,
     then the user callback services queued requests, so a request is serviced by the first AX frame
     whose nominal AI time follows it and renders one frame later; a disc stream's first-data callback
     arms its voices outside the AX callback, so they render in the first AX frame after it. The
     engines' start frames, counted from the first AX frame at or after the window anchor, must differ
     by at most one.
"""
import argparse
import bisect
import collections
import csv
import json
import re
from pathlib import Path

TB_PER_FRAME = 675000          # host::TB_HZ / 60
AX_PERIOD = 202500             # 0x280-byte AI DMA at 32 kHz stereo, in timebase ticks
WINDOWS = {                    # window: (anchor, first retrace offset, last retrace offset)
    "menu_music": ("menu", 0, 300),
    "announcer": ("match", 0, 300),
    "scripted_hits": ("match", 1000, 1400),
}
POINTER_WORDS = {0, 1, 2, 3, 28, 29, 39, 40}   # next/this block, ITD buffer, update list addresses
DEPOP_WORDS = set(range(41, 50))                # ucode-written depop history of the slot
ADDRESS_FIELDS = {"loop": (57, 58), "end": (59, 60), "cur": (61, 62)}
ADDRESS_WORDS = {w for pair in ADDRESS_FIELDS.values() for w in pair}
STREAM_SPAN = 0x60000          # three 64 KB hako buffers, in bytes
FIELD_NAMES = dict(enumerate(
    ["next_hi", "next_lo", "this_hi", "this_lo", "src_type", "coef_select", "mixer_control",
     "running", "is_stream"] +
    [f"mixer.{n}" for n in ("left", "left_delta", "right", "right_delta", "auxA_left",
                            "auxA_left_delta", "auxA_right", "auxA_right_delta", "auxB_left",
                            "auxB_left_delta", "auxB_right", "auxB_right_delta", "auxB_surround",
                            "auxB_surround_delta", "surround", "surround_delta", "auxA_surround",
                            "auxA_surround_delta")] +
    [f"itd.{n}" for n in ("on", "addr_hi", "addr_lo", "offset_left", "offset_right",
                          "target_left", "target_right")] +
    [f"updates.num{m}" for m in range(5)] + ["updates.data_hi", "updates.data_lo"] +
    [f"dpop.{n}" for n in range(9)] + ["vol_env.cur_volume", "vol_env.cur_volume_delta"] +
    [f"unknown3.{n}" for n in range(3)] +
    ["addr.looping", "addr.sample_format", "addr.loop_hi", "addr.loop_lo", "addr.end_hi",
     "addr.end_lo", "addr.cur_hi", "addr.cur_lo"] +
    [f"adpcm.coef{n}" for n in range(16)] +
    ["adpcm.gain", "adpcm.pred_scale", "adpcm.yn1", "adpcm.yn2", "src.ratio_hi", "src.ratio_lo",
     "src.cur_addr_frac"] + [f"src.last_sample{n}" for n in range(4)] +
    ["adpcm_loop.pred_scale", "adpcm_loop.yn1", "adpcm_loop.yn2", "lpf.enabled", "lpf.yn1",
     "lpf.a0", "lpf.b0"]))

LINE = re.compile(r"\[ax-vtrace\] retrace=(\d+) tb=(\d+) vi=(\d+) kind=(\w+) frame=(\d+)(.*)")
EVENT = re.compile(r"\[ax-(request|stream|bank)-(?:native|legacy)\] (.*)")
FIELD = re.compile(r"(\w+)=(\S+)")
CLOCK = re.compile(r"audio: AI clock phase retrace=(\d+) next_tick_after_boundary=(-?\d+)")


def anchors(state_csv):
    with state_csv.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    menu = next(int(r["frame"]) for r in rows if int(r["scene_major"], 16) == 1)
    match = next(int(r["frame"]) for r in rows if int(r["match_frame"], 16) == 1)
    return {"menu": menu, "match": match}


def byte_address(fmt, address):
    return address // 2 if fmt == 0 else address * 2 if fmt == 0x0A else address


def word_address(words, name):
    hi, lo = ADDRESS_FIELDS[name]
    return (words[hi] << 16) | words[lo]


class Voice:
    def __init__(self, engine, pb, record, words):
        self.engine = engine
        self.pb = pb
        self.start_frame = record["frame"]
        self.start_retrace = record["retrace"]
        self.start_order = record["order"]
        self.start_words = words
        self.cur_start = record["cur_begin"]
        self.end_addr = record["end"]
        self.fmt = record["fmt"]
        self.looping = record["looping"]
        self.frames = []
        self.last_frame = None
        self.last_cur_end = None
        self.source = None       # ("bank", entry, byte base) or ("stream", entry, byte base)
        self.cause = None
        self.rel = None

    def base_nibbles(self):
        if not self.source:
            return 0
        base = self.source[2]
        return base * 2 if self.fmt == 0 else base // 2 if self.fmt == 0x0A else base

    def identity(self):
        """Sample file (or stream), format and file-relative start and end."""
        base = self.base_nibbles()
        kind = self.source[:2] if self.source else ("absolute",)
        return (*kind, self.fmt, self.looping, self.cur_start - base, self.end_addr - base)

    def summary(self):
        source = "absolute" if not self.source else f"{self.source[0]} {self.source[1]} @{self.source[2]:08X}"
        return {"pb": f"{self.pb:08X}", "start_frame": self.start_frame,
                "start_retrace": self.start_retrace, "frames": len(self.frames), "source": source,
                "sample": f"fmt={self.fmt} start={self.cur_start:08X} end={self.end_addr:08X} "
                          f"looping={self.looping}"}


def parse(log_path):
    frames, records, events, clock, order = {}, [], [], None, 0
    with open(log_path, encoding="utf-8", errors="replace") as stream:
        for text in stream:
            order += 1
            if "[ax-vtrace]" in text:
                match = LINE.search(text)
                if not match:
                    continue
                retrace, tb, _vi, kind, frame, rest = match.groups()
                retrace, tb, frame = int(retrace), int(tb), int(frame)
                if kind == "frame":
                    frames[frame] = (retrace, tb, order)
                    continue
                fields = dict(FIELD.findall(rest))
                run_begin, run_end = (int(x) for x in fields["run"].split(">"))
                cur_begin, cur_end = (int(x, 16) for x in fields["cur"].split(">"))
                words = fields.get("words")
                records.append({
                    "frame": frame, "retrace": retrace, "tb": tb, "pb": int(fields["pb"], 16),
                    "run_begin": run_begin, "run_end": run_end, "ms": int(fields["ms"], 16),
                    "n": int(fields["n"]), "peak": int(fields["peak"]), "pcm": fields["pcm"],
                    "mix": fields["mix"], "cur_begin": cur_begin, "cur_end": cur_end,
                    "end": int(fields["end"], 16), "fmt": int(fields["fmt"]),
                    "looping": int(fields["looping"]), "vol": fields["vol"], "cpu": int(fields["cpu"]),
                    "words": [int(words[i:i + 4], 16) for i in range(0, len(words), 4)] if words else None,
                    "order": order})
                continue
            match = EVENT.search(text)
            if match:
                fields = dict(FIELD.findall(match.group(2)))
                event = {"kind": match.group(1), "order": order, "retrace": int(fields["retrace"]),
                         "tb": int(fields["tb"])}
                for key in ("id", "vol", "pan", "track", "channel", "entry", "bank"):
                    if key in fields:
                        event[key] = int(fields[key])
                for key in ("base", "size"):
                    if key in fields:
                        event[key] = int(fields[key], 16)
                if "phase" in fields:
                    event["phase"] = fields["phase"]
                events.append(event)
                continue
            if clock is None:
                match = CLOCK.search(text)
                if match:
                    clock = int(match.group(1)) * TB_PER_FRAME + int(match.group(2))
    return frames, records, events, clock


def build_voices(engine, records):
    voices, active, last_words = [], {}, {}
    for record in records:
        pb = record["pb"]
        voice = active.get(pb)
        if record["n"] > 0:
            if (voice is None or voice.last_frame != record["frame"] - 1 or
                    record["cur_begin"] != voice.last_cur_end):
                voice = Voice(engine, pb, record, record["words"] or last_words.get(pb))
                active[pb] = voice
                voices.append(voice)
            voice.frames.append(record)
            voice.last_frame = record["frame"]
            voice.last_cur_end = record["cur_end"]
        elif voice is not None and record["run_begin"] == 0:
            active.pop(pb, None)
        if record["words"]:
            last_words[pb] = record["words"]
    return voices


def attach_sources(voices, events):
    """The sample file (latest load covering the address) or disc stream each voice plays."""
    loads = [e for e in events if e["kind"] == "bank"]
    streams = [e for e in events if e["kind"] == "stream" and e.get("phase") == "first-data"]
    for voice in voices:
        address = byte_address(voice.fmt, voice.cur_start)
        stream = [e for e in streams if e["order"] < voice.start_order and
                  e["base"] <= address < e["base"] + STREAM_SPAN]
        if voice.looping and stream:
            voice.source = ("stream", stream[-1]["entry"], stream[-1]["base"])
            voice.stream_event = stream[-1]
            continue
        covering = [e for e in loads if e["order"] < voice.start_order and
                    e["base"] <= address < e["base"] + e["size"]]
        if covering:
            voice.source = ("bank", covering[-1]["entry"], covering[-1]["base"])


def compare_start(n, l, block_words):
    """Condition 1: every field but pointers and depop history; addresses relative to their base."""
    a, b = n.start_words, l.start_words
    if a is None or b is None:
        return None, None
    diffs = []
    for index in range(min(block_words, len(a), len(b))):
        if index in POINTER_WORDS or index in DEPOP_WORDS or index in ADDRESS_WORDS:
            continue
        if a[index] != b[index]:
            diffs.append({"field": FIELD_NAMES.get(index, f"word{index}"),
                          "native": f"{a[index]:04X}", "legacy": f"{b[index]:04X}"})
    n_base, l_base = n.base_nibbles(), l.base_nibbles()
    addresses = {}
    for name in ADDRESS_FIELDS:
        na, la = word_address(a, name) - n_base, word_address(b, name) - l_base
        addresses[name] = {"native_relative": f"{na:08X}", "legacy_relative": f"{la:08X}"}
        if na != la:
            diffs.append({"field": f"addr.{name} (relative to base)", "native": f"{na:08X}",
                          "legacy": f"{la:08X}"})
    same_source = (n.source[:2] if n.source else None) == (l.source[:2] if l.source else None)
    if not same_source:
        diffs.append({"field": "sample source", "native": n.summary()["source"],
                      "legacy": l.summary()["source"]})
    return diffs, addresses


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--run", type=Path, required=True, help="pair dir with native/ and translated/")
    parser.add_argument("--json", type=Path)
    parser.add_argument("--table", type=Path, help="write the per-voice table (text)")
    args = parser.parse_args()

    engines = {}
    for label, sub in (("native", "native"), ("legacy", "translated")):
        frames, records, events, clock = parse(args.run / sub / "game.log")
        voices = build_voices(label, records)
        attach_sources(voices, events)
        pbs = sorted({r["pb"] for r in records})
        stride = min((pbs[i + 1] - pbs[i] for i in range(len(pbs) - 1)), default=244)
        engines[label] = {"frames": frames, "voices": voices, "events": events, "clock": clock,
                          "anchors": anchors(args.run / sub / "state.csv"), "block_words": stride // 2,
                          "frame_list": sorted(frames)}

    # AX frame k is due at clock + k * AX_PERIOD (the AI DMA clock); how late each engine runs it.
    timeline = {}
    for label, e in engines.items():
        late = sorted(e["frames"][k][1] - (e["clock"] + k * AX_PERIOD) for k in e["frame_list"])
        timeline[label] = {"ai_clock_start_tb": e["clock"], "frames": len(late),
                           "ran_before_nominal": sum(1 for x in late if x < 0),
                           "delivery_delay_ticks": {"min": late[0], "median": late[len(late) // 2],
                                                    "p90": late[len(late) * 9 // 10], "max": late[-1]},
                           "block_words": e["block_words"]}

    # Causes: each request's delivered service frame (first AX frame run after it) and the frame the
    # AXOut model gives (first frame whose nominal time follows it); stream first-data likewise.
    for label, e in engines.items():
        frame_orders = sorted((order, k) for k, (_, _, order) in e["frames"].items())
        orders = [o for o, _ in frame_orders]
        for event in e["events"]:
            if event["kind"] == "bank":
                continue
            i = bisect.bisect_right(orders, event["order"])
            event["delivered_frame"] = frame_orders[i][1] if i < len(frame_orders) else None
            event["model_frame"] = (event["tb"] - e["clock"]) // AX_PERIOD + 1
        by_service = collections.defaultdict(list)
        for event in e["events"]:
            if event["kind"] == "request" and event.get("delivered_frame") is not None:
                by_service[event["delivered_frame"]].append(event)
        for voice in e["voices"]:
            if voice.source and voice.source[0] == "stream":
                data = voice.stream_event
                voice.cause = {"kind": "stream first data", "entry": data["entry"], "retrace": data["retrace"],
                               "tb": data["tb"], "delivered_frame": data["delivered_frame"],
                               "model_start_frame": data["model_frame"],
                               "latency_frames": voice.start_frame - data["model_frame"],
                               "ok": voice.start_frame == data["model_frame"]}
                continue
            requests = by_service.get(voice.start_frame - 1, [])
            if requests:
                model = [r["model_frame"] + 1 for r in requests]
                voice.cause = {"kind": "sound request", "ids": [r["id"] for r in requests],
                               "retrace": requests[0]["retrace"], "tb": [r["tb"] for r in requests],
                               "delivered_service_frame": voice.start_frame - 1,
                               "model_start_frame": model,
                               "latency_frames": [voice.start_frame - m + 1 for m in model],
                               "ok": all(voice.start_frame == m for m in model)}
            else:
                # A sound macro can wait whole AX ticks before a play command (AXDriverInterp); the
                # pair step attributes such a voice to the request with the same id and the same
                # delay in both engines.
                voice.cause = {"kind": "none serviced in the frame before", "ok": None}
                voice.earlier_requests = [r for r in e["events"] if r["kind"] == "request" and
                                          r["order"] < voice.start_order and
                                          voice.start_frame - r["model_frame"] <= 400]

    result = {"run": str(args.run), "timeline": timeline, "windows": {}}
    table = []
    for window, (anchor_name, first, last) in WINDOWS.items():
        per = {}
        for label, e in engines.items():
            anchor = e["anchors"][anchor_name]
            lo, hi = anchor + first, anchor + last
            anchor_frame = -(-(anchor * TB_PER_FRAME - e["clock"]) // AX_PERIOD)
            inside = [v for v in e["voices"] if any(lo <= f["retrace"] < hi for f in v.frames)]
            # Pairing also looks a few retraces past the edges, so a voice that starts one AX frame
            # after the window end in one engine still pairs with its counterpart in the other.
            margin = [v for v in e["voices"] if any(lo - 3 <= f["retrace"] < hi + 3 for f in v.frames)]
            for v in margin:
                v.rel = v.start_frame - anchor_frame
            per[label] = {"anchor_retrace": anchor, "retraces": [lo, hi], "anchor_frame": anchor_frame,
                          "voices": inside, "candidates": margin}
        nat, leg = per["native"], per["legacy"]
        pool = collections.defaultdict(list)
        for v in leg["candidates"]:
            pool[v.identity()].append(v)
        pairs, unmatched_native = [], []
        for v in sorted(nat["candidates"], key=lambda x: x.rel):
            candidates = pool.get(v.identity(), [])
            if not candidates:
                unmatched_native.append(v)
                continue
            best = min(candidates, key=lambda c: abs(c.rel - v.rel))
            candidates.remove(best)
            pairs.append((v, best))
        # Second pass: the same audio from a different sample file (same format, loop flag and
        # length, identical first-frame hashes, start within three AX frames).
        left = [v for group in pool.values() for v in group]
        still = []
        for v in unmatched_native:
            same = [c for c in left if (c.fmt, c.looping, c.end_addr - c.cur_start) ==
                    (v.fmt, v.looping, v.end_addr - v.cur_start) and abs(c.rel - v.rel) <= 3 and
                    (c.frames[0]["pcm"], c.frames[0]["mix"]) == (v.frames[0]["pcm"], v.frames[0]["mix"])]
            if not same:
                still.append(v)
                continue
            best = min(same, key=lambda c: abs(c.rel - v.rel))
            left.remove(best)
            pool[best.identity()].remove(best)
            pairs.append((v, best))
        unmatched_native = still
        inside_ids = {id(v) for v in nat["voices"]} | {id(v) for v in leg["voices"]}
        pairs = [(a, b) for a, b in pairs if id(a) in inside_ids or id(b) in inside_ids]
        unmatched_native = [v for v in unmatched_native if id(v) in inside_ids]
        unmatched_legacy = [v for group in pool.values() for v in group if id(v) in inside_ids]

        rows = []
        counts = {"c1_params": [0, 0], "c2_samples": [0, 0], "c3_model_native": [0, 0],
                  "c3_model_legacy": [0, 0], "c3_cross": [0, 0], "c3_cause_aligned": [0, 0]}
        max_start_diff = 0
        block_words = min(engines["native"]["block_words"], engines["legacy"]["block_words"])
        n_lo, n_hi = nat["retraces"]
        l_lo, l_hi = leg["retraces"]
        for n, l in sorted(pairs, key=lambda p: p[0].rel):
            diffs, addresses = compare_start(n, l, block_words)
            c1 = None if diffs is None else not diffs
            nf = {f["frame"] - n.start_frame: f for f in n.frames}
            lf = {f["frame"] - l.start_frame: f for f in l.frames}
            compared, mismatched, first_mismatch, only = 0, [], None, []
            for rel in sorted(set(nf) | set(lf)):
                a, b = nf.get(rel), lf.get(rel)
                if not ((a and n_lo <= a["retrace"] < n_hi) or (b and l_lo <= b["retrace"] < l_hi)):
                    continue
                if a is None or b is None:
                    only.append(rel)
                    continue
                compared += 1
                if (a["pcm"], a["mix"], a["n"]) != (b["pcm"], b["mix"], b["n"]):
                    mismatched.append(rel)
                    if first_mismatch is None:
                        keys = ("pcm", "mix", "n", "peak", "vol", "cpu")
                        first_mismatch = {"rel_frame": rel, "native": {k: a[k] for k in keys},
                                          "legacy": {k: b[k] for k in keys}}
            c2 = not mismatched and not only
            start_diff = l.rel - n.rel
            max_start_diff = max(max_start_diff, abs(start_diff))
            c3_cross = abs(start_diff) <= 1
            if n.cause["ok"] is None and l.cause["ok"] is None:
                delays_n = {(r["id"], n.start_frame - r["model_frame"]): r for r in getattr(n, "earlier_requests", [])}
                delays_l = {(r["id"], l.start_frame - r["model_frame"]): r for r in getattr(l, "earlier_requests", [])}
                common = sorted(set(delays_n) & set(delays_l), key=lambda k: k[1])
                if common:
                    key = common[0]
                    for voice, table_ in ((n, delays_n), (l, delays_l)):
                        r = table_[key]
                        voice.cause = {"kind": "sound-macro delayed start", "ids": [key[0]],
                                       "retrace": r["retrace"], "tb": [r["tb"]],
                                       "model_start_frame": [r["model_frame"] + 1],
                                       "latency_frames": [key[1]],
                                       "note": "same request id and the same delay in both engines",
                                       "ok": True}
            c3n, c3l = n.cause["ok"], l.cause["ok"]

            def after_cause(voice, eng):
                # Start frame counted from the first AX frame at or after the cause's retrace boundary.
                if "retrace" not in voice.cause:
                    return None
                boundary = voice.cause["retrace"] * TB_PER_FRAME
                return voice.start_frame - -(-(boundary - engines[eng]["clock"]) // AX_PERIOD)
            an, al = after_cause(n, "native"), after_cause(l, "legacy")
            cause_diff = None if an is None or al is None else al - an
            for key, value in (("c1_params", c1), ("c2_samples", c2), ("c3_model_native", c3n),
                               ("c3_model_legacy", c3l), ("c3_cross", c3_cross),
                               ("c3_cause_aligned", None if cause_diff is None else abs(cause_diff) <= 1)):
                if value is True:
                    counts[key][0] += 1
                elif value is False:
                    counts[key][1] += 1
            rows.append({"sample": n.summary()["sample"], "identity": [str(x) for x in n.identity()],
                         "native": {**n.summary(), "rel_start": n.rel, "cause": n.cause},
                         "legacy": {**l.summary(), "rel_start": l.rel, "cause": l.cause},
                         "start_frame_difference": start_diff,
                         "c1_params": c1, "c1_differences": diffs, "c1_addresses": addresses,
                         "c2_samples": c2, "c2_frames_compared": compared,
                         "c2_mismatched_rel_frames": mismatched[:20], "c2_first_mismatch": first_mismatch,
                         "c2_frames_only_one_engine": only[:20],
                         "c3_model_native": c3n, "c3_model_legacy": c3l, "c3_cross": c3_cross,
                         "c3_frames_after_cause_retrace": {"native": an, "legacy": al},
                         "c3_cause_aligned_difference": cause_diff})
        result["windows"][window] = {
            "native": dict({k: nat[k] for k in ("anchor_retrace", "retraces", "anchor_frame")}, voices=len(nat["voices"])),
            "legacy": dict({k: leg[k] for k in ("anchor_retrace", "retraces", "anchor_frame")}, voices=len(leg["voices"])),
            "matched_pairs": len(pairs),
            "unmatched_native": [dict(v.summary(), rel_start=v.rel) for v in unmatched_native],
            "unmatched_legacy": [dict(v.summary(), rel_start=v.rel) for v in unmatched_legacy],
            "condition_counts_pass_fail": counts,
            "max_start_frame_difference": max_start_diff,
            "voices": rows}
        table.append(f"== {window}: native {len(nat['voices'])} voices, legacy {len(leg['voices'])}, "
                     f"pairs {len(pairs)}, unmatched native {len(unmatched_native)} legacy {len(unmatched_legacy)}; "
                     f"pass/fail {counts}; max start diff {max_start_diff}")
        table.append(" rel_n  rel_l    d  source / sample                           c1 c2(cmp/mis) c3n c3l x  lat_n lat_l  dc  notes")
        for row in rows:
            def lat(cause):
                value = cause.get("latency_frames")
                return "-" if value is None else ",".join(map(str, value)) if isinstance(value, list) else str(value)
            notes = ",".join(d["field"] for d in (row["c1_differences"] or []))
            table.append(
                f"{row['native']['rel_start']:6d} {row['legacy']['rel_start']:6d} {row['start_frame_difference']:+4d}  "
                f"{(row['native']['source'] + ' ' + row['sample'][:26]):<42}{str(row['c1_params'])[0]}  "
                f"{str(row['c2_samples'])[0]}({row['c2_frames_compared']}/{len(row['c2_mismatched_rel_frames'])})"
                f"{'':<4}{str(row['c3_model_native'])[0]}   {str(row['c3_model_legacy'])[0]}   {str(row['c3_cross'])[0]}  "
                f"{lat(row['native']['cause']):>5} {lat(row['legacy']['cause']):>5}  "
                f"{'' if row['c3_cause_aligned_difference'] is None else format(row['c3_cause_aligned_difference'], '+d'):>3}  {notes}")
    rendered = json.dumps(result, indent=1)
    if args.json:
        args.json.write_text(rendered + "\n", encoding="utf-8")
    if args.table:
        args.table.write_text("\n".join(table) + "\n", encoding="utf-8")
    print(json.dumps(timeline, indent=1))
    print("\n".join(table))


if __name__ == "__main__":
    main()
