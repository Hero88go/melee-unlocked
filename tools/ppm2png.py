"""Convert a binary PPM (P6) to PNG without third-party packages: python ppm2png.py in.ppm out.png"""
import struct
import sys
import zlib


def main():
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, "rb").read()
    parts = data.split(b"\n", 3)
    assert parts[0] == b"P6"
    w, h = map(int, parts[1].split())
    pixels = parts[3]
    rows = b"".join(b"\x00" + pixels[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(kind, body):
        c = kind + body
        return struct.pack(">I", len(body)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b"")
    open(dst, "wb").write(png)
    print(dst, w, h)


if __name__ == "__main__":
    main()
