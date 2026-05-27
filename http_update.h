/**
 ******************************************************************************
 * @file    http_update.h
 * @author  Modified for Deep-sleep + HTTP Pull Architecture
 * @version V3.0.0
 * @date    2026-01-24
 * @brief   Deep-sleep + HTTP 拉取更新架构
 *          设备绝大多数时间处于 Deep-sleep（µA 级），
 *          只有按键或定时醒来后才联网拉取更新图片
 ******************************************************************************
 */

#ifndef HTTP_UPDATE_H
#define HTTP_UPDATE_H

#include <WiFi.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <FS.h>
#include <stdlib.h>
#include "esp_mac.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_sleep.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "buff.h"
#include "epd.h"
#include "EPD_7in3e.h"
#include "GUI_Paint.h"
#include "fonts.h"
#include "provisioning_fonts.h"
#include "logo_phenosolar.h"
#include "qrcode.h"

/* ============================================================================
 *                               配置参数
 * ============================================================================ */

/* 云端API配置 */
#define CLOUD_API_HOST "8.135.238.216"
#define CLOUD_API_PORT 8080  // 经 Nginx 代理访问后端
#define CLOUD_API_TIMEOUT_MS 10000  // HTTP请求超时时间（10秒）
#define CLOUD_DOWNLOAD_TIMEOUT_MS 60000  // 下载超时时间（60秒）

/* 设备ID配置 */
// 选择设备ID生成方式：
// 0 = 使用完整MAC地址 (12位，例如: 112233445566)
// 1 = 仅使用MAC地址前6位 (例如: 112233)
// 2 = 仅使用MAC地址后6位 (例如: 445566)
#ifndef DEVICE_ID_MODE
#define DEVICE_ID_MODE 2
#endif

/* Deep-sleep 配置 */
#define WAKEUP_GPIO GPIO_NUM_0  // GPIO0 按键唤醒（按键接地，低电平唤醒）
#define DEEP_SLEEP_INTERVAL_HOURS 12  // 定时唤醒间隔（小时）
#define DEEP_SLEEP_INTERVAL_US (DEEP_SLEEP_INTERVAL_HOURS * 60ULL * 60ULL * 1000000ULL)
// 避免“按键仍按下/引脚为低”导致刚入睡就立刻被再次唤醒
#define WAKEUP_RELEASE_WAIT_MS 2500

/* Flash临时存储配置 */
#define FLASH_TEMP_FILE "/temp_image.bin"
// 7.3" E6: 800x480，每像素 4bit（a~p 编码为单字符），总字符数固定
#define EPD_EXPECTED_CHARS 384000

/* NVS 配置 */
#define PREF_NAMESPACE "device"
#define PREF_KEY_CLAIMED "claimed"
#define PREF_KEY_IMG_VER "imgVer"

/* 本地 UI 页帧缓冲：按需 malloc，画完 free（去掉 192KB 静态 BSS 以腾出 SRAM 给 WiFi） */
#define EPD_PANEL_WIDTH 800
#define EPD_PANEL_HEIGHT 480

/* AP 配网页 / 设备码页统一使用 480x240 局部画布后居中 DisplayPart */
#define PROVISIONING_CANVAS_WIDTH 480
#define PROVISIONING_CANVAS_HEIGHT 240
#define PROVISIONING_CANVAS_PACKED_WIDTH ((PROVISIONING_CANVAS_WIDTH + 1) / 2)
#define PROVISIONING_CANVAS_SIZE (PROVISIONING_CANVAS_PACKED_WIDTH * PROVISIONING_CANVAS_HEIGHT)
#define PROVISIONING_QR_VERSION 5
#define PROVISIONING_QR_TARGET_SIZE 220

static UBYTE* g_epdUiFrameHeap = nullptr;
static size_t g_epdUiFrameCapacity = 0;

void releaseEpdUiFrame();

static void logUiFrameHeap(const char* hypothesisId, const char* message, size_t needBytes) {
    Serial.printf(
        "{\"sessionId\":\"958611\",\"hypothesisId\":\"%s\",\"location\":\"epdUiFrame\","
        "\"message\":\"%s\",\"data\":{\"need\":%u,\"free\":%u,\"largest\":%u},"
        "\"timestamp\":%lu}\n",
        hypothesisId, message, (unsigned)needBytes, (unsigned)ESP.getFreeHeap(),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        (unsigned long)millis());
}

