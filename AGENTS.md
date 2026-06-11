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

唤醒触发：GPIO0 按键（低电平）或定时器；默认 12 小时，云端可通过 `nextSleepSeconds` 动态下发。

## 2. 项目结构

```
Loader_esp32wf/
├── Loader_esp32wf.ino      # 主程序入口（setup/loop）
├── http_update.h           # HTTP 拉取更新核心逻辑（Deep-sleep 架构）
├── wifi_config.h           # WiFi 配网（AP 热点 + Captive Portal + Web 配网页面）
├── DEV_Config.h/cpp        # 硬件引脚定义与 SPI 初始化
├── epd.h                   # 墨水屏驱动接口（型号分发）
├── epd7in3.h               # 7.3寸 E6 驱动适配层
├── EPD_7in3e.h/cpp         # 墨水屏底层驱动（Waveshare 原版）
├── GUI_Paint.h/cpp         # GUI 绘制库（文字/图形）
├── qrcode.h / qrcode.c     # 内置二维码编码器（AP 配网二维码）
├── buff.h                  # 图像缓冲区辅助
├── fonts.h / provisioning_fonts.h                              # 基础字库声明 / AP 配网页字体角色
├── font12.cpp / font16.cpp / font24.cpp / font12CN.c / fontNum.c  # 基础 ASCII/中文/数字字库
├── font20CN.c / font24CN.c / font36CN.c / font38CN.c          # AP 配网页专用字库
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
| Deep-sleep 间隔 | `http_update.h` | `DEEP_SLEEP_INTERVAL_HOURS`（默认 12h 回退值）/ NVS `slpInt`（云端动态下发秒数） |
| 设备码模式 | `http_update.h` | `DEVICE_ID_MODE`（默认 2=后6位） |
| 长按配网阈值 | `Loader_esp32wf.ino` | `WIFI_RECONFIG_HOLD_MS`（冷启动默认 3000ms）/ `WIFI_RECONFIG_POST_WAKE_CONFIRM_MS`（GPIO唤醒后默认 1200ms） |
| 本地 UI 帧缓冲 | `http_update.h` | `acquireEpdUiFrame()` / `releaseEpdUiFrame()`（按需 malloc，画完 free） |
| MongoDB 容器/数据 | `docker-compose.yml` / `.env` | `mongodb` 服务、`MONGO_INITDB_ROOT_*`、`mongodb/data` |

## 3. 快速定位

- **修改云端地址**：`http_update.h` 第 37-38 行
- **修改默认唤醒间隔**：`http_update.h` 的 `DEEP_SLEEP_INTERVAL_HOURS`；模板动态间隔由云端返回 `nextSleepSeconds`，设备保存到 NVS `device/slpInt`
- **NVS 命名空间**：WiFi 配置用 `wifi_cfg`，设备状态用 `device`（键名 `claimed`/`imgVer`/`slpInt`）
- **SPIFFS 临时文件**：`/temp_image.bin`，期望大小固定 384000 字节（7.3" E6）
- **本地 UI 帧缓冲**：`http_update.h` 中 `acquireEpdUiFrame()`（AP 配网页 / 添加设备码页都使用 `PROVISIONING_FULL_STRIPE_SIZE` 的 800×144 条带，约 57600 字节），`releaseEpdUiFrame()` 释放；**不得**再保留 192KB 静态 BSS
- **AP 配网页字体角色**：`provisioning_fonts.h`（标题 / 提示 / 标签 / 动态值）
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

注意：使用 `docker compose`（无连字符），新版 Docker 标准命令。生产模板服务端口：前端 `8080:80`，后端仅 Docker 内网 `5000`，MongoDB 仅 Docker 内网 `27017`。
`cloud_server/mongodb/` 和 `cloud_server/backend/data/` 是服务器运行数据目录，首次部署会自动生成，不提交 Git；更新代码时不得删除或覆盖。

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
| GPIO1/4/5/6/7/10 | 墨水屏 SPI/控制信号（MOSI/RST/BUSY/DC/CS/SCK） |
| GPIO2/8/9 | Strapping pins，新硬件设计中避免接上电时会拉电平的外设 |
| GPIO12-17 | 模组内部 Flash 相关/未引出资源，固件不得初始化为用户 IO |
| GPIO20/21 | UART0 RX/TX（系统保留） |

### SPIFFS 文件长度校验
下载完成后和显示前各做一次校验，期望值 `EPD_EXPECTED_CHARS = 384000`。长度不匹配时删除临时文件并跳过刷新，不得强行传给 EPD 驱动（会导致 BUSY 卡死）。

### 墨水屏 BUSY 超时
墨水屏 `BUSY` 等待不得无限阻塞。初始化/上电/断电阶段默认 10 秒超时，显示刷新阶段默认 180 秒超时；超时后退出当前显示流程，保证 AP 配网服务器和主状态机不会因屏幕异常永久卡死。

## 7. 反模式（禁止做法）

- **禁止在 `loop()` 中重复查询云端**。本架构是一次性状态机，`loop()` 只执行已判定的下载任务。
- **禁止在 Deep-sleep 前不等待 GPIO0 释放**。按键仍为低电平时入睡会立刻再次唤醒（等待 `WAKEUP_RELEASE_WAIT_MS = 2500ms`）。
- **禁止重新引入 MQTT 常驻连接链路**。当前架构已完全切换为 Deep-sleep + HTTP 拉取。
- **禁止在 `http_update.h` 中定义 `Preferences preferences`**。该对象在 `.ino` 中定义，头文件只能 `extern` 引用。
- **禁止跳过 SPIFFS 长度校验直接刷新 EPD**。数据不完整会导致 BUSY 卡死。
- **禁止在 AP 配网模式下调用 `enterDeepSleep()`**。AP 模式需保持 Web 服务器运行。
- **禁止**再定义 `static g_epdUiFrame[192000]`（会永久占用 BSS）。AP 配网页 / 添加设备码页都使用 `PROVISIONING_FULL_STRIPE_SIZE` 条带流式刷整屏，约 58KB，按需 `malloc`→绘制→`free`。
- **禁止在全局作用域构造 `WebServer`**。使用 `wifi_config.h` 的 `getConfigServer()` 延后分配。

## 8. 项目特有模式

### 一次性状态机（唤醒周期）
```
唤醒
  -> 检测长按 3s -> 是：进入 AP 配网，屏幕显示二维码/热点名/192.168.4.1，不睡眠
  -> 判断唤醒原因 -> 非正常唤醒且已配网：连接WiFi查询云端
     -> 云端 claimed=true：直接 Deep-sleep
     -> 云端 claimed=false：显示设备码和云端配置网页二维码 -> Deep-sleep
  -> 判断唤醒原因 -> 正常唤醒（按键/定时）且未配网：进入 AP 配网并显示二维码/热点名/192.168.4.1
  -> 正常唤醒且已有 WiFi 配置但连接失败：保留配置，直接 Deep-sleep，下次唤醒重试
  -> WiFi 连接
  -> prepareUpdateDecisionOnce()（只执行一次）
     -> POST /api/device/status（上报 ip/rssi/uptime_ms/freeHeap/wakeType/wakeCause/currentSleepSeconds）-> 保存云端按当前内容/模板返回的 nextSleepSeconds -> 版本比较 -> 设置 g_updateNeeded
     -> HTTP_UPDATE__loop()
     -> 若 g_updateNeeded：流式下载到 SPIFFS -> 刷新 EPD -> 刷新成功后保存版本
     -> enterDeepSleep()
