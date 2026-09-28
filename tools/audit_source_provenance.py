"""Audit linked Source artifacts; does not establish gameplay/feature acceptance."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def identity(path):
    return {"path": str(path.resolve()), "bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def audit(host_map, game_map, sources):
    failures = []
    host = host_map.read_text(errors="replace")
    game = game_map.read_text(errors="replace")
    manifest = sources.read_text(errors="replace")
    # A map includes only extracted library members, so merely not seeing the
    # word 'guest.lib' is insufficient: check translated function/object names too.
    for name, pattern in (
        ("translated guest library", r"\bguest(?:\.lib)?:guest_"),
        ("translated function", r"\?f_[0-9a-f]{8}@guest@@"),
        ("PowerPC interpreter", r"\bruntime(?:\.lib)?:interp\.obj\b"),
    ):
        if re.search(pattern, host, re.I): failures.append(name + " linked into Source host")
    if not re.search(r"\?interpret@ppc@@[^\n]*source_guest_boundary\.obj", host):
        failures.append("Source interpreter rejection boundary absent")
    for pattern in (r"ppcleaf", r"leaf_generated", r"\b_ZN3ppc", r"guest_\d+\."):
        if re.search(pattern, game, re.I): failures.append("translated SDK/game object in native DLL: " + pattern)
    inputs = [line for line in manifest.splitlines() if line.strip()]
    if not inputs or not any("sdk_math.c" in line for line in inputs):
        failures.append("native source manifest missing SDK source implementation")
    if any(re.search(r"generated|ppcleaf|main\.dol|\.cpp$", line, re.I) for line in inputs):
        failures.append("generated/translated input in native game source manifest")
    if "sdk_math.c.obj" not in game or "estimates.c.obj" not in game:
        failures.append("source SDK math objects absent from linked game")
    return failures, len(inputs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host-map", type=Path, required=True)
    parser.add_argument("--game-map", type=Path, required=True)
    parser.add_argument("--sources", type=Path, required=True)
    parser.add_argument("--host-exe", type=Path, required=True)
    parser.add_argument("--game-dll", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    failures, count = audit(args.host_map, args.game_map, args.sources)
    record = {"passed": not failures, "failures": failures, "source_entries": count,
              "scope": "linked object provenance, not numerical or whole-port acceptance",
              "artifacts": [identity(p) for p in (args.host_map, args.game_map,
                            args.sources, args.host_exe, args.game_dll)]}
    args.out.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: record[k] for k in ("passed", "failures", "source_entries")}))
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
