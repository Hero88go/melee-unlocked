"""Reject packages built with a stale generated Slippi Gecko table."""

import re
from pathlib import Path


def verify_generated_gct(exe: Path) -> None:
    exe = exe.resolve()
    # build-root/port/Release/melee_port.exe
    cache = exe.parents[2] / "CMakeCache.txt"
    if not cache.is_file():
        raise ValueError(f"cannot find CMakeCache.txt for {exe}")
    match = re.search(r"^PORT_GEN:PATH=(.+)$", cache.read_text(encoding="utf-8"), re.M)
    if not match:
        raise ValueError(f"PORT_GEN missing from {cache}")
    generated = Path(match.group(1).strip()) / "gecko_data.cpp"
    if not generated.is_file():
        raise ValueError(f"generated Gecko table is missing: {generated}")
    source = generated.read_text(encoding="utf-8")
    match = re.search(r"const uint8_t slippi_gct\[(\d+)\] = \{(.*?)\};", source, re.S)
    if not match:
        raise ValueError(f"cannot read slippi_gct from {generated}")
    table = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", match.group(2)))
    if len(table) != int(match.group(1)):
        raise ValueError(f"generated GCT length mismatch in {generated}")
    if table not in exe.read_bytes():
        raise ValueError(
            f"{exe.name} does not embed the selected {len(table)}-byte GCT from {generated}; "
            "regenerate the guest and rebuild before packaging"
        )
