# AGENTS.md — ESP32-C3 墨水屏云端控制系统开发指南

本文件为 AI agent 和开发者提供本仓库专用的开发规范与上下文。

## 1. 概览

本项目是一套 **ESP32-C3 + 7.3寸六色墨水屏 + Flask 云端** 的低功耗图片推送系统。

核心设计原则：
- 设备绝大多数时间处于 **Deep-sleep（µA 级）**，不保持常驻连接
- 唤醒后执行一次性 HTTP 拉取流程，完成后立即回到 Deep-sleep
- 服务器端上传图片时**不要求设备在线**，设备下次唤醒自动拉取

系统架构：
```
用户浏览器 <--> 云服务器 (Nginx + Flask + MongoDB)
                    ^
                    | HTTP Pull（设备主动拉取）
                    v
             ESP32-C3（Deep-sleep）<--> 7.3寸 E6 墨水屏
```

唤醒触发：GPIO0 按键（低电平）或 12 小时定时器。

## 2. 项目结构

```
Loader_esp32wf/
├── Loader_esp32wf.ino      # 主程序入口（setup/loop）
├── http_update.h           # HTTP 拉取更新核心逻辑（Deep-sleep 架构）
├── wifi_config.h           # WiFi 配网（AP 热点 + Web 配网页面）
├── mqtt_config.h           # 历史遗留，已不再使用，勿修改
├── DEV_Config.h/cpp        # 硬件引脚定义与 SPI 初始化
├── epd.h                   # 墨水屏驱动接口（型号分发）
├── epd7in3.h               # 7.3寸 E6 驱动适配层
├── EPD_7in3e.h/cpp         # 墨水屏底层驱动（Waveshare 原版）
├── GUI_Paint.h/cpp         # GUI 绘制库（文字/图形）
├── buff.h                  # 图像缓冲区辅助
├── fonts.h / font12.cpp / font24.cpp  # 字库
├── Debug.h                 # 调试宏
├── partitions.csv          # 自定义 Flash 分区表（必须配套烧录）
├── README.md               # 用户文档
│
└── cloud_server/
    ├── docker-compose.yml  # Docker 部署配置（backend + frontend）
    ├── backend/app.py      # Flask 主应用（API 路由）
    ├── backend/six_color_epd.py  # 六色图像处理算法
    └── frontend/           # Nginx + 静态页面
```

**关键文件速查：**

| 需要修改的内容 | 文件 | 位置 |
|---|---|---|
| 云端 IP/端口 | `http_update.h` | `CLOUD_API_HOST` / `CLOUD_API_PORT` |
| Deep-sleep 间隔 | `http_update.h` | `DEEP_SLEEP_INTERVAL_HOURS`（默认 12h） |
| 设备码模式 | `http_update.h` | `DEVICE_ID_MODE`（默认 2=后6位） |
| 长按配网阈值 | `Loader_esp32wf.ino` | `WIFI_RECONFIG_HOLD_MS`（默认 3000ms） |
| MongoDB 连接 | `docker-compose.yml` | `MONGODB_URI` 环境变量 |

## 3. 快速定位

- **修改云端地址**：`http_update.h` 第 37-38 行
- **修改唤醒间隔**：`http_update.h` 第 51 行
- **NVS 命名空间**：WiFi 配置用 `wifi_cfg`，设备状态用 `device`（键名 `claimed`/`imgVer`）
- **SPIFFS 临时文件**：`/temp_image.bin`，期望大小固定 384000 字节（7.3" E6）
- **图像处理算法**：`cloud_server/backend/six_color_epd.py`

## 4. 构建与部署

### 固件（Arduino IDE，不支持 PlatformIO）

Arduino IDE 必要配置：
- 开发板：`ESP32C3 Dev Module`
- Partition Scheme：`Custom Partition Table`（自动读取根目录 `partitions.csv`）
- Flash Size：`4MB`，Flash Mode：`DIO`，Upload Speed：`921600`
- 必装库：`ArduinoJson`（by Benoit Blanchon）

首次烧录：工具 -> Erase Flash -> `All Flash Contents`，再上传。

**验证方式（本仓库无自动化 lint/test 基础设施）：**
- 固件：Arduino IDE 编译通过 + 串口日志观察（`Serial.println` 输出）
- 云端：`docker compose logs -f backend` 观察运行日志
- 单个测试命令当前不可用（无测试框架）

### 云端（Docker）

```bash
cd cloud_server
docker compose build --no-cache
docker compose up -d --force-recreate
docker compose logs -f backend   # 查看后端日志
```

注意：使用 `docker compose`（无连字符），新版 Docker 标准命令。服务端口：前端 `3000:80`，后端 `5000:5000`。

## 5. 代码规范

- 固件全局变量：小驼峰（`deviceId`）；宏定义：全大写下划线（`CLOUD_API_HOST`）；一次性状态机变量：`g_` 前缀（`g_statusChecked`）
- Python：snake_case；头文件保护：`#ifndef XXX_H / #define XXX_H / #endif`
- 错误处理：NVS 每次 `begin()` 后检查返回值，失败时调用 `end()` 并返回；HTTP 失败时调用 `http.end()` 并清理 SPIFFS
- 串口输出前缀：`✅` 成功、`❌` 失败、`⚠️` 警告、`📡` 网络操作
- Waveshare 原版驱动（`EPD_7in3e.h/cpp`、`GUI_Paint.h/cpp`）保持原格式，不做全局重排

## 6. 架构约束

