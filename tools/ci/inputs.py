"""Anonymous, digest-checked CI inputs and selective runtime extraction from a release zip."""
import argparse
import hashlib
import http.client
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def public_url(url):
    parsed = urllib.parse.urlsplit(url)
    if (parsed.scheme != "https" or parsed.hostname not in
            {"github.com", "raw.githubusercontent.com"} or parsed.username or
            parsed.password or parsed.query or parsed.fragment or parsed.port):
        raise ValueError("input URL must be anonymous HTTPS on github.com or raw.githubusercontent.com, without query/fragment/port")
    return url


DOWNLOAD_TRIES = 12
DOWNLOAD_WAITS = (10, 20, 40, 80)  # seconds after failed tries 1-4; 120 after each later one


def download_retryable(error):
    """A 5xx, a 429 or a connection failure can pass; any other HTTP status will not."""
    if isinstance(error, urllib.error.HTTPError):
        return error.code >= 500 or error.code == 429
    return True


def download(url, sha, dest):
    public_url(url)
    if not re.fullmatch(r"[a-fA-F0-9]{64}", sha):
        raise ValueError("expected a 64-character SHA-256")
    dest.parent.mkdir(parents=True, exist_ok=True)
    if not dest.exists():
        partial = dest.with_suffix(dest.suffix + ".partial")
        for attempt in range(1, DOWNLOAD_TRIES + 1):
            partial.unlink(missing_ok=True)
            try:
                # No Authorization header, GitHub token or credential helper.
                with urllib.request.urlopen(url, timeout=180) as response, partial.open("wb") as out:
                    if urllib.parse.urlsplit(response.url).scheme != "https":
                        raise ValueError("download redirected away from HTTPS")
                    shutil.copyfileobj(response, out)
                partial.replace(dest)
                break
            except (urllib.error.URLError, OSError, http.client.HTTPException) as error:
                partial.unlink(missing_ok=True)
                # Do not expose redirect URLs (which can contain signed query strings).
                reason = error.code if isinstance(error, urllib.error.HTTPError) else type(error).__name__
                print(f"download {dest.name}: try {attempt}/{DOWNLOAD_TRIES} failed: {reason}", flush=True)
                if attempt == DOWNLOAD_TRIES or not download_retryable(error):
                    raise ValueError("anonymous download failed; check the supplied public URL") from None
                time.sleep(DOWNLOAD_WAITS[attempt - 1] if attempt <= len(DOWNLOAD_WAITS) else 120)
    if digest(dest).lower() != sha.lower():
        dest.unlink()
        raise ValueError("SHA-256 mismatch; input deleted")
    print(f"verified {dest.name}: SHA-256 {sha.lower()}")


def extract_runtime(archive, dest):
    """Never extract release executables; take only the runtime inputs the host build links against."""
    allowed_dirs = {"Sys", "lang", "licenses", "Shaders"}
    with zipfile.ZipFile(archive) as src:
        roots = [PurePosixPath(info.filename).parent for info in src.infolist()
                 if PurePosixPath(info.filename).name == "dxr_pathtrace.dxil"]
        if len(roots) != 1:
            raise ValueError("release zip must have exactly one runtime root")
        root = roots[0]
        seen = set()
        for info in src.infolist():
            name = info.filename.replace("\\", "/")
            path = PurePosixPath(name)
            if path.is_absolute() or any(p in {"..", "."} or ":" in p for p in path.parts):
                raise ValueError("unsafe release zip path")
            try:
                rel = path.relative_to(root)
            except ValueError:
                continue
            if not rel.parts or info.is_dir():
                continue
            wanted = (len(rel.parts) == 1 and
                      (rel.suffix.lower() == ".dll" or rel.name in {"dxr_pathtrace.dxil", "README.txt"}))
            wanted |= rel.parts[0] in allowed_dirs
            if not wanted:
                continue
            if (rel.suffix.lower() in {".exe", ".dol", ".iso", ".gcm", ".rvz", ".gcz", ".ciso", ".wia", ".wbfs"}
                    or "recomp" in {p.lower() for p in rel.parts}):
                raise ValueError("forbidden input in release runtime folders")
            key = rel.as_posix().lower()
            if key in seen:
                raise ValueError("duplicate release runtime path")
            seen.add(key)
            target = dest.joinpath(*rel.parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            with src.open(info) as data, target.open("wb") as out:
                shutil.copyfileobj(data, out)
    for name in (*sorted(allowed_dirs), "dxr_pathtrace.dxil", "README.txt", "sl.interposer.dll", "sl.common.dll"):
        if not (dest / name).exists():
            raise ValueError(f"missing release runtime input: {name}")
    print(f"extracted {len(seen)} release runtime files; no release executables")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="command", required=True)
    dl = sub.add_parser("download")
    dl.add_argument("url_env")
    dl.add_argument("sha_env")
    dl.add_argument("dest", type=Path)
    ex = sub.add_parser("extract-runtime")
    ex.add_argument("archive", type=Path)
    ex.add_argument("dest", type=Path)
    args = ap.parse_args()
    if args.command == "download":
        download(os.environ[args.url_env], os.environ[args.sha_env], args.dest)
    elif args.command == "extract-runtime":
        extract_runtime(args.archive, args.dest)


if __name__ == "__main__":
    main()