static UBYTE* acquireEpdUiFrame(size_t needBytes) {
    if (g_epdUiFrameHeap != nullptr && g_epdUiFrameCapacity >= needBytes) {
        return g_epdUiFrameHeap;
    }
    releaseEpdUiFrame();

    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    logUiFrameHeap("H8", "malloc_before", needBytes);
    if (largest < needBytes) {
        logUiFrameHeap("H8", "malloc_failed", needBytes);
        return nullptr;
    }

    g_epdUiFrameHeap = (UBYTE*)heap_caps_malloc(needBytes, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (g_epdUiFrameHeap == nullptr) {
        g_epdUiFrameHeap = (UBYTE*)malloc(needBytes);
    }
    if (g_epdUiFrameHeap == nullptr) {
        logUiFrameHeap("H8", "malloc_failed", needBytes);
        g_epdUiFrameCapacity = 0;
        return nullptr;
    }
    g_epdUiFrameCapacity = needBytes;
    logUiFrameHeap("H8", "malloc_ok", needBytes);
    return g_epdUiFrameHeap;
}

void releaseEpdUiFrame() {
    if (g_epdUiFrameHeap == nullptr) {
        return;
    }
    heap_caps_free(g_epdUiFrameHeap);
    g_epdUiFrameHeap = nullptr;
    g_epdUiFrameCapacity = 0;
    logUiFrameHeap("H8", "free_done", 0);
}

/* ============================================================================
 *                               全局变量
 * ============================================================================ */

extern Preferences preferences;  // NVS持久化存储（在Loader_esp32wf.ino中定义）

String deviceId;
bool deviceClaimed = false;
int64_t localImageVersion = 0;

/* Flash临时文件 */
File flashTempFile;
bool flashTempFileOpen = false;
int flashTempFileSize = 0;

/* ============================================================================
 *                          本次唤醒的“一次性”状态机
 * 目标：
 * - 每次唤醒只做一次 status 检查（避免 loop 重复检查）
 * - 仅当需要更新时才在 loop 中执行下载+刷新
 * - Deep-sleep 进入流程幂等化（避免异常情况下重复执行）
 * ============================================================================ */

static bool g_statusChecked = false;          // 本次唤醒是否已完成 status 判定
static bool g_updateNeeded = false;           // 本次唤醒是否需要更新
static bool g_updateAttempted = false;        // 本次唤醒是否已尝试更新（避免重复下载）
static bool g_shouldEnterDeepSleep = false;   // 本次唤醒是否应立即回睡
static bool g_deepSleepRequested = false;     // 防止重复执行 deep-sleep 进入流程
static bool g_displayHardwareReady = false;   // 墨水屏底层是否已完成初始化
static int64_t g_targetImageVersion = 0;      // 需要更新到的版本（毫秒时间戳）
static String g_targetImageUrl = "";          // 需要下载的 URL

/* ============================================================================
 *                            前置声明
 * ============================================================================ */

void ensureDisplayHardwareReady();

/* ============================================================================
 *                            辅助函数：AP 配网页二维码
 * ============================================================================ */

static int getCFontTextWidth(const String& text, const cFONT* font);
static int getCenteredTextX(int areaX, int areaWidth, const String& text, const cFONT* font);
static bool drawProvisioningQrToPaintEx(const String& payload, int centerX, int centerY, int targetSize,
                                        int canvasWidth, int canvasHeight, UWORD darkColor);
static void drawBitmapMask(int x0, int y0, int width, int height, const uint8_t* bitmap, UWORD color);

String getProvisioningApPassword();

static bool drawProvisioningQrToPaintEx(const String& payload, int centerX, int centerY, int targetSize,
                                        int canvasWidth, int canvasHeight, UWORD darkColor) {
    if (payload.length() == 0) {
        return false;
    }

    QRCode qrcode;
    uint8_t qrData[qrcode_getBufferSize(PROVISIONING_QR_VERSION)];
    int8_t initResult = qrcode_initText(&qrcode, qrData, PROVISIONING_QR_VERSION, ECC_MEDIUM, payload.c_str());
    if (initResult < 0 || qrcode.size <= 0) {
        Serial.println("❌ 二维码生成失败");
        return false;
    }

    const int modules = qrcode.size;
    int scale = targetSize / modules;
    if (scale < 1) {
        scale = 1;
    }

    const int drawSize = modules * scale;
    const int x0 = centerX - drawSize / 2;
    const int y0 = centerY - drawSize / 2;

    for (int y = 0; y < modules; y++) {
        for (int x = 0; x < modules; x++) {
            UWORD color = qrcode_getModule(&qrcode, x, y) ? darkColor : EPD_7IN3E_WHITE;
            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++) {
                    int px = x0 + x * scale + dx;
                    int py = y0 + y * scale + dy;
                    if (px >= 0 && px < canvasWidth &&
                        py >= 0 && py < canvasHeight) {
                        Paint_SetPixel(px, py, color);
                    }
                }
            }
        }
    }

    return true;
}

static bool drawProvisioningQrToPaint(const String& payload, int centerX, int centerY, int targetSize) {
    return drawProvisioningQrToPaintEx(payload, centerX, centerY, targetSize,
                                       PROVISIONING_CANVAS_WIDTH, PROVISIONING_CANVAS_HEIGHT,
                                       EPD_7IN3E_BLACK);
}

static void drawBitmapMask(int x0, int y0, int width, int height, const uint8_t* bitmap, UWORD color) {
    if (bitmap == NULL || width <= 0 || height <= 0) {
        return;
    }

    const int rowBytes = (width + 7) / 8;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const uint8_t byte = bitmap[y * rowBytes + (x / 8)];
            if (byte & (0x80 >> (x % 8))) {
                Paint_SetPixel(x0 + x, y0 + y, color);
            }
        }
    }
}

static int getCFontTextWidth(const String& text, const cFONT* font) {
    if (font == NULL) {
        return 0;
    }

    int width = 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(text.c_str());
    while (*p != '\0') {
        if (*p < 0x80) {
            width += font->ASCII_Width;
            p += 1;
        } else {
            width += font->Width;
            if ((*p & 0xF0) == 0xE0 && p[1] != '\0' && p[2] != '\0') {
                p += 3;
            } else if ((*p & 0xE0) == 0xC0 && p[1] != '\0') {
                p += 2;
            } else {
                p += 1;
            }
        }
    }
    return width;
}

static int getCenteredTextX(int areaX, int areaWidth, const String& text, const cFONT* font) {
    int textWidth = getCFontTextWidth(text, font);
    int x = areaX + (areaWidth - textWidth) / 2;
    return x < areaX ? areaX : x;
}

