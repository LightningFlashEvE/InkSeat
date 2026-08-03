# ESP32-C3 墨水屏云端控制系统（Deep-sleep + HTTP Pull，无 MQTT）

本项目采用 **Deep-sleep + 按键/定时唤醒 + HTTP 拉取更新** 的低功耗架构：

- 设备绝大多数时间处于 **Deep-sleep（µA 级）**
- 仅在 **GPIO0 按键** 或 **定时唤醒** 后联网；默认 12 小时，云端可通过 `nextSleepSeconds` 动态下发
- 唤醒后通过 HTTP 查询版本并按需下载图片，刷新墨水屏后立刻回到 Deep-sleep
- **服务器端上传图片时不要求设备在线**；设备下次唤醒即可拉到最新内容
- 已保存 WiFi 但本次连接失败时会保留原配置；普通按键/定时唤醒直接回睡，下次再试。需要重新配网时长按 GPIO0。

## 系统架构（无 MQTT）

```
用户浏览器 <--> 云服务器 (Nginx + Flask + MongoDB) <--> ESP32-C3 (HTTP 客户端/Deep-sleep) <--> 墨水屏
```

### 工作流程（核心）

1. 用户在 Web 页面处理图片并点击 **“发布”**（上传到云端）
2. 云端将最新 EPD 数据原子保存到 `cloud_server/backend/data/epd/<deviceId>/latest.txt`，同时在 `versions/<imageVersion>.txt` 保留最近 5 个不可变版本，并记录当前内容类型、模板 ID、内容标签与云端期望唤醒间隔
3. 设备在按键/定时唤醒后执行一次性流程：
   - `POST /api/device/status` 上报网络、唤醒、固件版本、复位原因、本地图片版本和待补报更新诊断，并获取 `claimed/imageVersion/imageUrl/nextSleepSeconds`
   - 若云端返回 `nextSleepSeconds > 0`，设备保存到 NVS `device/slpInt`，本次回睡时按该秒数配置定时唤醒；未返回或返回 0 时保留本地已有间隔，若本地未设置则使用默认 12 小时
   - 若云端图片同步标记 `imageVersion > 0` 且 `imageVersion != NVS(imgVer)`：按 `imageUrl?v=<version>` 下载精确版本 → 校验并刷新墨水屏 → 刷新成功后写入 NVS 新版本 → `POST /api/device/update-result` 回传结果 → Deep-sleep
   - 若版本一致：直接 Deep-sleep
   - 若未绑定：显示满屏添加设备页（云端配置二维码 + 设备码）→ Deep-sleep

## 硬件要求

### ESP32-C3 模块
- **当前硬件**：ESP32-C3-WROOM-02U-N4（4MB Flash，外接天线，**无 PSRAM**）
- **Flash容量**：4MB（用于固件和SPIFFS存储）
- **WiFi**：2.4GHz 802.11 b/g/n
- **SRAM 注意**：运行时堆最大连续块约 **115KB**，无法动态分配 192KB 全屏画布；AP 配网页和添加设备码页都使用 **800×144 条带**流式绘制整屏，按需分配/释放约 **58KB** 缓冲

### 墨水屏
- **型号**：7.3寸 E6 彩色电子纸（800x480分辨率）
- **颜色**：6色（黑、白、黄、红、蓝、绿）
- **注意**：橙色未使用（官方驱动中已注释）

## 引脚连接

### 墨水屏连接（ESP32-C3）

| 功能 | GPIO | 说明 |
|------|------|------|
| **SCK** | **GPIO10** | SPI时钟 |
| **MOSI/DIN** | **GPIO1** | SPI数据输出 |
| **CS** | **GPIO7** | SPI片选（低电平有效） |
| **RST** | **GPIO4** | 复位信号（低电平复位） |
| **DC** | **GPIO6** | 数据/命令选择（高=数据，低=命令） |
| **BUSY** | **GPIO5** | 忙信号（输入，低电平=忙碌） |

### 系统初始化引脚
当前固件不再额外拉低 `GPIO12/GPIO13`。ESP32-C3-WROOM-02U-N4 以模组引脚为准，未引出的内部 Flash 相关 GPIO 不应作为用户 IO 处理。

### Deep-sleep 唤醒按键（GPIO0）

