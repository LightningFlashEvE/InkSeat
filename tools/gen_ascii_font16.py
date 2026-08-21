#!/usr/bin/env python3
"""Generate fixed-width ASCII sFONT tables for provisioning UI values."""
from __future__ import annotations

import os
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

OUT_DIR = Path(__file__).resolve().parent.parent / "firmware" / "Loader_esp32wf"
FONT_DIR = Path(os.environ.get("PHENO_FONT_DIR", r"C:\Windows\Fonts"))


def pick_font() -> str:
    for name in ("MiSans-Normal.ttf", "MiSans-Semibold.ttf", "MiSans-Bold.ttf"):
        path = FONT_DIR / name
        if path.exists():
            return str(path)
    return r"C:\Windows\Fonts\msyhbd.ttc"


FONT_PATH = pick_font()

FONT_SPECS = [
    {"name": "font16.cpp", "var": "Font16", "table": "Font16_Table", "cell_width": 12, "cell_height": 16, "ttf_size": 16},
    {"name": "font24.cpp", "var": "Font24", "table": "Font24_Table", "cell_width": 17, "cell_height": 24, "ttf_size": 24},
]


def render_char(ch: str, cell_width: int, cell_height: int, ttf_size: int) -> Image.Image:
    img = Image.new("1", (cell_width, cell_height), 1)
    draw = ImageDraw.Draw(img)
    font = ImageFont.truetype(FONT_PATH, ttf_size)
    bbox = draw.textbbox((0, 0), ch, font=font)
    w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
    x = (cell_width - w) // 2 - bbox[0]
    y = (cell_height - h) // 2 - bbox[1]
    draw.text((x, y), ch, font=font, fill=0)
    return img


def img_to_bytes(img: Image.Image) -> list[int]:
    out: list[int] = []
    width, height = img.size
    row_bytes = (width + 7) // 8
    for y in range(height):
        for byte_col in range(row_bytes):
            b = 0
            for bit in range(8):
                x = byte_col * 8 + bit
                if x < width and img.getpixel((x, y)) == 0:
                    b |= 0x80 >> bit
            out.append(b)
    return out


def emit_font(spec: dict) -> Path:
    cell_width = spec["cell_width"]
    cell_height = spec["cell_height"]
    ttf_size = spec["ttf_size"]
    entries: list[str] = []
    for code in range(32, 127):
        ch = chr(code)
        data = img_to_bytes(render_char(ch, cell_width, cell_height, ttf_size))
        comment = ch.replace("\\", "\\\\")
        bytes_text = ",\n".join(
            ", ".join(f"0x{b:02X}" for b in data[i:i + 8])
            for i in range(0, len(data), 8)
        )
        entries.append(f"\t// @{(code - 32) * len(data)} '{comment}'\n\t{bytes_text},\n")

    content = f'''#include "fonts.h"

// Font data for {Path(FONT_PATH).name} {ttf_size}px, fixed cell {cell_width}x{cell_height}

const uint8_t {spec["table"]}[] =
{{
{''.join(entries)}}};

sFONT {spec["var"]} = {{
  {spec["table"]},
  {cell_width}, /* Width */
  {cell_height}, /* Height */
}};
'''
    out_file = OUT_DIR / spec["name"]
    out_file.write_text(content, encoding="utf-8", newline="\n")
    print(f"Generated {out_file} ({cell_width}x{cell_height}) from {Path(FONT_PATH).name}")
    return out_file


def main() -> None:
    for spec in FONT_SPECS:
        emit_font(spec)


if __name__ == "__main__":
    main()
