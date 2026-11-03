"""Disc, manifest and plain VCDIFF helpers (Python 3.9, standard library only)."""
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess

FORMAT = "vcdiff-xdelta3-3.2.1-nosecondary"
# The source tree's Sys folders (where the targets are read from) and the same folders as a build names them (what
# the manifest lists: the host applier, port/runtime/host/sysgen.cpp, accepts only these names).
SYS = ["slippi_sys", "slippi_sys_general", "slippi_sys_playback"]
BUILD_SYS = ["Sys", "SysPlayback", "slippi_sys_general"]
TARGETS = ["GameSetup_gui.dat", "slpCSS.dat"]
XDELTA = "/opt/homebrew/bin/xdelta3"


def sha1(data):
    return hashlib.sha1(data).hexdigest()


class Disc:
    """Read a bounded GCM FST, preserving directory paths and rejecting ambiguity."""
    def __init__(self, path):
        self.path = Path(path)
        self.size = self.path.stat().st_size
        with self.path.open("rb") as f:
            header = f.read(0x42C)
            if len(header) != 0x42C or header[0x1C:0x20] != b"\xc2\x33\x9f\x3d":
                raise ValueError("not a GameCube GCM image")
            self.id = header[:6].decode("ascii")
            self.revision = header[7]
            offset, size = struct.unpack_from(">II", header, 0x424)
            if size < 12 or offset + size > self.size:
                raise ValueError("FST outside disc bounds")
            f.seek(offset)
            fst = f.read(size)
        root, parent, count = struct.unpack_from(">III", fst)
        if root != 0x01000000 or parent != 0 or count < 1 or count * 12 > size:
            raise ValueError("invalid FST root/entry count")
        strings = fst[count * 12:]
        self.files = {}
        stack = [("", count, 0)]
        paths = set()
        for i in range(1, count):
            while stack and i >= stack[-1][1]:
                stack.pop()
            flag, offset, length = struct.unpack_from(">III", fst, i * 12)
            name_offset = flag & 0xFFFFFF
            if name_offset >= len(strings):
                raise ValueError("FST name outside string table")
            end = strings.find(b"\0", name_offset)
            if end < 0:
                raise ValueError("unterminated FST name")
            name = strings[name_offset:end].decode("ascii")
            if not name or name in (".", "..") or "/" in name or "\\" in name:
                raise ValueError("unsafe FST name")
            path = stack[-1][0] + name
            if path in paths:
                raise ValueError("duplicate FST path: " + path)
            paths.add(path)
            if flag >> 24 == 1:
                if offset != stack[-1][2] or not i < length <= stack[-1][1]:
                    raise ValueError("invalid FST directory bounds/parent")
                stack.append((path + "/", length, i))
            elif flag >> 24 == 0:
                if offset + length > self.size:
                    raise ValueError("FST file outside disc: " + path)
                self.files[path] = (offset, length)
            else:
                raise ValueError("invalid FST entry type")

    def chunks(self, name):
        offset, remaining = self.files[name]
        with self.path.open("rb") as f:
            f.seek(offset)
            while remaining:
                chunk = f.read(min(1024 * 1024, remaining))
                if not chunk:
                    raise ValueError("truncated disc file: " + name)
                remaining -= len(chunk)
                yield chunk

    def blocks(self, name):
        tail = b""
        for chunk in self.chunks(name):
            data = tail + chunk
            stop = max(0, (len(data) - 64) // 32 + 1)
            for i in range(stop):
                yield data[i * 32:i * 32 + 64]
            tail = data[stop * 32:]


def blocks(data):
    for offset in range(0, len(data) - 63, 32):
        block = data[offset:offset + 64]
        if block != block[:1] * 64:
            yield offset, block


def run_xdelta(arguments):
    # xdelta interprets XDELTA as extra options; never let it affect reproducibility.
    child_env = os.environ.copy()
    child_env.pop("XDELTA", None)
    result = subprocess.run([XDELTA] + list(map(str, arguments)), env=child_env,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise ValueError("xdelta3 failed: " + result.stderr.decode("utf-8", "replace").strip())
    return (result.stdout + result.stderr).decode("utf-8", "replace")


def require_xdelta():
    version = run_xdelta(["-V"])
    if not re.search(r"Xdelta version 3\.2\.1(?:,|\s)", version):
        raise ValueError("requires xdelta3 3.2.1 at " + XDELTA)


def source_concat(disc, sources, destination):
    """Only callers' TemporaryDirectory destinations may hold retail bytes."""
    with Path(destination).open("wb") as out:
        for source in sources:
            name = source["disc"]
            if name not in disc.files:
                raise ValueError("missing source " + name)
            if disc.files[name][1] != source["size"]:
                raise ValueError("source size mismatch: " + name)
            digest = hashlib.sha1()
            for chunk in disc.chunks(name):
                digest.update(chunk)
                out.write(chunk)
            if digest.hexdigest() != source["sha1"]:
                raise ValueError("source SHA-1 mismatch: " + name)


def validate_manifest(manifest):
    if not isinstance(manifest, dict) or set(manifest) != {"format", "disc", "files"}:
        raise ValueError("invalid manifest fields")
    if manifest["format"] != FORMAT or manifest["disc"] != {"id": "GALE01", "revision": 2}:
        raise ValueError("unsupported manifest format/disc")
    files = manifest["files"]
    if not isinstance(files, list) or len(files) != 2:
        raise ValueError("manifest must describe exactly two targets")
    seen = set()
    for entry in files:
        if not isinstance(entry, dict) or set(entry) != {"target", "sys", "target_sha1", "target_size", "sources", "delta", "delta_sha1", "delta_size"}:
            raise ValueError("invalid target fields")
        name = entry["target"]
        if name not in TARGETS or name in seen or entry["sys"] != BUILD_SYS or entry["delta"] != name + ".vcdiff":
            raise ValueError("invalid/duplicate target or delta path")
        seen.add(name)
        if not isinstance(entry["sources"], list) or not entry["sources"]:
            raise ValueError("target has no sources: " + name)
        source_names = set()
        for source in entry["sources"]:
            if not isinstance(source, dict) or set(source) != {"disc", "sha1", "size"}:
                raise ValueError("invalid source fields")
            path = source["disc"]
            if not isinstance(path, str) or not path or path.startswith("/") or any(part in ("", ".", "..") for part in path.split("/")) or "\\" in path or path in source_names:
                raise ValueError("unsafe/duplicate source path")
            source_names.add(path)
            check_hash_size(source, "sha1", "size")
        check_hash_size(entry, "target_sha1", "target_size")
        check_hash_size(entry, "delta_sha1", "delta_size")
    return manifest


def check_hash_size(entry, hash_key, size_key):
    if not isinstance(entry[hash_key], str) or not re.fullmatch("[0-9a-f]{40}", entry[hash_key]):
        raise ValueError("invalid SHA-1: " + hash_key)
    if type(entry[size_key]) is not int or entry[size_key] <= 0:
        raise ValueError("invalid size: " + size_key)


def load_manifest(directory):
    def unique_pairs(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("duplicate JSON key: " + key)
            result[key] = value
        return result
    with (Path(directory) / "manifest.json").open() as f:
        return validate_manifest(json.load(f, object_pairs_hook=unique_pairs))


def check_disc(disc, manifest):
    if {"id": disc.id, "revision": disc.revision} != manifest["disc"]:
        raise ValueError("disc identity/revision mismatch")


def rebuild(disc, entry, directory, temporary):
    source = Path(temporary) / "source.bin"
    result = Path(temporary) / "rebuilt.dat"
    source_concat(disc, entry["sources"], source)
    delta = Path(directory) / entry["delta"]
    data = delta.read_bytes()
    if len(data) != entry["delta_size"] or sha1(data) != entry["delta_sha1"]:
        raise ValueError("delta size/SHA-1 mismatch: " + delta.name)
    vcdiff_stats(data)
    run_xdelta(["-d", "-f", "-D", "-R", "-s", source, delta, result])
    data = result.read_bytes()
    if len(data) != entry["target_size"] or sha1(data) != entry["target_sha1"]:
        raise ValueError("target size/SHA-1 mismatch: " + entry["target"])


class Cursor:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, count):
        if count < 0 or self.pos + count > len(self.data):
            raise ValueError("truncated VCDIFF")
        result = self.data[self.pos:self.pos + count]
        self.pos += count
        return result

    def byte(self):
        return self.take(1)[0]

    def integer(self):
        value = 0
        for _ in range(10):
            byte = self.byte()
            value = (value << 7) | (byte & 127)
            if not byte & 128:
                return value
        raise ValueError("oversized VCDIFF integer")


def default_table():
    table = [[("RUN", 0)], [("ADD", 0)]]
    table += [[("ADD", size)] for size in range(1, 18)]
    for _mode in range(9):
        table += [[("COPY", size)] for size in [0] + list(range(4, 19))]
    for mode in range(9):
        for add in range(1, 5):
            for copy in (range(4, 7) if mode < 6 else [4]):
                table.append([("ADD", add), ("COPY", copy)])
    table += [[("COPY", 4), ("ADD", 1)] for _mode in range(9)]
    assert len(table) == 256
    return table


TABLE = default_table()


def vcdiff_stats(data):
    """Count actual ADD instructions, including paired default-table opcodes."""
    cursor = Cursor(data)
    if cursor.take(5) != b"\xd6\xc3\xc4\0\0":
        raise ValueError("requires plain VCDIFF: no secondary compression, custom table, or app header")
    total_add = total_target = 0
    while cursor.pos < len(data):
        indicator = cursor.byte()
        if indicator & ~7 or indicator & 3 == 3:
            raise ValueError("invalid VCDIFF window indicator")
        if indicator & 3:
            cursor.integer()  # source segment size
            cursor.integer()  # source segment position
        encoding_size = cursor.integer()
        end = cursor.pos + encoding_size
        target_size = cursor.integer()
        if cursor.byte() != 0:
            raise ValueError("secondary-compressed VCDIFF section")
        data_size, inst_size, addr_size = [cursor.integer() for _ in range(3)]
        if indicator & 4:
            cursor.take(4)  # xdelta's Adler-32 window checksum extension
        cursor.take(data_size)
        instructions = Cursor(cursor.take(inst_size))
        cursor.take(addr_size)
        if cursor.pos != end:
            raise ValueError("VCDIFF encoding size mismatch")
        produced = used_data = 0
        while instructions.pos < len(instructions.data):
            for kind, size in TABLE[instructions.byte()]:
                if size == 0:
                    size = instructions.integer()
                produced += size
                if kind == "ADD":
                    total_add += size
                    used_data += size
                elif kind == "RUN":
                    used_data += 1
        if produced != target_size or used_data != data_size:
            raise ValueError("VCDIFF instruction/data length mismatch")
        total_target += target_size
    return total_add, total_target


def scan_disc(disc, deltas, chunk_size=1 << 24):
    """Index every delta offset; scan the entire image at disc-aligned 32-byte steps.

    Logic follows datcheck/delta_scan.py. Keep each delta separately (including
    shared windows), confirm digest matches exactly, and never double-count a
    disc window at a streaming chunk boundary. Values are (disc, delta) offsets.
    """
    index = {}
    hits = {name: [] for name in deltas}
    for name, data in deltas.items():
        for offset in range(len(data) - 63):
            window = data[offset:offset + 64]
            if window.count(window[:1]) != 64:
                digest = hashlib.blake2b(window, digest_size=8).digest()
                # One occurrence per delta is enough to locate the stored bytes.
                index.setdefault(digest, {}).setdefault(name, offset)
    with disc.path.open("rb") as stream:
        tail, offset, next_offset = b"", 0, 0
        while True:
            chunk = stream.read(chunk_size)
            if not chunk:
                break
            data = tail + chunk
            base = offset - len(tail)
            start = max(next_offset, base + (-base) % 32)
            for position in range(start - base, len(data) - 63, 32):
                window = data[position:position + 64]
                if window.count(window[:1]) == 64:
                    continue
                digest = hashlib.blake2b(window, digest_size=8).digest()
                for name, delta_offset in index.get(digest, {}).items():
                    if deltas[name][delta_offset:delta_offset + 64] == window:
                        hits[name].append((base + position, delta_offset))
            offset += len(chunk)
            next_offset = max(next_offset, ((offset - 64) // 32 + 1) * 32)
            tail = data[-64:]
    return hits


def hit_files(disc, offset):
    """All FST files touched by this 64-byte hit (also covers file boundaries)."""
    return [(name, offset - start) for name, (start, size) in sorted(disc.files.items())
            if start < offset + 64 and offset < start + size]


def report_scan(disc, name, hits):
    print("{}: full-disc scan hits {} {}".format(
        name, len(hits), "FAIL" if hits else "PASS"), flush=True)
    for disc_offset, delta_offset in hits:
        owners = hit_files(disc, disc_offset)
        location = ", ".join("{} {:+d}".format(path, relative) for path, relative in owners)
        print("  disc offset {}: {}; delta offset {}".format(
            disc_offset, location or "outside FST files", delta_offset), flush=True)


def compliance(disc, entries, directory):
    """Require zero nonuniform 64-byte disc windows anywhere inside any delta."""
    deltas = {}
    for entry in entries:
        data = (Path(directory) / entry["delta"]).read_bytes()
        if len(data) != entry["delta_size"] or sha1(data) != entry["delta_sha1"]:
            raise ValueError("delta size/SHA-1 mismatch: " + entry["delta"])
        add, target_size = vcdiff_stats(data)
        if target_size != entry["target_size"]:
            raise ValueError("VCDIFF target size mismatch")
        print("{}: ADD {} / target {} bytes".format(entry["target"], add, target_size), flush=True)
        deltas[entry["delta"]] = data
    hits = scan_disc(disc, deltas)
    for name, found in hits.items():
        report_scan(disc, name, found)
    return not any(hits.values())