- **唤醒引脚**：GPIO0（低电平唤醒）
- **建议硬件**：GPIO0 通过 **10k 上拉到 3.3V**，按键按下接地
- 仅使用内部上拉也可工作，但抗干扰不如外部上拉稳定
- **长按功能**：长按GPIO0 **3秒**可清除WiFi配置并进入AP配网模式（用于重新配网）
- 调试期串口启动后会打印 `WAKE DEBUG`，包含唤醒原因、GPIO0当前电平和 Deep-sleep 唤醒计数。

### 系统保留引脚（ESP32-C3-WROOM-02U-N4）

| 功能 | GPIO | 说明 | 备注 |
|------|------|------|------|
| **UART0_TX** | GPIO21 | 串口发送（下载/调试） | 系统使用 |
| **UART0_RX** | GPIO20 | 串口接收（下载/调试） | 系统使用 |
| **GPIO2/8/9** | Strapping pins | 启动采样脚 | 避免外设在上电/复位时拉电平 |
| **GPIO12-17** | 模组内部资源 | Flash 相关/未引出 | 不作为用户 IO 使用 |

## 目录结构

```
.
├── cloud_server/              # 云端服务器（Python Flask + Nginx）
│   ├── backend/              # Flask后端API
│   │   ├── app.py            # 主应用
│   │   ├── config.py         # 配置管理
│   │   ├── six_color_epd.py  # 六色图像处理算法（支持三种算法）
│   │   └── requirements.txt  # Python依赖
│   ├── frontend/             # Web前端
│   │   ├── index.html        # 主页面
│   │   ├── control.html      # 设备控制页
│   │   ├── app.js            # 前端逻辑
│   │   └── nginx.conf        # Nginx配置
│   └── docker-compose.yml    # Docker部署配置
│
├── Loader_esp32wf.ino         # ESP32主程序
├── http_update.h              # HTTP 拉取更新（Deep-sleep 架构核心）
├── wifi_config.h              # WiFi配网功能
├── DEV_Config.h/cpp           # 硬件配置（引脚定义）
├── epd.h                      # 墨水屏驱动接口
├── epd7in3.h                  # 7.3寸E6驱动适配层
├── EPD_7in3e.h/cpp            # 墨水屏驱动
├── GUI_Paint.h/cpp            # GUI绘制库
├── qrcode.h / qrcode.c        # 内置二维码编码器（AP配网二维码）
├── fonts.h                    # 基础字库声明
├── provisioning_fonts.h       # AP 配网页字体角色映射
├── font24.cpp                 # 24像素 ASCII 字体
├── font16.cpp                 # 16像素 ASCII 字体（AP 页动态值）
├── font12.cpp                 # 12像素字体数据
├── font12CN.c                 # 中文提示字库（16x16）
├── fontNum.c                  # 数字/英文混排字库（8x16）
├── partitions.csv             # Flash分区表
└── README.md                  # 本文件
```

## 部署步骤

### 1. 准备云服务器

#### 要求
- Linux服务器 (Ubuntu 20.04+ 推荐)
- 公网域名与有效 TLS 证书
- Docker 和 Docker Compose Plugin
- 对外放行 `443/tcp`；容器入口 `8080` 推荐仅监听回环地址

#### MongoDB 数据目录

MongoDB 由 `cloud_server/docker-compose.yml` 中的 `mongodb` 服务启动。首次执行 `docker compose up -d` 时，Docker 会自动创建：

```text
cloud_server/mongodb/data      # MongoDB 数据库文件
cloud_server/mongodb/restore   # 预留恢复目录
```

这两个目录是服务器运行数据，不是源码，不提交 Git。更新服务器代码时不要删除或覆盖 `cloud_server/mongodb/`。

### 2. 部署云端服务