static bool renderProvisioningCompactScreen(UBYTE* imageBuffer, const String& apSSID,
                                            const String& wifiQrPayload,
                                            const String& apPassword) {
    (void)apPassword;
    const int paintWidth = PROVISIONING_CANVAS_WIDTH;
    const int paintHeight = PROVISIONING_CANVAS_HEIGHT;
    const UWORD fg = EPD_7IN3E_BLACK;
    const UWORD bg = EPD_7IN3E_WHITE;
    const int headerHeight = 42;
    const int dividerX = 239;
    const int qrSize = 126;
    const int qrCenterX = 120;
    const int qrCenterY = 109;
    const int rightAreaX = 242;
    const int rightAreaW = paintWidth - rightAreaX - 8;
    const int logoX = 356;
    const int logoY = 180;

    Paint_NewImage(imageBuffer, paintWidth, paintHeight, 0, EPD_7IN3E_WHITE);
    Paint_SetScale(6);
    Paint_SelectImage(imageBuffer);
    Paint_Clear(EPD_7IN3E_WHITE);

    cFONT* titleFont = provisioningTitleFont();
    cFONT* hintFont = provisioningHintFont();
    cFONT* labelFont = provisioningLabelFont();
    sFONT* valueFont = provisioningValueFont();

    Paint_DrawString_CN(getCenteredTextX(0, paintWidth, "钙钛矿墨水屏会议牌", titleFont),
                        2, "钙钛矿墨水屏会议牌", titleFont, EPD_7IN3E_BLUE, bg);

    if (!drawProvisioningQrToPaintEx(wifiQrPayload, qrCenterX, qrCenterY, qrSize,
                                     paintWidth, paintHeight, EPD_7IN3E_BLACK)) {
        return false;
    }

    Paint_DrawRectangle(dividerX, headerHeight, dividerX + 2, paintHeight - 1,
                        fg, DOT_PIXEL_1X1, DRAW_FILL_FULL);

    Paint_DrawString_CN(getCenteredTextX(rightAreaX, rightAreaW, "扫二维码或连接热点", hintFont),
                        96, "扫二维码或连接热点", hintFont, EPD_7IN3E_RED, bg);
    Paint_DrawString_CN(getCenteredTextX(rightAreaX, rightAreaW, "配置设备WiFi", hintFont),
                        132, "配置设备WiFi", hintFont, EPD_7IN3E_RED, bg);

    const int labelX = 18;
    const int valueX = 82;
    const int dynamicValueX = valueX + 20;
    const int ssidRowY = 178;
    const int ipRowY = 206;
    Paint_DrawString_CN(labelX, ssidRowY, "热点", labelFont, fg, bg);
    Paint_DrawString_EN(dynamicValueX, ssidRowY + 2, apSSID.c_str(), valueFont,
                        EPD_7IN3E_WHITE, EPD_7IN3E_BLUE);

    Paint_DrawString_EN(labelX, ipRowY + 3, "ip", valueFont,
                        EPD_7IN3E_WHITE, EPD_7IN3E_BLACK);
    Paint_DrawString_CN(labelX + 28, ipRowY, "地址", labelFont, fg, bg);
    Paint_DrawString_EN(dynamicValueX, ipRowY + 2, "192.168.4.1", valueFont,
                        EPD_7IN3E_WHITE, EPD_7IN3E_BLUE);

    drawBitmapMask(logoX, logoY, PHENOSOLAR_LOGO_WIDTH, PHENOSOLAR_LOGO_HEIGHT,
                   phenosolar_logo_black_mask, EPD_7IN3E_BLACK);
    drawBitmapMask(logoX, logoY, PHENOSOLAR_LOGO_WIDTH, PHENOSOLAR_LOGO_HEIGHT,
                   phenosolar_logo_green_mask, EPD_7IN3E_GREEN);

    return true;
}

String getCloudPortalUrl() {
    return "http://" + String(CLOUD_API_HOST) + ":" + String(CLOUD_API_PORT) + "/";
}

bool displayProvisioningScreen(const String& apSSID, const String& deviceCode, const String& wifiQrPayload) {
    (void)deviceCode;

    if (apSSID.length() == 0 || wifiQrPayload.length() == 0) {
        Serial.println("⚠️  配网页二维码信息不完整，跳过二维码显示");
        return false;
    }

    Serial.println("📱 开始显示AP配网页二维码...");
    Serial.printf("   SSID: %s\n", apSSID.c_str());
    Serial.printf("   画板: 堆分配 %u 字节 (480x240)\n", (unsigned)PROVISIONING_CANVAS_SIZE);

    UBYTE* imageBuffer = acquireEpdUiFrame(PROVISIONING_CANVAS_SIZE);
    if (imageBuffer == nullptr) {
        Serial.println("❌ 配网页画布分配失败（请查看 malloc_before / largest 日志）");
        return false;
    }

    dbgSetEpdActive(true);

    if (EPD_dispIndex < 0 || EPD_dispIndex >= (sizeof(EPD_dispMass) / sizeof(EPD_dispMass[0]))) {
        EPD_dispIndex = 0;
    }

    ensureDisplayHardwareReady();
    EPD_7IN3E_ClearBusyTimeout();
    EPD_dispInit();
    if (EPD_7IN3E_LastBusyTimeout()) {
        dbgSetEpdActive(false);
        releaseEpdUiFrame();
        Serial.println("❌ 配网页二维码显示初始化失败，请检查BUSY线、屏幕供电和排线");
        return false;
    }

    const bool rendered = renderProvisioningCompactScreen(imageBuffer, apSSID, wifiQrPayload,
                                                          getProvisioningApPassword());
    if (rendered) {
        const UWORD xstart = (EPD_PANEL_WIDTH - PROVISIONING_CANVAS_WIDTH) / 2;
        const UWORD ystart = (EPD_PANEL_HEIGHT - PROVISIONING_CANVAS_HEIGHT) / 2;
        EPD_7IN3E_ClearBusyTimeout();
        EPD_7IN3E_DisplayPart(imageBuffer, xstart, ystart,
                              PROVISIONING_CANVAS_WIDTH, PROVISIONING_CANVAS_HEIGHT);
    }
    dbgSetEpdActive(false);
    releaseEpdUiFrame();

    if (!rendered) {
        Serial.println("❌ 配网页二维码显示未正常完成，请检查BUSY线、屏幕供电和排线");
        return false;
    }

    Serial.println("✅ AP配网页二维码已显示在屏幕上（画布已释放）");
    return true;
}

/* ============================================================================
 *                            辅助函数：显示硬件初始化
 * ============================================================================ */

void ensureDisplayHardwareReady() {
    if (g_displayHardwareReady) {
        return;
    }

    Serial.println("🛠️ 初始化显示硬件...");
    DEV_Module_Init();
    EPD_initSPI();
    g_displayHardwareReady = true;
    Serial.println("✅ 显示硬件初始化完成");
}

/* ============================================================================
 *                            辅助函数：设备ID
 * ============================================================================ */

/**
 * 获取设备ID（基于MAC地址）
 */
inline String getDeviceIdFromMac() {
    uint8_t mac[6] = {0};

    esp_err_t ret = esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);
    if (ret != ESP_OK) {
        Serial.println("⚠️  displayDeviceCode: esp_read_mac 失败，回退到 WiFi.macAddress()");
        WiFi.macAddress(mac);
    }

    if (mac[3] == 0 && mac[4] == 0 && mac[5] == 0) {
        ret = esp_wifi_get_mac(WIFI_IF_STA, mac);
        if (ret != ESP_OK || (mac[3] == 0 && mac[4] == 0 && mac[5] == 0)) {
            ret = esp_wifi_get_mac(WIFI_IF_AP, mac);
            if (ret == ESP_OK) {
                Serial.println("✅ displayDeviceCode: 使用 WIFI_IF_AP MAC");
            }
        } else {
            Serial.println("✅ displayDeviceCode: 使用 WIFI_IF_STA MAC");
        }
    }

    Serial.printf("🔍 displayDeviceCode MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    char buf[32];
    
    #if DEVICE_ID_MODE == 1
        snprintf(buf, sizeof(buf), "%02X%02X%02X", mac[0], mac[1], mac[2]);
    #elif DEVICE_ID_MODE == 2
        snprintf(buf, sizeof(buf), "%02X%02X%02X", mac[3], mac[4], mac[5]);
    #else
        snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    #endif
    
    return String(buf);
}

