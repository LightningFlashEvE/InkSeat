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
| 云端 HTTPS / 根 CA | `http_update.h` | `CLOUD_API_USE_HTTPS` / `CLOUD_API_ROOT_CA_PEM` |
| 设备状态查询超时 | `http_update.h` | `CLOUD_API_TIMEOUT_MS`（默认 30s，覆盖动态模板按需渲染） |
| Deep-sleep 间隔 | `http_update.h` | `DEEP_SLEEP_INTERVAL_HOURS`（默认 12h 回退值）/ NVS `slpInt`（云端动态下发秒数） |
| 设备码模式 | `http_update.h` | `DEVICE_ID_MODE`（默认 2=后6位） |
| 长按配网阈值 | `Loader_esp32wf.ino` | `WIFI_RECONFIG_HOLD_MS`（冷启动和 GPIO 唤醒统一为 3000ms） |
| 本地 UI 帧缓冲 | `http_update.h` | `acquireEpdUiFrame()` / `releaseEpdUiFrame()`（按需 malloc，画完 free） |
| MongoDB 容器/数据 | `docker-compose.yml` / `.env` | `mongodb` 服务、`MONGO_INITDB_ROOT_*`、`mongodb/data` |

## 3. 快速定位

- **修改云端地址**：`http_update.h` 的 `CLOUD_API_HOST` / `CLOUD_API_PORT`；生产环境同时启用 HTTPS 并配置根 CA
- **修改默认唤醒间隔**：`http_update.h` 的 `DEEP_SLEEP_INTERVAL_HOURS`；模板动态间隔由云端返回 `nextSleepSeconds`，设备保存到 NVS `device/slpInt`
- **NVS 命名空间**：WiFi 配置用 `wifi_cfg`，设备状态用 `device`（键名 `claimed`/`imgVer`/`slpInt`/`devKey`）
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

**验证方式：**
- 静态/跨层契约：`python tools/verify_project.py`
- 后端回归：`python cloud_server/backend/test_app.py`
- 变更空白检查：`git diff --check`
- 固件：Arduino IDE 编译通过 + 串口日志观察（`Serial.println` 输出）；自动镜像脚本首次接管旧 `C:\Loader_esp32wf` 时必须显式加 `-AdoptLegacyMirror`
- 云端运行态：`docker compose logs -f backend`，并检查三个容器健康状态

### 云端（Docker）

```bash
cd cloud_server
docker compose build --no-cache
docker compose up -d --force-recreate
docker compose logs -f backend   # 查看后端日志
```

注意：使用 `docker compose`（无连字符），新版 Docker 标准命令。生产模板服务端口由 `.env` 的 `FRONTEND_BIND` / `FRONTEND_PORT` 控制，后端仅 Docker 内网 `5000`，MongoDB 仅 Docker 内网 `27017`。生产环境必须提供 `PUBLIC_BASE_URL`、`SECRET_KEY`、`ADMIN_BOOTSTRAP_TOKEN` 等必填项；升级顺序和设备认证兼容期见 `cloud_server/README.md`。
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

### 设备身份、配对与链路完整性
设备首次启动生成 32 字节随机密钥并以 64 位十六进制保存到 NVS `device/devKey`；`/api/device/status` 和 `/api/epd/raw/<deviceId>` 都必须携带 `X-Device-Key`。未绑定设备由状态接口返回 6 位短期配对码，用户绑定时必须同时提交设备码与配对码，设备码本身不是凭据。从未绑定过的 TOFU 身份使用 `unclaimedExpiresAt` TTL 自动清理，每次上报续期；曾绑定设备的密钥哈希永久保留。图片下载 URL 只能接受与固件配置完全同源的 raw 端点，下载后必须校验长度、`a`~`p` 字符集和云端 SHA-256。正式环境必须使用受信证书的 HTTPS；不得把设备密钥或登录令牌长期暴露在明文 HTTP 上。

## 7. 反模式（禁止做法）

