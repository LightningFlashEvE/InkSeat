#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
模板渲染引擎 - 将模板配置渲染为 EPD 六色图片数据

支持模板：
- weather:  天气（依赖 Open-Meteo API）
- quote:    每日一言（依赖 yiyan.codeever.cn）
- qrcode:   二维码（本地生成）
- todo:     待办事项（占位）
- calendar: 日历（本地日期）
- nameplate: 铭牌姓名
"""

from __future__ import annotations

import io
import os
import json
import base64
import math
import urllib.parse
from datetime import datetime, timedelta, timezone
from functools import lru_cache
from pathlib import Path
from typing import Optional, Dict, Any
from zoneinfo import ZoneInfo

import requests

from PIL import Image, ImageDraw, ImageFont
import numpy as np

from six_color_epd import process_e6_image, E6_IDX2RGB

TEMPLATE_TIMEZONE = os.environ.get('TEMPLATE_DAY_TIMEZONE', 'Asia/Shanghai')
try:
    TEMPLATE_TZ = ZoneInfo(TEMPLATE_TIMEZONE)
except Exception:
    TEMPLATE_TZ = timezone(timedelta(hours=8))

NAMEPLATE_ASSET_DIR = Path(__file__).resolve().parent / 'assets' / 'nameplate'
NAMEPLATE_COMPANY_CN = '现象创新（深圳）科技有限公司'
NAMEPLATE_COMPANY_EN = 'Pheno Innovations Technology Co., Ltd.'
_NAMEPLATE_ASSET_CACHE: dict[str, Image.Image] = {}
NAMEPLATE_LOGO_MAX_BYTES = 512 * 1024
NAMEPLATE_LOGO_MAX_DIMENSION = 4096
NAMEPLATE_LOGO_MAX_PIXELS = 4096 * 4096
NAMEPLATE_LOGO_SCALE_MIN = 0.5
NAMEPLATE_LOGO_SCALE_MAX = 2.0
NAMEPLATE_LOGO_MIME_FORMATS = {
    'image/png': 'PNG',
    'image/jpeg': 'JPEG',
    'image/webp': 'WEBP',
}
NAMEPLATE_E6_ALGORITHM = 'nearest_color'
NAMEPLATE_NAME_WITH_ROLE_MAX_SIZE = 132
NAMEPLATE_PROFILE_NAME_MAX_SIZE = 108
NAMEPLATE_ROLE_CENTER_Y = 288
NAMEPLATE_PROFILE_ROLE_TOP = 270
NAMEPLATE_COMPANY_FONT_MAX_SIZE = 26
NAMEPLATE_COMPANY_FONT_MIN_SIZE = 18
NAMEPLATE_FOOTER_LOGO_DEFAULT_X = 24


def _local_now() -> datetime:
    return datetime.now(TEMPLATE_TZ)


# ==================== 字体配置 ====================
# 优先使用系统中文字体，保证中文显示
def _get_font(size: int) -> ImageFont.FreeTypeFont:
    """获取支持中文的字体"""
    candidates = [
        "C:/Windows/Fonts/simhei.ttf",     # 黑体
        "C:/Windows/Fonts/simsun.ttc",     # 宋体
        "C:/Windows/Fonts/msyh.ttc",       # 微软雅黑
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansSymbols-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/System/Library/Fonts/PingFang.ttc",
    ]
    for path in candidates:
        try:
            return ImageFont.truetype(path, size)
        except Exception:
            continue
    return ImageFont.load_default()


def _get_font_bold(size: int) -> ImageFont.FreeTypeFont:
    """获取粗体字体"""
    candidates = [
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/msyhbd.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/opentype/noto/NotoSerifCJK-Bold.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
    ]
    for path in candidates:
        try:
            return ImageFont.truetype(path, size)
        except Exception:
            continue
    return _get_font(size)


def _contains_cjk(text: str) -> bool:
    return any('\u4e00' <= ch <= '\u9fff' for ch in str(text or ''))


def _get_latin_font(size: int, bold: bool = True) -> ImageFont.FreeTypeFont:
    candidates = [
        "C:/Windows/Fonts/arialbd.ttf" if bold else "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeuib.ttf" if bold else "C:/Windows/Fonts/segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    ]
    for path in candidates:
        try:
            return ImageFont.truetype(path, size)
        except Exception:
            continue
    return _get_font_bold(size) if bold else _get_font(size)


def _get_nameplate_font(text: str, size: int, bold: bool = True) -> ImageFont.FreeTypeFont:
    if _contains_cjk(text):
        return _get_font_bold(size) if bold else _get_font(size)
    return _get_latin_font(size, bold)


# ==================== EPD 数据编码 ====================
def _encode_epd_string(color_indices: np.ndarray) -> str:
    """将颜色索引数组编码为 a~p 字符串（与前端一致：低4位在前，高4位在后）"""
    height, width = color_indices.shape
    packed_width = (width + 1) // 2
    chars = []
    for y in range(height):
        for x in range(0, width, 2):
            idx1 = int(color_indices[y, x]) & 0xF
            idx2 = (int(color_indices[y, x + 1]) & 0xF) if x + 1 < width else 0x1
            packed = (idx1 << 4) | idx2
            # 与前端一致：低nibble先，高nibble后
            chars.append(chr(97 + (packed & 0x0F)))
            chars.append(chr(97 + ((packed >> 4) & 0x0F)))
    return ''.join(chars)


def _pil_to_epd_string(img: Image.Image, algorithm: str = 'floyd_steinberg') -> str:
    """PIL Image -> EPD a~p 编码字符串"""
    result = process_e6_image(img, target_size=(800, 480), algorithm=algorithm)
    return _encode_epd_string(result['color_indices'])


# ==================== 通用绘制工具 ====================
def _create_base_canvas(bg_color: tuple = (255, 255, 255)) -> tuple[Image.Image, ImageDraw.ImageDraw]:
    """创建 800x480 白色画布"""
    img = Image.new('RGB', (800, 480), bg_color)
    draw = ImageDraw.Draw(img)
    return img, draw


def _draw_text_centered(draw: ImageDraw.ImageDraw, text: str, x: int, y: int, font: ImageFont.FreeTypeFont,
                        fill: tuple = (0, 0, 0), anchor: str = "mm") -> None:
    """居中绘制文字（使用 anchor 模式，Pillow >= 8.0）"""
    try:
        draw.text((x, y), text, font=font, fill=fill, anchor=anchor)
    except Exception:
        # 旧版 Pillow 不支持 anchor
        bbox = draw.textbbox((0, 0), text, font=font)
        tw = bbox[2] - bbox[0]
        th = bbox[3] - bbox[1]
        draw.text((x - tw // 2, y - th // 2), text, font=font, fill=fill)


def _text_width(draw: ImageDraw.ImageDraw, text: str, font: ImageFont.FreeTypeFont) -> int:
    bbox = draw.textbbox((0, 0), text, font=font)
    return bbox[2] - bbox[0]


def _wrap_text(draw: ImageDraw.ImageDraw, text: str, font: ImageFont.FreeTypeFont, max_width: int) -> list[str]:
    lines = []
    current_line = ''
    for char in text:
        test_line = current_line + char
        if _text_width(draw, test_line, font) > max_width and current_line:
            lines.append(current_line)
            current_line = char
        else:
            current_line = test_line
    if current_line:
        lines.append(current_line)
    return lines


def _fit_wrapped_text(draw: ImageDraw.ImageDraw, text: str, max_width: int, max_height: int,
                      start_size: int, min_size: int) -> tuple[ImageFont.FreeTypeFont, list[str], int]:
    for size in range(start_size, min_size - 1, -2):
        font = _get_font_bold(size)
        lines = _wrap_text(draw, text, font, max_width)
        line_height = max(size + 12, int(size * 1.35))
        if len(lines) * line_height <= max_height:
            return font, lines, line_height

    font = _get_font_bold(min_size)
    return font, _wrap_text(draw, text, font, max_width), max(min_size + 12, int(min_size * 1.35))


def _fit_single_line_font(draw: ImageDraw.ImageDraw, text: str, max_width: int,
                          start_size: int, min_size: int,
                          bold: bool = True) -> ImageFont.FreeTypeFont:
    for size in range(start_size, min_size - 1, -2):
        font = _get_nameplate_font(text, size, bold)
        if _text_width(draw, text, font) <= max_width:
            return font
    return _get_nameplate_font(text, min_size, bold)


def _load_nameplate_asset(filename: str) -> Optional[Image.Image]:
    cached = _NAMEPLATE_ASSET_CACHE.get(filename)
    if cached is not None:
        return cached.copy()

    path = NAMEPLATE_ASSET_DIR / filename
    try:
        asset = Image.open(path).convert('RGBA')
        _NAMEPLATE_ASSET_CACHE[filename] = asset
        return asset.copy()
    except Exception as e:
        print(f'⚠️ 名牌资产加载失败: {path} -> {e}')
        return None


def _paste_nameplate_asset(img: Image.Image, filename: str, x: int, y: int, width: int, height: int) -> None:
    asset = _load_nameplate_asset(filename)
    if asset is None:
        return
    asset = asset.resize((width, height), Image.LANCZOS)
    img.paste(asset, (x, y), asset)


@lru_cache(maxsize=16)
def _load_custom_nameplate_logo(data_url: str) -> Optional[Image.Image]:
    if not isinstance(data_url, str) or not data_url.startswith('data:image/'):
        return None

    header, separator, payload = data_url.partition(',')
    if not separator or not header.endswith(';base64'):
        return None
    mime_type = header[5:-7].lower()
    expected_format = NAMEPLATE_LOGO_MIME_FORMATS.get(mime_type)
    if not expected_format:
        return None

    try:
        raw = base64.b64decode(payload, validate=True)
        if not raw or len(raw) > NAMEPLATE_LOGO_MAX_BYTES:
            return None
        with Image.open(io.BytesIO(raw)) as logo:
            if (logo.format or '').upper() != expected_format:
                return None
            width, height = logo.size
            if (
                width <= 0 or height <= 0
                or width > NAMEPLATE_LOGO_MAX_DIMENSION
                or height > NAMEPLATE_LOGO_MAX_DIMENSION
                or width * height > NAMEPLATE_LOGO_MAX_PIXELS
            ):
                return None
            logo.load()
            return logo.convert('RGBA')
    except Exception:
        return None


def _contain_nameplate_asset(asset: Image.Image, width: int, height: int) -> Image.Image:
    scale = min(width / asset.width, height / asset.height)
    target_width = max(1, round(asset.width * scale))
    target_height = max(1, round(asset.height * scale))
    return asset.resize((target_width, target_height), Image.LANCZOS)


def _paste_configured_nameplate_logo(img: Image.Image, config: Dict[str, Any],
                                     fallback_filename: str, default_x: int, default_y: int,
                                     width: int, height: int) -> None:
    if config.get('logoHidden') is True:
        return

    custom_logo = _load_custom_nameplate_logo(str(config.get('logoDataUrl') or ''))
    asset = custom_logo.copy() if custom_logo is not None else _load_nameplate_asset(fallback_filename)
    if asset is None:
        return

    raw_scale = config.get('logoScale')
    scale = float(raw_scale) if (
        isinstance(raw_scale, (int, float)) and not isinstance(raw_scale, bool)
        and math.isfinite(raw_scale)
    ) else 1.0
    scale = min(max(scale, NAMEPLATE_LOGO_SCALE_MIN), NAMEPLATE_LOGO_SCALE_MAX)
    width = max(1, round(width * scale))
    height = max(1, round(height * scale))

    x = config.get('logoX')
    y = config.get('logoY')
    x = int(round(x)) if isinstance(x, (int, float)) and not isinstance(x, bool) else default_x
    y = int(round(y)) if isinstance(y, (int, float)) and not isinstance(y, bool) else default_y
    x = min(max(x, 0), 800 - width)
    y = min(max(y, 0), 480 - height)

    if custom_logo is not None:
        asset = _contain_nameplate_asset(asset, width, height)
        paste_x = x + (width - asset.width) // 2
        paste_y = y + (height - asset.height) // 2
    else:
        asset = asset.resize((width, height), Image.LANCZOS)
        paste_x = x
        paste_y = y
    img.paste(asset, (paste_x, paste_y), asset)


# ==================== 天气模板 ====================
QWEATHER_KEY = os.environ.get('QWEATHER_KEY', '').strip()
if not QWEATHER_KEY:
    print('⚠️ 警告: 环境变量 QWEATHER_KEY 未设置，天气模板将无法获取数据。请在 .env 中配置。')

WEATHER_REQUEST_TIMEOUT = 3
WEATHER_REQUEST_RETRIES = 2


def _fetch_weather(city: str) -> Optional[Dict[str, Any]]:
    """通过和风天气(QWeather)获取天气数据（带重试，使用 requests）"""
    import time
    headers = {'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)'}

    for attempt in range(WEATHER_REQUEST_RETRIES):
        try:
            # 1. 城市搜索：城市名 -> 城市ID
            geo_url = f"https://geoapi.qweather.com/v2/city/lookup?location={urllib.parse.quote(city)}&key={QWEATHER_KEY}"
            resp = requests.get(geo_url, headers=headers, timeout=WEATHER_REQUEST_TIMEOUT)
            resp.raise_for_status()
            geo_data = resp.json()

            if geo_data.get('code') != '200':
                print(f'⚠️ 城市搜索失败: {geo_data.get("code")}')
                return None

            locations = geo_data.get('location', [])
            if not locations:
                return None

            city_id = locations[0]['id']
            city_name = locations[0].get('name', city)

            # 2. 实时天气
            now_url = f"https://devapi.qweather.com/v7/weather/now?location={city_id}&key={QWEATHER_KEY}"
            resp = requests.get(now_url, headers=headers, timeout=WEATHER_REQUEST_TIMEOUT)
            resp.raise_for_status()
            now_data = resp.json()

            if now_data.get('code') != '200':
                print(f'⚠️ 实时天气失败: {now_data.get("code")}')
                return None

            now = now_data.get('now', {})

            # 3. 3天预报（获取今日最高/最低温）
            forecast_url = f"https://devapi.qweather.com/v7/weather/3d?location={city_id}&key={QWEATHER_KEY}"
            resp = requests.get(forecast_url, headers=headers, timeout=WEATHER_REQUEST_TIMEOUT)
            resp.raise_for_status()
            forecast_data = resp.json()

            daily = forecast_data.get('daily', [])
            today = daily[0] if daily else {}

            return {
                'city': city_name,
                'temperature': now.get('temp'),
                'weather_code': now.get('icon'),
                'weatherText': now.get('text'),
                'wind_speed': now.get('windScale'),
                'humidity': now.get('humidity'),
                'temp_max': today.get('tempMax'),
                'temp_min': today.get('tempMin'),
            }
        except Exception as e:
            print(f'⚠️ 获取天气失败 (attempt {attempt + 1}/{WEATHER_REQUEST_RETRIES}): {e}')
            if attempt < WEATHER_REQUEST_RETRIES - 1:
                time.sleep(0.3)
    return None


def _weather_code_to_text(code: Optional[str]) -> str:
    """和风天气直接返回中文天气文本，无需额外映射"""
    return code or '未知'


def render_weather(config: Dict[str, Any]) -> str:
    """渲染天气模板，返回 EPD a~p 字符串"""
    img = _render_weather_image(config)
    return _pil_to_epd_string(img)


# ==================== 每日一言模板 ====================
QUOTE_REQUEST_TIMEOUT = 3
QUOTE_REQUEST_RETRIES = 2


def _fetch_quote() -> Optional[Dict[str, str]]:
    """从 yiyan.codeever.cn 获取一言（带重试，使用 requests）"""
    import time
    url = 'https://yiyan.codeever.cn/api'
    headers = {'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)'}
    for attempt in range(QUOTE_REQUEST_RETRIES):
        try:
            resp = requests.get(url, headers=headers, timeout=QUOTE_REQUEST_TIMEOUT)
            resp.raise_for_status()
            data = resp.json()
            # 接口返回格式: {"code": 200, "data": {"content": "...", "from": "..."}}
            inner = data.get('data', {})
            return {
                'content': inner.get('content', inner.get('hitokoto', '')),
                'origin': inner.get('origin', inner.get('from', '')),
                'author': inner.get('author', ''),
            }
        except Exception as e:
            print(f'⚠️ 获取一言失败 (attempt {attempt + 1}/{QUOTE_REQUEST_RETRIES}): {e}')
            if attempt < QUOTE_REQUEST_RETRIES - 1:
                time.sleep(0.3)
    return None


def render_quote(config: Dict[str, Any]) -> str:
    """渲染每日一言模板，返回 EPD a~p 字符串"""
    img = _render_quote_image(config)
    return _pil_to_epd_string(img)


# ==================== 二维码模板 ====================
def render_qrcode(config: Dict[str, Any]) -> str:
    """渲染二维码模板，返回 EPD a~p 字符串"""
    img = _render_qrcode_image(config)
    return _pil_to_epd_string(img)


# ==================== 日历模板 ====================
def render_calendar(config: Dict[str, Any]) -> str:
    """渲染日历模板，返回 EPD a~p 字符串"""
    img = _render_calendar_image(config)
    return _pil_to_epd_string(img)


# ==================== 代办事项模板（占位） ====================
def render_todo(config: Dict[str, Any]) -> str:
    """渲染代办事项模板（占位），返回 EPD a~p 字符串"""
    img = _render_todo_image(config)
    return _pil_to_epd_string(img)


# ==================== 铭牌模板 ====================
def render_nameplate(config: Dict[str, Any]) -> str:
    """渲染铭牌模板，返回 EPD a~p 字符串"""
    img = _render_nameplate_image(config)
    return _pil_to_epd_string(img, algorithm=NAMEPLATE_E6_ALGORITHM)


# ==================== 模板渲染（返回 PIL Image） ====================

def _render_weather_image(config: Dict[str, Any]) -> Image.Image:
    """渲染天气模板，返回 PIL Image"""
    city = config.get('city', '')
    if not city:
        city = '北京'

    weather = _fetch_weather(city)
    img, draw = _create_base_canvas((255, 255, 255))

    font_title = _get_font(36)
    font_temp = _get_font_bold(120)
    font_info = _get_font(32)
    font_small = _get_font(24)

    if weather:
        date_str = _local_now().strftime('%m月%d日')
        _draw_text_centered(draw, f"{weather['city']}  {date_str}", 400, 50, font_title, (0, 0, 0))

        temp = weather.get('temperature')
        if temp is not None:
            _draw_text_centered(draw, f"{int(temp)}°C", 400, 200, font_temp, (0, 0, 255))

        weather_text = weather.get('weatherText') or weather.get('weather_code') or '未知'
        _draw_text_centered(draw, weather_text, 400, 310, font_info, (255, 0, 0))

        info_parts = []
        if weather.get('humidity') is not None:
            info_parts.append(f"湿度 {weather['humidity']}%")
        if weather.get('wind_speed') is not None:
            info_parts.append(f"风速 {weather['wind_speed']}km/h")
        if weather.get('temp_max') is not None and weather.get('temp_min') is not None:
            info_parts.append(f"{int(weather['temp_min'])}°C~{int(weather['temp_max'])}°C")

        if info_parts:
            info_text = '  |  '.join(info_parts)
            _draw_text_centered(draw, info_text, 400, 400, font_small, (0, 0, 0))
    else:
        _draw_text_centered(draw, f"{city}", 400, 150, font_title, (0, 0, 0))
        _draw_text_centered(draw, "天气数据获取失败", 400, 260, font_info, (255, 0, 0))
        _draw_text_centered(draw, "请检查城市名称", 400, 320, font_small, (0, 0, 0))

    return img


def _render_quote_image(config: Dict[str, Any]) -> Image.Image:
    """渲染每日一言模板，返回 PIL Image"""
    quote = _fetch_quote()
    img, draw = _create_base_canvas((255, 255, 255))

    font_content = _get_font_bold(44)
    font_source = _get_font(30)

    if quote and quote.get('content'):
        content = quote['content']
        font_content, lines, line_height = _fit_wrapped_text(
            draw,
            content,
            max_width=720,
            max_height=300,
            start_size=44,
            min_size=32,
        )
        total_height = len(lines) * line_height
        start_y = max(86, (480 - total_height) // 2 - 18)

        for i, line in enumerate(lines):
            _draw_text_centered(draw, line, 400, start_y + i * line_height, font_content, (0, 0, 0))

        source_parts = []
        if quote.get('author'):
            source_parts.append(quote['author'])
        if quote.get('origin'):
            source_parts.append(f"《{quote['origin']}》")
        if source_parts:
            source_text = '  '.join(source_parts)
            _draw_text_centered(draw, source_text, 400, 420, font_source, (0, 0, 0))
    else:
        fallback_quotes = [
            "千里之行，始于足下。 —— 老子",
            "学而不思则罔，思而不学则殆。 —— 孔子",
            "天行健，君子以自强不息。 —— 周易",
            "不积跬步，无以至千里。 —— 荀子",
            "知者不惑，仁者不忧，勇者不惧。 —— 论语",
        ]
        import random
        text = random.choice(fallback_quotes)
        _draw_text_centered(draw, text, 400, 240, font_content, (0, 0, 0))

    return img


def _render_qrcode_image(config: Dict[str, Any]) -> Image.Image:
    """渲染二维码模板，返回 PIL Image"""
    content = config.get('content', '')
    title = config.get('title', '')

    img, draw = _create_base_canvas((255, 255, 255))

    if content:
        try:
            import qrcode
            qr = qrcode.QRCode(
                version=None,
                error_correction=qrcode.constants.ERROR_CORRECT_M,
                box_size=10,
                border=2,
            )
            qr.add_data(content)
            qr.make(fit=True)
            qr_img = qr.make_image(fill_color="black", back_color="white").convert('RGB')

            qr_size = 320
            qr_img = qr_img.resize((qr_size, qr_size), Image.LANCZOS)
            x = (800 - qr_size) // 2
            y = (480 - qr_size) // 2 - 30
            img.paste(qr_img, (x, y))
        except Exception as e:
            print(f'⚠️ 生成二维码失败: {e}')
            _draw_text_centered(draw, "二维码生成失败", 400, 240, _get_font(32), (255, 0, 0))
    else:
        _draw_text_centered(draw, "请在设置中配置二维码内容", 400, 240, _get_font(32), (0, 0, 0))

    if title:
        font_title = _get_font(36)
        _draw_text_centered(draw, title, 400, 430, font_title, (0, 0, 0))

    return img


def _render_calendar_image(config: Dict[str, Any]) -> Image.Image:
    """渲染日历模板，返回 PIL Image"""
    img, draw = _create_base_canvas((255, 255, 255))

    now = _local_now()
    year = now.year
    month = now.month
    day = now.day
    weekday = now.weekday()
    weekdays = ['周一', '周二', '周三', '周四', '周五', '周六', '周日']

    font_year = _get_font(36)
    font_month = _get_font_bold(80)
    font_day = _get_font_bold(200)
    font_weekday = _get_font(48)

    draw.text((60, 40), f"{year}年", font=font_year, fill=(0, 0, 0))
    draw.text((60, 100), f"{month}月", font=font_month, fill=(0, 0, 0))

    _draw_text_centered(draw, str(day), 500, 220, font_day, (0, 0, 255))
    _draw_text_centered(draw, weekdays[weekday], 500, 380, font_weekday, (255, 0, 0))

    return img


def _render_todo_image(config: Dict[str, Any]) -> Image.Image:
    """渲染代办事项模板（占位），返回 PIL Image"""
    img, draw = _create_base_canvas((255, 255, 255))

    font_title = _get_font_bold(48)
    font_hint = _get_font(32)

    _draw_text_centered(draw, "待办事项", 400, 180, font_title, (0, 0, 0))
    _draw_text_centered(draw, "功能开发中...", 400, 280, font_hint, (0, 0, 0))

    return img


def _draw_left_text(draw: ImageDraw.ImageDraw, xy: tuple[int, int], text: str,
                    font: ImageFont.FreeTypeFont, fill: tuple = (0, 0, 0),
                    anchor: str = 'lm') -> None:
    try:
        draw.text(xy, text, font=font, fill=fill, anchor=anchor)
    except Exception:
        bbox = draw.textbbox((0, 0), text, font=font)
        if anchor == 'lm':
            y = xy[1] - (bbox[3] - bbox[1]) // 2
        else:
            y = xy[1]
        draw.text((xy[0], y), text, font=font, fill=fill)


def _resolve_nameplate_company_x(config: Dict[str, Any], company_width: int,
                                 reference_width: Optional[int] = None) -> int:
    is_custom_position = str(
        config.get('companyPositionMode') or ''
    ).strip().lower() == 'custom'
    raw_x = config.get('companyX') if is_custom_position else None
    if (
        isinstance(raw_x, (int, float)) and not isinstance(raw_x, bool)
        and math.isfinite(raw_x)
    ):
        x = int(round(raw_x))
        if reference_width is not None:
            x += int(round((reference_width - company_width) / 2))
    else:
        x = int(round((800 - company_width) / 2))
    return min(max(x, 0), max(0, 800 - company_width))


def _draw_pheno_footer_nameplate(img: Image.Image, draw: ImageDraw.ImageDraw, name: str,
                                 style: str, role_text: str, company_text: str,
                                 config: Dict[str, Any]) -> None:
    accent = (0, 255, 0) if style == 'formal_green' else (255, 0, 0)
    footer_top = 385
    has_role = bool(role_text)
    draw.rectangle((0, 0, 799, footer_top - 1), fill=accent)
    draw.rectangle((0, footer_top, 799, 479), fill=(255, 255, 255))

    font_name = _fit_single_line_font(
        draw,
        name,
        590,
        NAMEPLATE_NAME_WITH_ROLE_MAX_SIZE if has_role else 148,
        62 if has_role else 72,
    )
    _draw_text_centered(draw, name, 400, 155 if has_role else 184, font_name, (255, 255, 255))

    if has_role:
        font_role = _fit_single_line_font(draw, role_text, 590, 48, 28, bold=False)
        _draw_text_centered(
            draw, role_text, 400, NAMEPLATE_ROLE_CENTER_Y, font_role, (255, 255, 255)
        )

    _paste_configured_nameplate_logo(
        img, config, 'pheno-logo-black.png',
        NAMEPLATE_FOOTER_LOGO_DEFAULT_X, 410, 181, 39
    )

    font_company = _fit_single_line_font(
        draw,
        company_text,
        390,
        NAMEPLATE_COMPANY_FONT_MAX_SIZE,
        NAMEPLATE_COMPANY_FONT_MIN_SIZE,
    )
    company_width = _text_width(draw, company_text, font_company)
    reference_width = None
    reference_text = str(config.get('companyReferenceText') or '').strip()
    if reference_text and reference_text != company_text:
        reference_font = _fit_single_line_font(
            draw,
            reference_text,
            390,
            NAMEPLATE_COMPANY_FONT_MAX_SIZE,
            NAMEPLATE_COMPANY_FONT_MIN_SIZE,
        )
        reference_width = _text_width(draw, reference_text, reference_font)
    company_x = _resolve_nameplate_company_x(
        config, company_width, reference_width
    )
    _draw_left_text(
        draw, (company_x, 430), company_text, font_company, (0, 0, 0), anchor='lm'
    )


def _draw_pheno_green_band_nameplate(img: Image.Image, draw: ImageDraw.ImageDraw, name: str,
                                     role_text: str, config: Dict[str, Any]) -> None:
    band_top = 361
    has_role = bool(role_text)
    draw.rectangle((0, 0, 799, band_top - 1), fill=(255, 255, 255))
    draw.rectangle((0, band_top, 799, 479), fill=(0, 255, 0))

    font_name = _fit_single_line_font(
        draw,
        name,
        590,
        NAMEPLATE_NAME_WITH_ROLE_MAX_SIZE if has_role else 150,
        62 if has_role else 72,
    )
    _draw_text_centered(draw, name, 400, 155 if has_role else 184, font_name, (0, 0, 0))

    if has_role:
        font_role = _fit_single_line_font(draw, role_text, 590, 48, 28, bold=False)
        _draw_text_centered(
            draw, role_text, 400, NAMEPLATE_ROLE_CENTER_Y, font_role, (0, 0, 0)
        )

    _paste_configured_nameplate_logo(
        img, config, 'pheno-logo-white.png', 276, 390, 248, 54
    )


def _draw_pheno_profile_nameplate(img: Image.Image, draw: ImageDraw.ImageDraw, name: str,
                                  role_text: str, company_text: str,
                                  config: Dict[str, Any]) -> None:
    draw.rectangle((0, 0, 799, 479), fill=(255, 255, 255))

    mark_size = 128
    gap = 26
    min_margin = 80
    max_text_width = 800 - min_margin * 2 - mark_size - gap

    font_name = _fit_single_line_font(
        draw, name, max_text_width, NAMEPLATE_PROFILE_NAME_MAX_SIZE, 58
    )
    name_width = _text_width(draw, name, font_name)

    role_width = 0
    font_role = None
    if role_text:
        font_role = _fit_single_line_font(draw, role_text, max_text_width, 40, 24, bold=False)
        role_width = _text_width(draw, role_text, font_role)

    text_width = max(name_width, role_width)
    group_width = mark_size + gap + text_width
    group_left = max(min_margin, round((800 - group_width) / 2))
    mark_top = 153
    text_left = group_left + mark_size + gap

    _paste_configured_nameplate_logo(
        img, config, 'pheno-mark-square.png', group_left, mark_top, mark_size, mark_size
    )

    _draw_left_text(draw, (text_left, 151), name, font_name, (0, 0, 0), anchor='lt')
    if role_text and font_role:
        _draw_left_text(
            draw,
            (text_left, NAMEPLATE_PROFILE_ROLE_TOP),
            role_text,
            font_role,
            (0, 0, 0),
            anchor='lt',
        )

    font_company = _fit_single_line_font(
        draw,
        company_text,
        370,
        NAMEPLATE_COMPANY_FONT_MAX_SIZE,
        NAMEPLATE_COMPANY_FONT_MIN_SIZE,
        bold=False,
    )
    company_width = _text_width(draw, company_text, font_company)
    reference_width = None
    reference_text = str(config.get('companyReferenceText') or '').strip()
    if reference_text and reference_text != company_text:
        reference_font = _fit_single_line_font(
            draw,
            reference_text,
            370,
            NAMEPLATE_COMPANY_FONT_MAX_SIZE,
            NAMEPLATE_COMPANY_FONT_MIN_SIZE,
            bold=False,
        )
        reference_width = _text_width(draw, reference_text, reference_font)
    text_x = _resolve_nameplate_company_x(
        config, company_width, reference_width
    )
    line_gap = 20
    line_y = 406
    line_height = 16

    left_line_right = max(0, text_x - line_gap)
    right_line_left = min(800, text_x + company_width + line_gap)
    if left_line_right > 0:
        draw.rectangle((0, line_y, left_line_right, line_y + line_height - 1), fill=(0, 0, 0))
    if right_line_left < 800:
        draw.rectangle((right_line_left, line_y, 799, line_y + line_height - 1), fill=(0, 0, 0))

    _draw_left_text(draw, (text_x, 402), company_text, font_company, (0, 0, 0), anchor='lt')


def _render_nameplate_image(config: Dict[str, Any]) -> Image.Image:
    """渲染 Pheno 品牌姓名牌。"""
    name = str(config.get('name') or config.get('personName') or '').strip()
    title = str(config.get('title') or config.get('organization') or '').strip()
    subtitle = str(config.get('subtitle') or config.get('note') or '').strip()
    style = str(config.get('backgroundStyle') or 'formal_red').strip().lower()

    if not name:
        name = '姓名'

    img, draw = _create_base_canvas((255, 255, 255))
    if style == 'formal_blue':
        _draw_pheno_profile_nameplate(
            img, draw, name, title, subtitle or NAMEPLATE_COMPANY_EN, config
        )
    elif style == 'plain':
        _draw_pheno_green_band_nameplate(img, draw, name, title, config)
    else:
        _draw_pheno_footer_nameplate(
            img, draw, name, style, title, subtitle or NAMEPLATE_COMPANY_CN, config
        )

    return img


def _safe_template_log_config(config: Dict[str, Any]) -> Dict[str, Any]:
    log_config = dict(config or {})
    if log_config.get('logoDataUrl'):
        log_config['logoDataUrl'] = f'<custom logo: {len(log_config["logoDataUrl"])} chars>'
    return log_config


# 渲染器映射（返回 PIL Image 的版本）
TEMPLATE_IMAGE_RENDERERS = {
    'weather': _render_weather_image,
    'quote': _render_quote_image,
    'qrcode': _render_qrcode_image,
    'calendar': _render_calendar_image,
    'todo': _render_todo_image,
    'nameplate': _render_nameplate_image,
}


def render_template_image(template_id: str, config: Dict[str, Any]) -> Optional[Image.Image]:
    """
    渲染指定模板，返回 PIL Image 对象（800x480 RGB）

    参数:
        template_id: 模板ID
        config: 模板配置数据

    返回:
        PIL.Image (800x480 RGB)，失败返回 None
    """
    template_id = str(template_id).strip().lower() if template_id else ''
    renderer = TEMPLATE_IMAGE_RENDERERS.get(template_id)
    if not renderer:
        print(f'❌ 未知模板: {template_id}')
        return None

    try:
        print(f'🎨 渲染模板图像: {template_id}, config={_safe_template_log_config(config)}')
        img = renderer(config)
        if img:
            # 确保尺寸正确
            if img.size != (800, 480):
                img = img.resize((800, 480), Image.LANCZOS)
            print(f'✅ 模板图像渲染成功: {template_id}, size={img.size}')
            return img
        else:
            print(f'❌ 模板图像渲染返回空: {template_id}')
            return None
    except Exception as e:
        print(f'❌ 模板图像渲染异常: {template_id} -> {e}')
        import traceback
        traceback.print_exc()
        return None


# ==================== 主渲染入口（向后兼容） ====================
TEMPLATE_RENDERERS = {
    'weather': render_weather,
    'quote': render_quote,
    'qrcode': render_qrcode,
    'calendar': render_calendar,
    'todo': render_todo,
    'nameplate': render_nameplate,
}


def render_template_with_preview(template_id: str, config: Dict[str, Any]) -> dict:
    """
    渲染指定模板，一次性返回：原始图 + 抖动预览图 + 4bit数据 + EPD编码字符串

    用于前端发布时，确保 mainCanvas 显示后端渲染的原始排版，
    processedCanvas 显示 Floyd-Steinberg 抖动后的效果。

    返回:
        {
            'originalImage': str (Base64 PNG, 原始渲染图),
            'previewImage': str (Base64 PNG, 抖动后预览图),
            'data4bit': str (Base64, 4bit打包数据),
            'epdData': str (a~p 编码字符串, 384000字符)
        }
    """
    template_id = str(template_id).strip().lower() if template_id else ''

    try:
        print(f'🎨 渲染模板完整链路: {template_id}, config={_safe_template_log_config(config)}')
        # 1. 渲染原始 PIL Image（只渲染一次）
        img = render_template_image(template_id, config)
        if not img:
            return {}

        # 2. 原始图 → Base64（用于前端 mainCanvas 显示排版）
        orig_buffer = io.BytesIO()
        img.save(orig_buffer, format='PNG')
        original_b64 = base64.b64encode(orig_buffer.getvalue()).decode('utf-8')

        # 3. 名牌等扁平图文直接映射六色，照片型模板保留误差扩散抖动。
        algorithm = (
            NAMEPLATE_E6_ALGORITHM if template_id == 'nameplate'
            else 'floyd_steinberg'
        )
        result = process_e6_image(img, target_size=(800, 480), algorithm=algorithm)

        # 抖动后预览图 → Base64
        preview_buffer = io.BytesIO()
        result['preview_image'].save(preview_buffer, format='PNG')
        preview_b64 = base64.b64encode(preview_buffer.getvalue()).decode('utf-8')

        # 4bit 数据 → Base64
        data_4bit_b64 = base64.b64encode(result['data_4bit']).decode('utf-8')

        # 4. 颜色索引 → EPD a~p 编码字符串（用于保存到文件供设备拉取）
        epd_data = _encode_epd_string(result['color_indices'])

        print(f'✅ 模板完整链路渲染成功: {template_id}, EPD长度={len(epd_data)}')
        return {
            'originalImage': original_b64,
            'previewImage': preview_b64,
            'data4bit': data_4bit_b64,
            'epdData': epd_data,
        }
    except Exception as e:
        print(f'❌ 模板完整链路渲染异常: {template_id} -> {e}')
        import traceback
        traceback.print_exc()
        return {}


def _render_template_with_preview(template_id: str, config: Dict[str, Any]) -> dict:
    return render_template_with_preview(template_id, config)


def render_template(template_id: str, config: Dict[str, Any]) -> Optional[str]:
    """
    渲染指定模板，返回 EPD a~p 编码字符串。
    兼容旧调用，但内部统一走 PIL 原图 -> Floyd-Steinberg -> EPD 数据链路。
    """
    result = render_template_with_preview(template_id, config)
    epd_data = result.get('epdData') if isinstance(result, dict) else None
    if epd_data and len(epd_data) == 384000:
        return epd_data

    print(f'❌ 模板渲染结果长度异常: {len(epd_data) if epd_data else 0}')
    return None
