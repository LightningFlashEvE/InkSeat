#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
7.3寸E6六色墨水屏（黑/白/红/绿/蓝/黄）处理算法模块

主要功能：
- 支持多种图像处理算法：
  1. Floyd-Steinberg抖动算法：误差扩散，适合渐变和细节丰富的图像
  2. 梯度边界混合算法：基于Sobel梯度检测边界，在边界区域进行颜色混合，减少量化伪影
  3. 灰阶与颜色映射算法：将灰度图映射到自定义颜色梯度（黑->蓝->绿->黄->红->白）
- 转换为4bit格式（每字节两个像素）
- 生成预览图像

依赖：Pillow, numpy, opencv-python
"""

from __future__ import annotations

from typing import Tuple, Optional, Literal
import io
import base64
import os

import numpy as np
from PIL import Image
import cv2


EPD_TARGET_SIZE = (800, 480)
MAX_SOURCE_IMAGE_BYTES = int(os.environ.get('MAX_SOURCE_IMAGE_BYTES', 8 * 1024 * 1024))
MAX_SOURCE_IMAGE_PIXELS = int(os.environ.get('MAX_SOURCE_IMAGE_PIXELS', 20_000_000))
MAX_SOURCE_BASE64_CHARS = ((MAX_SOURCE_IMAGE_BYTES + 2) // 3) * 4 + 16


# ==================== E6 六色调色板 ====================
# 颜色索引：0=黑, 1=白, 2=黄, 3=红, 5=蓝, 6=绿
# 注意：索引4未使用（官方定义中橙色被注释掉了）

# RGB到驱动索引的映射（对应EPD_7in3e.h中的定义）
E6_RGB2IDX = {
    (0,   0,   0  ): 0x0,  # 黑色   -> EPD_7IN3E_BLACK
    (255, 255, 255): 0x1,  # 白色   -> EPD_7IN3E_WHITE
    (255, 255, 0  ): 0x2,  # 黄色   -> EPD_7IN3E_YELLOW
    (255, 0,   0  ): 0x3,  # 红色   -> EPD_7IN3E_RED
    (0,   0,   255): 0x5,  # 蓝色   -> EPD_7IN3E_BLUE
    (0,   255, 0  ): 0x6,  # 绿色   -> EPD_7IN3E_GREEN
}

# 驱动索引到RGB的反向映射（用于预览）
E6_IDX2RGB = {
    0x0: (0,   0,   0  ),  # 黑色
    0x1: (255, 255, 255),  # 白色
    0x2: (255, 255, 0  ),  # 黄色
    0x3: (255, 0,   0  ),  # 红色
    0x5: (0,   0,   255),  # 蓝色
    0x6: (0,   255, 0  ),  # 绿色
}

# 构建PIL调色板（需要768个值，RGB各256）
def build_pil_palette() -> list:
    """
    构建PIL调色板格式（768个值）
    只包含我们定义的6种颜色，其余位置用白色填充
    """
    palette = []
    # 添加6种颜色的RGB值
    for rgb in E6_RGB2IDX.keys():
        palette.extend(rgb)
    # 填充到256色（768个值），剩余位置用白色填充
    while len(palette) < 768:
        palette.extend([255, 255, 255])  # 白色
    # 确保正好是768个值
    palette = palette[:768]
    return palette


# ==================== E6 调色板数组（用于梯度边界混合算法）====================
# 注意：顺序必须与驱动索引对应：0=黑, 1=白, 2=黄, 3=红, 5=蓝, 6=绿
E6_PALETTE_ARRAY = np.array([
    [0,   0,   0  ],  # 0x0: 黑色
    [255, 255, 255],  # 0x1: 白色
    [255, 255, 0  ],  # 0x2: 黄色
    [255, 0,   0  ],  # 0x3: 红色
    [0,   0,   0  ],  # 0x4: 未使用（占位）
    [0,   0,   255],  # 0x5: 蓝色
    [0,   255, 0  ],  # 0x6: 绿色
], dtype=np.uint8)

# 驱动索引映射（用于从调色板索引转换为驱动索引）
PALETTE_IDX_TO_DRIVER_IDX = {
    0: 0x0,  # 黑
    1: 0x1,  # 白
    2: 0x2,  # 黄
    3: 0x3,  # 红
    4: 0x5,  # 蓝（跳过0x4）
    5: 0x6,  # 绿
}

# ==================== Floyd-Steinberg 抖动处理 ====================

def convert_to_e6_with_dither(img: Image.Image, target_size: Optional[Tuple[int, int]] = None) -> Tuple[Image.Image, np.ndarray]:
    """
    将RGB图像转换为E6六色格式，使用Floyd-Steinberg抖动
    
    参数：
        img: PIL.Image，任意模式，内部会转为RGB
        target_size: (width, height)，如果提供则先缩放到指定分辨率
    
    返回：
        (paletted_image, color_indices)
        - paletted_image: PIL.Image (P模式，调色板索引)
        - color_indices: numpy数组 [H, W]，值为0-6（颜色索引）
    """
    # 转换为RGB并调整尺寸
    rgb_img = img.convert("RGB")
    if target_size is not None:
        rgb_img = rgb_img.resize(target_size, Image.LANCZOS)
    
    width, height = rgb_img.size
    
    # 构建调色板
    palette_data = build_pil_palette()
    
    # 创建调色板图像（P模式，256色）
    # 先创建一个包含所有6种颜色的调色板图像
    palette_img = Image.new("P", (1, 1))
    palette_img.putpalette(palette_data)
    
    # 使用PIL的quantize方法，配合Floyd-Steinberg抖动
    # 这会自动将图像映射到最接近的调色板颜色，并使用误差扩散
    paletted = rgb_img.quantize(
        palette=palette_img,
        dither=Image.FLOYDSTEINBERG
    )
    
    # 获取quantize后的palette索引（0-255）
    palette_indices = np.array(paletted, dtype=np.uint8)
    
    # 获取调色板的RGB值
    pal = paletted.getpalette()  # 返回768个值的列表
    
    # 将palette索引映射到驱动颜色索引（0x0-0x6）
    # quantize返回的是palette中的索引，我们需要：
    # 1. 从palette中获取RGB值
    # 2. 通过RGB值查找对应的驱动索引
    # 使用向量化操作提高效率
    height, width = palette_indices.shape
    mapped_indices = np.zeros_like(palette_indices, dtype=np.uint8)
    
    # 创建palette索引到驱动索引的查找表（0-255 -> 0x0-0x6）
    # 使用numpy数组实现高效的向量化查找
    palette_to_driver = np.zeros(256, dtype=np.uint8)
    
    for pal_idx in range(256):
        r = pal[pal_idx * 3 + 0]
        g = pal[pal_idx * 3 + 1]
        b = pal[pal_idx * 3 + 2]
        rgb = (r, g, b)
        
        if rgb in E6_RGB2IDX:
            palette_to_driver[pal_idx] = E6_RGB2IDX[rgb]
        else:
            # 如果找不到精确匹配，找最接近的颜色
            # 使用欧氏距离找最接近的RGB
            min_dist = float('inf')
            closest_idx = 0x1  # 默认白色
            for target_rgb, target_idx in E6_RGB2IDX.items():
                dist = sum((a - b) ** 2 for a, b in zip(rgb, target_rgb))
                if dist < min_dist:
                    min_dist = dist
                    closest_idx = target_idx
            palette_to_driver[pal_idx] = closest_idx
    
    # 使用向量化查找表映射（高效）
    mapped_indices = palette_to_driver[palette_indices]
    
    return paletted, mapped_indices


# ==================== 灰阶与颜色映射算法 ====================

def build_grayscale_to_e6_index_map() -> np.ndarray:
    """
    构建灰度值直接到E6驱动索引的映射表（256级）
    
    颜色梯度按亮度顺序排列，更符合人眼对灰度的感知：
    黑 -> 蓝 -> 红 -> 绿 -> 黄 -> 白
    
    亮度计算（Y = 0.299*R + 0.587*G + 0.114*B）：
    - 黑色 (0,0,0): 0
    - 蓝色 (0,0,255): 29
    - 红色 (255,0,0): 76
    - 深红色 (128,0,0): 38 (应该在红色段，但可能被映射到蓝色)
    - 绿色 (0,255,0): 150
    - 黄色 (255,255,0): 226
    - 白色 (255,255,255): 255
    
    对应E6驱动索引：0x0 -> 0x5 -> 0x3 -> 0x6 -> 0x2 -> 0x1
    
    优化分段边界，确保深红色正确映射：
    - 0-40: 黑色（包含深红色等暗色）
    - 41-70: 蓝色（蓝色亮度29，范围41-70）
    - 71-120: 红色（红色亮度76，扩大范围包含深红色）
    - 121-180: 绿色（绿色亮度150）
    - 181-235: 黄色（扩大黄色范围，避免被映射为白色）
    - 236-255: 白色
    
    返回：
        numpy数组，256个值，每个值是对应的E6驱动索引
    """
    # E6 驱动索引（按亮度递增顺序：黑、蓝、红、绿、黄、白）
    e6_indices = [0x0, 0x5, 0x3, 0x6, 0x2, 0x1]
    
    # 创建映射表：256个灰度值 -> E6驱动索引
    index_map = np.zeros(256, dtype=np.uint8)
    
    # 优化分段边界，确保深红色和黄色正确映射
    # 分段边界：[0, 41, 71, 121, 181, 236, 256)
    boundaries = [0, 41, 71, 121, 181, 236, 256]
    
    for i in range(256):
        # 找到灰度值 i 属于哪个颜色段
        for segment in range(len(boundaries) - 1):
            if boundaries[segment] <= i < boundaries[segment + 1]:
                index_map[i] = e6_indices[segment]
                break
        else:
            # 如果 i == 255，映射到白色
            index_map[i] = e6_indices[-1]
    
    return index_map


def convert_to_e6_with_grayscale_color_map(
    img: Image.Image,
    target_size: Optional[Tuple[int, int]] = None
) -> Tuple[Image.Image, np.ndarray]:
    """
    将RGB图像转换为E6六色格式，使用灰阶与颜色映射算法（纯色模式）
    
    纯色模式：将256级灰度强制量化为6个纯色段，确保相同区域的像素映射到同一颜色，
    形成完全无抖动的纯色块效果。
    
    参数：
        img: PIL.Image，任意模式，内部会转为RGB
        target_size: (width, height)，如果提供则先缩放到指定分辨率
    
    返回：
        (mapped_image, color_indices)
        - mapped_image: PIL.Image (RGB模式，颜色映射后的图像，纯色块)
        - color_indices: numpy数组 [H, W]，值为驱动索引（0x0, 0x1, 0x2, 0x3, 0x5, 0x6）
    """
    # 转换为RGB并调整尺寸
    rgb_img = img.convert("RGB")
    if target_size is not None:
        rgb_img = rgb_img.resize(target_size, Image.LANCZOS)
    
    # PIL Image 转 numpy array (RGB格式)
    img_array = np.array(rgb_img)
    
    # 转为灰度图
    gray = cv2.cvtColor(img_array, cv2.COLOR_RGB2GRAY)
    
    # 构建灰度到E6索引的直接映射表
    index_map = build_grayscale_to_e6_index_map()
    
    # 直接根据灰度值查表得到E6驱动索引
    # 这个映射表确保：相同灰度值的像素 → 同一个E6颜色 → 纯色块
    color_indices = index_map[gray]
    
    # 根据驱动索引生成RGB图像用于预览（纯色块）
    # 使用向量化操作，确保每个驱动索引对应唯一的RGB值
    h, w = color_indices.shape
    rgb_array = np.zeros((h, w, 3), dtype=np.uint8)
    
    # 直接映射：每个驱动索引对应一个纯色RGB值
    for idx, rgb in E6_IDX2RGB.items():
        mask = (color_indices == idx)
        if np.any(mask):
            rgb_array[mask] = rgb
    
    # 转回PIL Image
    mapped_img = Image.fromarray(rgb_array, mode="RGB")
    
    return mapped_img, color_indices


# ==================== 梯度边界混合算法 ====================

def compute_gradient_mask(img_gray: np.ndarray, grad_thresh: int = 40) -> np.ndarray:
    """
    计算梯度掩码，用于识别边界区域
    
    参数：
        img_gray: 灰度图像 (numpy数组, uint8)
        grad_thresh: 梯度阈值，默认40
    
    返回：
        二值掩码，边界区域为255，其他为0
    """
    # Sobel 梯度
    grad_x = cv2.Sobel(img_gray, cv2.CV_32F, 1, 0, ksize=3)
    grad_y = cv2.Sobel(img_gray, cv2.CV_32F, 0, 1, ksize=3)
    grad_mag = cv2.magnitude(grad_x, grad_y)
    grad_norm = cv2.normalize(grad_mag, None, 0, 255, cv2.NORM_MINMAX).astype(np.uint8)
    
    # 阈值化
    _, edge_mask = cv2.threshold(grad_norm, grad_thresh, 255, cv2.THRESH_BINARY)
    # 膨胀扩大边界区域
    kernel = np.ones((5, 5), np.uint8)
    edge_expand = cv2.dilate(edge_mask, kernel, iterations=1)
    
    return edge_expand


def blend_boundary(img: np.ndarray, edge_mask: np.ndarray) -> np.ndarray:
    """
    在边界区域进行颜色混合，减少量化伪影
    
    参数：
        img: RGB图像 (numpy数组, uint8, HxWx3)
        edge_mask: 边界掩码 (numpy数组, uint8, HxW)
    
    返回：
        混合后的RGB图像
    """
    h, w = img.shape[:2]
    blended = img.copy().astype(np.float32)
    
    # 归一化梯度权重
    gray = cv2.cvtColor(img, cv2.COLOR_RGB2GRAY).astype(np.float32)
    grad_x = cv2.Sobel(gray, cv2.CV_32F, 1, 0, ksize=3)
    grad_y = cv2.Sobel(gray, cv2.CV_32F, 0, 1, ksize=3)
    weights = cv2.normalize(cv2.magnitude(grad_x, grad_y), None, 0, 1, cv2.NORM_MINMAX)
    
    for y in range(h):
        for x in range(w):
            if edge_mask[y, x] > 0:
                wgt = weights[y, x]
                # 四邻域平均颜色
                y0, y1 = max(y - 1, 0), min(y + 1, h - 1)
                x0, x1 = max(x - 1, 0), min(x + 1, w - 1)
                neighbors = img[y0:y1+1, x0:x1+1].astype(np.float32)
                avg_color = neighbors.mean(axis=(0, 1))
                blended[y, x] = blended[y, x] * (1 - wgt) + avg_color * wgt
    
    return np.clip(blended, 0, 255).astype(np.uint8)


def quantize_to_palette(img: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
    """
    将RGB图像量化到E6调色板，使用最近邻匹配（向量化实现）
    
    参数：
        img: RGB图像 (numpy数组, uint8, HxWx3)
    
    返回：
        (quantized_img, color_indices)
        - quantized_img: 量化后的RGB图像
        - color_indices: 驱动索引数组 (HxW)，值为0x0, 0x1, 0x2, 0x3, 0x5, 0x6
    """
    h, w = img.shape[:2]
    
    # 展平图像为 (H*W, 3)
    img_flat = img.reshape(-1, 3).astype(np.float32)
    num_pixels = img_flat.shape[0]
    
    # 有效的调色板索引（跳过索引4）
    valid_palette_indices = np.array([0, 1, 2, 3, 5, 6], dtype=np.uint8)
    palette_float = E6_PALETTE_ARRAY[valid_palette_indices].astype(np.float32)  # (6, 3)
    
    # 计算每个像素到所有调色板颜色的距离
    # img_flat: (H*W, 3), palette_float: (6, 3)
    # 使用广播计算所有距离: (H*W, 6)
    distances = np.sqrt(np.sum((img_flat[:, np.newaxis, :] - palette_float[np.newaxis, :, :]) ** 2, axis=2))
    
    # 找到最近的颜色索引（在valid_palette_indices中的位置）
    closest_idx_in_valid = np.argmin(distances, axis=1)  # (H*W,)
    
    # 映射回驱动索引
    color_indices_flat = valid_palette_indices[closest_idx_in_valid]
    color_indices = color_indices_flat.reshape(h, w)
    
    # 生成量化后的图像
    quantized_flat = E6_PALETTE_ARRAY[color_indices_flat]
    quantized = quantized_flat.reshape(h, w, 3)
    
    return quantized, color_indices


def convert_to_e6_nearest_color(
    img: Image.Image,
    target_size: Optional[Tuple[int, int]] = None
) -> Tuple[Image.Image, np.ndarray]:
    """Map flat artwork to the nearest E6 color without error diffusion."""
    rgb_img = img.convert("RGB")
    if target_size is not None and rgb_img.size != target_size:
        rgb_img = rgb_img.resize(target_size, Image.LANCZOS)

    quantized, color_indices = quantize_to_palette(np.asarray(rgb_img))
    return Image.fromarray(quantized), color_indices

def convert_to_e6_with_gradient_blend(
    img: Image.Image,
    target_size: Optional[Tuple[int, int]] = None,
    grad_thresh: int = 40
) -> Tuple[Image.Image, np.ndarray]:
    """
    将RGB图像转换为E6六色格式，使用梯度边界混合算法
    
    参数：
        img: PIL.Image，任意模式，内部会转为RGB
        target_size: (width, height)，如果提供则先缩放到指定分辨率
        grad_thresh: 梯度阈值，默认40
    
    返回：
        (quantized_image, color_indices)
        - quantized_image: PIL.Image (RGB模式，量化后的图像)
        - color_indices: numpy数组 [H, W]，值为驱动索引（0x0, 0x1, 0x2, 0x3, 0x5, 0x6）
    """
    # 转换为RGB并调整尺寸
    rgb_img = img.convert("RGB")
    if target_size is not None:
        rgb_img = rgb_img.resize(target_size, Image.LANCZOS)
    
    # PIL Image 转 numpy array (RGB格式)
    img_array = np.array(rgb_img)
    
    # 转换为灰度图用于梯度计算
    gray = cv2.cvtColor(img_array, cv2.COLOR_RGB2GRAY)
    
    # 计算梯度掩码
    edge_mask = compute_gradient_mask(gray, grad_thresh)
    
    # 边界混合
    blended = blend_boundary(img_array, edge_mask)
    
    # 量化到调色板
    quantized, color_indices = quantize_to_palette(blended)
    
    # 转回PIL Image
    quantized_img = Image.fromarray(quantized, mode="RGB")
    
    return quantized_img, color_indices


def convert_indices_to_4bit(color_indices: np.ndarray) -> bytes:
    """
    将颜色索引数组转换为4bit格式（每字节两个像素）
    
    参数：
        color_indices: numpy数组 [H, W]，值为驱动索引（0x0, 0x1, 0x2, 0x3, 0x5, 0x6）
    
    返回：
        bytes: 4bit打包后的数据
    """
    height, width = color_indices.shape
    packed_width = (width + 1) // 2  # 向上取整
    output = bytearray(height * packed_width)
    
    for y in range(height):
        for x in range(0, width, 2):
            # 获取两个像素的驱动索引
            idx1 = int(color_indices[y, x]) & 0xF  # 确保只取低4bit
            idx2 = (int(color_indices[y, x + 1]) & 0xF) if x + 1 < width else 0x1  # 默认白色
            
            # 打包：高4bit是第一个像素，低4bit是第二个像素
            packed_byte = (idx1 << 4) | idx2
            output[y * packed_width + x // 2] = packed_byte
    
    return bytes(output)


def build_preview_image(color_indices: np.ndarray) -> Image.Image:
    """
    根据颜色索引生成预览RGB图像
    
    参数：
        color_indices: numpy数组 [H, W]，值为驱动索引（0x0, 0x1, 0x2, 0x3, 0x5, 0x6）
    
    返回：
        PIL.Image (RGB模式)
    """
    height, width = color_indices.shape
    rgb_array = np.zeros((height, width, 3), dtype=np.uint8)
    
    # 映射驱动索引到RGB值
    for idx, rgb in E6_IDX2RGB.items():
        mask = (color_indices == idx)
        if np.any(mask):
            rgb_array[mask] = rgb
    
    # 处理无效索引（默认白色）
    valid_indices = set(E6_IDX2RGB.keys())
    invalid_mask = ~np.isin(color_indices, list(valid_indices))
    if np.any(invalid_mask):
        rgb_array[invalid_mask] = E6_IDX2RGB[0x1]  # 白色
    
    return Image.fromarray(rgb_array, mode="RGB")


def process_e6_image(
    img: Image.Image,
    target_size: Optional[Tuple[int, int]] = None,
    algorithm: Literal['floyd_steinberg', 'nearest_color', 'gradient_blend', 'grayscale_color_map'] = 'floyd_steinberg',
    grad_thresh: int = 40
) -> dict:
    """
    完整的E6图像处理流程
    
    参数：
        img: PIL.Image
        target_size: (width, height)，默认(800, 480)
        algorithm: 处理算法，'floyd_steinberg'、'nearest_color'、'gradient_blend' 或 'grayscale_color_map'
        grad_thresh: 梯度阈值（仅用于 gradient_blend 算法），默认40
    
    返回：
        {
            'preview_image': PIL.Image (RGB预览),
            'color_indices': np.ndarray (颜色索引),
            'data_4bit': bytes (4bit打包数据),
            'width': int,
            'height': int
        }
    """
    if target_size is None:
        target_size = (800, 480)
    
    # 根据算法选择处理方式
    if algorithm == 'floyd_steinberg':
        paletted, color_indices = convert_to_e6_with_dither(img, target_size)
        # Floyd-Steinberg算法：使用paletted图像作为预览（包含抖动效果）
        # paletted是P模式，需要转换为RGB
        preview = paletted.convert("RGB")
    elif algorithm == 'nearest_color':
        quantized_img, color_indices = convert_to_e6_nearest_color(img, target_size)
        paletted = quantized_img
        preview = quantized_img
    elif algorithm == 'gradient_blend':
        quantized_img, color_indices = convert_to_e6_with_gradient_blend(
            img, target_size, grad_thresh
        )
        paletted = quantized_img
        # 梯度边界混合算法：直接使用quantized_img作为预览（包含边界混合效果）
        preview = quantized_img
    elif algorithm == 'grayscale_color_map':
        mapped_img, color_indices = convert_to_e6_with_grayscale_color_map(
            img, target_size
        )
        paletted = mapped_img
        # 灰阶映射算法：直接使用mapped_img作为预览（已经是纯色块）
        preview = mapped_img
    else:
        raise ValueError(f"未知的算法: {algorithm}")
    
    # 转换为4bit格式
    data_4bit = convert_indices_to_4bit(color_indices)
    
    # 验证数据大小
    height, width = color_indices.shape
    expected_bytes = (width + 1) // 2 * height  # 4bit打包后的字节数
    actual_bytes = len(data_4bit)
    
    if actual_bytes != expected_bytes:
        print(f'⚠️  警告：4bit数据大小不匹配！期望 {expected_bytes} 字节，实际 {actual_bytes} 字节')
    
    return {
        'preview_image': preview,
        'color_indices': color_indices,  # numpy数组，需要转换为列表以便JSON序列化
        'data_4bit': data_4bit,
        'width': target_size[0],
        'height': target_size[1]
    }


# ==================== Flask API 辅助函数 ====================

def process_e6_image_from_base64(
    base64_data: str,
    width: int = 800,
    height: int = 480,
    algorithm: Literal['floyd_steinberg', 'nearest_color', 'gradient_blend', 'grayscale_color_map'] = 'floyd_steinberg',
    grad_thresh: int = 40
) -> dict:
    """
    从base64字符串处理图像
    
    参数：
        base64_data: base64编码的图片数据（不含data:image前缀）
        width: 目标宽度
        height: 目标高度
        algorithm: 处理算法，'floyd_steinberg'、'nearest_color'、'gradient_blend' 或 'grayscale_color_map'
        grad_thresh: 梯度阈值（仅用于 gradient_blend 算法），默认40
    
    返回：
        包含预览图base64、4bit数据base64等的字典
    """
    if type(width) is not int or type(height) is not int or (width, height) != EPD_TARGET_SIZE:
        raise ValueError('目标尺寸必须为 800x480')
    if not isinstance(base64_data, str) or not base64_data:
        raise ValueError('图片数据为空')
    if len(base64_data) > MAX_SOURCE_BASE64_CHARS:
        raise ValueError('图片数据超过大小限制')

    try:
        img_bytes = base64.b64decode(base64_data, validate=True)
    except Exception as exc:
        raise ValueError('图片 Base64 格式无效') from exc
    if len(img_bytes) > MAX_SOURCE_IMAGE_BYTES:
        raise ValueError('图片数据超过大小限制')

    img = Image.open(io.BytesIO(img_bytes))
    source_width, source_height = img.size
    if source_width <= 0 or source_height <= 0:
        raise ValueError('图片尺寸无效')
    if source_width * source_height > MAX_SOURCE_IMAGE_PIXELS:
        raise ValueError('源图像素数超过限制')
    img.load()
    
    # 处理图像
    result = process_e6_image(
        img,
        target_size=(width, height),
        algorithm=algorithm,
        grad_thresh=grad_thresh
    )
    
    # 将预览图转换为base64
    preview_buffer = io.BytesIO()
    result['preview_image'].save(preview_buffer, format='PNG')
    preview_base64 = base64.b64encode(preview_buffer.getvalue()).decode('utf-8')
    
    # 将4bit数据转换为base64
    data_4bit_base64 = base64.b64encode(result['data_4bit']).decode('utf-8')
    
    # 将颜色索引数组展平为一维列表（按行扫描）
    color_indices_flat = result['color_indices'].flatten().tolist()
    
    return {
        'success': True,
        'previewImage': preview_base64,
        'data4bit': data_4bit_base64,
        'width': result['width'],
        'height': result['height'],
        'colorIndices': color_indices_flat,  # 一维列表，便于前端使用
        'algorithm': algorithm  # 返回使用的算法
    }