### Deep-sleep 幂等性
每次唤醒通过静态标志保证一次性执行：`g_statusChecked`（防止重复查询云端）、`g_updateAttempted`（防止重复下载）、`g_deepSleepRequested`（防止重复执行 deep-sleep 流程）。修改 `http_update.h` 时不得绕过这些标志。

### 禁止占用的引脚

| GPIO | 用途 |
|---|---|
| GPIO0 | Deep-sleep 唤醒按键（低电平） |
| GPIO2/3/4 | SPI SCK/MOSI/CS（墨水屏） |
| GPIO6/7/8 | EPD RST/DC/BUSY |
| GPIO12/13 | 系统初始化输出低电平 |
| GPIO14-17 | Flash SPI（系统保留，绝对不可占用） |
| GPIO20/21 | UART0 RX/TX（系统保留） |

### SPIFFS 文件长度校验
下载完成后和显示前各做一次校验，期望值 `EPD_EXPECTED_CHARS = 384000`。长度不匹配时删除临时文件并跳过刷新，不得强行传给 EPD 驱动（会导致 BUSY 卡死）。

## 7. 反模式（禁止做法）

- **禁止在 `loop()` 中重复查询云端**。本架构是一次性状态机，`loop()` 只执行已判定的下载任务。
- **禁止在 Deep-sleep 前不等待 GPIO0 释放**。按键仍为低电平时入睡会立刻再次唤醒（等待 `WAKEUP_RELEASE_WAIT_MS = 2500ms`）。
- **禁止使用 `mqtt_config.h`**。历史遗留文件，当前架构已完全切换为 HTTP 拉取。
- **禁止在 `http_update.h` 中定义 `Preferences preferences`**。该对象在 `.ino` 中定义，头文件只能 `extern` 引用。
- **禁止跳过 SPIFFS 长度校验直接刷新 EPD**。数据不完整会导致 BUSY 卡死。
- **禁止在 AP 配网模式下调用 `enterDeepSleep()`**。AP 模式需保持 Web 服务器运行。

## 8. 项目特有模式

### 一次性状态机（唤醒周期）
```
唤醒
  -> 检测长按 3s -> 是：进入 AP 配网，不睡眠
  -> 判断唤醒原因 -> 非正常唤醒且已配网：连接WiFi查询云端
     -> 云端 claimed=true：直接 Deep-sleep
     -> 云端 claimed=false：显示设备码 -> Deep-sleep
  -> 判断唤醒原因 -> 正常唤醒（按键/定时）且未配网：进入 AP 配网
  -> WiFi 连接
  -> prepareUpdateDecisionOnce()（只执行一次）
     -> POST /api/device/status -> 版本比较 -> 设置 g_updateNeeded
  -> HTTP_UPDATE__loop()
     -> 若 g_updateNeeded：流式下载到 SPIFFS -> 刷新 EPD -> 保存版本
     -> enterDeepSleep()
```

### 流式下载到 SPIFFS
图片数据（384KB）不能全部放入 RAM，必须用 512 字节缓冲区流式写入 SPIFFS，再从 SPIFFS 读取传给 EPD 驱动。

### 长按检测（GPIO0 复用）
GPIO0 同时作为唤醒键和长按配网入口。检测逻辑：从函数入口开始就必须是低电平，中途松开则返回 false。

### 设备码生成
基于 MAC 地址，`DEVICE_ID_MODE=2`（默认后 6 位，如 `B6DA20`）。AP 热点名称：`EPD-XXXXXX`。读取 MAC 前必须先初始化 WiFi（`WIFI_AP_STA` 模式），否则返回全零。

## 9. 常见陷阱

- **NVS `nvs_open failed: NOT_FOUND`**：首次使用正常，命名空间自动创建。持续出现则全擦重烧。
- **SPIFFS 挂载失败**：首次使用正常，自动格式化。持续失败：工具 -> Erase Flash -> All Flash Contents。
- **设备唤醒后立刻再次唤醒**：GPIO0 缺少上拉电阻或按键未松开。建议硬件加 10k 上拉到 3.3V。
- **图片版本不更新**：检查 `/api/device/status` 返回的 `imageVersion` 是否大于 NVS 中的 `imgVer`。
- **下载失败（内容长度异常）**：云端图片必须恰好 384000 字节（800x480，4bit 编码，无多余换行符）。

## 10. 依赖清单

### 固件

| 依赖 | 来源 |
|---|---|
| ArduinoJson | Arduino 库管理器（by Benoit Blanchon） |
| WiFi / HTTPClient / SPIFFS / Preferences / WebServer | 随 ESP32 Arduino 包内置 |

### 云端
`flask==3.0.0`、`flask-cors==4.0.0`、`pymongo`（不固定版本）、`python-dotenv==1.0.0`、`gunicorn==21.2.0`、`numpy==1.26.4`、`Pillow==10.3.0`、`opencv-python-headless==4.8.1.78`。注意：`paho-mqtt` 已移除，当前架构不使用 MQTT。

## 11. 参考资料

- [ESP32-C3 技术参考手册（中文）](https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_cn.pdf)
- [合宙 ESP32C3-CORE 开发板 Wiki](https://wiki.luatos.com/chips/esp32c3/board.html)
- [ArduinoJson 文档](https://arduinojson.org/)
- [ESP-IDF Deep-sleep API](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32c3/api-reference/system/sleep_modes.html)
- [Waveshare 7.3" E-Paper E6 产品页](https://www.waveshare.com/7.3inch-e-paper-hat-f.htm)

## 附：规则文件状态

- `.cursorrules`：当前仓库未配置
- `.cursor/rules/`：当前仓库未配置
- `.github/copilot-instructions.md`：当前仓库未配置