/* ============================================================================
 *                            辅助函数：NVS 存储
 * ============================================================================ */

/**
 * 读取本地持久化的claimed状态
 */
bool loadClaimedStatus() {
    if (!preferences.begin(PREF_NAMESPACE, true)) {
        preferences.end();
        Serial.println("📖 读取本地绑定状态: 未绑定（首次使用）");
        return false;
    }
    bool claimed = preferences.getBool(PREF_KEY_CLAIMED, false);
    preferences.end();
    Serial.printf("📖 读取本地绑定状态: %s\n", claimed ? "已绑定" : "未绑定");
    return claimed;
}

/**
 * 保存本地持久化的claimed状态
 */
void saveClaimedStatus(bool claimed) {
    if (!preferences.begin(PREF_NAMESPACE, false)) {
        Serial.println("⚠️  NVS命名空间打开失败，无法保存绑定状态");
        return;
    }
    preferences.putBool(PREF_KEY_CLAIMED, claimed);
    preferences.end();
    Serial.printf("💾 保存本地绑定状态: %s\n", claimed ? "已绑定" : "未绑定");
}

/**
 * 读取本地图片版本号（毫秒时间戳）
 */
int64_t loadImageVersion() {
    if (!preferences.begin(PREF_NAMESPACE, true)) {
        preferences.end();
        return 0;
    }
    int64_t version = preferences.getLong64(PREF_KEY_IMG_VER, 0);
    preferences.end();
    Serial.printf("📖 读取本地图片版本: %lld\n", version);
    return version;
}

/**
 * 保存本地图片版本号（毫秒时间戳）
 */
void saveImageVersion(int64_t version) {
    if (!preferences.begin(PREF_NAMESPACE, false)) {
        Serial.println("⚠️  NVS命名空间打开失败，无法保存图片版本");
        return;
    }
    preferences.putLong64(PREF_KEY_IMG_VER, version);
    preferences.end();
    Serial.printf("💾 保存本地图片版本: %lld\n", version);
}

/* ============================================================================
 *                            辅助函数：Flash 存储
 * ============================================================================ */

/**
 * 初始化Flash存储（SPIFFS）
 */
bool initFlashStorage() {
    Serial.println("📁 初始化SPIFFS文件系统...");
    
    if (!SPIFFS.begin(false)) {
        Serial.println("⚠️  SPIFFS挂载失败，尝试格式化...");
        if (!SPIFFS.format()) {
            Serial.println("❌ SPIFFS格式化失败");
            return false;
        }
        if (!SPIFFS.begin(false)) {
            Serial.println("❌ SPIFFS重新挂载失败");
            return false;
        }
    }
    
    Serial.println("✅ SPIFFS初始化成功");
    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();
    Serial.printf("   总大小: %.2f KB, 已使用: %.2f KB, 可用: %.2f KB\n", 
                  totalBytes / 1024.0, usedBytes / 1024.0, (totalBytes - usedBytes) / 1024.0);
    
    // 清除旧的临时文件
    if (SPIFFS.exists(FLASH_TEMP_FILE)) {
        SPIFFS.remove(FLASH_TEMP_FILE);
        Serial.println("🗑️  已清除旧的临时文件");
    }
    
    flashTempFileOpen = false;
    flashTempFileSize = 0;
    return true;
}

/**
 * 关闭Flash临时文件
 */
void closeFlashTempFile() {
    if (flashTempFileOpen && flashTempFile) {
        flashTempFile.close();
        flashTempFileOpen = false;
        Serial.printf("📁 Flash文件已关闭，总大小: %d 字节\n", flashTempFileSize);
    }
}

/**
 * 清除Flash临时文件
 */
void clearFlashTempFile() {
    closeFlashTempFile();
    if (SPIFFS.exists(FLASH_TEMP_FILE)) {
        SPIFFS.remove(FLASH_TEMP_FILE);
        Serial.println("🗑️  Flash临时文件已清除");
    }
    flashTempFileSize = 0;
}

/* ============================================================================
 *                            辅助函数：显示设备码
 * ============================================================================ */

/**
 * 在屏幕上显示设备码（使用大号数字）
 */