```

### 流式下载到 SPIFFS
图片数据（384KB）不能全部放入 RAM，必须用 512 字节缓冲区流式写入 SPIFFS，刷新时由 `epd7in3.h` 的 `EPD_load_7in3E_from_buff()` 以 **400 字节行缓冲** 流式送屏（不占 192KB RAM）。

本地 `imgVer` 只能在 EPD 刷新成功后写入。下载成功但 BUSY 超时、文件异常、`EPD_dispLoad` 未设置或显示失败时，不得保存新版本号；这样下次唤醒仍会重试同一云端版本。`imageVersion` 在设备端只作为云端图片同步标记，不作为必须递增的大小比较依据；当云端 `imageVersion > 0` 且与本地 `imgVer` 不一致时必须按云端当前图片同步。

### 长按检测（GPIO0 复用）
GPIO0 同时作为唤醒键和长按配网入口。检测逻辑：从函数入口开始就必须是低电平，中途松开则返回 false。
如果本次是 GPIO 唤醒，固件会把“唤醒按下”视为重新配网动作的一部分，因此只要求在启动后继续保持低电平一个较短确认窗口（默认 `1200ms`）；冷启动/复位场景仍使用 `WIFI_RECONFIG_HOLD_MS`（默认 `3000ms`）。

### 设备码生成
基于 MAC 地址，`DEVICE_ID_MODE=2`（默认后 6 位，如 `B6DA20`）。AP 热点名称：`EPD-XXXXXX`。AP 阶段优先 `esp_read_mac(ESP_MAC_EFUSE_FACTORY)`，无需先启动 WiFi。

### 本地 UI 显示内存（WiFi 配网页 + 设备码页）
ESP32-C3-WROOM-02U 无 PSRAM，**堆最大连续块约 115KB**（实测 `largest≈114676`），无法 `malloc(192000)` 全屏画布。固件不得再保留 192KB 静态回退缓冲；AP 配网页 / 添加设备码页都使用 `PROVISIONING_FULL_STRIPE_SIZE`（800×144，约 58KB）条带流式刷整屏，按需 `malloc`/`free`。`startAPMode()` 内 **先刷屏再启 softAP**；Captive Web 仍延后到 DHCP 之后。

| 页面 | 函数 | 画布用法 | 刷新 |
|------|------|----------|------|
| WiFi AP 配网 | `displayProvisioningScreen()` | 共用缓冲按 800×144 条带绘制整屏 | 一次性流式发送完整 800×480，避免 `DisplayPart` 外围变白 |
| 未绑定添加设备码 | `displayDeviceCode()` | 共用缓冲按 800×144 条带绘制整屏 | 一次性流式发送完整 800×480，避免 `DisplayPart` 外围变白 |

顺序约束：**WiFi OFF → `malloc` 刷配网二维码 → `free` → `softAP`（不启 Web/DNS）→ 手机关联且 DHCP 完成后再 `ensureApWebServices()`**。`WebServer` 在 `initConfigServer()` 中 `new`。同一唤醒周期内不要连续画两页后再刷云端图。**云端会议牌**仍走 SPIFFS + 行缓冲，不使用 UI 帧缓冲。

### AP 配网二维码与 Captive Portal
AP 配网热点 `EPD-XXXXXX` 默认**开放网络**（`PROVISIONING_AP_PSK` 留空）；若需 WPA2 可设 ≥8 字符密码，屏上与二维码会同步显示。开放热点使用最简 `WiFi.softAP(ssid)`，与历史可用版本一致。墨水屏使用 **满屏 AP 配网页** 显示 WiFi 二维码、热点名与 `192.168.4.1`；`wifi_config.h` 的 `DNSServer` 将常见 Captive Portal 探测路径统一返回配网页。自动弹出为 best-effort，须保留 `192.168.4.1` 备用入口。

### 设备状态遥测
固件每次调用 `/api/device/status` 时除 `deviceId` 外，还会上报 `ip`、`rssi`、`uptime_ms`、`freeHeap`、`wakeType`、`wakeCause`、`currentSleepSeconds`。后端保存到 `device_status_collection`，并由 `/api/devices` 返回给前端设备卡片显示当前内容、唤醒间隔、预计自动唤醒时间和最后唤醒信息。`wakeType=manual` 更新 `lastManualWake`，`wakeType=auto` 更新 `lastAutoWake`。云端会在响应中返回按 `activeContentMode` / `activeTemplateId` 计算的 `nextSleepSeconds > 0`，设备保存到 NVS `device/slpInt` 并在本次回睡时使用。修改字段名时必须同步 `http_update.h`、`cloud_server/backend/app.py` 和 `cloud_server/frontend/devices.js`。当前内置模板为时钟、天气、日历、待办、每日一言、二维码；计数器和空白页不再作为内置模板入口。

### 未配网开机显示
当设备没有本地 WiFi 配置时，`startAPMode()` 内会先渲染并显示 AP 配网页，再启动 `softAP`；Captive Portal Web/DNS 延后到手机拿到 IP 且刷屏结束后。`loop()` 不再补刷 AP 页面，避免重复刷新墨水屏或引入第二条渲染路径。

## 9. 常见陷阱

- **NVS `nvs_open failed: NOT_FOUND`**：首次使用正常，命名空间自动创建。持续出现则全擦重烧。
- **SPIFFS 挂载失败**：首次使用正常，自动格式化。持续失败：工具 -> Erase Flash -> All Flash Contents。
- **设备唤醒后立刻再次唤醒**：GPIO0 缺少上拉电阻或按键未松开。建议硬件加 10k 上拉到 3.3V。
- **图片版本不更新**：检查 `/api/device/status` 返回的 `imageVersion` 是否与 NVS 中的 `imgVer` 不一致；只要不一致就应按云端当前图片重新同步。
- **下载失败（内容长度异常）**：云端图片必须恰好 384000 字节（800x480，4bit 编码，无多余换行符）。
- **AP 配网页 / 添加设备码页不显示或画布分配失败**：确认固件日志为 `画板: 堆分配 57600 字节 (800x144 条带)`；若仍失败，优先检查启动前剩余堆和最大连续块，而不是重新引入 192KB 全屏缓冲。
- **iPhone 未自动弹出配网页**：属于系统 Captive Portal 策略差异，先确认已连接 `EPD-XXXXXX`，再手动打开 `http://192.168.4.1`。

## 10. 依赖清单

### 固件

| 依赖 | 来源 |
|---|---|
| ArduinoJson | Arduino 库管理器（by Benoit Blanchon） |
| WiFi / HTTPClient / SPIFFS / Preferences / WebServer | 随 ESP32 Arduino 包内置 |

### 云端
`flask==3.0.0`、`flask-cors==4.0.0`、`pymongo`（不固定版本）、`python-dotenv==1.0.0`、`gunicorn==21.2.0`、`numpy==1.26.4`、`Pillow==10.3.0`、`opencv-python-headless==4.8.1.78`。Docker 部署同时启动 `mongo:8.2.6` 容器。注意：`paho-mqtt` 已移除，当前架构不使用 MQTT。

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
