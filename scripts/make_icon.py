#!/usr/bin/env python3
"""Generate icons/chess.png (64x64 pawn on a dark badge). Stdlib only."""

import math
import struct
import zlib
from pathlib import Path

N = 64        # output size
S = 8         # supersampling factor per axis

BADGE = (26, 30, 40)
PAWN = (236, 239, 244)


def rounded_rect(x, y, cx, cy, hw, hh, r):
    dx = abs(x - cx) - (hw - r)
    dy = abs(y - cy) - (hh - r)
    outside = math.hypot(max(dx, 0.0), max(dy, 0.0))
    inside = min(max(dx, dy), 0.0)
    return outside + inside - r


def inside_pawn(x, y):
    if math.hypot(x - 32, y - 20.5) <= 7.2:                      # head
        return True
    if abs(y - 30.2) <= 1.9 and abs(x - 32) <= 8.2:              # collar
        return True
    if 30.0 <= y <= 47.5:                                        # tapered body
        t = (y - 30.0) / 17.5
        if abs(x - 32) <= 4.3 + 6.2 * t:
            return True
    if 46.5 <= y <= 54.0:                                        # base
        return rounded_rect(x, y, 32, 50.3, 13.0, 3.2, 2.5) <= 0
    return False


def inside_badge(x, y):
    return rounded_rect(x, y, 32, 32, 28, 28, 14) <= 0


def render():
    rgba = bytearray()
    for py in range(N):
        for px in range(N):
            badge = pawn = 0
            for sy in range(S):
                for sx in range(S):
                    x = px + (sx + 0.5) / S
                    y = py + (sy + 0.5) / S
                    if inside_badge(x, y):
                        badge += 1
                        if inside_pawn(x, y):
                            pawn += 1
            total = S * S
            a = badge / total
            pw = (pawn / badge) if badge else 0.0
            r = BADGE[0] * (1 - pw) + PAWN[0] * pw
            g = BADGE[1] * (1 - pw) + PAWN[1] * pw
            b = BADGE[2] * (1 - pw) + PAWN[2] * pw
            rgba += bytes((round(r), round(g), round(b), round(a * 255)))
    return bytes(rgba)


def write_png(path, w, h, rgba):
    raw = b"".join(
        b"\x00" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h)
    )

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


if __name__ == "__main__":
    out = Path(__file__).resolve().parent.parent / "icons" / "chess.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    write_png(out, N, N, render())
    print(f"wrote {out}")
