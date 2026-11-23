#!/usr/bin/env python3
"""Convert the renderer's PPM files to PNG (standard library only), optionally upscaled."""
import struct, sys, zlib
from pathlib import Path

def convert(src: Path, scale: int) -> Path:
    data = src.read_bytes()
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    px = parts[3]
    rows = []
    for y in range(h):
        row = px[y * w * 3:(y + 1) * w * 3]
        if scale > 1:
            row = b"".join(row[i:i + 3] * scale for i in range(0, len(row), 3))
        rows.extend([b"\x00" + row] * scale)
    raw = b"".join(rows)
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale, 8, 2, 0, 0, 0)) \
        + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    dst = src.with_suffix(".png")
    dst.write_bytes(png)
    return dst

if __name__ == "__main__":
    scale = int(sys.argv[2]) if len(sys.argv) > 2 else 2
    for f in sorted(Path(sys.argv[1]).glob("*.ppm")):
        print(convert(f, scale))