```bash
# 1. 上传代码到服务器
cd /opt
sudo git clone <your-repo> esp32-cloud
cd /opt/esp32-cloud/cloud_server

# 2. 使用Docker部署（推荐）
# 注意：使用 docker compose（无连字符），这是新版本Docker的标准命令

# 首次部署或更新代码后：
cd /opt/esp32-cloud/cloud_server  # 或 <your-project>/cloud_server
git pull  # 拉取最新代码
# 首次部署复制并编辑环境变量；PUBLIC_BASE_URL、管理员引导令牌和随机密钥为必填项
cp .env.example .env
# 后端镜像会安装中文/符号字体；模板或字体相关更新后必须无缓存重建
docker compose build --no-cache  # 重新构建镜像
docker compose up -d --force-recreate  # 强制重新创建容器

# 查看服务状态
docker compose ps

# 查看日志
docker compose logs -f backend  # 后端日志
docker compose logs -f frontend  # 前端日志

# 或手动部署：
cd backend
pip3 install -r requirements.txt

# 3. 配置环境变量（无 MQTT）
# 复制 cloud_server/.env.example 为 .env，并至少修改 MongoDB 密码、SECRET_KEY、公网地址：
export MONGO_INITDB_ROOT_USERNAME="esp32_epd_root"
export MONGO_INITDB_ROOT_PASSWORD="change_this_mongo_password"
export MONGODB_DB="esp32_epd"
# PUBLIC_BASE_URL 用于 status 返回 imageUrl，必须与固件 HTTPS origin 一致
export FLASK_HOST="epd.example.com"
export FLASK_PORT="8080"
export PUBLIC_BASE_URL="https://epd.example.com"
export SECRET_KEY="<random-secret>"
export ADMIN_BOOTSTRAP_TOKEN="<at-least-32-random-characters>"
export ALLOW_REGISTRATION="false"
export DEVICE_AUTH_REQUIRED="true"

# 4. 初始化数据库索引
python3 create_indexes.py

# 5. 启动服务
python3 app.py
```

### 3. 配置防火墙

```bash
# 开放端口
sudo ufw allow 443/tcp     # Web/API统一 HTTPS 入口
sudo ufw enable
```

### 4. 烧录ESP32-C3程序

#### Arduino IDE配置

1. **安装ESP32开发板支持**：
   - 文件 -> 首选项 -> 附加开发板管理器网址：
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - 工具 -> 开发板 -> 开发板管理器 -> 搜索 "esp32" -> 安装 **esp32 by Espressif Systems 3.3.10**（已验证版本）

2. **选择开发板**：
   - 工具 -> 开发板 -> ESP32 Arduino -> **ESP32C3 Dev Module**
   - 工具 -> Partition Scheme -> **Custom Partition Table**
   - 工具 -> Flash Size -> **4MB (32Mb)**
   - 工具 -> Flash Frequency -> **80MHz**
   - 工具 -> Flash Mode -> **DIO**
   - 工具 -> Upload Speed -> **921600**

3. **安装依赖库**：
   - **ArduinoJson 7.x** (by Benoit Blanchon，已验证主版本)

4. **说明**：
   - 设备端已改为 `http_update.h` 的 **HTTP 拉取**模式，不再依赖旧的 MQTT 长连接链路。
   - **必改项**：生产部署应把 `CLOUD_API_USE_HTTPS` 设为 `1`，同步配置 `CLOUD_API_HOST`、`CLOUD_API_PORT=443` 和正确的 `CLOUD_API_ROOT_CA_PEM`；它们必须与云端 `PUBLIC_BASE_URL=https://你的域名` 及外部 TLS 反向代理一致。
   - 未启用 HTTPS 时，登录令牌和设备 `X-Device-Key` 仍可能被链路窃取，只适合受信内网或迁移期。

5. **分区表配置**：

项目根目录的 `partitions.csv` 已配置好：
```
nvs,      data, nvs,     0x9000,  0x5000,
phy_init, data, phy,     0xe000,  0x2000,
factory,  app,  factory, 0x10000, 0x240000,
spiffs,   data, spiffs,  0x250000, 0x1B0000,
```

**注意**：确保 Arduino IDE 中选择了 **Custom Partition Table**，这样会自动使用项目根目录的 `partitions.csv`。

当前分区是单 `factory` 应用的 **No OTA** 布局。`factory` 已扩展到 `0x240000`，正好使用到 `spiffs` 起始地址前，原有 SPIFFS 起始位置和容量不变。若要增加 OTA，仍必须重新设计整张分区表并重新评估两个应用槽与 SPIFFS 容量，不能只新增接口。

6. **编译上传**：
   - 工具 -> Erase Flash -> **All Flash Contents**（首次烧录建议完全擦除）
   - 点击上传

#### Windows 命令行编译（推荐）

如果 Arduino IDE 在中文路径下报：

```text
Sketch too big
text section exceeds available space in board
```

优先不要先怀疑代码本身，先改用仓库自带脚本在 ASCII 临时目录里编译：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1
```

如果本机已有旧版脚本创建的、尚无安全标记的 `C:\Loader_esp32wf`，新版脚本会拒绝自动清空它。确认目录确实是旧固件镜像后，仅首次显式接管：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1 -AdoptLegacyMirror
```