void displayDeviceCode() {
    if (deviceId.length() == 0) {
        deviceId = getDeviceIdFromMac();
        Serial.printf("ℹ️  设备码未预先生成，现按当前MAC计算: %s\n", deviceId.c_str());
    }

    Serial.println("📱 开始显示设备码...");
    Serial.print("⭐ 设备码: ");
    Serial.println(deviceId);
    String portalUrl = getCloudPortalUrl();
    String portalLabel = String(CLOUD_API_HOST) + ":" + String(CLOUD_API_PORT);
    Serial.printf("🌐 云端配置页: %s\n", portalUrl.c_str());
    
    // 默认使用 7.3" E6 屏
    if (EPD_dispIndex < 0 || EPD_dispIndex >= (sizeof(EPD_dispMass) / sizeof(EPD_dispMass[0]))) {
        EPD_dispIndex = 0;
    }
    
    ensureDisplayHardwareReady();
    EPD_7IN3E_ClearBusyTimeout();
    EPD_dispInit();
    if (EPD_7IN3E_LastBusyTimeout()) {
        Serial.println("❌ 设备码显示初始化失败，请检查BUSY线、屏幕供电和排线");
        return;
    }

    String code = deviceId;
    const int paintWidth = PROVISIONING_CANVAS_WIDTH;
    const int paintHeight = PROVISIONING_CANVAS_HEIGHT;

    UBYTE* imageBuffer = acquireEpdUiFrame(PROVISIONING_CANVAS_SIZE);
    if (imageBuffer == nullptr) {
        Serial.println("❌ 设备码画布分配失败");
        return;
    }

    Serial.printf("   画板: 堆分配 %dx%d (%u 字节)\n", paintWidth, paintHeight,
                  (unsigned)PROVISIONING_CANVAS_SIZE);

    Paint_NewImage(imageBuffer, paintWidth, paintHeight, 0, EPD_7IN3E_WHITE);
    Paint_SetScale(6);
    Paint_SelectImage(imageBuffer);
    Paint_Clear(EPD_7IN3E_WHITE);

    if (!drawProvisioningQrToPaint(portalUrl, 118, paintHeight / 2, 188)) {
        Serial.println("❌ 云端网页二维码生成失败");
        releaseEpdUiFrame();
        return;
    }

    // Paint_DrawString_EN 内部前景/背景参数顺序与声明相反，这里传 WHITE/BLACK 才能得到白底黑字。
    Paint_DrawString_EN(250, 40, "Scan QR to open web page", &Font12, EPD_7IN3E_WHITE, EPD_7IN3E_BLACK);
    Paint_DrawString_EN(250, 74, "Device Code", &Font12, EPD_7IN3E_WHITE, EPD_7IN3E_BLACK);

    int fontScale = 2;
    int charWidth = Font24.Width * fontScale;
    int charHeight = Font24.Height * fontScale;
    int textWidth = code.length() * charWidth;
    int startX = 250;
    int startY = 96;
    if (startX + textWidth > paintWidth - 12) {
        startX = paintWidth - textWidth - 12;
        if (startX < 220) {
            startX = 220;
        }
    }

    const char* pStr = code.c_str();
    int charX = startX;
    int charY = startY;

    while (*pStr != '\0') {
        char c = *pStr;
        uint32_t Char_Offset = (c - ' ') * Font24.Height * (Font24.Width / 8 + (Font24.Width % 8 ? 1 : 0));
        const unsigned char *ptr = &Font24.table[Char_Offset];

        for (int Page = 0; Page < Font24.Height; Page++) {
            for (int Column = 0; Column < Font24.Width; Column++) {
                bool pixelOn = (*ptr & (0x80 >> (Column % 8))) != 0;

                for (int sy = 0; sy < fontScale; sy++) {
                    for (int sx = 0; sx < fontScale; sx++) {
                        int px = charX + Column * fontScale + sx;
                        int py = charY + Page * fontScale + sy;
                        if (px < paintWidth && py < paintHeight) {
                            Paint_SetPixel(px, py, pixelOn ? EPD_7IN3E_BLUE : EPD_7IN3E_WHITE);
                        }
                    }
                }

                if (Column % 8 == 7) ptr++;
            }
            if (Font24.Width % 8 != 0) ptr++;
        }

        charX += charWidth;
        pStr++;
    }

    Paint_DrawString_EN(250, 188, portalLabel.c_str(), &Font12, EPD_7IN3E_WHITE, EPD_7IN3E_BLACK);
    
    const UWORD xstart = (EPD_PANEL_WIDTH - paintWidth) / 2;
    const UWORD ystart = (EPD_PANEL_HEIGHT - paintHeight) / 2;

    EPD_7IN3E_ClearBusyTimeout();
    EPD_7IN3E_DisplayPart(imageBuffer, xstart, ystart, paintWidth, paintHeight);

    releaseEpdUiFrame();

    if (EPD_7IN3E_LastBusyTimeout()) {
        Serial.println("❌ 设备码显示未正常完成，请检查BUSY线、屏幕供电和排线");
    } else {
        Serial.println("✅ 设备码已显示在屏幕上");
    }
}

/* ============================================================================
 *                            云端API调用
 * ============================================================================ */

/**
 * 设备状态响应结构
 */
struct DeviceStatusResponse {
    bool success;
    bool claimed;
    int64_t imageVersion;
    String imageUrl;
    String error;
};

/**
 * 向云端查询设备状态
 */
DeviceStatusResponse queryDeviceStatus() {
    DeviceStatusResponse result = {false, false, 0, "", ""};
    
    if (WiFi.status() != WL_CONNECTED) {
        result.error = "WiFi未连接";
        return result;
    }
    
    HTTPClient http;
    String url = "http://" + String(CLOUD_API_HOST) + ":" + String(CLOUD_API_PORT) + "/api/device/status";
    
    Serial.printf("📡 查询设备状态: %s\n", url.c_str());
    
    http.begin(url);
    http.setTimeout(CLOUD_API_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    
    esp_sleep_wakeup_cause_t wakeupCause = esp_sleep_get_wakeup_cause();
    const char* wakeType = "other";
    const char* wakeCauseText = "OTHER";
    switch (wakeupCause) {
        case ESP_SLEEP_WAKEUP_TIMER:
            wakeType = "auto";
            wakeCauseText = "TIMER";
            break;
        case ESP_SLEEP_WAKEUP_GPIO:
        case ESP_SLEEP_WAKEUP_EXT0:
        case ESP_SLEEP_WAKEUP_EXT1:
            wakeType = "manual";
            wakeCauseText = "GPIO";
            break;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
            wakeType = "reset";
            wakeCauseText = "POWERON_OR_RESET";
            break;
        default:
            wakeType = "other";
            wakeCauseText = "OTHER";
            break;
    }

    StaticJsonDocument<768> doc;
    doc["deviceId"] = deviceId;
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    doc["uptime_ms"] = (uint32_t)millis();
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["wakeType"] = wakeType;
    doc["wakeCause"] = wakeCauseText;
    String requestBody;
    serializeJson(doc, requestBody);
    Serial.printf("   上报状态: ip=%s, rssi=%d dBm, uptime=%lu ms, freeHeap=%lu, wakeType=%s, wakeCause=%s\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.RSSI(),
                  (unsigned long)millis(),
                  (unsigned long)ESP.getFreeHeap(),
                  wakeType,
                  wakeCauseText);
    
    int httpCode = http.POST(requestBody);
    
    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
        String response = http.getString();
        Serial.printf("✅ 云端响应: %s\n", response.c_str());
        
        StaticJsonDocument<1024> respDoc;
        DeserializationError error = deserializeJson(respDoc, response);
        
        if (!error) {
            result.success = true;
            result.claimed = respDoc["claimed"].as<bool>();
            
            if (respDoc["imageVersion"].is<long long>()) {
                result.imageVersion = respDoc["imageVersion"].as<long long>();
            } else if (respDoc["imageVersion"].is<int>()) {
                // 兼容旧版本（int 类型）
                result.imageVersion = respDoc["imageVersion"].as<int>();
            }
            
            if (respDoc["imageUrl"].is<String>()) {
                result.imageUrl = respDoc["imageUrl"].as<String>();
            }
            
            Serial.printf("   绑定状态: %s\n", result.claimed ? "已绑定" : "未绑定");
            Serial.printf("   图片版本: %lld\n", result.imageVersion);
            if (result.imageUrl.length() > 0) {
                Serial.printf("   图片URL: %s\n", result.imageUrl.c_str());
            }
        } else {
            result.error = "JSON解析失败";
            Serial.printf("❌ JSON解析失败: %s\n", error.c_str());
        }
    } else {
        result.error = "HTTP错误: " + String(httpCode);
        Serial.printf("❌ HTTP错误: %d\n", httpCode);
        if (httpCode < 0) {
            Serial.printf("   错误详情: %s\n", http.errorToString(httpCode).c_str());
        }
    }
    
    http.end();
    return result;
}

