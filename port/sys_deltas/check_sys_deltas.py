#!/usr/bin/env python3
"""Rebuild each manifest target from a retail disc; never fall back on a mismatch."""
import argparse
from pathlib import Path
import sys
import tempfile

from sys_delta_common import compliance, Disc, check_disc, load_manifest, rebuild, require_xdelta


def check(disc_path, directory):
    try:
        manifest = load_manifest(directory)
        disc = Disc(disc_path)
        check_disc(disc, manifest)
        require_xdelta()
    except (OSError, ValueError, KeyError, TypeError) as error:
        print("FAIL: " + str(error))
        return 1
    failed = False
    for entry in manifest["files"]:
        try:
            with tempfile.TemporaryDirectory(prefix="check-sys-deltas-") as temporary:
                rebuild(disc, entry, directory, temporary)
            print(entry["target"] + ": PASS (target SHA-1 " + entry["target_sha1"] + ")")
        except (OSError, ValueError, KeyError, TypeError) as error:
            failed = True
            print(entry["target"] + ": FAIL (" + str(error) + ")")
    try:
        if not compliance(disc, manifest["files"], directory):
            failed = True
    except (OSError, ValueError, KeyError, TypeError) as error:
        failed = True
        print("FAIL: " + str(error))
    return int(failed)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("disc", type=Path)
    parser.add_argument("--dir", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args(argv)
    return check(args.disc, args.dir)


if __name__ == "__main__":
    sys.exit(main())
