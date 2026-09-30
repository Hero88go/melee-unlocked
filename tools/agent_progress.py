#!/usr/bin/env python3
"""Record a parallel agent's progress for the local dashboard (run-source/agent-progress.json).

    python tools/agent_progress.py --agent fuzzA --title "Vanilla fuzz crash hunt" --done 3 --total 25 \
        --status running --note "seed 22 pair exact 4,100 frames"

--status: running, done, blocked or stopped. Fields left out keep their previous value.
"""
import argparse
import json
import os
import tempfile
from datetime import datetime, timezone
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "run-source" / "agent-progress.json"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--agent", required=True)
    ap.add_argument("--title")
    ap.add_argument("--done", type=int)
    ap.add_argument("--total", type=int)
    ap.add_argument("--status", choices=("running", "done", "blocked", "stopped"))
    ap.add_argument("--note")
    ap.add_argument("--remove", action="store_true")
    a = ap.parse_args()
    try:
        data = json.loads(PATH.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        data = {"agents": {}}
    agents = data.setdefault("agents", {})
    if a.remove:
        agents.pop(a.agent, None)
    else:
        entry = agents.setdefault(a.agent, {"title": a.agent, "done": 0, "total": 1, "status": "running",
                                            "note": "", "started": datetime.now(timezone.utc).isoformat(timespec="seconds")})
        for key in ("title", "done", "total", "status", "note"):
            value = getattr(a, key)
            if value is not None:
                entry[key] = value
        entry["updated"] = datetime.now(timezone.utc).isoformat(timespec="seconds")
    PATH.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=PATH.parent, suffix=".tmp")
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1)
    os.replace(tmp, PATH)
    e = agents.get(a.agent)
    print(json.dumps({a.agent: e}) if e else "removed")


if __name__ == "__main__":
    main()
