#!/usr/bin/env python3
"""Check the production collision display lists against their vertex arrays."""
import re
import struct
import sys
from pathlib import Path


def array(source, name):
    match = re.search(r"u8\s+" + name + r"\[(0x[\da-fA-F]+|\d+)\]\s*=\s*\{([^}]+)\}", source)
    if not match:
        raise ValueError(f"missing production array {name}")
    data = bytes(int(x, 16) for x in re.findall(r"0x([\da-fA-F]{2})\b", match[2]))
    size = int(match[1], 0)
    assert len(data) <= size, name
    return data + bytes(size - len(data))


def check(source):
    for shape, normal_scale in (("Sphere", 16384), ("Cylinder", 64)):
        positions = array(source, f"lbColl_{shape}Positions")
        normals = array(source, f"lbColl_{shape}Normals")
        commands = array(source, f"lbColl_{shape}DisplayList")
        cursor = 0
        vertices = 0
        while cursor < len(commands):
            opcode = commands[cursor]
            cursor += 1
            if opcode == 0:
                continue
            assert opcode == 0x98, f"unexpected {shape} display opcode {opcode:#x}"
            count = int.from_bytes(commands[cursor:cursor + 2], "big")
            cursor += 2
            for _ in range(count):
                p, n = commands[cursor:cursor + 2]
                cursor += 2
                assert 6 * (p + 1) <= len(positions), f"{shape} position {p} outside {len(positions)} bytes"
                assert 6 * (n + 1) <= len(normals), f"{shape} normal {n} outside {len(normals)} bytes"
                xyz = [v / 16384 for v in struct.unpack_from(">hhh", positions, 6 * p)]
                normal = [v / normal_scale for v in struct.unpack_from(">hhh", normals, 6 * n)]
                radius2 = sum(v * v for v in (xyz if shape == "Sphere" else xyz[1:]))
                assert abs(radius2 - 1) < 0.001, (shape, p, xyz)
                if shape == "Cylinder":
                    assert abs(xyz[0]) <= 0.501, (shape, p, xyz)
                assert abs(sum(v * v for v in normal) - 1) < 0.025, (shape, n, normal)
                vertices += 1
        assert vertices > 0, shape
    print("PASS: collision display lists only reference complete capsule geometry")


if __name__ == "__main__":
    check(Path(sys.argv[1]).read_text(encoding="utf-8"))
