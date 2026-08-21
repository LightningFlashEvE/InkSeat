"""Verify font12CN.c glyphs match SimSun 16x16 and GUI_Paint bit order."""
from __future__ import annotations

import re
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

FONT = r"C:\Windows\Fonts\simsun.ttc"
SIZE = 16
FONT12CN = Path(__file__).resolve().parent.parent / "firmware" / "Loader_esp32wf" / "font12CN.c"


def render_char(ch: str) -> Image.Image:
    img = Image.new("1", (SIZE, SIZE), 1)
    draw = ImageDraw.Draw(img)
    font = ImageFont.truetype(FONT, SIZE)
    bbox = draw.textbbox((0, 0), ch, font=font)
    w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
    x = (SIZE - w) // 2 - bbox[0]
    y = (SIZE - h) // 2 - bbox[1]
    draw.text((x, y), ch, font=font, fill=0)
    return img


def img_to_bytes(img: Image.Image) -> bytes:
    w, h = img.size
    out = bytearray()
    for row in range(h):
        for byte_col in range(w // 8):
            b = 0
            for bit in range(8):
                col = byte_col * 8 + bit
                if img.getpixel((col, row)) == 0:
                    b |= 0x80 >> bit
            out.append(b)
    return bytes(out)


def bytes_to_img(data: bytes, w: int = 16, h: int = 16) -> Image.Image:
    """Decode same way GUI_Paint reads glyphs."""
    img = Image.new("1", (w, h), 1)
    ptr = 0
    for j in range(h):
        for i in range(w):
            if data[ptr] & (0x80 >> (i % 8)):
                img.putpixel((i, j), 0)
            if i % 8 == 7:
                ptr += 1
    return img


def parse_font12cn(path: Path) -> dict[str, bytes]:
    text = path.read_text(encoding="utf-8")
    pattern = re.compile(
        r'\{"([^"]{1,3})",\s*((?:0x[0-9A-Fa-f]{2},\s*)+0x[0-9A-Fa-f]{2})\}',
        re.MULTILINE,
    )
    glyphs: dict[str, bytes] = {}
    for ch, hex_blob in pattern.findall(text):
        nums = [int(x.strip(), 16) for x in hex_blob.split(",") if x.strip()]
        glyphs[ch] = bytes(nums)
    return glyphs


def main() -> None:
    glyphs = parse_font12cn(FONT12CN)
    print(f"Parsed {len(glyphs)} glyphs from {FONT12CN.name}")

    bad: list[str] = []
    for ch, stored in sorted(glyphs.items(), key=lambda x: x[0]):
        expected = img_to_bytes(render_char(ch))
        if stored != expected:
            bad.append(ch)
            # Save diff previews for first few failures
            if len(bad) <= 5:
                out = Path(__file__).parent / "_verify_out"
                out.mkdir(exist_ok=True)
                bytes_to_img(stored).save(out / f"{ord(ch):04x}_stored.png")
                bytes_to_img(expected).save(out / f"{ord(ch):04x}_expected.png")

    legacy = set("电子相册首先连接然后输入密码")

    if bad:
        print(f"MISMATCH: {len(bad)} glyph(s) vs SimSun 16pt:")
        for ch in bad:
            tag = "legacy" if ch in legacy else "generated"
            print(f"  - {ch} (U+{ord(ch):04X}) [{tag}]")
    else:
        print("OK: all glyphs match SimSun 16pt (row-major, MSB first, 32 bytes each)")

    generated = [ch for ch in glyphs if ch not in legacy]
    gen_bad = [ch for ch in bad if ch not in legacy]
    print(f"Generated batch: {len(generated) - len(gen_bad)}/{len(generated)} match SimSun")
    if gen_bad:
        raise SystemExit(1)
    if bad and not gen_bad:
        print("Note: only legacy glyphs differ; they still render, style may differ slightly.")

    # Spot-check round-trip through Paint decode
    for ch in ("电", "钙", "设", "备", "手", "置"):
        if ch not in glyphs:
            continue
        a = render_char(ch)
        b = bytes_to_img(glyphs[ch])
        if list(a.getdata()) != list(b.getdata()):
            print(f"WARN: round-trip pixel diff for {ch}")
        else:
            print(f"  round-trip OK: {ch}")


if __name__ == "__main__":
    main()