脚本会自动：
- 把工程镜像到 `C:\Loader_esp32wf`
- 固定使用 `ESP32C3 Dev Module` 对应 FQBN
- 固定使用 `FlashSize=4M`、`FlashMode=dio`、`PartitionScheme=custom`
- 输出 `.bin/.elf/partitions.csv` 到 `C:\Loader_esp32wf\.build\esp32c3`

如果你需要保留镜像目录用于后续烧录：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1 -KeepMirror
```

### 5. WiFi配网

设备支持两种WiFi配网方式：

#### 方式1：AP配网模式（首次使用或WiFi未配置）

1. 设备启动后，如果未配置WiFi，会自动进入AP模式
2. 设备会创建一个WiFi热点：`EPD-XXXXXX`（XXXXXX为设备码）
3. 设备进入 AP 配网模式后，墨水屏会显示 **满屏绿色配网页**：
   - WiFi 二维码（可直接扫码加入热点）
   - 热点名称 `EPD-XXXXXX`
   - 配网说明与备用访问地址 `192.168.4.1`
4. iPhone 可直接用相机扫描二维码并加入开放热点；也可在系统 WiFi 列表手动连接 `EPD-XXXXXX`（默认无密码）
5. 设备会尝试通过 Captive Portal 弹出配网页；如果系统没有自动弹出，请手动打开 `http://192.168.4.1`
6. 输入 WiFi 名称与密码（如有），点击连接
7. 设备会自动重启并连接WiFi

#### 方式2：长按GPIO0重新配网（推荐）

如果设备已配置WiFi但需要更换WiFi网络，可以使用此方法：

1. **从Deep-sleep唤醒**：短按GPIO0按键唤醒设备（或等待定时唤醒）
2. **长按进入配网**：
   - 如果设备当前处于 Deep-sleep：按下 GPIO0 唤醒后，**继续按住约 1.2 秒**，设备会立刻清除 WiFi 配置并进入二维码配网
   - 如果设备是上电/复位启动：从启动开始保持 GPIO0 低电平 **3 秒**
3. **自动进入AP模式**：设备检测到长按后会：
   - 清除已保存的WiFi配置
   - 自动进入AP配网模式
   - 创建WiFi热点 `EPD-XXXXXX`
   - 在墨水屏上满屏显示 WiFi 配网界面（二维码、热点名、`192.168.4.1` 等）
4. **完成配网**：按照方式1的步骤4-7完成WiFi配置

**注意**：
- 长按检测在设备启动时立即进行，建议在设备唤醒后不要松手，直接持续按住直到屏幕切到二维码配网页
- 如果设备从 Deep-sleep 被按键唤醒，当前固件会把“唤醒按下”也算进重新配网动作，因此不需要再从开机后重新数满 3 秒
- 如果设备是定时唤醒或复位唤醒，需要在上电后立即按住 GPIO0 按键 3 秒
- iOS / Android 的 Captive Portal 自动弹出受系统策略影响，不能保证 100% 弹出，因此屏幕会始终保留 `192.168.4.1` 作为备用入口

### 6. 测试系统

1. **查看ESP32串口输出**：
```
========================================
  WiFi配网初始化
========================================
✅ WiFi已连接
========================================
  Deep-sleep + HTTP 更新模式
========================================
⏰ 唤醒原因: GPIO按键唤醒 / 定时器唤醒
🔄 开始一次性更新判定...
```

2. **访问Web界面**：
   - 生产环境打开：`https://你的域名/`
   - `http://服务器IP:8080` 仅作为受信内网或迁移期诊断入口；完整 TLS、首次管理员和设备凭据恢复步骤见 `cloud_server/README.md`
   - 首次访问需要注册/登录

3. **添加设备**：
   - 登录后进入设备管理页面
   - 点击"添加设备"
   - 同时输入ESP32显示的设备码（例如：`B6DA20`）和 6 位配对码
   - 配对码校验通过后设备才会绑定；设备码本身不再作为凭据

