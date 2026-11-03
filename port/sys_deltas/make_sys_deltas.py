#!/usr/bin/env python3
"""Generate verified, reproducible Sys deltas from a player's GALE01 revision 2 disc."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import tempfile

from sys_delta_common import (BUILD_SYS, Disc, FORMAT, SYS, TARGETS, blocks, check_disc,
                              compliance, load_manifest, rebuild, require_xdelta,
                              run_xdelta, sha1, source_concat, validate_manifest,
                              scan_disc, hit_files, report_scan)

DEFAULT_OUT = Path(__file__).resolve().parent
PORT = DEFAULT_OUT.parent


def targets():
    result = {}
    for name in TARGETS:
        copies = [(PORT / folder / "GameFiles" / "GALE01" / name).read_bytes() for folder in SYS]
        if any(copy != copies[0] for copy in copies[1:]):
            raise ValueError("Sys copies differ: " + name)
        result[name] = copies[0]
        print("{}: three Sys copies SHA-1 {} PASS".format(name, sha1(copies[0])), flush=True)
    return result


def select_sources(disc, target_data):
    # Index each nonuniform target block, then scan EVERY FST file (not only menus).
    indexes = {}
    for name, data in target_data.items():
        for offset, block in blocks(data):
            indexes.setdefault(block, []).append((name, offset // 32))
    coverage = {name: {} for name in target_data}
    print("Scanning {} disc files for source coverage...".format(len(disc.files)), flush=True)
    for filename in sorted(disc.files):
        matches = {block for block in disc.blocks(filename) if block in indexes}
        cells = {name: set() for name in target_data}
        for block in matches:
            for name, cell in indexes[block]:
                cells[name].update((cell, cell + 1))
        for name, covered in cells.items():
            if covered:
                coverage[name][filename] = covered
    result = {}
    for name, remaining in coverage.items():
        covered, selected = set(), []
        while remaining:
            filename = min(remaining, key=lambda filename: (-len(remaining[filename] - covered), filename))
            added = len(remaining[filename] - covered) * 32
            if added < 64:
                break
            covered.update(remaining.pop(filename))
            digest = hashlib.sha1()
            for chunk in disc.chunks(filename):
                digest.update(chunk)
            selected.append({"disc": filename, "sha1": digest.hexdigest(), "size": disc.files[filename][1]})
            print("{}: source {} ({} bytes), adds {} covered bytes".format(name, filename, disc.files[filename][1], added), flush=True)
        if not selected:
            raise ValueError("no source contributes at least 64 bytes: " + name)
        result[name] = selected
    return result


def source_record(disc, name):
    digest = hashlib.sha1()
    for chunk in disc.chunks(name):
        digest.update(chunk)
    return {"disc": name, "sha1": digest.hexdigest(), "size": disc.files[name][1]}


def close_sources(disc, target_data, selected, temporary):
    """Encode, scan, append implicated sources, and repeat for at most 8 rounds."""
    temporary = Path(temporary)
    entries = {}
    pending = list(target_data)
    for round_number in range(1, 9):
        for name in pending:
            source = temporary / "source.bin"
            source_concat(disc, selected[name], source)
            target = temporary / "target.dat"
            target.write_bytes(target_data[name])
            delta = temporary / (name + ".vcdiff")
            # -a disables armor; -A omits filenames; -n writes no window checksum (the fork's host decoder,
            # port/runtime/host/vcdiff.cpp, reads that field as open-vcdiff's varint, not xdelta3's adler32;
            # the target SHA-1 is the check). Cover the entire source.
            source_window = max(1 << 20, 1 << (source.stat().st_size - 1).bit_length())
            run_xdelta(["-e", "-f", "-S", "none", "-9", "-a", "-A", "-n", "-D",
                        "-B", source_window, "-s", source, target, delta])
            data = delta.read_bytes()
            entry = {"target": name, "sys": BUILD_SYS[:], "target_sha1": sha1(target_data[name]),
                     "target_size": len(target_data[name]), "sources": selected[name][:],
                     "delta": delta.name, "delta_sha1": sha1(data), "delta_size": len(data)}
            rebuild(disc, entry, temporary, temporary)
            print("{}: round {}; {} delta bytes; decoded SHA-1 PASS".format(
                name, round_number, len(data)), flush=True)
            entries[name] = entry
        hits = scan_disc(disc, {name: (temporary / entries[name]["delta"]).read_bytes()
                                for name in pending})
        pending = []
        for name, found in hits.items():
            report_scan(disc, name, found)
            if not found:
                continue
            if round_number == 8:
                raise ValueError("source closure failed after 8 rounds: " + name)
            counts = {}
            for offset, _ in found:
                owners = hit_files(disc, offset)
                if not owners:
                    raise ValueError("disc hit outside FST files; cannot add a source: " + name)
                for filename, _ in owners:
                    counts[filename] = counts.get(filename, 0) + 1
            existing = {source["disc"] for source in selected[name]}
            additions = sorted(counts, key=lambda filename: (-counts[filename], filename))
            additions = [filename for filename in additions if filename not in existing]
            if not additions:
                raise ValueError("source closure made no progress: " + name)
            for filename in additions:
                selected[name].append(source_record(disc, filename))
                print("{}: append source {} ({} hits)".format(name, filename, counts[filename]), flush=True)
            pending.append(name)
        if not pending:
            return [entries[name] for name in target_data]
    raise AssertionError("unreachable closure round")


def generate(disc, out):
    require_xdelta()
    identity = {"format": FORMAT, "disc": {"id": "GALE01", "revision": 2}, "files": []}
    check_disc(disc, identity)
    target_data = targets()
    selected = select_sources(disc, target_data)
    # All retail concatenations, interim deltas and decoded targets stay in temp.
    with tempfile.TemporaryDirectory(prefix="sys-deltas-") as temporary:
        temporary = Path(temporary)
        entries = close_sources(disc, target_data, selected, temporary)
        identity["files"] = entries
        validate_manifest(identity)
        out.mkdir(parents=True, exist_ok=True)
        for entry in entries:
            (out / entry["delta"]).write_bytes((temporary / entry["delta"]).read_bytes())
        (out / "manifest.json").write_text(json.dumps(identity, indent=2) + "\n", encoding="utf-8")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("disc", type=Path)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--compliance-only", action="store_true", help="require zero full-disc matches at any delta offset")
    args = parser.parse_args(argv)
    try:
        disc = Disc(args.disc)
        if args.compliance_only:
            manifest = load_manifest(args.out)
            check_disc(disc, manifest)
            return 0 if compliance(disc, manifest["files"], args.out) else 1
        generate(disc, args.out)
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print("FAIL: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
