#!/usr/bin/env python3
"""Record verified local work in the live source-port dashboard feed."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ACTIVITY = ROOT / "run-source" / "live-progress.json"
TASK_STATES = ("DONE", "CURRENT", "UP NEXT", "BLOCKED")


def now():
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def update(args):
    if ACTIVITY.exists():
        data = json.loads(ACTIVITY.read_text(encoding="utf-8"))
    else:
        data = {"events": [], "work_items": []}

    timestamp = now()
    if args.phase:
        data["phase"] = args.phase
    if args.current:
        data["current"] = args.current
    if args.detail is not None:
        data["detail"] = args.detail
    data.setdefault("started", timestamp)
    data["updated"] = timestamp

    if args.task:
        items = data.setdefault("work_items", [])
        item = next((entry for entry in items if entry.get("title") == args.task), None)
        if item is None:
            item = {"title": args.task}
            items.append(item)
        item["state"] = args.task_state or "CURRENT"
        if args.evidence:
            item["evidence"] = args.evidence

    if args.event_title:
        event = {
            "time": timestamp,
            "state": args.event_state or "UPDATE",
            "title": args.event_title,
            "detail": args.event_detail or "",
        }
        if args.evidence:
            event["evidence"] = args.evidence
        events = data.setdefault("events", [])
        events.insert(0, event)
        del events[30:]

    ACTIVITY.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_path = tempfile.mkstemp(prefix="live-progress-", suffix=".json", dir=str(ACTIVITY.parent))
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
            json.dump(data, stream, indent=2, ensure_ascii=False)
            stream.write("\n")
        os.replace(temp_path, str(ACTIVITY))
    finally:
        if os.path.exists(temp_path):
            os.unlink(temp_path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--phase", help="Milestone or work phase")
    parser.add_argument("--current", help="The exact active task")
    parser.add_argument("--detail", help="Current evidence or next action")
    parser.add_argument("--task", help="Checklist item to create or update")
    parser.add_argument("--task-state", choices=TASK_STATES, help="Checklist state")
    parser.add_argument("--event-title", help="Add a timestamped feed event")
    parser.add_argument("--event-state", help="PASS, FAILED, IN PROGRESS, or UPDATE")
    parser.add_argument("--event-detail", help="Result and measured outcome")
    parser.add_argument("--evidence", help="Local evidence path")
    args = parser.parse_args()
    if not any((args.phase, args.current, args.detail is not None, args.task, args.event_title)):
        parser.error("provide an activity field, --task, or --event-title")
    if args.task_state and not args.task:
        parser.error("--task-state requires --task")
    update(args)


if __name__ == "__main__":
    main()