4. **上传图片**：
   - 在设备控制页面选择已添加的设备
   - 选择墨水屏型号：**7.3寸 E6**
   - 拖拽或选择图片
   - 切换到"处理"面板
   - 选择处理算法：
     - **Floyd-Steinberg抖动**：适合渐变和细节丰富的图像，使用误差扩散技术
     - **梯度边界混合**：适合线条和边界明显的图像，在边界区域进行颜色混合
     - **灰阶与颜色映射**：将灰度映射到颜色梯度（黑->蓝->红->绿->黄->白），纯色块效果
   - 点击"处理并预览"查看效果
   - 点击 **“发布”**
   - 图片会保存到云端，设备下次唤醒后自动拉取并刷新
   - 发布成功后，主画布会回填后端处理后的 6 色预览；设备实际显示应以这张预览为准

## Flash存储说明

### 分区表配置

项目使用自定义分区表（`partitions.csv`）：

| 分区 | 类型 | 偏移 | 大小 | 说明 |
|------|------|------|------|------|
| nvs | DATA | 0x9000 | 20KB | NVS存储（WiFi配置、设备绑定状态） |
| phy_init | DATA | 0xE000 | 8KB | PHY初始化数据 |
| factory | APP | 0x10000 | 2304KiB | 应用程序 |
| spiffs | DATA | 0x250000 | 1728KB | SPIFFS文件系统（图片缓存） |

### SPIFFS使用

- **用途**：存储云端下发的临时图片（下载过程 512 字节流式写入，刷新时 **400 字节行缓冲** 送屏，不占 192KB RAM）
- **自动格式化**：首次使用时自动格式化
- **文件路径**：`/temp_image.bin`
- **期望大小**：384000 字符（`a`~`p` 编码的 800×480 四色数据）

### 显示与内存（本地 UI vs 云端图）

固件有两条独立的显示路径：

| 场景 | 数据来源 | RAM 占用 | 关键代码 |
|------|----------|----------|----------|
| **云端会议牌** | SPIFFS `/temp_image.bin` | ~400 B 行缓冲 | `EPD_load_7in3E_from_buff()`（`epd7in3.h`） |
| **WiFi AP 配网页** | 设备实时绘制 | `800x144` 条带（约 58KB）按需分配，整屏流式发送 | `displayProvisioningScreen()` |
| **未绑定添加设备码页** | 设备实时绘制 | `800x144` 条带（约 58KB）按需分配，整屏流式发送 | `displayDeviceCode()` |

- `http_update.h` 通过 `acquireEpdUiFrame()` / `releaseEpdUiFrame()` 管理共享 UI 画布，WiFi 配网页与设备码页 **共用、互斥**（同一唤醒周期只画其一）。
- AP 模式顺序：**WiFi OFF → 分配约 58KB 条带缓冲流式刷整屏配网页 → 释放画布 → 启动开放热点 → 终端关联 / DHCP 后再启动 DNS 和 Web 配网**；AP 页面只走这一条预渲染路径，不再在 `loop()` 中补刷。可选 WPA2：在 `wifi_config.h` 将 `PROVISIONING_AP_PSK` 设为 8–63 个可打印 ASCII 字符；密码会完整显示在墨水屏上，并写入带四模块静区的 V9/M WiFi 二维码。
- 未绑定添加设备码页同样使用约 58KB 条带缓冲流式刷整屏，二维码 payload 为云端 `index.html` 地址，设备码按当前 MAC 动态绘制。
- 新增本地页面时：优先复用约 58KB 的 UI 缓冲；需要满屏时使用条带流式发送，**不要** `malloc(192000)`，也不要再引入 192KB 静态缓冲。

## 设备码说明

设备码基于MAC地址生成，支持三种模式（在 `device_identity.h` 中配置）：

- **模式0**：完整12位（例如：`3C8A1FB6DA20`）
- **模式1**：前6位（例如：`3C8A1F`）
- **模式2**：后6位（例如：`B6DA20`）**（默认）**

## 故障排查

### ESP32无法连接WiFi

- 检查WiFi SSID和密码是否正确
- 确认WiFi信号强度（2.4GHz）
- 查看串口输出的错误信息
- **重新配网**：使用长按GPIO0 3秒的方式清除配置并重新配网
- 或尝试使用AP配网模式重新配置

### 重新烧录后设备不显示配置码

如果设备重新烧录固件后没有显示配置码，可能是以下原因：

