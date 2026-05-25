# ESP32-C3 墨水屏云端控制系统（Deep-sleep + HTTP Pull，无 MQTT）

本项目采用 **Deep-sleep + 按键/定时唤醒 + HTTP 拉取更新** 的低功耗架构：

- 设备绝大多数时间处于 **Deep-sleep（µA 级）**
- 仅在 **GPIO0 按键** 或 **12 小时定时** 唤醒后联网
- 唤醒后通过 HTTP 查询版本并按需下载图片，刷新墨水屏后立刻回到 Deep-sleep
- **服务器端上传图片时不要求设备在线**；设备下次唤醒即可拉到最新内容
- 已保存 WiFi 但本次连接失败时会保留原配置；普通按键/定时唤醒直接回睡，下次再试。需要重新配网时长按 GPIO0。

## 系统架构（无 MQTT）

```
用户浏览器 <--> 云服务器 (Nginx + Flask + MongoDB) <--> ESP32-C3 (HTTP 客户端/Deep-sleep) <--> 墨水屏
```

### 工作流程（核心）

1. 用户在 Web 页面处理图片并点击 **“发布”**（上传到云端）
2. 云端将最新 EPD 数据持久化保存：`cloud_server/backend/data/epd/<deviceId>/latest.txt`，并递增 `devices.imageVersion`
3. 设备在按键/定时唤醒后执行一次性流程：
   - `POST /api/device/status` 获取 `claimed/imageVersion/imageUrl`
   - 若 `imageVersion > NVS(imgVer)`：`GET imageUrl` 流式下载到 SPIFFS 临时文件 → 刷新墨水屏 → 写入 NVS 新版本 → Deep-sleep
   - 若版本一致：直接 Deep-sleep
   - 若未绑定：显示设备码/配对码提示 → Deep-sleep

## 硬件要求

### ESP32-C3 模块
- **当前硬件**：ESP32-C3-WROOM-02U-N4（4MB Flash，外接天线）
- **Flash容量**：4MB（用于固件和SPIFFS存储）
- **WiFi**：2.4GHz 802.11 b/g/n

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
├── fonts.h                    # 字库头文件
├── font24.cpp                 # 24像素字体数据
├── font12.cpp                 # 12像素字体数据
├── partitions.csv             # Flash分区表
└── README.md                  # 本文件
```

## 部署步骤

### 1. 准备云服务器

#### 要求
- Linux服务器 (Ubuntu 20.04+ 推荐)
- 公网IP或域名
- Python 3.8+
- MongoDB（用于设备管理）

#### 安装MongoDB

```bash
# Ubuntu/Debian
sudo apt install mongodb

# 启动服务
sudo systemctl start mongodb
sudo systemctl enable mongodb
```

### 2. 部署云端服务

```bash
# 1. 上传代码到服务器
cd /opt
sudo git clone <your-repo> esp32-cloud
cd esp32-cloud/cloud_server

# 2. 使用Docker部署（推荐）
# 注意：使用 docker compose（无连字符），这是新版本Docker的标准命令

# 首次部署或更新代码后：
cd /opt/cloud_server  # 或你的项目路径
git pull  # 拉取最新代码
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
# 编辑 docker-compose.yml 或设置环境变量：
export MONGODB_URI="mongodb://<user>:<pass>@<host>:27017/esp32_epd?authSource=admin"
export MONGODB_DB="esp32_epd"
# 用于 status 返回 imageUrl（建议填公网域名或公网IP）
export FLASK_HOST="<public-ip-or-domain>"
export FLASK_PORT="5000"

# 4. 初始化数据库索引
python3 create_indexes.py

