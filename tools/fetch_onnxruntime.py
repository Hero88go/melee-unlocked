"""Fetch onnxruntime's runtime library for Fight a pro into port/third_party/onnxruntime/bin/.

The headers and license are in the repository; the libraries (17 MB on Windows) are not. This
downloads Microsoft's release archives at a pinned version, checks each against the SHA-256 GitHub
publishes for it, and extracts onnxruntime.dll (Windows) and libonnxruntime.so (Linux). The build
copies the DLL beside melee_source.exe; the packagers ship them.

    py -3.12 tools/fetch_onnxruntime.py [--windows] [--linux]   (both by default)
"""
from __future__ import annotations

import argparse
import hashlib
import io
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path

VERSION = "1.31.0"
BASE = f"https://github.com/microsoft/onnxruntime/releases/download/v{VERSION}/"
ARCHIVES = {
    "windows": (f"onnxruntime-win-x64-{VERSION}.zip",
                "3958d8a44984160692c1b4ffd0de3cb48f8c39f9d34bb31e433db06e30b82195",
                f"onnxruntime-win-x64-{VERSION}/lib/onnxruntime.dll", "onnxruntime.dll"),
    "linux": (f"onnxruntime-linux-x64-{VERSION}.tgz",
              "cc5c72baf5ae5c8238a6841f0897227be2d02826b9cf98eaf02fdefaeeb03a57",
              f"onnxruntime-linux-x64-{VERSION}/lib/libonnxruntime.so.{VERSION}", "libonnxruntime.so"),
}
OUT = Path(__file__).resolve().parents[1] / "port/third_party/onnxruntime/bin"


def fetch(kind: str) -> None:
    name, sha, member, dest_name = ARCHIVES[kind]
    req = urllib.request.Request(BASE + name, headers={"User-Agent": "melee-unlocked-build"})
    data = urllib.request.urlopen(req, timeout=600).read()
    if hashlib.sha256(data).hexdigest() != sha:
        raise SystemExit(f"{name}: SHA-256 does not match the published release")
    if name.endswith(".zip"):
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            lib = z.read(member)
    else:
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as t:
            f = t.extractfile(member)
            if f is None:
                raise SystemExit(f"{name}: {member} is not a file")
            lib = f.read()
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / dest_name).write_bytes(lib)
    print(f"{dest_name}: {len(lib) / 1e6:.1f} MB from onnxruntime {VERSION}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--windows", action="store_true")
    ap.add_argument("--linux", action="store_true")
    args = ap.parse_args()
    kinds = [k for k in ("windows", "linux") if getattr(args, k)] or ["windows", "linux"]
    for kind in kinds:
        fetch(kind)
    return 0


if __name__ == "__main__":
    sys.exit(main())