1. **NVS分区未擦除**：烧录时没有选择 "Erase Flash: All Flash Contents"，导致旧的WiFi配置残留
   - **解决方法**：在Arduino IDE中选择 **工具 → Erase Flash → All Flash Contents**，然后重新烧录
   - **串口特征**：会看到 `📦 本地WiFi配置: 已存在`，此时设备会优先尝试连接旧网络，不会直接广播 `EPD-XXXXXX`

2. **设备已绑定**：设备码已在云端被其他用户绑定
   - **解决方法**：在云端数据库中删除该设备记录，或使用新的设备

3. **设备码与MAC地址不匹配**：检查固件中 `DEVICE_ID_MODE` 设置
   - 模式0：完整12位MAC
   - 模式1：前6位MAC
   - 模式2：后6位MAC（默认）

**重要**：从V3.0.0开始，设备在复位重启后会主动连接WiFi查询云端状态。如果云端显示 `claimed=false`，设备会在屏幕上显示 **满屏添加设备码页**（云端配置二维码 + 设备码）。

### AP 配网页或设备码页不显示

1. **能搜到热点但无法加入**：确认连接的是开放热点 `EPD-XXXXXX`（无密码）；串口应出现 `📱 设备已关联热点`。若自定义了 `PROVISIONING_AP_PSK`，扫码或手动输入该密码。仍失败时可改 `PROVISIONING_AP_CHANNEL` 为 `1` 或 `11` 后重试。
2. **串口出现画布分配失败**：确认 AP 页 / 添加设备码页日志为 `画板: 堆分配 57600 字节 (800x144 条带)`，并检查 `malloc_before` / `largest`；当前固件不应再依赖 192KB 静态缓冲。
3. **屏幕 BUSY 超时**：检查排线、供电与 `BUSY` 上拉；当前固件会先刷屏再启动热点，若 BUSY 卡死会直接跳过本次本地页面显示。
4. **已有旧 WiFi 配置**：上电会优先连路由器而非 AP，需 Erase Flash 或长按 GPIO0 重新配网。

如果串口出现 `❌ AP热点启动失败`，说明不是“已有旧WiFi配置”的问题，而是 AP 本身没有起起来。此时优先检查供电稳定性、天线，以及 Arduino IDE 中选择的板卡/Flash/分区参数是否与本文档一致。

### 设备不更新图片

- 确认 `/api/device/status` 返回的 `imageVersion` 是否与设备本地 `imgVer` **不一致**
- 确认发布后后端日志是否出现版本递增（例如 `2 -> 3`）
- 设备端会把图片版本保存在 NVS：`namespace=device key=imgVer`
- `imageVersion` 在设备端只作为云端图片同步标记，不作为必须递增的大小比较依据。如果云端重建、清库或数据库恢复导致云端标记与本地 `imgVer` 不一致，设备会按云端当前图片重新同步，并在刷新成功后把本地 `imgVer` 写回云端标记。

### SPIFFS挂载失败

- **首次使用**：这是正常现象，会自动格式化
- **持续失败**：
  1. 确认分区表已正确烧录
  2. 在Arduino IDE中：工具 -> Erase Flash -> All Flash Contents
  3. 重新编译并烧录

### 图片无法上传

- 打开浏览器开发者工具查看网络请求
- 检查设备ID是否正确
- Deep-sleep 架构下设备大多数时间离线是正常现象；设备无需在线也能“发布”成功
- 查看云服务器日志
- 检查设备是否已绑定

### NVS错误

- `nvs_open failed: NOT_FOUND` - 首次使用时正常，会自动创建命名空间
- 如果持续出现，检查Flash是否损坏

## 图像处理算法

系统支持三种图像处理算法，用户可以在Web界面中选择：

### 1. Floyd-Steinberg 抖动算法

- **特点**：误差扩散技术，适合渐变和细节丰富的图像
- **原理**：将量化误差扩散到相邻像素，产生平滑的视觉过渡
- **适用场景**：照片、渐变图像、细节丰富的图像

### 2. 梯度边界混合算法

- **特点**：基于Sobel梯度检测边界，在边界区域进行颜色混合
- **原理**：检测图像边界，在边界区域混合颜色以减少量化伪影
- **适用场景**：线条图、边界明显的图像、文字图像
- **参数**：梯度阈值（10-100，默认40），可调整边界检测敏感度

### 3. 灰阶与颜色映射算法