/**
 * 流式下载图片数据到SPIFFS（不占用大量RAM）
 * @param imageUrl 图片下载URL
 * @return 下载是否成功
 */
bool downloadImageToFlash(const String& imageUrl) {
    Serial.println("\n========== 开始下载图片 ==========");
    Serial.printf("   URL: %s\n", imageUrl.c_str());
    Serial.printf("   剩余内存: %d 字节\n", ESP.getFreeHeap());
    
    // 清除旧文件并创建新文件
    if (SPIFFS.exists(FLASH_TEMP_FILE)) {
        SPIFFS.remove(FLASH_TEMP_FILE);
    }
    
    flashTempFile = SPIFFS.open(FLASH_TEMP_FILE, "w");
    if (!flashTempFile) {
        Serial.println("❌ 无法创建Flash临时文件");
        return false;
    }
    flashTempFileOpen = true;
    flashTempFileSize = 0;
    
    HTTPClient http;
    if (!http.begin(imageUrl)) {
        Serial.println("❌ HTTP begin失败");
        flashTempFile.close();
        flashTempFileOpen = false;
        return false;
    }
    
    http.setTimeout(CLOUD_DOWNLOAD_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    
    int httpCode = http.GET();
    Serial.printf("   HTTP状态码: %d\n", httpCode);
    
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("❌ HTTP下载失败: %d\n", httpCode);
        http.end();
        flashTempFile.close();
        flashTempFileOpen = false;
        SPIFFS.remove(FLASH_TEMP_FILE);
        return false;
    }
    
    int contentLength = http.getSize();
    Serial.printf("   内容长度: %d 字节 (%.2f KB)\n", contentLength, contentLength / 1024.0);

    // 设备端最小防护：如果云端返回了 Content-Length，但不是期望长度，直接判失败
    // 这样可以避免把“坏/半截数据”交给 EPD 驱动，导致 busy 卡死
    if (contentLength > 0 && contentLength != EPD_EXPECTED_CHARS) {
        Serial.printf("❌ 内容长度异常，期望 %d，实际 %d，放弃下载\n", EPD_EXPECTED_CHARS, contentLength);
        http.end();
        flashTempFile.close();
        flashTempFileOpen = false;
        SPIFFS.remove(FLASH_TEMP_FILE);
        flashTempFileSize = 0;
        return false;
    }
    
    // 流式下载，分块写入SPIFFS
    WiFiClient *stream = http.getStreamPtr();
    uint8_t buffer[512];  // 512字节缓冲区
    int totalRead = 0;
    unsigned long startTime = millis();
    int noDataCount = 0;
    const int MAX_NO_DATA_COUNT = 100;
    
    while (http.connected() && (contentLength > 0 || contentLength == -1)) {
        if (millis() - startTime > CLOUD_DOWNLOAD_TIMEOUT_MS) {
            Serial.println("❌ 下载超时！");
            break;
        }
        
        size_t available = stream->available();
        if (available) {
            noDataCount = 0;
            int bytesToRead = (available > sizeof(buffer)) ? sizeof(buffer) : available;
            int bytesRead = stream->readBytes(buffer, bytesToRead);

            if (bytesRead <= 0) {
                noDataCount++;
                delay(10);
                continue;
            }
            
            // 直接写入Flash
            flashTempFile.write(buffer, bytesRead);
            flashTempFileSize += bytesRead;
            totalRead += bytesRead;
            
            if (contentLength > 0) {
                contentLength -= bytesRead;
            }
            
            // 每64KB输出一次进度
            if (totalRead % 65536 == 0) {
                Serial.printf("   已下载: %.2f KB\n", totalRead / 1024.0);
            }

            // 如果 contentLength 未知（-1），但我们已经达到期望长度，也直接结束（防止超读）
            if (contentLength == -1 && totalRead >= EPD_EXPECTED_CHARS) {
                break;
            }
        } else {
            noDataCount++;
            if (noDataCount > MAX_NO_DATA_COUNT) {
                break;
            }
            delay(10);
        }
    }
    
    flashTempFile.flush();
    flashTempFile.close();
    flashTempFileOpen = false;
    
    http.end();
    
    // 检查下载结果
    Serial.printf("✅ 下载完成: %d 字符 (%.2f KB)\n", flashTempFileSize, flashTempFileSize / 1024.0);
    Serial.printf("   期望大小: %d 字符\n", EPD_EXPECTED_CHARS);

    // 设备端最小防护：只要不是“完全匹配”，就视为失败并删除临时文件
    if (flashTempFileSize != EPD_EXPECTED_CHARS) {
        Serial.printf("❌ 下载不完整：期望 %d，实际 %d，删除临时文件并放弃本次刷新\n",
                      EPD_EXPECTED_CHARS, flashTempFileSize);
        SPIFFS.remove(FLASH_TEMP_FILE);
        flashTempFileSize = 0;
        Serial.println("========== 下载失败 ==========\n");
        return false;
    }
    
    Serial.println("========== 下载完成 ==========\n");
    return true;
}

/**
 * 显示下载的图片（从Flash读取并刷新EPD）
 */
