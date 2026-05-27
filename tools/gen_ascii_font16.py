#!/usr/bin/env python3
"""Generate a fixed-width ASCII sFONT for provisioning UI values."""
from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

FONT_PATH = r"C:\Windows\Fonts\msyhbd.ttc"
OUT_FILE = Path(__file__).resolve().parent.parent / "font16.cpp"
CELL_WIDTH = 12
CELL_HEIGHT = 16
TTF_SIZE = 16


def render_char(ch: str) -> Image.Image:
    img = Image.new("1", (CELL_WIDTH, CELL_HEIGHT), 1)
    draw = ImageDraw.Draw(img)
    font = ImageFont.truetype(FONT_PATH, TTF_SIZE)
    bbox = draw.textbbox((0, 0), ch, font=font)
    w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
    x = (CELL_WIDTH - w) // 2 - bbox[0]
    y = (CELL_HEIGHT - h) // 2 - bbox[1]
    draw.text((x, y), ch, font=font, fill=0)
    return img


def img_to_bytes(img: Image.Image) -> list[int]:
    out: list[int] = []
    row_bytes = (CELL_WIDTH + 7) // 8
    for y in range(CELL_HEIGHT):
        for byte_col in range(row_bytes):
            b = 0
            for bit in range(8):
                x = byte_col * 8 + bit
                if x < CELL_WIDTH and img.getpixel((x, y)) == 0:
                    b |= 0x80 >> bit
            out.append(b)
    return out


def main() -> None:
    entries: list[str] = []
    for code in range(32, 127):
        ch = chr(code)
        data = img_to_bytes(render_char(ch))
        comment = ch.replace("\\", "\\\\")
        bytes_text = ",\n".join(
            ", ".join(f"0x{b:02X}" for b in data[i:i + 8])
            for i in range(0, len(data), 8)
        )
        entries.append(f"\t// @{(code - 32) * len(data)} '{comment}'\n\t{bytes_text},\n")

    content = f'''#include "fonts.h"

// Font data for Microsoft YaHei Bold {TTF_SIZE}px, fixed cell {CELL_WIDTH}x{CELL_HEIGHT}

const uint8_t Font16_Table[] =
{{
{''.join(entries)}}};

sFONT Font16 = {{
  Font16_Table,
  {CELL_WIDTH}, /* Width */
  {CELL_HEIGHT}, /* Height */
}};
'''
    OUT_FILE.write_text(content, encoding="utf-8")
    print(f"Generated {OUT_FILE} ({CELL_WIDTH}x{CELL_HEIGHT})")


if __name__ == "__main__":
    main()