- **特点**：将灰度图映射到自定义颜色梯度，产生纯色块效果
- **原理**：将256级灰度均匀映射到6种E6颜色，无抖动
- **颜色梯度**：黑 -> 蓝 -> 红 -> 绿 -> 黄 -> 白（按亮度顺序）
- **适用场景**：需要纯色块效果的图像、简化图像
- **优化**：
  - 非均匀分段，确保深红色和黄色正确识别
  - 深红色范围：71-120（避免被映射为蓝色）
  - 黄色范围：181-235（避免被映射为白色）

## 功耗管理与睡眠模式

### 当前实现：Deep-sleep（推荐）

- 设备不保持常驻连接，不常驻 loop
- 唤醒后执行一次性 HTTP 拉取流程，完成后立即回到 Deep-sleep
- 默认定时唤醒间隔为 12 小时；云端可在 `/api/device/status` 响应中返回 `nextSleepSeconds`，设备持久化到 NVS 并在下一次入睡时使用，适配时钟、天气、日历等模板的不同刷新频率。动态模板在设备唤醒查询状态时按需重渲染；天气每次唤醒刷新，每日一言在按键手动唤醒时强制换一句，定时唤醒按 `Asia/Shanghai` 日期刷新，日历也按 `Asia/Shanghai` 日期刷新；固件状态查询超时默认为 30 秒。当前内置模板为时钟、天气、日历、待办、每日一言、二维码；计数器和空白页不再作为内置模板入口。
- 云端没有常驻连接可用于实时在线判断：最后上报后的 5 分钟活动窗口显示“在线”；之后显示“睡眠”。允许第一次预计自动唤醒没有上报，只有第二次预计自动唤醒再加 5 分钟上报宽限后仍无状态，才显示“离线”。例如 12 小时周期会在最后上报约 24 小时 5 分钟后判定离线。
- 墨水屏断电仍保持画面，因此无需常供电刷新
- 墨水屏 `BUSY` 等待带超时保护：初始化/上电/断电阶段默认 10 秒，显示刷新阶段默认 180 秒。超时会打印错误并退出当前显示流程，避免新板调试时因屏幕异常永久卡死。
- 每次设备查询 `/api/device/status` 时会同步上报网络、唤醒、固件版本、`esp_reset_reason()`、本地 `imgVer` 和待补报诊断。实际图片更新使用 NVS `device/updDiag` 记录 `download → verify → epd_power_on → epd_refresh → epd_power_off → nvs_commit → done` 阶段；云端设备信息区显示固件、本地/云端版本、最近刷新结果、错误阶段和诊断时间。
- 只有图片实际刷新完成后才保存本地 `imgVer`；如果下载成功但显示失败，下次唤醒会继续重试同一版本。

## 扩展功能

### 已实现功能

- ✅ WiFi AP配网模式
- ✅ **长按GPIO0重新配网功能**（长按3秒清除WiFi配置）
- ✅ **复位后自动查询云端**（V3.0.0+：复位重启后也会连接WiFi查询claimed状态）
- ✅ 设备绑定管理
- ✅ 图片发布到云端持久化（设备离线可用）
- ✅ 设备唤醒后 HTTP 拉取更新（流式写入 SPIFFS + 行缓冲刷屏）
- ✅ 本地 UI 双页共用静态帧缓冲（满屏 WiFi 配网 + 居中设备码，适配 ESP32-C3 无 PSRAM）
- ✅ 多用户支持
- ✅ 三种图像处理算法（Floyd-Steinberg抖动、梯度边界混合、灰阶颜色映射）
- ✅ 算法选择界面和实时预览
- ✅ Deep-sleep + 按键/定时唤醒（低功耗）

### 可扩展功能

1. **OTA升级**：可扩展为唤醒后通过 HTTP 检查并拉取固件更新
2. **模板按需刷新**：设备手动或定时唤醒后检查天气、日历、每日一言等动态模板并拉取最新图片
3. **批量控制**：支持同时向多个设备推送图片
4. **图片历史**：保存用户上传的图片，支持快速重新发送
5. **更多图片处理选项**：
   - 更多抖动算法（Atkinson, Burkes等）
   - 自动裁剪和对齐
   - 文字叠加
   - 二维码生成

## 许可证

基于原Waveshare项目修改，保留原作者版权信息。

## 参考资料

- [ESP32-C3技术参考手册](https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_cn.pdf)
- [合宙ESP32C3-CORE开发板](https://wiki.luatos.com/chips/esp32c3/board.html)
- [ArduinoJson文档](https://arduinojson.org/)
