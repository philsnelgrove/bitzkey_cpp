"""Draw the app icon, a grazer Bitzkey (its leaf, in its dot colour, on a black centre inside its
ring), into packaging/bitzkey.ico.

Run it with the Python project's interpreter, since it borrows that project's icon shapes:
    ..\\bitzkey\\.venv\\Scripts\\python.exe packaging\\make_icon.py
Each size is drawn four times larger and scaled down, for smooth edges, and stored as a PNG.
"""

from __future__ import annotations

import io
import struct
import sys
from pathlib import Path

import pygame as pg

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent.parent / "bitzkey"))
from display.Icons import draw_icon  # noqa: E402
from display.Look import CENTRE, OUTLINE, outline_offsets  # noqa: E402

SIZES = (16, 24, 32, 48, 64, 128, 256)
RING = (60, 150, 120)
DOT = (235, 200, 80)


def picture(size: int) -> bytes:
    big = size * 4
    surface = pg.Surface((big, big), pg.SRCALPHA)
    centre = (big // 2, big // 2)
    pg.draw.circle(surface, RING, centre, big // 2)
    dot = round(big * 0.38)
    pg.draw.circle(surface, CENTRE, centre, dot)
    edge = max(4, big // 40)  # the outline, about as thin as the app's own
    for dx, dy in outline_offsets(edge):
        draw_icon(surface, "grazer", (centre[0] + dx, centre[1] + dy), dot * 0.8, OUTLINE)
    draw_icon(surface, "grazer", centre, dot * 0.8, DOT)
    small = pg.transform.smoothscale(surface, (size, size))
    data = io.BytesIO()
    pg.image.save(small, data, "png")
    return data.getvalue()


def main() -> None:
    pictures = [picture(size) for size in SIZES]
    header = struct.pack("<HHH", 0, 1, len(SIZES))
    offset = len(header) + 16 * len(SIZES)
    entries = b""
    for size, data in zip(SIZES, pictures):
        side = 0 if size >= 256 else size  # 0 means 256
        entries += struct.pack("<BBBBHHII", side, side, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    (HERE / "bitzkey.ico").write_bytes(header + entries + b"".join(pictures))
    pg.image.save(pg.image.load(io.BytesIO(pictures[-1])), str(HERE / "bitzkey.png"))


if __name__ == "__main__":
    main()
