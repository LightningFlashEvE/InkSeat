"""Generate 16x16 SimSun glyphs for font12CN.c (row-major, MSB first)."""
from PIL import Image, ImageDraw, ImageFont

FONT = r"C:\Windows\Fonts\simsun.ttc"
SIZE = 16


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


def format_c(ch: str, data: list[int]) -> str:
    hexes = [f"0x{b:02X}" for b in data]
    lines = []
    for i in range(0, len(hexes), 8):
        lines.append(",".join(hexes[i : i + 8]))
    body = ",\n".join(lines)
    return f'/*--  {ch}  --*/\n{{"{ch}",\n{body}}},'


if __name__ == "__main__":
    import sys

    text = sys.argv[1] if len(sys.argv) > 1 else "钙钛矿会议牌"
    seen = set()
    for ch in text:
        if ch in seen:
            continue
        seen.add(ch)
        print(format_c(ch, img_to_bytes(render_char(ch))))
        print()
