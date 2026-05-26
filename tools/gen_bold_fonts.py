#!/usr/bin/env python3
"""Generate subset cFONT tables (row-major, MSB first) for provisioning UI."""
from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

# Microsoft YaHei Bold — 中英文粗体
FONT_PATH = r"C:\Windows\Fonts\msyhbd.ttc"
OUT_DIR = Path(__file__).resolve().parent.parent

# 每套字库只含本档用到的字符（省 Flash）
FONT_SPECS: dict[str, dict] = {
    "font38CN": {
        "size": 38,
        "var": "Font38CN",
        "comment": "顶部标题 Font38_Bold",
        "chars": "钙钛矿墨水屏会议牌",
    },
    "font36CN": {
        "size": 36,
        "var": "Font36CN",
        "comment": "右侧三行提示 Font36_Bold",
        # 显示为「进行 WiFi 配置」
        "chars": "手机扫描二维码连接设备热点进行配置 WiFi",
    },
    "font24CN": {
        "size": 24,
        "var": "Font24CN",
        "comment": "WIFI 配置小框 Font24_Bold",
        "chars": "WIFI配置",
    },
    "font20CN": {
        "size": 20,
        "var": "Font20CN",
        "comment": "左侧标签与内容 Font20_Bold",
        # 含 EPD-xxxx 热点前缀及 MeetingBoard-Setup / 192.168.4.1
        "chars": "热点名称IP地址MeetingBoard-Setup192.168.4.1EPD",
    },
}


def cell_width(size: int) -> int:
    return ((size + 7) // 8) * 8


def render_char(ch: str, size: int, cw: int) -> Image.Image:
    img = Image.new("1", (cw, size), 1)
    draw = ImageDraw.Draw(img)
    font = ImageFont.truetype(FONT_PATH, size)
    bbox = draw.textbbox((0, 0), ch, font=font)
    w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
    x = (cw - w) // 2 - bbox[0]
    y = (size - h) // 2 - bbox[1]
    draw.text((x, y), ch, font=font, fill=0)
    return img


def img_to_bytes(img: Image.Image) -> list[int]:
    w, h = img.size
    out: list[int] = []
    for row in range(h):
        for byte_col in range(w // 8):
            b = 0
            for bit in range(8):
                col = byte_col * 8 + bit
                if img.getpixel((col, row)) == 0:
                    b |= 0x80 >> bit
            out.append(b)
    return out


def unique_chars(s: str) -> str:
    seen: set[str] = set()
    out: list[str] = []
    for ch in s:
        if ch not in seen:
            seen.add(ch)
            out.append(ch)
    return "".join(out)


def format_entry(ch: str, data: list[int]) -> str:
    hexes = [f"0x{b:02X}" for b in data]
    lines = [",".join(hexes[i : i + 8]) for i in range(0, len(hexes), 8)]
    body = ",\n".join(lines)
    display = ch if ch != " " else "SP"
    esc = ch.replace("\\", "\\\\").replace('"', '\\"')
    return f'/*--  {display}  --*/\n{{"{esc}",\n{body}}},'


def emit_c_file(name: str, spec: dict) -> Path:
    size = spec["size"]
    cw = cell_width(size)
    chars = unique_chars(spec["chars"])
    var = spec["var"]
    table = f"{var}_Table"

    entries: list[str] = []
    for ch in chars:
        data = img_to_bytes(render_char(ch, size, cw))
        entries.append(format_entry(ch, data))

    content = f"""#include "fonts.h"

/*
 * {spec["comment"]} — {size}px 粗体 (msyhbd)
 * 取模：逐行、MSB 在前；宽 {cw}px × 高 {size}px，每字 {len(entries[0].split(chr(10))[1].split(",")) if entries else 0} 字节
 * 字符集: {chars}
 */
const CH_CN {table}[] = {{
{chr(10).join(entries)}
}};

cFONT {var} = {{
  {table},
  sizeof({table}) / sizeof(CH_CN),
  {cw}, /* ASCII Width */
  {cw}, /* Width */
  {size}, /* Height */
}};
"""
    out = OUT_DIR / f"{name}.c"
    out.write_text(content, encoding="utf-8")
    nbytes = sum(len(img_to_bytes(render_char(c, size, cw))) for c in chars)
    print(f"{out.name}: {len(chars)} glyphs, ~{nbytes} bytes data, cell {cw}x{size}")
    return out


def main() -> None:
    for name, spec in FONT_SPECS.items():
        emit_c_file(name, spec)
    print("Done. Update fonts.h MAX_HEIGHT/MAX_WIDTH to >= 48 if not already.")


if __name__ == "__main__":
    main()
