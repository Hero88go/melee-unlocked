"""List or extract files from a GameCube ISO, by walking the disc's FST.

    python tools\\iso_file.py --iso "...\\ACE.iso" --list
    python tools\\iso_file.py --iso "...\\ACE.iso" --extract codes.gct --out build\\codes.gct

Put this in the repo's tools\\ folder. --list prints every file with its disc offset and size;
--extract matches on the full path or just the file name, case-insensitively.
"""
import argparse
import struct
from pathlib import Path


def read_fst(stream):
    """Returns [(path, disc offset, size)] for every file on the disc."""
    stream.seek(0x424)
    fst_offset, fst_size = struct.unpack(">II", stream.read(8))
    stream.seek(fst_offset)
    fst = stream.read(fst_size)
    if len(fst) < 12:
        raise SystemExit("no FST on this disc")
    entries = struct.unpack(">I", fst[8:12])[0]
    strings = entries * 12
    files = []
    stack = [(entries, "")]        # (index this directory ends at, path prefix)
    for i in range(1, entries):
        entry = fst[i * 12:(i + 1) * 12]
        is_dir = entry[0]
        name_offset = int.from_bytes(entry[1:4], "big")
        a, b = struct.unpack(">II", entry[4:12])
        start = strings + name_offset
        end = fst.index(0, start)
        name = fst[start:end].decode("latin1")
        while len(stack) > 1 and i >= stack[-1][0]:
            stack.pop()
        prefix = stack[-1][1]
        if is_dir:
            stack.append((b, prefix + name + "/"))
        else:
            files.append((prefix + name, a, b))
    return files


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", required=True)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--extract", help="file name or full path on the disc")
    ap.add_argument("--out", help="where to write it (default: the file's own name)")
    args = ap.parse_args()

    with open(args.iso, "rb") as stream:
        files = read_fst(stream)
        if args.list or not args.extract:
            print("%d files" % len(files))
            for path, offset, size in files:
                print("  %-40s offset %08X  size %8d" % (path, offset, size))
            return
        want = args.extract.lower().replace("\\", "/")
        hits = [f for f in files if f[0].lower() == want or f[0].lower().rsplit("/", 1)[-1] == want]
        if not hits:
            raise SystemExit("%s: not on this disc" % args.extract)
        if len(hits) > 1:
            raise SystemExit("ambiguous, matches: %s" % ", ".join(h[0] for h in hits))
        path, offset, size = hits[0]
        stream.seek(offset)
        data = stream.read(size)
    out = Path(args.out or path.rsplit("/", 1)[-1])
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    print("%s: %d bytes from disc offset %08X -> %s" % (path, size, offset, out))


if __name__ == "__main__":
    main()