- **禁止在 `loop()` 中重复查询云端**。本架构是一次性状态机，`loop()` 只执行已判定的下载任务。
- **禁止在 Deep-sleep 前不等待 GPIO0 释放**。按键仍为低电平时入睡会立刻再次唤醒（等待 `WAKEUP_RELEASE_WAIT_MS = 2500ms`）。
- **禁止重新引入 MQTT 常驻连接链路**。当前架构已完全切换为 Deep-sleep + HTTP 拉取。
- **禁止在 `http_update.h` 中定义 `Preferences preferences`**。该对象在 `.ino` 中定义，头文件只能 `extern` 引用。
- **禁止跳过 SPIFFS 长度校验直接刷新 EPD**。数据不完整会导致 BUSY 卡死。
- **禁止恢复“只凭设备码绑定”**。绑定必须原子消费设备屏幕显示的有效配对码。
- **禁止把现有 `deviceKeyHash` 在普通解绑/清理时删除**。否则同一设备码可被重新 TOFU 抢占；密钥丢失只走所有者授权的短时重置窗口。
- **禁止在生产环境关闭 HTTPS 或省略 CA 校验**。`CLOUD_API_USE_HTTPS=1`、固件 host/port、`PUBLIC_BASE_URL` 和外层 TLS 入口必须一致。
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
     -> 云端 claimed=false：显示设备码、6 位配对码和云端配置网页二维码 -> Deep-sleep
  -> 判断唤醒原因 -> 正常唤醒（按键/定时）且未配网：进入 AP 配网并显示二维码/热点名/192.168.4.1
  -> 正常唤醒且已有 WiFi 配置但连接失败：保留配置，直接 Deep-sleep，下次唤醒重试
  -> WiFi 连接
  -> prepareUpdateDecisionOnce()（只执行一次）
     -> POST /api/device/status（携带 X-Device-Key，上报 ip/rssi/uptime_ms/freeHeap/wakeType/wakeCause/currentSleepSeconds）-> 保存云端按当前内容/模板返回的 nextSleepSeconds -> 版本比较 -> 设置 g_updateNeeded
     -> HTTP_UPDATE__loop()
     -> 若 g_updateNeeded：流式下载到 SPIFFS -> 刷新 EPD -> 刷新成功后保存版本
     -> enterDeepSleep()
