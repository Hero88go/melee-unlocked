"""Launch the isolated development native port at normal simulation speed."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ISO = Path(r"C:\Games\Smash\DOLPHIN AND SMASH GAMES\Super Smash Bros. Melee (v1.02).iso")

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iso", type=Path, default=DEFAULT_ISO)
    ap.add_argument("--scale", default="2", help="internal resolution multiplier 1-25, or 'auto' to follow the window")
    ap.add_argument("--window", default="1280x960", help="initial client size WxH")
    ap.add_argument("--threaded-renderer", action="store_true")
    ap.add_argument("--fps", help="display rate: a number, 'monitor', or 'unlocked' (enables sub-frame presentation)")
    ap.add_argument("--frame-mode", choices=["extrapolate", "interpolate", "authored", "off"])
    ap.add_argument("--vsync", action="store_true")
    ap.add_argument("--volume", type=int, default=0, help="audio volume percent (default 0: muted)")
    ap.add_argument("--hidden", action="store_true")
    ap.add_argument("--frames", type=int, default=0)
    args, extra = ap.parse_known_args()   # unknown options pass straight to melee_port (e.g. --user-dir, --online-delay, --local-peer)
    if args.scale != "auto" and not (args.scale.isdigit() and 1 <= int(args.scale) <= 25):
        ap.error("scale must be 'auto' or between 1 and 25")
    import re
    if not re.fullmatch(r"\d{3,5}x\d{3,5}", args.window): ap.error("window must look like 1920x1080")
    if args.fps and args.frame_mode not in ("interpolate", "extrapolate", "authored"):
        ap.error("--fps requires explicit experimental --frame-mode interpolate, extrapolate or authored")
    if args.frames < 0: ap.error("frames cannot be negative")
    executable = ROOT / "build-review/port/Release/melee_port.exe"
    if not executable.is_file(): ap.error("native build missing; follow NATIVE_DEVELOPMENT.md")
    if not args.iso.is_file(): ap.error(f"ISO not found: {args.iso}")
    command = [str(executable), "--iso", str(args.iso.resolve()), "--scale", args.scale, "--window", args.window]
    if args.threaded_renderer: command.append("--threaded-renderer")
    if args.fps:
        if args.fps not in ("unlocked", "monitor") and not (args.fps.isdigit() and int(args.fps) >= 1): ap.error("fps must be 'unlocked', 'monitor', or a positive number")
        command += ["--fps", args.fps]
    if args.frame_mode: command += ["--frame-mode", args.frame_mode]
    if args.vsync: command.append("--vsync")
    if not 0 <= args.volume <= 100: ap.error("volume must be 0-100")
    command += ["--volume", str(args.volume)]
    if args.frames: command += ["--frames", str(args.frames)]
    command += extra
    if args.hidden: command.append("--hidden")
    return subprocess.call(command, cwd=ROOT,
                           creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0) if args.hidden else 0)

if __name__ == "__main__": raise SystemExit(main())
