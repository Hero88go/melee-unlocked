"""Lay the files built from this commit over the newest published release zip (the "Latest" prerelease).

The base release keeps its layout: one top folder (renamed MeleeUnlocked-latest/) with every file the release has.
Replaced from this commit's build: melee_source.exe, melee_game.dll/.snapexcl, MeleeUnlockedLauncher.exe and the
rtx_*.dxil shaders. Added: the SSE2 compatibility build (melee_source_compat.exe, melee_game_compat.dll/.snapexcl),
inside the zip so it matches this build. Refreshed from the repository at this commit, as tools/package_release.py
does: yougame-native.json (port/yougame/), deltas/ (port/sys_deltas/) and lang/.
Left out: melee_port.exe (the Static Recomp needs the disc-derived translation, which this CI does not have, and the
release's copy would be an older version) and compat.sha256 (it points the launcher at the release's own compat zip,
which would replace the compatibility build above with an older one).
LATEST.txt records the commit and the base release.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile

BUILT = ("melee_source.exe", "melee_game.dll", "melee_game.snapexcl", "MeleeUnlockedLauncher.exe",
         "rtx_transform.dxil", "rtx_trace.dxil", "rtx_direct.dxil",
         "melee_source_compat.exe", "melee_game_compat.dll", "melee_game_compat.snapexcl")
DROPPED = {"melee_port.exe", "compat.sha256"}
ROOT = "MeleeUnlocked-latest"


def sha256(path):
    with open(path, "rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--base", type=Path, required=True)
    ap.add_argument("--base-sha256", required=True)
    ap.add_argument("--base-version", required=True)
    ap.add_argument("--built", type=Path, required=True)
    ap.add_argument("--repo", type=Path, required=True)
    ap.add_argument("--commit", required=True)
    ap.add_argument("--source-url", required=True, help="the repository URL the commit is on")
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    if sha256(args.base) != args.base_sha256.lower():
        raise SystemExit("base release zip SHA-256 differs from the release listing")
    repo_files = {"yougame-native.json": args.repo / "port/yougame/yougame-native.json"}
    manifest = json.loads((args.repo / "port/sys_deltas/manifest.json").read_text(encoding="utf-8"))
    for name in ["manifest.json"] + [f["delta"] for f in manifest["files"]]:
        repo_files[f"deltas/{name}"] = args.repo / "port/sys_deltas" / name
    for path in sorted((args.repo / "lang").rglob("*")):
        if path.is_file():
            repo_files["lang/" + path.relative_to(args.repo / "lang").as_posix()] = path
    replaced = {n.lower(): args.built / n for n in BUILT}
    replaced.update({n.lower(): p for n, p in repo_files.items()})
    for path in replaced.values():
        if not path.is_file() or path.stat().st_size == 0:
            raise SystemExit(f"missing or empty input: {path}")

    with zipfile.ZipFile(args.base) as base:
        infos = base.infolist()
        tops = {PurePosixPath(i.filename).parts[0] for i in infos}
        if len(tops) != 1 or not all("/" in i.filename for i in infos if not i.is_dir()):
            raise SystemExit(f"base zip should have one top folder, found {sorted(tops)}")
        prefix = tops.pop() + "/"
        kept = dropped = 0
        args.out.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as out:
            for info in infos:
                rel = info.filename[len(prefix):]
                parts = PurePosixPath(rel).parts
                if rel and (rel.startswith("/") or ".." in parts or ":" in rel or "\\" in rel):
                    raise SystemExit(f"unsafe name in base zip: {info.filename}")
                key = rel.lower()
                # lang/ comes from the repository whole, so a removed translation does not linger.
                if key in DROPPED or key in replaced or (key.startswith("lang/") and not info.is_dir()):
                    dropped += 1
                    continue
                entry = zipfile.ZipInfo(f"{ROOT}/{rel}", date_time=info.date_time)
                entry.external_attr = info.external_attr
                entry.create_system = info.create_system
                if info.is_dir():
                    out.writestr(entry, b"")
                else:
                    out.writestr(entry, base.read(info.filename), compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)
                kept += 1
            for name in [*BUILT, *repo_files]:
                out.write(replaced[name.lower()], f"{ROOT}/{name}")
            note = (f"Melee Unlocked, automatic build of main at commit {args.commit}.\n"
                    f"Not a release: the newest release, v{args.base_version}, with this commit's Source Port, "
                    "compatibility build and launcher laid over it.\n"
                    f"Base release zip SHA-256: {args.base_sha256.lower()}\n"
                    "The Static Recomp (melee_port.exe) is not included in this build.\n"
                    f"Source: {args.source_url}/tree/{args.commit}\n")
            out.writestr(f"{ROOT}/LATEST.txt", note.replace("\n", "\r\n"))
    print(f"kept {kept} entries of v{args.base_version}, left out or replaced {dropped}, "
          f"wrote {len(BUILT)} built and {len(repo_files)} repository files")
    print(f"zip {args.out.name} {args.out.stat().st_size} bytes sha256 {sha256(args.out)}")


if __name__ == "__main__":
    main()