```

### 流式下载到 SPIFFS
图片数据（384KB）不能全部放入 RAM，必须用 512 字节缓冲区流式写入 SPIFFS，刷新时由 `epd7in3.h` 的 `EPD_load_7in3E_from_buff()` 以 **400 字节行缓冲** 流式送屏（不占 192KB RAM）。

本地 `imgVer` 只能在 EPD 刷新成功后写入。下载成功但 BUSY 超时、文件异常、SHA-256 不匹配、`EPD_dispLoad` 未设置或显示失败时，不得保存新版本号；这样下次唤醒仍会重试同一云端版本。`imageVersion` 在设备端只作为云端图片同步标记，不作为必须递增的大小比较依据；当云端 `imageVersion > 0` 且与本地 `imgVer` 不一致时必须按云端当前图片同步。

### 长按检测（GPIO0 复用）
GPIO0 同时作为唤醒键和长按配网入口。检测逻辑：从函数入口开始就必须是低电平，中途松开则返回 false。
GPIO 唤醒、冷启动和复位场景都必须从检测入口开始连续保持低电平 `WIFI_RECONFIG_HOLD_MS`（默认 `3000ms`）才进入重新配网；中途松开只作为普通手动唤醒，不清除 WiFi。

### 设备码生成
基于 MAC 地址，`DEVICE_ID_MODE=2`（默认后 6 位，如 `B6DA20`）。AP 热点名称：`EPD-XXXXXX`。AP 阶段优先 `esp_read_mac(ESP_MAC_EFUSE_FACTORY)`，无需先启动 WiFi。

设备码用于寻址，不是认证秘密。真正的设备身份由 `devKey` 证明，首次绑定还必须使用屏幕显示的 6 位配对码。默认 6 位设备码存在首次 TOFU 被抢注的边界；要彻底消除需出厂预置密钥或升级为更强的硬件信任链。

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
固件每次调用 `/api/device/status` 时除基础网络和唤醒字段外，还会上报 `firmwareVersion`、`firmwareBuild`、`resetReason`、`localImageVersion`、`gpio0StuckLow` 及待补报更新诊断。实际更新用 NVS `device/updDiag` 保存带校验的紧凑事务记录；完成后调用 `/api/device/update-result`，失败则在下次状态请求补报。后端保存到 `device_status_collection`，并由 `/api/devices` 返回前端展示本地/云端版本、复位原因和刷新阶段。`wakeType=manual` 更新 `lastManualWake`，`wakeType=auto` 更新 `lastAutoWake`。云端仍按当前内容返回 `nextSleepSeconds > 0`，设备保存到 NVS `device/slpInt`。动态模板只在设备手动或定时唤醒调用 `/api/device/status` 时按需重渲染；固件状态查询超时必须覆盖后端渲染耗时。修改字段名时必须同步 `http_update.h`、`cloud_server/backend/app.py` 和 `cloud_server/frontend/devices.js`。当前产品入口只开放会议名牌 `nameplate`；其他后端渲染代码仍保留，但不得误写成当前前端已开放功能。

设备状态不是长连接状态，而是按最后上报和自动唤醒计划推断：最后上报后 5 分钟为“在线”；之后为“睡眠”；允许第一次预计自动唤醒漏报，第二次预计自动唤醒再加 5 分钟上报宽限后仍无状态才判定“离线”。预计唤醒时间在第一次漏报并超过宽限后推进到第二次唤醒时间。模板周期在设备上次上报后发生变更时，必须继续用设备已上报的 `currentSleepSeconds` 推算，并保持 `wakePolicyPending=true`，直到设备下一次上报同步新周期。

### 未配网开机显示
当设备没有本地 WiFi 配置时，`startAPMode()` 内会先渲染并显示 AP 配网页，再启动 `softAP`；Captive Portal Web/DNS 延后到手机拿到 IP 且刷屏结束后。`loop()` 不再补刷 AP 页面，避免重复刷新墨水屏或引入第二条渲染路径。

## 9. 常见陷阱

- **NVS `nvs_open failed: NOT_FOUND`**：首次使用正常，命名空间自动创建。持续出现则全擦重烧。
- **SPIFFS 挂载失败**：首次使用正常，自动格式化。持续失败：工具 -> Erase Flash -> All Flash Contents。
- **设备唤醒后立刻再次唤醒**：GPIO0 缺少上拉电阻或按键未松开。建议硬件加 10k 上拉到 3.3V。
- **图片版本不更新**：检查 `/api/device/status` 返回的 `imageVersion` 是否与 NVS 中的 `imgVer` 不一致；只要不一致就应按云端当前图片重新同步。
- **下载失败（内容长度异常）**：云端图片必须恰好 384000 字节（800x480，4bit 编码，无多余换行符）。
- **设备返回 `Invalid device credentials`**：先确认已刷入带 `devKey` 的新固件；NVS 密钥丢失时由设备所有者在前端开启短时凭据重置窗口，再让设备唤醒注册新密钥，禁止直接删数据库哈希。
- **AP 配网页 / 添加设备码页不显示或画布分配失败**：确认固件日志为 `画板: 堆分配 57600 字节 (800x144 条带)`；若仍失败，优先检查启动前剩余堆和最大连续块，而不是重新引入 192KB 全屏缓冲。
- **iPhone 未自动弹出配网页**：属于系统 Captive Portal 策略差异，先确认已连接 `EPD-XXXXXX`，再手动打开 `http://192.168.4.1`。

## 10. 依赖清单

### 固件

| 依赖 | 来源 |
|---|---|
| ArduinoJson | Arduino 库管理器（by Benoit Blanchon） |
| WiFi / HTTPClient / SPIFFS / Preferences / WebServer | 随 ESP32 Arduino 包内置 |

### 云端
`flask==3.1.3`、`flask-cors==6.0.0`、`pymongo>=4.6,<5`、`python-dotenv==1.2.2`、`gunicorn==22.0.0`、`numpy==1.26.4`、`Pillow==12.3.0`、`opencv-python-headless==4.8.1.78`、`requests==2.33.0`。Docker 部署同时启动 `mongo:8.2.6` 容器。后端 Dockerfile 还会安装 `fonts-noto-cjk` / `fonts-noto-core`，动态模板依赖这些字体渲染中文与常用符号；字体或模板渲染变更后必须重新 `docker compose build --no-cache`，不得依赖 Pillow 默认字体。依赖变更后必须运行 `pip-audit`；注意：`paho-mqtt` 已移除，当前架构不使用 MQTT。

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
