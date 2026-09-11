"""Print duration, RMS and peak per second of a 16-bit stereo WAV (validation of audio output)."""
import struct
import sys
from pathlib import Path

def main():
    data = Path(sys.argv[1]).read_bytes()
    assert data[:4] == b"RIFF" and data[8:12] == b"WAVE"
    pos = 12
    rate = 0
    pcm = b""
    while pos + 8 <= len(data):
        tag, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if tag == b"fmt ":
            rate = struct.unpack("<I", body[4:8])[0]
        elif tag == b"data":
            pcm = body
        pos += 8 + size + (size & 1)
    frames = len(pcm) // 4
    print(f"{sys.argv[1]}: {frames} frames, {frames / rate:.2f} s at {rate} Hz")
    per = rate
    loud_seconds = 0
    for s in range(0, frames, per):
        chunk = pcm[s * 4:(s + per) * 4]
        n = len(chunk) // 2
        vals = struct.unpack(f"<{n}h", chunk)
        rms = (sum(v * v for v in vals) / max(1, n)) ** 0.5
        peak = max(abs(v) for v in vals) if vals else 0
        if rms > 50:
            loud_seconds += 1
        if len(sys.argv) > 2 and sys.argv[2] == "-v":
            print(f"  t={s // per:4d}s rms={rms:8.1f} peak={peak:6d}")
    print(f"seconds with audible signal (rms > 50): {loud_seconds} of {(frames + per - 1) // per}")

if __name__ == "__main__":
    main()
