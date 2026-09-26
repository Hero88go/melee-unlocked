"""Capture the packaged settings gallery while running from the package folder."""

import argparse
import os
import subprocess
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    package = args.package.resolve()
    iso = args.iso.resolve()
    output = args.out.resolve()
    exe = package / "melee_port.exe"
    if not exe.is_file() or not iso.is_file():
        raise SystemExit("package executable or ISO is missing")
    output.parent.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["MELEE_SETTINGS_TAB"] = "Customize"
    env["MELEE_TEST_STYLE_GALLERY"] = "1"
    command = [
        str(exe), "--iso", str(iso), "--hidden", "--load-settings", "--pc-settings-open",
        "--backend", "d3d12", "--frames", "360", "--capture-frame", "0",
        "--capture-sim-frame", "200", "--capture", str(output), "--window", "800x600",
        "--volume", "0", "--no-music", "--settings-path", str(package / "port-settings.ini"),
        "--sys-dir", str(package / "Sys"), "--user-dir", str(package / "User/Slippi"),
        "--replay-dir", str(package / "Replays"),
        "--card-dir", str(package / "User/GC/CardA"),
        "--log-file", str(output.with_suffix(".log")),
    ]
    with output.with_suffix(".stdout.txt").open("w", encoding="utf-8") as log:
        result = subprocess.run(command, cwd=package, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=120, check=False)
    print(f"exit={result.returncode} capture={output.is_file()} path={output}")
    if result.returncode != 0 or not output.is_file():
        raise SystemExit(1)


if __name__ == "__main__":
    main()