bool displayDownloadedImage() {
    Serial.println("📺 开始显示图片...");
    
    if (!SPIFFS.exists(FLASH_TEMP_FILE)) {
        Serial.println("❌ 临时文件不存在");
        return false;
    }

    // 设备端最小防护：显示前再做一次长度检查，避免 EPD 驱动因数据异常 busy 卡死
    {
        File f = SPIFFS.open(FLASH_TEMP_FILE, "r");
        if (!f) {
            Serial.println("❌ 无法打开临时文件");
            SPIFFS.remove(FLASH_TEMP_FILE);
            return false;
        }
        size_t sz = f.size();
        f.close();
        if ((int)sz != EPD_EXPECTED_CHARS) {
            Serial.printf("❌ 临时文件大小异常：期望 %d，实际 %d；跳过刷新并删除临时文件\n",
                          EPD_EXPECTED_CHARS, (int)sz);
            SPIFFS.remove(FLASH_TEMP_FILE);
            return false;
        }
    }
    
    // 初始化EPD
    if (EPD_dispIndex < 0 || EPD_dispIndex >= (sizeof(EPD_dispMass) / sizeof(EPD_dispMass[0]))) {
        EPD_dispIndex = 0;
    }
    ensureDisplayHardwareReady();
    EPD_dispInit();
    
    // 调用显示函数（从Flash读取）
    if (EPD_dispLoad != nullptr) {
        EPD_dispLoad();
        if (EPD_7in3E_LastBusyTimeout()) {
            Serial.println("❌ 图片显示未正常完成，请检查BUSY线、屏幕供电和排线");
            clearFlashTempFile();
            return false;
        } else {
            Serial.println("✅ 图片显示完成");
        }
    } else {
        Serial.println("❌ EPD_dispLoad未设置");
        clearFlashTempFile();
        return false;
    }
    
    // 清除临时文件
    clearFlashTempFile();
    return true;
}

/* ============================================================================
 *                            Deep-sleep 管理
 * ============================================================================ */

/**
 * 打印唤醒原因
 */
void printWakeupReason() {
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    
    Serial.println("\n========================================");
    Serial.print("⏰ 唤醒原因: ");
    
    switch (wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT0:
            Serial.println("外部信号 (RTC_IO) 唤醒");
            break;
        case ESP_SLEEP_WAKEUP_EXT1:
            Serial.println("外部信号 (RTC_CNTL) 唤醒");
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
            Serial.println("定时器唤醒 (每12小时)");
            break;
        case ESP_SLEEP_WAKEUP_TOUCHPAD:
            Serial.println("触摸板唤醒");
            break;
        case ESP_SLEEP_WAKEUP_ULP:
            Serial.println("ULP程序唤醒");
            break;
        case ESP_SLEEP_WAKEUP_GPIO:
            Serial.println("GPIO按键唤醒");
            break;
        default:
            Serial.printf("其他原因 (%d) - 首次启动或复位\n", wakeup_reason);
            break;
    }
    Serial.println("========================================\n");
}

/**
 * 配置Deep-sleep唤醒源并进入睡眠
 */
void enterDeepSleep() {
    // 幂等：如果已经开始准备进入 deep-sleep，避免重复执行关 WiFi/配置唤醒源等耗时动作
    if (g_deepSleepRequested) {
        Serial.flush();
        delay(50);
        esp_deep_sleep_start();
        return;
    }
    g_deepSleepRequested = true;

    Serial.println("\n========================================");
    Serial.println("💤 准备进入Deep-sleep...");
    Serial.println("========================================");
    
    // 1. 关闭WiFi
    Serial.println("   关闭WiFi...");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();
    delay(100);
    
    // 1.5 处理按键“仍处于低电平”的情况：等待松开再入睡，避免立即再次唤醒
    // 同时启用内部上拉（仍建议外部上拉电阻，避免深睡时上拉状态不稳）
    pinMode((int)WAKEUP_GPIO, INPUT_PULLUP);
    gpio_pullup_en(WAKEUP_GPIO);
    gpio_pulldown_dis(WAKEUP_GPIO);

    if (gpio_get_level(WAKEUP_GPIO) == 0) {
        Serial.println("⚠️  检测到GPIO0仍为低电平（按键可能未松开/无上拉），等待释放...");
        unsigned long startWait = millis();
        while (gpio_get_level(WAKEUP_GPIO) == 0 && (millis() - startWait) < WAKEUP_RELEASE_WAIT_MS) {
            delay(20);
        }
        if (gpio_get_level(WAKEUP_GPIO) == 0) {
            Serial.println("⚠️  等待超时，GPIO0仍为低电平：可能会立刻再次唤醒（请检查硬件上拉/按键）");
        } else {
            Serial.println("✅ GPIO0已恢复高电平，继续进入Deep-sleep");
        }
    }

    // 2. 配置GPIO0按键唤醒（低电平唤醒）
    // ESP32-C3使用esp_deep_sleep_enable_gpio_wakeup
    Serial.println("   配置GPIO0按键唤醒...");
    esp_deep_sleep_enable_gpio_wakeup(1ULL << WAKEUP_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW);
    
    // 3. 配置定时唤醒（12小时）
    Serial.printf("   配置定时唤醒: %d 小时\n", DEEP_SLEEP_INTERVAL_HOURS);
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_INTERVAL_US);
    
    // 4. 打印信息
    Serial.println("\n✅ Deep-sleep配置完成:");
    Serial.println("   - GPIO0 按键唤醒（低电平）");
    Serial.printf("   - 定时唤醒: %d 小时后\n", DEEP_SLEEP_INTERVAL_HOURS);
    Serial.println("   - 墨水屏将保持当前画面");
    Serial.println("\n💤 进入Deep-sleep...\n");
    Serial.flush();
    delay(100);
    
    // 5. 进入Deep-sleep
    esp_deep_sleep_start();
    
    // 不会执行到这里
}

/* ============================================================================
 *                            主要更新流程（一次性判定 + 条件执行）
 * ============================================================================ */

/**
 * 本次唤醒：执行一次性“是否需要更新”的判定（不在这里下载/刷新）
 * - 只做 status 查询与版本比较
 * - 结果写入 g_updateNeeded/g_target*，供 loop 决策
 */
