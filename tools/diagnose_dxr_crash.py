"""Compare hidden VS-match runs with DXR disabled and enabled using isolated settings."""

import argparse
import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def remembered_iso() -> Path:
    profile = Path(os.environ["LOCALAPPDATA"]) / "MeleeUnlocked" / "launcher.ini"
    for line in profile.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("iso="):
            iso = Path(line[4:])
            if iso.is_file():
                return iso
    raise SystemExit("The launcher has no valid remembered ISO")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=3300)
    parser.add_argument("--idle", action="store_true", help="stay on the memory-card/main-menu path")
    parser.add_argument("--only", choices=("raster", "dxr"))
    args = parser.parse_args()
    package = args.package.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    iso = remembered_iso()
    for name, path_tracing, reconstruction in (("raster", 0, 0), ("dxr", 1, 1)):
        if args.only and name != args.only:
            continue
        case = out / name
        case.mkdir(exist_ok=True)
        card = case / "CardA"
        card.mkdir(exist_ok=True)
        saves = [] if args.idle else list((package / "User" / "GC" / "CardA").glob("*.gci"))
        if not saves and not args.idle:
            saves = list((ROOT / "User" / "GC" / "CardA").glob("*.gci"))
        for save in saves:
            shutil.copy2(save, card / save.name)
        settings = case / "settings.ini"
        settings.write_text(
            f"backend d3d12\ndlss 2\npathtracing {path_tracing}\nrayreconstruction {reconstruction}\n",
            encoding="utf-8",
        )
        command = [
            str(package / "melee_port.exe"), "--iso", str(iso),
            "--sys-dir", str(package / "Sys"), "--user-dir", str(case / "User"),
            "--replay-dir", str(case / "Replays"), "--card-dir", str(case / "CardA"),
            "--settings-path", str(settings), "--load-settings",
            "--hidden", "--fast", "--threaded-renderer", "--frames", str(args.frames),
            "--log-file", str(case / "game.log"),
        ]
        if not args.idle:
            command.extend(("--script", str(ROOT / "port/scripts/vs_match.txt")))
        print(f"Running {name} for {args.frames} frames", flush=True)
        try:
            result = subprocess.run(command, cwd=package, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, timeout=360)
            (case / "stdout.txt").write_text(result.stdout, encoding="utf-8")
            print(f"{name}: exit {result.returncode}", flush=True)
        except subprocess.TimeoutExpired:
            print(f"{name}: timeout", flush=True)


if __name__ == "__main__":
    main()
