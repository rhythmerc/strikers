"""Converts binary PPMs (Aurora's AURORA_XR_DUMP images) to PNGs beside them, with no dependencies.

Usage: python3 tools/ppm2png.py [--half] FILE.ppm...   (--half: every other pixel, a quarter the size)
"""

import struct
import sys
import zlib
from pathlib import Path


def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)


def convert(path, step):
    data = Path(path).read_bytes()
    magic, w, h, _maxval, pixels = data.split(maxsplit=4)
    if magic != b"P6":
        raise SystemExit(f"{path}: not a binary PPM")
    w, h = int(w), int(h)
    rows = []
    for y in range(0, h, step):
        row = pixels[y * w * 3:(y + 1) * w * 3]
        if step > 1:
            row = b"".join(row[x * 3:x * 3 + 3] for x in range(0, w, step))
        rows.append(b"\0" + row)
    sw, sh = (w + step - 1) // step, (h + step - 1) // step
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", sw, sh, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(b"".join(rows), 6)) + chunk(b"IEND", b""))
    Path(path).with_suffix(".png").write_bytes(png)


def main():
    args = sys.argv[1:]
    step = 1
    if args and args[0] == "--half":
        step = 2
        args = args[1:]
    for path in args:
        convert(path, step)


if __name__ == "__main__":
    main()