void prepareUpdateDecisionOnce() {
    Serial.println("\n========================================");
    Serial.println("🔄 开始一次性更新判定（仅检查，不下载）...");
    Serial.println("========================================\n");

    // 防止被重复调用（例如某些异常路径下 setup/loop 误触发）
    if (g_statusChecked) {
        Serial.println("ℹ️ 本次唤醒已完成过更新判定，跳过重复检查");
        return;
    }
    
    // 1. 初始化设备ID
    deviceId = getDeviceIdFromMac();
    Serial.printf("⭐ 设备ID: %s\n", deviceId.c_str());
    
    // 2. 读取本地状态
    deviceClaimed = loadClaimedStatus();
    localImageVersion = loadImageVersion();
    Serial.printf("📋 本地状态: claimed=%s, imageVersion=%lld\n",
                  deviceClaimed ? "是" : "否", localImageVersion);
    
    // 3. 初始化Flash存储
    if (!initFlashStorage()) {
        Serial.println("❌ Flash初始化失败，本次唤醒直接进入Deep-sleep");
        g_shouldEnterDeepSleep = true;
        g_statusChecked = true;
        return;
    }
    
    // 4. 设置默认EPD型号
    EPD_dispIndex = 0;
    
    // 5. 基础检查：WiFi 必须已连接（理论上 .ino 已保证，这里兜底）
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("⚠️  WiFi未连接，跳过云端查询，直接进入Deep-sleep");
        g_shouldEnterDeepSleep = true;
        g_statusChecked = true;
        return;
    }

    // 5. 查询云端状态
    Serial.println("\n📡 查询云端状态...");
    DeviceStatusResponse status = queryDeviceStatus();
    
    if (!status.success) {
        Serial.printf("❌ 云端查询失败: %s\n", status.error.c_str());
        Serial.println("   直接进入Deep-sleep，下次唤醒再试");
        g_shouldEnterDeepSleep = true;
        g_statusChecked = true;
        return;
    }
    
    // 6. 处理绑定状态
    if (!status.claimed) {
        Serial.println("\n📱 设备未绑定，显示设备码...");
        
        // 更新本地状态
        if (deviceClaimed) {
            deviceClaimed = false;
            saveClaimedStatus(false);
        }
        
        // 显示设备码
        displayDeviceCode();
        
        Serial.println("✅ 设备码已显示，请通过网页绑定设备");
        Serial.printf("   网页地址: %s\n", getCloudPortalUrl().c_str());
        Serial.println("   设备将进入Deep-sleep等待下次唤醒");

        g_shouldEnterDeepSleep = true;
        g_statusChecked = true;
        return;
    }
    
    // 7. 设备已绑定，更新本地状态
    if (!deviceClaimed) {
        deviceClaimed = true;
        saveClaimedStatus(true);
    }
    
    // 8. 检查是否需要更新图片
    Serial.printf("\n📊 图片同步标记检查: 云端=%lld, 本地=%lld\n",
                  status.imageVersion, localImageVersion);
    
    bool cloudHasImage = status.imageVersion > 0;
    bool imageVersionMismatch = cloudHasImage && (status.imageVersion != localImageVersion);

    if (imageVersionMismatch) {
        if (status.imageUrl.length() == 0) {
            Serial.println("⚠️  云端图片标记不一致但未返回 imageUrl，本次跳过下载，直接Deep-sleep");
            g_shouldEnterDeepSleep = true;
        } else {
            Serial.println("✅ 云端图片标记与本地不一致，按云端数据同步（下载/刷新将在 loop 中执行）");
            g_updateNeeded = true;
            g_targetImageVersion = status.imageVersion;
            g_targetImageUrl = status.imageUrl;
        }
    } else if (!cloudHasImage) {
        Serial.println("ℹ️  云端暂无已发布图片，无需更新");
        g_shouldEnterDeepSleep = true;
    } else {
        Serial.println("✅ 图片已是最新版本，无需更新");
        g_shouldEnterDeepSleep = true;
    }

    // 标记：本次唤醒已完成判定（确保一次性）
    g_statusChecked = true;
}

/* ============================================================================
 *                            初始化和主循环
 * ============================================================================ */

/**
 * HTTP更新模式初始化（在setup中调用）
 */
void HTTP_UPDATE__setup() {
    Serial.println("\n========================================");
    Serial.println("  Deep-sleep + HTTP 更新模式");
    Serial.println("========================================");
    
    // 打印唤醒原因
    printWakeupReason();
    
    // 重置本次唤醒的一次性状态
    g_statusChecked = false;
    g_updateNeeded = false;
    g_updateAttempted = false;
    g_shouldEnterDeepSleep = false;
    g_deepSleepRequested = false;
    g_displayHardwareReady = false;
    g_targetImageVersion = 0;
    g_targetImageUrl = "";

    // 注意：WiFi连接在 wifi_config.h 中完成（.ino 里保证已连上才会进入这里）
    // 本函数只做一次性判定，不做下载/刷新，不在这里立即 deep-sleep
    prepareUpdateDecisionOnce();
}

/**
 * HTTP更新模式主循环（在loop中调用）
 * Deep-sleep架构下loop几乎不会被执行
 */
void HTTP_UPDATE__loop() {
    // 1) 理论上 setup 已经完成一次性判定；如果没有（异常），直接回睡避免耗电
    if (!g_statusChecked) {
        Serial.println("⚠️  未完成更新判定，直接进入Deep-sleep（避免重复/耗电）");
        g_shouldEnterDeepSleep = true;
    }

    // 2) 仅当需要更新时，执行一次下载 + 刷新（只尝试一次，避免 loop 重复下载）
    if (g_updateNeeded && !g_updateAttempted) {
        g_updateAttempted = true;

        Serial.println("\n========================================");
        Serial.println("⬇️  loop: 检测到需要更新，开始下载并刷新...");
        Serial.println("========================================\n");

        if (g_targetImageUrl.length() == 0 || g_targetImageVersion <= 0) {
            Serial.println("⚠️  更新参数不完整，跳过更新");
        } else {
            if (downloadImageToFlash(g_targetImageUrl)) {
                if (displayDownloadedImage()) {
                    saveImageVersion(g_targetImageVersion);
                    localImageVersion = g_targetImageVersion;
                    Serial.printf("✅ 已更新到版本: %lld\n", localImageVersion);
                } else {
                    Serial.println("❌ 显示失败，保留本地版本号，下次唤醒继续重试该图片");
                }
            } else {
                Serial.println("❌ 下载失败，本次不再重试，直接Deep-sleep");
            }
        }

        // 无论成功与否，本次唤醒都不再重复更新
        g_updateNeeded = false;
        g_shouldEnterDeepSleep = true;
    }

    // 3) 不需要更新：直接回睡（不做重复检查/重复动作）
    if (!g_updateNeeded) {
        g_shouldEnterDeepSleep = true;
    }

    // 4) 进入 deep-sleep（幂等）
    if (g_shouldEnterDeepSleep) {
        enterDeepSleep();
    }

    // 正常不会走到这里
    delay(100);
}

#endif // HTTP_UPDATE_H