# 5. 启动服务
python3 app.py
```

### 3. 配置防火墙

```bash
# 开放端口
sudo ufw allow 80/tcp      # Web服务（Nginx）
sudo ufw allow 5000/tcp    # Flask后端（如果直接访问）
sudo ufw enable
```

### 4. 烧录ESP32-C3程序

#### Arduino IDE配置

1. **安装ESP32开发板支持**：
   - 文件 -> 首选项 -> 附加开发板管理器网址：
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - 工具 -> 开发板 -> 开发板管理器 -> 搜索 "esp32" -> 安装

2. **选择开发板**：
   - 工具 -> 开发板 -> ESP32 Arduino -> **ESP32C3 Dev Module**
   - 工具 -> Partition Scheme -> **Custom Partition Table**
   - 工具 -> Flash Size -> **4MB (32Mb)**
   - 工具 -> Flash Frequency -> **80MHz**
   - 工具 -> Flash Mode -> **DIO**
   - 工具 -> Upload Speed -> **921600**

3. **安装依赖库**：
   - ArduinoJson (by Benoit Blanchon)

4. **说明**：
   - 设备端已改为 `http_update.h` 的 **HTTP 拉取**模式，不再依赖旧的 MQTT 长连接链路。
   - 服务器地址/端口在 `http_update.h` 中通过 `CLOUD_API_HOST/CLOUD_API_PORT` 配置。

5. **分区表配置**：

项目根目录的 `partitions.csv` 已配置好：
```
nvs,      data, nvs,     0x9000,  0x5000,
phy_init, data, phy,     0xe000,  0x2000,
factory,  app,  factory, 0x10000, 0x150000,
spiffs,   data, spiffs,  0x250000, 0x1B0000,
```

**注意**：确保 Arduino IDE 中选择了 **Custom Partition Table**，这样会自动使用项目根目录的 `partitions.csv`。

6. **编译上传**：
   - 工具 -> Erase Flash -> **All Flash Contents**（首次烧录建议完全擦除）
   - 点击上传

### 5. WiFi配网

设备支持三种WiFi连接方式：

#### 方式1：AP配网模式（首次使用或WiFi未配置）

1. 设备启动后，如果未配置WiFi，会自动进入AP模式
2. 设备会创建一个WiFi热点：`EPD-XXXXXX`（XXXXXX为设备码）
3. 设备进入 AP 配网模式后，会同步在墨水屏上显示该设备码，便于网页绑定时直接核对
4. 用手机/电脑连接此热点（默认无密码；如你自行修改了热点配置，请以实际为准）
5. 打开浏览器访问配网页面（设备热点的网关地址，通常为默认网关地址）
6. 输入 WiFi 名称与密码（如有），点击连接
7. 设备会自动重启并连接WiFi

#### 方式2：长按GPIO0重新配网（推荐）

如果设备已配置WiFi但需要更换WiFi网络，可以使用此方法：

1. **从Deep-sleep唤醒**：短按GPIO0按键唤醒设备（或等待定时唤醒）
2. **长按进入配网**：在设备唤醒后，**继续按住GPIO0按键3秒**（不要松开）
3. **自动进入AP模式**：设备检测到长按后会：
   - 清除已保存的WiFi配置
   - 自动进入AP配网模式
   - 创建WiFi热点 `EPD-XXXXXX`
   - 在墨水屏上显示当前设备码
4. **完成配网**：按照方式1的步骤4-7完成WiFi配置

**注意**：
- 长按检测在设备启动时立即进行，建议在设备唤醒后立即按住按键
- 如果设备从Deep-sleep被按键唤醒，可以在唤醒后继续按住按键3秒
- 如果设备是定时唤醒或复位唤醒，需要在上电后立即按住GPIO0按键3秒

#### 方式3：代码配置（开发测试）

在 `wifi_config.h` 中可以设置默认WiFi（仅用于开发测试）。

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
   - 打开浏览器访问：`http://你的服务器IP` 或 `http://your-domain.com`
   - 首次访问需要注册/登录

3. **添加设备**：
   - 登录后进入设备管理页面
   - 点击"添加设备"
   - 输入ESP32显示的设备码（例如：`B6DA20`）
   - 设备会自动绑定

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

## Flash存储说明

### 分区表配置

项目使用自定义分区表（`partitions.csv`）：

| 分区 | 类型 | 偏移 | 大小 | 说明 |
|------|------|------|------|------|
| nvs | DATA | 0x9000 | 20KB | NVS存储（WiFi配置、设备绑定状态） |
| phy_init | DATA | 0xE000 | 8KB | PHY初始化数据 |
| factory | APP | 0x10000 | 1344KB | 应用程序 |
| spiffs | DATA | 0x250000 | 1728KB | SPIFFS文件系统（图片缓存） |

### SPIFFS使用

- **用途**：存储临时图片数据（避免内存不足；下载过程流式写入）
- **自动格式化**：首次使用时自动格式化
- **文件路径**：`/temp_image.bin`

## 设备码说明

设备码基于MAC地址生成，支持三种模式（在 `http_update.h` 中配置）：

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

**重要**：从V3.0.0开始，设备在复位重启后会主动连接WiFi查询云端状态。如果云端显示 `claimed=false`，设备会在屏幕上显示配置码。

如果串口出现 `❌ AP热点启动失败`，说明不是“已有旧WiFi配置”的问题，而是 AP 本身没有起起来。此时优先检查供电稳定性、天线，以及 Arduino IDE 中选择的板卡/Flash/分区参数是否与本文档一致。

### 设备不更新图片

- 确认 `/api/device/status` 返回的 `imageVersion` 是否 **大于** 设备本地 `imgVer`
- 确认发布后后端日志是否出现版本递增（例如 `2 -> 3`）
- 设备端会把图片版本保存在 NVS：`namespace=device key=imgVer`

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
- 墨水屏断电仍保持画面，因此无需常供电刷新
- 墨水屏 `BUSY` 等待带超时保护：初始化/上电/断电阶段默认 10 秒，显示刷新阶段默认 180 秒。超时会打印错误并退出当前显示流程，避免新板调试时因屏幕异常永久卡死。

## 扩展功能

### 已实现功能

- ✅ WiFi AP配网模式
- ✅ **长按GPIO0重新配网功能**（长按3秒清除WiFi配置）
- ✅ **复位后自动查询云端**（V3.0.0+：复位重启后也会连接WiFi查询claimed状态）
- ✅ 设备绑定管理
- ✅ 图片发布到云端持久化（设备离线可用）
- ✅ 设备唤醒后 HTTP 拉取更新（流式写入 SPIFFS）
- ✅ 多用户支持
- ✅ 三种图像处理算法（Floyd-Steinberg抖动、梯度边界混合、灰阶颜色映射）
- ✅ 算法选择界面和实时预览
- ✅ Deep-sleep + 按键/定时唤醒（低功耗）

### 可扩展功能

1. **OTA升级**：可扩展为唤醒后通过 HTTP 检查并拉取固件更新
2. **定时任务**：支持定时推送图片到设备
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
