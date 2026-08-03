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
#include <WiFiClientSecure.h>
#include <SPIFFS.h>
#include <FS.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#include "esp_mac.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_random.h"
#include "driver/gpio.h"
#include "buff.h"
#include "epd.h"
#include "EPD_7in3e.h"
#include "GUI_Paint.h"
#include "fonts.h"
#include "provisioning_fonts.h"
#include "logo_phenosolar.h"
#include "qrcode.h"
#include "device_identity.h"

/* ============================================================================
 *                               配置参数
 * ============================================================================ */

/* 云端API配置 */
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "3.1.0"
#endif
#ifndef FIRMWARE_BUILD
#define FIRMWARE_BUILD __DATE__ " " __TIME__
#endif
#ifndef EPD_STRUCTURED_DEBUG
#define EPD_STRUCTURED_DEBUG 0
#endif
#define CLOUD_API_HOST "8.135.238.216"
#define CLOUD_API_PORT 8080  // 经 Nginx 代理访问后端
#ifndef CLOUD_API_USE_HTTPS
#define CLOUD_API_USE_HTTPS 0  // 0=兼容现网HTTP；1=HTTPS并强制校验下方CA证书
#endif
#ifndef CLOUD_API_ROOT_CA_PEM
// HTTPS启用时必须替换/外部定义为签发服务器证书的根CA PEM；留空会拒绝联网，不会降级为不安全TLS。
#define CLOUD_API_ROOT_CA_PEM ""
#endif
#ifndef CLOUD_TLS_NTP_SERVER_1
#define CLOUD_TLS_NTP_SERVER_1 "ntp.aliyun.com"
#endif
#ifndef CLOUD_TLS_NTP_SERVER_2
#define CLOUD_TLS_NTP_SERVER_2 "pool.ntp.org"
#endif
#define CLOUD_TLS_TIME_SYNC_TIMEOUT_MS 15000
#define CLOUD_TLS_VALID_EPOCH 1704067200LL  // 2024-01-01 UTC，用于判定系统时间是否已初始化
#define CLOUD_API_TIMEOUT_MS 30000  // 单次状态网络操作超时
#define CLOUD_STATUS_TOTAL_TIMEOUT_MS 60000UL  // status请求与响应的绝对总预算
#define CLOUD_DOWNLOAD_TIMEOUT_MS 60000  // 下载连续无数据超时（60秒，有数据时重新计时）
#define CLOUD_DOWNLOAD_TOTAL_TIMEOUT_MS 300000UL  // 单次图片下载绝对总时限（5分钟）
#define CLOUD_STATUS_MAX_RESPONSE_BYTES 8192U  // status JSON正常远小于8KB，异常响应不得耗尽堆

#if CLOUD_API_USE_HTTPS
using CloudApiClient = WiFiClientSecure;
#else
using CloudApiClient = WiFiClient;
#endif

static String getCloudApiOrigin() {
    String origin = CLOUD_API_USE_HTTPS ? "https://" : "http://";
    origin += CLOUD_API_HOST;
    origin += ":";
    origin += String(CLOUD_API_PORT);
    return origin;
}

static bool beginCloudApiRequest(HTTPClient& http, CloudApiClient& client,
                                 const String& url) {
#if CLOUD_API_USE_HTTPS
    const char* rootCa = CLOUD_API_ROOT_CA_PEM;
    if (rootCa == nullptr || rootCa[0] == '\0') {
        Serial.println("❌ 已启用HTTPS但未配置CLOUD_API_ROOT_CA_PEM，拒绝不安全连接");
        return false;
    }
    time_t now = time(nullptr);
    if ((int64_t)now < CLOUD_TLS_VALID_EPOCH) {
        Serial.println("🕒 HTTPS证书校验前同步系统时间...");
        configTime(0, 0, CLOUD_TLS_NTP_SERVER_1, CLOUD_TLS_NTP_SERVER_2);
        const uint32_t syncStartedAt = millis();
        do {
            delay(250);
            now = time(nullptr);
        } while ((int64_t)now < CLOUD_TLS_VALID_EPOCH &&
                 millis() - syncStartedAt < CLOUD_TLS_TIME_SYNC_TIMEOUT_MS);
        if ((int64_t)now < CLOUD_TLS_VALID_EPOCH) {
            Serial.println("❌ 系统时间同步失败，无法可靠校验证书有效期；拒绝HTTPS连接");
            return false;
        }
        Serial.println("✅ 系统时间已同步");
    }
    client.setCACert(rootCa);
#endif
    return http.begin(client, url);
}

class LimitedStringStream : public Stream {
public:
    LimitedStringStream(String& output, size_t limit,
                        uint32_t startedAt, uint32_t totalBudgetMs)
        : output_(output), limit_(limit), startedAt_(startedAt),
          totalBudgetMs_(totalBudgetMs), overflowed_(false),
          allocationFailed_(false), totalBudgetExceeded_(false) {}

    size_t write(uint8_t data) override {
        return write(&data, 1);
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        if (deadlineReached()) {
            totalBudgetExceeded_ = true;
            return 0;
        }
        if (size == 0) {
            return 0;
        }
        if (buffer == nullptr || output_.length() > limit_ ||
            size > limit_ - output_.length()) {
            overflowed_ = true;
            return 0;
        }
        if (!output_.concat(reinterpret_cast<const char*>(buffer), (unsigned int)size)) {
            allocationFailed_ = true;
            return 0;
        }
        if (deadlineReached()) {
            totalBudgetExceeded_ = true;
            return 0;
        }
        return size;
    }

    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}

    bool overflowed() const { return overflowed_; }
    bool allocationFailed() const { return allocationFailed_; }
    bool totalBudgetExceeded() const { return totalBudgetExceeded_; }

private:
    bool deadlineReached() const {
        return totalBudgetMs_ > 0 &&
               (uint32_t)(millis() - startedAt_) >= totalBudgetMs_;
    }

    String& output_;
    size_t limit_;
    uint32_t startedAt_;
    uint32_t totalBudgetMs_;
    bool overflowed_;
    bool allocationFailed_;
    bool totalBudgetExceeded_;
};

/* Deep-sleep 配置 */
#define WAKEUP_GPIO GPIO_NUM_0  // GPIO0 按键唤醒（按键接地，低电平唤醒）
#define DEEP_SLEEP_INTERVAL_HOURS 12  // 默认定时唤醒间隔（小时），可由云端 nextSleepSeconds 覆盖
#define DEFAULT_SLEEP_INTERVAL_SECONDS (DEEP_SLEEP_INTERVAL_HOURS * 3600UL)
#define DEEP_SLEEP_INTERVAL_US (DEFAULT_SLEEP_INTERVAL_SECONDS * 1000000ULL)
#define MIN_SLEEP_INTERVAL_SECONDS 300UL
#define MAX_SLEEP_INTERVAL_SECONDS (30UL * 24UL * 60UL * 60UL)
// 避免“按键仍按下/引脚为低”导致刚入睡就立刻被再次唤醒
#define WAKEUP_RELEASE_WAIT_MS 2500
#define WAKEUP_STUCK_LOW_RETRY_SECONDS 300

/* Flash临时存储配置 */
#define FLASH_TEMP_FILE "/temp_image.bin"
// 7.3" E6: 800x480，每像素 4bit（a~p 编码为单字符），总字符数固定
#define EPD_EXPECTED_CHARS 384000

/* NVS 配置 */
#define PREF_NAMESPACE "device"
#define PREF_KEY_CLAIMED "claimed"
#define PREF_KEY_IMG_VER "imgVer"
#define PREF_KEY_SLEEP_INTERVAL "slpInt"  // 单位：秒，0 = 未设置（使用默认值）
#define PREF_KEY_DEVICE_KEY "devKey"      // 32字节随机设备密钥的64字符十六进制表示
#define PREF_KEY_UPDATE_DIAG "updDiag"    // 紧凑的更新事务诊断记录
#define DEVICE_KEY_BYTES 32
#define DEVICE_KEY_HEX_LENGTH (DEVICE_KEY_BYTES * 2)
#define DEVICE_KEY_HEADER "X-Device-Key"

/* 本地 UI 页帧缓冲：按需 malloc，画完 free（去掉 192KB 静态 BSS 以腾出 SRAM 给 WiFi） */
#define EPD_PANEL_WIDTH 800
#define EPD_PANEL_HEIGHT 480

/* AP 配网页 / 添加设备码页按 800x144 条带流式发送整屏，避免 192KB 全屏画布 */
#define PROVISIONING_CANVAS_WIDTH 480
#define PROVISIONING_CANVAS_HEIGHT 240
#define PROVISIONING_CANVAS_PACKED_WIDTH ((PROVISIONING_CANVAS_WIDTH + 1) / 2)
#define PROVISIONING_CANVAS_SIZE (PROVISIONING_CANVAS_PACKED_WIDTH * PROVISIONING_CANVAS_HEIGHT)
#define PROVISIONING_FULL_STRIPE_HEIGHT 144
#define PROVISIONING_FULL_STRIPE_SIZE (((EPD_PANEL_WIDTH + 1) / 2) * PROVISIONING_FULL_STRIPE_HEIGHT)
#define PROVISIONING_QR_VERSION 9
#define PROVISIONING_QR_TARGET_SIZE 193
#define PROVISIONING_QR_QUIET_ZONE_MODULES 4
#define PROVISIONING_QR_MAX_BYTE_PAYLOAD 180U
#define PROVISIONING_QR_MODULE_COUNT (4 * PROVISIONING_QR_VERSION + 17)
#define PROVISIONING_QR_BUFFER_SIZE (((PROVISIONING_QR_MODULE_COUNT * PROVISIONING_QR_MODULE_COUNT) + 7) / 8)

static UBYTE* g_epdUiFrameHeap = nullptr;
static size_t g_epdUiFrameCapacity = 0;

void releaseEpdUiFrame();

static void logUiFrameHeap(const char* hypothesisId, const char* message, size_t needBytes) {
#if EPD_STRUCTURED_DEBUG
    Serial.printf(
        "{\"component\":\"ui_frame\",\"event\":\"%s\",\"location\":\"epdUiFrame\","
        "\"message\":\"%s\",\"data\":{\"need\":%u,\"free\":%u,\"largest\":%u},"
        "\"timestamp\":%lu}\n",
        hypothesisId, message, (unsigned)needBytes, (unsigned)ESP.getFreeHeap(),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        (unsigned long)millis());
#else
    (void)hypothesisId;
    (void)message;
    (void)needBytes;
#endif
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
static String g_deviceKey;
bool deviceClaimed = false;
int64_t localImageVersion = 0;

enum UpdateResultCode : uint8_t {
    UPDATE_RESULT_NONE = 0,
    UPDATE_RESULT_PENDING = 1,
    UPDATE_RESULT_SUCCESS = 2,
    UPDATE_RESULT_FAILED = 3,
    UPDATE_RESULT_INTERRUPTED = 4,
};

enum UpdateStageCode : uint8_t {
    UPDATE_STAGE_IDLE = 0,
    UPDATE_STAGE_DOWNLOAD = 1,
    UPDATE_STAGE_VERIFY = 2,
    UPDATE_STAGE_EPD_POWER_ON = 3,
    UPDATE_STAGE_EPD_REFRESH = 4,
    UPDATE_STAGE_EPD_POWER_OFF = 5,
    UPDATE_STAGE_NVS_COMMIT = 6,
    UPDATE_STAGE_DONE = 7,
};

enum UpdateErrorCode : uint8_t {
    UPDATE_ERROR_NONE = 0,
    UPDATE_ERROR_DOWNLOAD_HTTP = 1,
    UPDATE_ERROR_DOWNLOAD_TIMEOUT = 2,
    UPDATE_ERROR_SIZE_MISMATCH = 3,
    UPDATE_ERROR_CHARSET_INVALID = 4,
    UPDATE_ERROR_SHA_MISMATCH = 5,
    UPDATE_ERROR_SPIFFS_WRITE = 6,
    UPDATE_ERROR_EPD_NOT_BOUND = 7,
    UPDATE_ERROR_BUSY_POWER_ON = 8,
    UPDATE_ERROR_BUSY_REFRESH = 9,
    UPDATE_ERROR_BUSY_POWER_OFF = 10,
    UPDATE_ERROR_NVS_SAVE = 11,
    UPDATE_ERROR_INTERRUPTED = 12,
    UPDATE_ERROR_VERSION_EXPIRED = 13,
};

static const uint32_t UPDATE_DIAGNOSTIC_MAGIC = 0x45504444UL;  // "EPDD"
static const uint8_t UPDATE_DIAGNOSTIC_SCHEMA_VERSION = 1;

struct __attribute__((packed)) UpdateDiagnosticRecord {
    uint32_t magic;
    uint8_t schemaVersion;
    uint8_t result;
    uint8_t stage;
    uint8_t error;
    uint64_t attemptId;
    int64_t targetImageVersion;
    int64_t localImageVersion;
    uint32_t durationMs;
    uint8_t reportPending;
    uint8_t reserved[3];
    uint32_t checksum;
};

static UpdateDiagnosticRecord g_updateDiagnostic = {};
static bool g_updateDiagnosticLoaded = false;
static uint32_t g_updateAttemptStartedAt = 0;
static UpdateErrorCode g_currentUpdateError = UPDATE_ERROR_NONE;
static uint32_t g_retrySleepSeconds = 0;
RTC_DATA_ATTR bool g_rtcGpio0StuckLow = false;

static const char* updateResultName(UpdateResultCode result) {
    switch (result) {
        case UPDATE_RESULT_PENDING: return "pending";
        case UPDATE_RESULT_SUCCESS: return "success";
        case UPDATE_RESULT_FAILED: return "failed";
        case UPDATE_RESULT_INTERRUPTED: return "interrupted";
        default: return "none";
    }
}

static const char* updateStageName(UpdateStageCode stage) {
    switch (stage) {
        case UPDATE_STAGE_DOWNLOAD: return "download";
        case UPDATE_STAGE_VERIFY: return "verify";
        case UPDATE_STAGE_EPD_POWER_ON: return "epd_power_on";
        case UPDATE_STAGE_EPD_REFRESH: return "epd_refresh";
        case UPDATE_STAGE_EPD_POWER_OFF: return "epd_power_off";
        case UPDATE_STAGE_NVS_COMMIT: return "nvs_commit";
        case UPDATE_STAGE_DONE: return "done";
        default: return "idle";
    }
}

static const char* updateErrorName(UpdateErrorCode error) {
    switch (error) {
        case UPDATE_ERROR_DOWNLOAD_HTTP: return "download_http";
        case UPDATE_ERROR_DOWNLOAD_TIMEOUT: return "download_timeout";
        case UPDATE_ERROR_SIZE_MISMATCH: return "size_mismatch";
        case UPDATE_ERROR_CHARSET_INVALID: return "charset_invalid";
        case UPDATE_ERROR_SHA_MISMATCH: return "sha_mismatch";
        case UPDATE_ERROR_SPIFFS_WRITE: return "spiffs_write";
        case UPDATE_ERROR_EPD_NOT_BOUND: return "epd_not_bound";
        case UPDATE_ERROR_BUSY_POWER_ON: return "busy_power_on";
        case UPDATE_ERROR_BUSY_REFRESH: return "busy_refresh";
        case UPDATE_ERROR_BUSY_POWER_OFF: return "busy_power_off";
        case UPDATE_ERROR_NVS_SAVE: return "nvs_save";
        case UPDATE_ERROR_INTERRUPTED: return "interrupted";
        case UPDATE_ERROR_VERSION_EXPIRED: return "version_expired";
        default: return "none";
    }
}

static const char* resetReasonName() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_EXT: return "EXTERNAL";
        case ESP_RST_SW: return "SOFTWARE";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT: return "OTHER_WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO: return "SDIO";
        case ESP_RST_USB: return "USB";
        case ESP_RST_JTAG: return "JTAG";
        case ESP_RST_EFUSE: return "EFUSE";
        case ESP_RST_PWR_GLITCH: return "POWER_GLITCH";
        case ESP_RST_CPU_LOCKUP: return "CPU_LOCKUP";
        default: return "UNKNOWN";
    }
}

static uint32_t updateDiagnosticChecksum(const UpdateDiagnosticRecord& record) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
    const size_t checksumOffset = offsetof(UpdateDiagnosticRecord, checksum);
    uint32_t hash = 2166136261UL;
    for (size_t i = 0; i < checksumOffset; ++i) {
        hash ^= bytes[i];
        hash *= 16777619UL;
    }
    return hash;
}

static bool persistUpdateDiagnostic() {
    if (!g_updateDiagnosticLoaded) {
        return false;
    }
    g_updateDiagnostic.magic = UPDATE_DIAGNOSTIC_MAGIC;
    g_updateDiagnostic.schemaVersion = UPDATE_DIAGNOSTIC_SCHEMA_VERSION;
    g_updateDiagnostic.checksum = updateDiagnosticChecksum(g_updateDiagnostic);
    if (!preferences.begin(PREF_NAMESPACE, false)) {
        preferences.end();
        Serial.println("⚠️  NVS命名空间打开失败，无法保存更新诊断");
        return false;
    }
    const size_t written = preferences.putBytes(
        PREF_KEY_UPDATE_DIAG, &g_updateDiagnostic, sizeof(g_updateDiagnostic));
    preferences.end();
    if (written != sizeof(g_updateDiagnostic)) {
        Serial.printf("⚠️  更新诊断写入NVS失败: %u/%u\n",
                      (unsigned)written, (unsigned)sizeof(g_updateDiagnostic));
        return false;
    }
    return true;
}

static bool loadUpdateDiagnostic() {
    g_updateDiagnostic = {};
    g_updateDiagnosticLoaded = false;
    if (!preferences.begin(PREF_NAMESPACE, true)) {
        preferences.end();
        return false;
    }
    const size_t storedSize = preferences.getBytesLength(PREF_KEY_UPDATE_DIAG);
    const size_t readSize = storedSize == sizeof(g_updateDiagnostic)
        ? preferences.getBytes(PREF_KEY_UPDATE_DIAG, &g_updateDiagnostic, sizeof(g_updateDiagnostic))
        : 0;
    preferences.end();
    if (readSize != sizeof(g_updateDiagnostic) ||
        g_updateDiagnostic.magic != UPDATE_DIAGNOSTIC_MAGIC ||
        g_updateDiagnostic.schemaVersion != UPDATE_DIAGNOSTIC_SCHEMA_VERSION ||
        g_updateDiagnostic.checksum != updateDiagnosticChecksum(g_updateDiagnostic)) {
        g_updateDiagnostic = {};
        return false;
    }
    g_updateDiagnosticLoaded = true;
    return true;
}

static void clearUpdateDiagnostic() {
    if (preferences.begin(PREF_NAMESPACE, false)) {
        preferences.remove(PREF_KEY_UPDATE_DIAG);
        preferences.end();
    } else {
        preferences.end();
    }
    g_updateDiagnostic = {};
    g_updateDiagnosticLoaded = false;
}

static void prepareUpdateDiagnosticForBoot() {
    if (!loadUpdateDiagnostic()) {
        return;
    }
    if (g_updateDiagnostic.result == UPDATE_RESULT_PENDING) {
        g_updateDiagnostic.result = UPDATE_RESULT_INTERRUPTED;
        g_updateDiagnostic.error = UPDATE_ERROR_INTERRUPTED;
        g_updateDiagnostic.localImageVersion = localImageVersion;
        g_updateDiagnostic.reportPending = 1;
        persistUpdateDiagnostic();
        Serial.printf("⚠️  检测到上次更新在阶段 %s 被中断，复位原因=%s\n",
                      updateStageName((UpdateStageCode)g_updateDiagnostic.stage), resetReasonName());
    }
}

static void beginUpdateDiagnostic(int64_t targetVersion) {
    g_updateDiagnostic = {};
    g_updateDiagnosticLoaded = true;
    g_updateAttemptStartedAt = millis();
    g_currentUpdateError = UPDATE_ERROR_NONE;
    uint64_t attemptId = ((uint64_t)esp_random() << 32) | (uint64_t)esp_random();
    if (attemptId == 0) {
        attemptId = 1;
    }
    g_updateDiagnostic.magic = UPDATE_DIAGNOSTIC_MAGIC;
    g_updateDiagnostic.schemaVersion = UPDATE_DIAGNOSTIC_SCHEMA_VERSION;
    g_updateDiagnostic.result = UPDATE_RESULT_PENDING;
    g_updateDiagnostic.stage = UPDATE_STAGE_DOWNLOAD;
    g_updateDiagnostic.error = UPDATE_ERROR_NONE;
    g_updateDiagnostic.attemptId = attemptId;
    g_updateDiagnostic.targetImageVersion = targetVersion;
    g_updateDiagnostic.localImageVersion = localImageVersion;
    g_updateDiagnostic.durationMs = 0;
    g_updateDiagnostic.reportPending = 1;
    persistUpdateDiagnostic();
}

static void setUpdateStage(UpdateStageCode stage) {
    if (!g_updateDiagnosticLoaded || g_updateDiagnostic.result != UPDATE_RESULT_PENDING ||
        g_updateDiagnostic.stage == stage) {
        return;
    }
    g_updateDiagnostic.stage = stage;
    g_updateDiagnostic.durationMs = millis() - g_updateAttemptStartedAt;
    persistUpdateDiagnostic();
}

static void setUpdateError(UpdateErrorCode error) {
    if (error != UPDATE_ERROR_NONE) {
        g_currentUpdateError = error;
    }
}

static void onEpdUpdateStage(const char* stage) {
    if (stage == nullptr) return;
    if (strcmp(stage, "epd_power_on") == 0) {
        setUpdateStage(UPDATE_STAGE_EPD_POWER_ON);
    } else if (strcmp(stage, "epd_refresh") == 0) {
        setUpdateStage(UPDATE_STAGE_EPD_REFRESH);
    } else if (strcmp(stage, "epd_power_off") == 0) {
        setUpdateStage(UPDATE_STAGE_EPD_POWER_OFF);
    }
}

static void finishUpdateDiagnostic(bool success) {
    if (!g_updateDiagnosticLoaded) return;
    g_updateDiagnostic.result = success ? UPDATE_RESULT_SUCCESS : UPDATE_RESULT_FAILED;
    if (success) {
        g_updateDiagnostic.stage = UPDATE_STAGE_DONE;
        g_updateDiagnostic.error = UPDATE_ERROR_NONE;
    } else {
        g_updateDiagnostic.error = g_currentUpdateError;
    }
    g_updateDiagnostic.localImageVersion = localImageVersion;
    g_updateDiagnostic.durationMs = millis() - g_updateAttemptStartedAt;
    g_updateDiagnostic.reportPending = 1;
    persistUpdateDiagnostic();
}

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
static String g_targetImageSha256 = "";       // status返回的目标图片SHA-256（可为空以兼容旧数据）

/* ============================================================================
 *                            前置声明
 * ============================================================================ */

void ensureDisplayHardwareReady();
bool saveImageVersion(int64_t version);
void enterDeepSleep();
void enterDeepSleepForRetry(uint32_t retrySeconds);

static bool invalidateCloudImageVersionForLocalUi(const char* reason) {
    if (!saveImageVersion(0)) {
        Serial.printf("❌ 无法在显示%s前失效云端图片版本，取消本地页面刷新\n", reason);
        return false;
    }
    localImageVersion = 0;
    Serial.printf("🔄 显示%s前已失效本地图片版本，下次绑定状态将强制拉取云端图\n", reason);
    return true;
}

static bool tryEnterEpdDeepSleep(const char* context) {
    if (!g_displayHardwareReady) {
        return true;
    }

    EPD_7IN3E_ClearBusyTimeout();
    EPD_7IN3E_Sleep();
    if (EPD_7IN3E_LastBusyTimeout()) {
        Serial.printf("⚠️  %s完成，但EPD进入深睡时BUSY超时\n", context);
        return false;
    } else {
        Serial.printf("💤 %s完成，EPD已进入深睡\n", context);
    }
    return true;
}

/* ============================================================================
 *                            辅助函数：AP 配网页二维码
 * ============================================================================ */

static int getCFontTextWidth(const String& text, const cFONT* font);
static int getCenteredTextX(int areaX, int areaWidth, const String& text, const cFONT* font);
static bool drawProvisioningQrToPaintEx(const String& payload, int centerX, int centerY, int targetSize,
                                        int canvasWidth, int canvasHeight, UWORD darkColor);
static void drawBitmapMask(int x0, int y0, int width, int height, const uint8_t* bitmap, UWORD color);
static void drawBitmapMaskClipped(int x0, int y0, int width, int height,
                                  const uint8_t* bitmap, UWORD color,
                                  int canvasWidth, int canvasHeight);

String getProvisioningApPassword();

static bool drawProvisioningQrToPaintEx(const String& payload, int centerX, int centerY, int targetSize,
                                        int canvasWidth, int canvasHeight, UWORD darkColor) {
    const size_t payloadBytes = payload.length();
    if (payloadBytes == 0) {
        return false;
    }
    if (payloadBytes > PROVISIONING_QR_MAX_BYTE_PAYLOAD) {
        Serial.printf("❌ 二维码内容过长: %u 字节（V9/M上限 %u 字节）\n",
                      (unsigned)payloadBytes, (unsigned)PROVISIONING_QR_MAX_BYTE_PAYLOAD);
        return false;
    }

    QRCode qrcode;
    uint8_t qrData[PROVISIONING_QR_BUFFER_SIZE];
    const int8_t initResult = qrcode_initText(&qrcode, qrData, PROVISIONING_QR_VERSION,
                                               ECC_MEDIUM, payload.c_str());
    if (initResult < 0 || qrcode.size != PROVISIONING_QR_MODULE_COUNT) {
        Serial.println("❌ 二维码生成失败");
        return false;
    }

    const int modules = qrcode.size;
    const int totalModules = modules + 2 * PROVISIONING_QR_QUIET_ZONE_MODULES;
    const int scale = targetSize / totalModules;
    if (scale < 1) {
        Serial.println("❌ 二维码目标区域不足以容纳静区");
        return false;
    }

    const int drawSize = totalModules * scale;
    const int quietX0 = centerX - drawSize / 2;
    const int quietY0 = centerY - drawSize / 2;
    const int moduleX0 = quietX0 + PROVISIONING_QR_QUIET_ZONE_MODULES * scale;
    const int moduleY0 = quietY0 + PROVISIONING_QR_QUIET_ZONE_MODULES * scale;

    // 先绘制完整白底，明确保留四模块静区。
    for (int y = 0; y < drawSize; y++) {
        const int py = quietY0 + y;
        if (py < 0 || py >= canvasHeight) {
            continue;
        }
        for (int x = 0; x < drawSize; x++) {
            const int px = quietX0 + x;
            if (px >= 0 && px < canvasWidth) {
                Paint_SetPixel(px, py, EPD_7IN3E_WHITE);
            }
        }
    }

    for (int y = 0; y < modules; y++) {
        for (int x = 0; x < modules; x++) {
            if (!qrcode_getModule(&qrcode, x, y)) {
                continue;
            }
            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++) {
                    const int px = moduleX0 + x * scale + dx;
                    const int py = moduleY0 + y * scale + dy;
                    if (px >= 0 && px < canvasWidth && py >= 0 && py < canvasHeight) {
                        Paint_SetPixel(px, py, darkColor);
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
    drawBitmapMaskClipped(x0, y0, width, height, bitmap, color, Paint.Width, Paint.Height);
}

static void drawBitmapMaskClipped(int x0, int y0, int width, int height,
                                  const uint8_t* bitmap, UWORD color,
                                  int canvasWidth, int canvasHeight) {
    if (bitmap == NULL || width <= 0 || height <= 0) {
        return;
    }

    const int rowBytes = (width + 7) / 8;
    for (int y = 0; y < height; y++) {
        const int py = y0 + y;
        if (py < 0 || py >= canvasHeight) {
            continue;
        }
        for (int x = 0; x < width; x++) {
            const int px = x0 + x;
            if (px < 0 || px >= canvasWidth) {
                continue;
            }
            const uint8_t byte = bitmap[y * rowBytes + (x / 8)];
            if (byte & (0x80 >> (x % 8))) {
                Paint_SetPixel(px, py, color);
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

static bool isElementInStripe(int y, int height, int stripeY, int stripeHeight) {
    return y >= stripeY && (y + height) <= (stripeY + stripeHeight);
}

static void drawStringENInStripe(int x, int y, const char* text, sFONT* font,
                                 UWORD fg, UWORD bg, int stripeY, int stripeHeight) {
    if (text == NULL || font == NULL || !isElementInStripe(y, font->Height, stripeY, stripeHeight)) {
        return;
    }
    // Paint_DrawString_EN 内部前景/背景参数顺序与声明相反，这里用包装函数统一为 fg/bg。
    Paint_DrawString_EN(x, y - stripeY, text, font, bg, fg);
}

static void drawStringENSpacedInStripe(int x, int y, const char* text, sFONT* font,
                                       UWORD fg, UWORD bg, int spacing,
                                       int stripeY, int stripeHeight) {
    if (text == NULL || font == NULL || !isElementInStripe(y, font->Height, stripeY, stripeHeight)) {
        return;
    }

    int cursorX = x;
    while (*text != '\0') {
        Paint_DrawChar(cursorX, y - stripeY, *text, font, fg, bg);
        cursorX += font->Width + spacing;
        text++;
    }
}

static void drawStringCNInStripe(int x, int y, const char* text, cFONT* font,
                                 UWORD fg, UWORD bg, int stripeY, int stripeHeight) {
    if (text == NULL || font == NULL || !isElementInStripe(y, font->Height, stripeY, stripeHeight)) {
        return;
    }
    Paint_DrawString_CN(x, y - stripeY, text, font, fg, bg);
}

static void fillRoundedRectClipped(int x0, int y0, int width, int height, int radius,
                                   UWORD color, int canvasWidth, int canvasHeight) {
    if (width <= 0 || height <= 0 || radius <= 0) {
        return;
    }

    const int x1 = x0 + width - 1;
    const int y1 = y0 + height - 1;
    const int startX = x0 < 0 ? 0 : x0;
    const int endX = x1 >= canvasWidth ? canvasWidth - 1 : x1;
    const int startY = y0 < 0 ? 0 : y0;
    const int endY = y1 >= canvasHeight ? canvasHeight - 1 : y1;
    const int r2 = radius * radius;

    for (int y = startY; y <= endY; y++) {
        for (int x = startX; x <= endX; x++) {
            int dx = 0;
            int dy = 0;
            if (x < x0 + radius) {
                dx = x0 + radius - x;
            } else if (x > x1 - radius) {
                dx = x - (x1 - radius);
            }
            if (y < y0 + radius) {
                dy = y0 + radius - y;
            } else if (y > y1 - radius) {
                dy = y - (y1 - radius);
            }
            if (dx > 0 && dy > 0 && (dx * dx + dy * dy) > r2) {
                continue;
            }
            Paint_SetPixel(x, y, color);
        }
    }
}

static bool renderProvisioningFullStripe(UBYTE* imageBuffer, int stripeY, int stripeHeight,
                                         const String& apSSID, const String& wifiQrPayload,
                                         const String& apPassword) {

    const int paintWidth = EPD_PANEL_WIDTH;
    const UWORD fg = EPD_7IN3E_BLACK;
    const UWORD bg = EPD_7IN3E_GREEN;
    const int logoX = 26;
    const int logoY = 22;
    const int leftX = 78;
    const int titleCnX = 77;
    const int valueX = 222;
    const int qrBoxX = 531;
    const int qrBoxY = 214;
    const int qrBoxW = 195;
    const int qrBoxH = 195;
    const int qrRadius = 10;
    const int qrCenterX = 628;
    const int qrCenterY = 311;
    const int qrSize = 193;

    Paint_NewImage(imageBuffer, paintWidth, stripeHeight, 0, bg);
    Paint_SetScale(6);
    Paint_SelectImage(imageBuffer);
    Paint_Clear(bg);

    cFONT* titleCnFont = &Font36CN;
    cFONT* hintFont = provisioningHintFont();
    cFONT* labelFont = hintFont;
    sFONT* largeValueFont = &Font24;

    drawBitmapMaskClipped(logoX, logoY - stripeY, PHENOSOLAR_LOGO_WIDTH, PHENOSOLAR_LOGO_HEIGHT,
                          phenosolar_logo_white_mask, EPD_7IN3E_WHITE,
                          paintWidth, stripeHeight);

    drawStringENInStripe(leftX, 113, "NETWORK CONFIGURATION", largeValueFont,
                         fg, bg, stripeY, stripeHeight);
    drawStringCNInStripe(titleCnX, 145, "配网设置", titleCnFont,
                         fg, bg, stripeY, stripeHeight);

    drawStringCNInStripe(leftX, 219, "手机扫描右侧二维码", hintFont,
                         fg, bg, stripeY, stripeHeight);
    drawStringCNInStripe(leftX, 258, "连接设备热点进行WiFi配置", hintFont,
                         fg, bg, stripeY, stripeHeight);

    fillRoundedRectClipped(qrBoxX, qrBoxY - stripeY, qrBoxW, qrBoxH, qrRadius,
                           EPD_7IN3E_WHITE, paintWidth, stripeHeight);
    if (!drawProvisioningQrToPaintEx(wifiQrPayload, qrCenterX, qrCenterY - stripeY, qrSize,
                                     paintWidth, stripeHeight, EPD_7IN3E_BLACK)) {
        return false;
    }

    drawStringCNInStripe(leftX, 329, "热点名称", labelFont, fg, bg, stripeY, stripeHeight);
    drawStringENSpacedInStripe(valueX, 329, apSSID.c_str(), largeValueFont,
                               EPD_7IN3E_WHITE, bg, 6, stripeY, stripeHeight);
    drawStringENInStripe(leftX, 370, "IP", largeValueFont, fg, bg, stripeY, stripeHeight);
    drawStringCNInStripe(leftX + 45, 370, "地址", labelFont, fg, bg, stripeY, stripeHeight);
    drawStringENSpacedInStripe(valueX, 370, "192.168.4.1", largeValueFont,
                               EPD_7IN3E_WHITE, bg, 3, stripeY, stripeHeight);

    const char* passwordText = apPassword.length() >= 8 ? apPassword.c_str() : "OPEN";
    drawStringENInStripe(leftX, 444, "AP PASSWORD", &Font12,
                         fg, bg, stripeY, stripeHeight);
    drawStringENInStripe(180, 444, passwordText, &Font12,
                         EPD_7IN3E_WHITE, bg, stripeY, stripeHeight);

    return true;
}

static bool renderAddDeviceFullStripe(UBYTE* imageBuffer, int stripeY, int stripeHeight,
                                       const String& portalUrl, const String& code,
                                       const String& pairingCode) {
    const int paintWidth = EPD_PANEL_WIDTH;
    const UWORD fg = EPD_7IN3E_BLACK;
    const UWORD bg = EPD_7IN3E_GREEN;
    const int logoX = 26;
    const int logoY = 22;
    const int leftX = 78;
    const int qrBoxX = 531;
    const int qrBoxY = 214;
    const int qrBoxW = 195;
    const int qrBoxH = 195;
    const int qrRadius = 10;
    const int qrCenterX = 628;
    const int qrCenterY = 311;
    const int qrSize = 193;

    Paint_NewImage(imageBuffer, paintWidth, stripeHeight, 0, bg);
    Paint_SetScale(6);
    Paint_SelectImage(imageBuffer);
    Paint_Clear(bg);

    cFONT* titleCnFont = &Font36CN;
    cFONT* bodyFont = provisioningHintFont();
    sFONT* smallValueFont = provisioningValueFont();
    sFONT* largeValueFont = &Font24;

    drawBitmapMaskClipped(logoX, logoY - stripeY, PHENOSOLAR_LOGO_WIDTH, PHENOSOLAR_LOGO_HEIGHT,
                          phenosolar_logo_white_mask, EPD_7IN3E_WHITE,
                          paintWidth, stripeHeight);

    drawStringENInStripe(leftX, 112, "ADD DEVICE", largeValueFont,
                         fg, bg, stripeY, stripeHeight);
    drawStringCNInStripe(leftX, 145, "添加设备", titleCnFont,
                         fg, bg, stripeY, stripeHeight);

    drawStringCNInStripe(leftX, 217, "手机扫描右侧二维码", bodyFont,
                         fg, bg, stripeY, stripeHeight);
    drawStringCNInStripe(leftX, 251, "或浏览器地址输入网址", bodyFont,
                         fg, bg, stripeY, stripeHeight);
    drawStringENInStripe(leftX, 288, portalUrl.c_str(), smallValueFont,
                         fg, bg, stripeY, stripeHeight);
    drawStringCNInStripe(leftX, 318, "登录管理网页添加设备", bodyFont,
                          fg, bg, stripeY, stripeHeight);

    drawStringCNInStripe(leftX, 350, "设备码", bodyFont,
                          fg, bg, stripeY, stripeHeight);
    drawStringENSpacedInStripe(190, 350, code.c_str(), largeValueFont,
                               EPD_7IN3E_WHITE, bg, 7, stripeY, stripeHeight);
    drawStringENInStripe(leftX, 394, "PAIR CODE", smallValueFont,
                         fg, bg, stripeY, stripeHeight);
    drawStringENSpacedInStripe(190, 388, pairingCode.c_str(), largeValueFont,
                               EPD_7IN3E_WHITE, bg, 3, stripeY, stripeHeight);

    fillRoundedRectClipped(qrBoxX, qrBoxY - stripeY, qrBoxW, qrBoxH, qrRadius,
                           EPD_7IN3E_WHITE, paintWidth, stripeHeight);
    if (!drawProvisioningQrToPaintEx(portalUrl, qrCenterX, qrCenterY - stripeY, qrSize,
                                     paintWidth, stripeHeight, EPD_7IN3E_BLACK)) {
        return false;
    }

    return true;
}

static void epdWriteCommandByte(UBYTE command) {
    DEV_Digital_Write(EPD_DC_PIN, 0);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(command);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

static void epdWriteDataByte(UBYTE data) {
    DEV_Digital_Write(EPD_DC_PIN, 1);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(data);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

static void epdWriteDataBuffer(const UBYTE* data, int length) {
    DEV_Digital_Write(EPD_DC_PIN, 1);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_Write_nByte((UBYTE*)data, length);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

static bool refreshEpdAfterFullFrame() {
    epdWriteCommandByte(0x04);
    if (!EPD_7in3E_WaitBusy(EPD7IN3_BUSY_INIT_TIMEOUT_MS, "配网页上电")) {
        return false;
    }

    epdWriteCommandByte(0x06);
    epdWriteDataByte(0x6F);
    epdWriteDataByte(0x1F);
    epdWriteDataByte(0x17);
    epdWriteDataByte(0x49);

    epdWriteCommandByte(0x12);
    epdWriteDataByte(0x00);
    if (!EPD_7in3E_WaitBusy(EPD7IN3_BUSY_REFRESH_TIMEOUT_MS, "配网页刷新")) {
        return false;
    }

    epdWriteCommandByte(0x02);
    epdWriteDataByte(0x00);
    return EPD_7in3E_WaitBusy(EPD7IN3_BUSY_INIT_TIMEOUT_MS, "配网页断电");
}

static bool displayProvisioningFullScreen(UBYTE* imageBuffer, const String& apSSID,
                                          const String& wifiQrPayload,
                                          const String& apPassword) {
    const int packedWidth = (EPD_PANEL_WIDTH + 1) / 2;
    EPD_7in3E_ClearBusyTimeout();
    epdWriteCommandByte(0x10);

    for (int stripeY = 0; stripeY < EPD_PANEL_HEIGHT; stripeY += PROVISIONING_FULL_STRIPE_HEIGHT) {
        int stripeHeight = PROVISIONING_FULL_STRIPE_HEIGHT;
        if (stripeY + stripeHeight > EPD_PANEL_HEIGHT) {
            stripeHeight = EPD_PANEL_HEIGHT - stripeY;
        }

        if (!renderProvisioningFullStripe(imageBuffer, stripeY, stripeHeight,
                                          apSSID, wifiQrPayload, apPassword)) {
            return false;
        }

        for (int row = 0; row < stripeHeight; row++) {
            epdWriteDataBuffer(imageBuffer + row * packedWidth, packedWidth);
        }
        EPD_ProvisioningYield();
    }

    return refreshEpdAfterFullFrame();
}

static bool displayAddDeviceFullScreen(UBYTE* imageBuffer, const String& portalUrl,
                                        const String& code, const String& pairingCode) {
    const int packedWidth = (EPD_PANEL_WIDTH + 1) / 2;
    EPD_7in3E_ClearBusyTimeout();
    epdWriteCommandByte(0x10);

    for (int stripeY = 0; stripeY < EPD_PANEL_HEIGHT; stripeY += PROVISIONING_FULL_STRIPE_HEIGHT) {
        int stripeHeight = PROVISIONING_FULL_STRIPE_HEIGHT;
        if (stripeY + stripeHeight > EPD_PANEL_HEIGHT) {
            stripeHeight = EPD_PANEL_HEIGHT - stripeY;
        }

        if (!renderAddDeviceFullStripe(imageBuffer, stripeY, stripeHeight,
                                       portalUrl, code, pairingCode)) {
            return false;
        }

        for (int row = 0; row < stripeHeight; row++) {
            epdWriteDataBuffer(imageBuffer + row * packedWidth, packedWidth);
        }
        EPD_ProvisioningYield();
    }

    return refreshEpdAfterFullFrame();
}

String getCloudPortalUrl() {
    return getCloudApiOrigin() + "/";
}

bool displayProvisioningScreen(const String& apSSID, const String& deviceCode, const String& wifiQrPayload) {
    (void)deviceCode;

    if (apSSID.length() == 0 || wifiQrPayload.length() == 0) {
        Serial.println("⚠️  配网页二维码信息不完整，跳过二维码显示");
        return false;
    }

    Serial.println("📱 开始显示AP配网页二维码...");
    Serial.printf("   SSID: %s\n", apSSID.c_str());
    Serial.printf("   画板: 堆分配 %u 字节 (800x144 条带)\n", (unsigned)PROVISIONING_FULL_STRIPE_SIZE);

    UBYTE* imageBuffer = acquireEpdUiFrame(PROVISIONING_FULL_STRIPE_SIZE);
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
        tryEnterEpdDeepSleep("AP配网页初始化异常收尾");
        dbgSetEpdActive(false);
        releaseEpdUiFrame();
        Serial.println("❌ 配网页二维码显示初始化失败，请检查BUSY线、屏幕供电和排线");
        return false;
    }

    // 本地页面即将覆盖云端图；必须先持久化失效 imgVer，避免改完WiFi后误判“已最新”。
    if (!invalidateCloudImageVersionForLocalUi("AP配网页")) {
        tryEnterEpdDeepSleep("AP配网页取消");
        dbgSetEpdActive(false);
        releaseEpdUiFrame();
        return false;
    }

    EPD_7IN3E_ClearBusyTimeout();
    const bool rendered = displayProvisioningFullScreen(imageBuffer, apSSID, wifiQrPayload,
                                                         getProvisioningApPassword());
    tryEnterEpdDeepSleep(rendered ? "AP配网页刷新" : "AP配网页刷新异常收尾");
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
    String derivedId;
    if (!deriveDeviceIdentity(derivedId)) {
        return "";
    }
    return derivedId;
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
bool saveClaimedStatus(bool claimed) {
    if (!preferences.begin(PREF_NAMESPACE, false)) {
        preferences.end();
        Serial.println("⚠️  NVS命名空间打开失败，无法保存绑定状态");
        return false;
    }
    const bool saved = preferences.putBool(PREF_KEY_CLAIMED, claimed) == sizeof(uint8_t);
    preferences.end();
    if (!saved) {
        Serial.println("⚠️  绑定状态写入NVS失败");
        return false;
    }
    Serial.printf("💾 保存本地绑定状态: %s\n", claimed ? "已绑定" : "未绑定");
    return true;
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
bool saveImageVersion(int64_t version) {
    if (!preferences.begin(PREF_NAMESPACE, false)) {
        preferences.end();
        Serial.println("⚠️  NVS命名空间打开失败，无法保存图片版本");
        return false;
    }
    const bool saved = preferences.putLong64(PREF_KEY_IMG_VER, version) == sizeof(int64_t);
    preferences.end();
    if (!saved) {
        Serial.printf("⚠️  图片版本写入NVS失败: %lld\n", version);
        return false;
    }
    Serial.printf("💾 保存本地图片版本: %lld\n", version);
    return true;
}

/**
 * 读取云端下发的定时唤醒间隔（秒）
 * 返回 0 表示未设置，使用默认 DEEP_SLEEP_INTERVAL_HOURS。
 */
static bool isValidSleepInterval(uint32_t seconds) {
    return seconds >= MIN_SLEEP_INTERVAL_SECONDS &&
           seconds <= MAX_SLEEP_INTERVAL_SECONDS;
}

uint32_t loadSleepInterval() {
    if (!preferences.begin(PREF_NAMESPACE, true)) {
        preferences.end();
        return 0;
    }
    uint32_t interval = preferences.getUInt(PREF_KEY_SLEEP_INTERVAL, 0);
    preferences.end();
    if (interval != 0 && !isValidSleepInterval(interval)) {
        Serial.printf("⚠️  NVS唤醒间隔超出安全范围，忽略: %u 秒（允许 %u~%u）\n",
                      interval, (unsigned)MIN_SLEEP_INTERVAL_SECONDS,
                      (unsigned)MAX_SLEEP_INTERVAL_SECONDS);
        return 0;
    }
    return interval;
}

/**
 * 保存云端下发的定时唤醒间隔（秒）
 */
bool saveSleepInterval(uint32_t seconds) {
    if (!isValidSleepInterval(seconds)) {
        Serial.printf("⚠️  拒绝保存越界唤醒间隔: %u 秒（允许 %u~%u）\n",
                      seconds, (unsigned)MIN_SLEEP_INTERVAL_SECONDS,
                      (unsigned)MAX_SLEEP_INTERVAL_SECONDS);
        return false;
    }
    if (!preferences.begin(PREF_NAMESPACE, false)) {
        preferences.end();
        Serial.println("⚠️  NVS命名空间打开失败，无法保存唤醒间隔");
        return false;
    }
    const bool saved = preferences.putUInt(PREF_KEY_SLEEP_INTERVAL, seconds) == sizeof(uint32_t);
    preferences.end();
    if (!saved) {
        Serial.printf("⚠️  唤醒间隔写入NVS失败: %u 秒\n", seconds);
        return false;
    }
    Serial.printf("💾 保存本地唤醒间隔: %u 秒\n", seconds);
    return true;
}

static bool isValidDeviceKey(const String& key) {
    if (key.length() != DEVICE_KEY_HEX_LENGTH) {
        return false;
    }
    for (size_t i = 0; i < key.length(); i++) {
        const char c = key.charAt(i);
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

static bool isValidSha256Hex(const String& hash) {
    if (hash.length() != 64) {
        return false;
    }
    for (size_t i = 0; i < hash.length(); i++) {
        const char c = hash.charAt(i);
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

static int sha256Starts(mbedtls_sha256_context* context) {
#if MBEDTLS_VERSION_MAJOR >= 3
    return mbedtls_sha256_starts(context, 0);
#else
    return mbedtls_sha256_starts_ret(context, 0);
#endif
}

static int sha256Update(mbedtls_sha256_context* context, const uint8_t* data, size_t length) {
#if MBEDTLS_VERSION_MAJOR >= 3
    return mbedtls_sha256_update(context, data, length);
#else
    return mbedtls_sha256_update_ret(context, data, length);
#endif
}

static int sha256Finish(mbedtls_sha256_context* context, uint8_t output[32]) {
#if MBEDTLS_VERSION_MAJOR >= 3
    return mbedtls_sha256_finish(context, output);
#else
    return mbedtls_sha256_finish_ret(context, output);
#endif
}

static String sha256ToHex(const uint8_t digest[32]) {
    static const char hexDigits[] = "0123456789abcdef";
    char output[65];
    for (size_t i = 0; i < 32; i++) {
        output[i * 2] = hexDigits[(digest[i] >> 4) & 0x0F];
        output[i * 2 + 1] = hexDigits[digest[i] & 0x0F];
    }
    output[64] = '\0';
    return String(output);
}

static bool isValidPairingCode(const String& code) {
    if (code.length() != 6) {
        return false;
    }
    for (size_t i = 0; i < code.length(); i++) {
        const char c = code.charAt(i);
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

static bool loadOrCreateDeviceKey() {
    if (isValidDeviceKey(g_deviceKey)) {
        return true;
    }

    // 只打开一次读写句柄：既能创建首次使用的命名空间，也避免只读打开瞬时失败时误生成新密钥。
    if (!preferences.begin(PREF_NAMESPACE, false)) {
        preferences.end();
        Serial.println("❌ 无法打开NVS，设备密钥不可用；禁止发送无认证请求");
        return false;
    }

    String storedKey = preferences.getString(PREF_KEY_DEVICE_KEY, "");
    if (isValidDeviceKey(storedKey)) {
        preferences.end();
        storedKey.toUpperCase();
        g_deviceKey = storedKey;
        Serial.println("🔐 已加载设备身份密钥");
        return true;
    }
    if (storedKey.length() > 0) {
        Serial.println("⚠️  NVS中的设备密钥格式无效，将重新生成");
    }

    uint8_t randomKey[DEVICE_KEY_BYTES];
    esp_fill_random(randomKey, sizeof(randomKey));

    char keyHex[DEVICE_KEY_HEX_LENGTH + 1];
    static const char hexDigits[] = "0123456789ABCDEF";
    for (size_t i = 0; i < sizeof(randomKey); i++) {
        keyHex[i * 2] = hexDigits[(randomKey[i] >> 4) & 0x0F];
        keyHex[i * 2 + 1] = hexDigits[randomKey[i] & 0x0F];
    }
    keyHex[DEVICE_KEY_HEX_LENGTH] = '\0';

    const size_t written = preferences.putString(PREF_KEY_DEVICE_KEY, keyHex);
    const String verifiedKey = preferences.getString(PREF_KEY_DEVICE_KEY, "");
    preferences.end();

    if (written != DEVICE_KEY_HEX_LENGTH || verifiedKey != keyHex) {
        Serial.println("❌ 设备密钥写入或回读校验失败；禁止发送无认证请求");
        g_deviceKey = "";
        return false;
    }

    g_deviceKey = keyHex;
    Serial.println("🔐 已首次生成并保存设备身份密钥");
    return true;
}

static bool isTrustedDeviceImageUrl(const String& imageUrl) {
    if (deviceId.length() == 0) {
        return false;
    }
    const String expectedBase = getCloudApiOrigin() + "/api/epd/raw/" + deviceId;
    if (!imageUrl.startsWith(expectedBase)) {
        return false;
    }
    return imageUrl.length() == expectedBase.length() ||
           imageUrl.charAt(expectedBase.length()) == '?';
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
bool displayDeviceCode(const String& pairingCode) {
    if (!isValidPairingCode(pairingCode)) {
        Serial.println("❌ 配对码必须是6位数字，取消设备码页显示");
        return false;
    }
    if (deviceId.length() == 0) {
        deviceId = getDeviceIdFromMac();
        Serial.printf("ℹ️  设备码未预先生成，现按当前MAC计算: %s\n", deviceId.c_str());
    }
    if (deviceId.length() == 0) {
        Serial.println("❌ 无法生成设备码，取消设备码页显示");
        return false;
    }

    Serial.println("📱 开始显示设备码...");
    Serial.print("⭐ 设备码: ");
    Serial.println(deviceId);
    Serial.printf("🔑 配对码: %s\n", pairingCode.c_str());
    String portalUrl = getCloudPortalUrl() + "index.html";
    Serial.printf("🌐 云端配置页: %s\n", portalUrl.c_str());
    
    // 默认使用 7.3" E6 屏
    if (EPD_dispIndex < 0 || EPD_dispIndex >= (sizeof(EPD_dispMass) / sizeof(EPD_dispMass[0]))) {
        EPD_dispIndex = 0;
    }
    
    ensureDisplayHardwareReady();
    EPD_7IN3E_ClearBusyTimeout();
    EPD_dispInit();
    if (EPD_7IN3E_LastBusyTimeout()) {
        tryEnterEpdDeepSleep("设备码页初始化异常收尾");
        Serial.println("❌ 设备码显示初始化失败，请检查BUSY线、屏幕供电和排线");
        return false;
    }

    UBYTE* imageBuffer = acquireEpdUiFrame(PROVISIONING_FULL_STRIPE_SIZE);
    if (imageBuffer == nullptr) {
        Serial.println("❌ 设备码画布分配失败");
        tryEnterEpdDeepSleep("设备码页取消");
        return false;
    }

    Serial.printf("   画板: 堆分配 %u 字节 (800x144 条带)\n",
                  (unsigned)PROVISIONING_FULL_STRIPE_SIZE);

    // 设备码页会覆盖云端图；先失效版本，重新绑定后即使版本号未变也会重新拉取。
    if (!invalidateCloudImageVersionForLocalUi("设备码页")) {
        releaseEpdUiFrame();
        tryEnterEpdDeepSleep("设备码页取消");
        return false;
    }

    EPD_7IN3E_ClearBusyTimeout();
    bool rendered = displayAddDeviceFullScreen(imageBuffer, portalUrl, deviceId, pairingCode);
    const bool refreshCompleted = rendered && !EPD_7IN3E_LastBusyTimeout();
    tryEnterEpdDeepSleep(refreshCompleted ? "设备码页刷新" : "设备码页刷新异常收尾");

    releaseEpdUiFrame();

    if (!refreshCompleted) {
        Serial.println("❌ 设备码显示未正常完成，请检查BUSY线、屏幕供电和排线");
        return false;
    } else {
        Serial.println("✅ 设备码已显示在屏幕上");
    }
    return true;
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
    bool diagnosticAccepted;
    int64_t imageVersion;
    String imageUrl;
    String imageSha256;
    uint32_t nextSleepSeconds;
    String pairingCode;
    String error;
};

static String updateAttemptIdHex(uint64_t attemptId) {
    char buffer[17];
    snprintf(buffer, sizeof(buffer), "%08lx%08lx",
             (unsigned long)(attemptId >> 32), (unsigned long)(attemptId & 0xFFFFFFFFULL));
    return String(buffer);
}

static bool appendDeviceDiagnosticTelemetry(JsonDocument& doc) {
    doc["firmwareVersion"] = FIRMWARE_VERSION;
    doc["firmwareBuild"] = FIRMWARE_BUILD;
    doc["resetReason"] = resetReasonName();
    doc["localImageVersion"] = localImageVersion;
    doc["gpio0StuckLow"] = g_rtcGpio0StuckLow;

    const bool hasPendingUpdateDiagnostic =
        g_updateDiagnosticLoaded && g_updateDiagnostic.reportPending != 0;
    doc["diagnosticPresent"] = hasPendingUpdateDiagnostic || g_rtcGpio0StuckLow;
    if (!hasPendingUpdateDiagnostic) {
        return g_rtcGpio0StuckLow;
    }

    doc["targetImageVersion"] = g_updateDiagnostic.targetImageVersion;
    doc["updateAttemptId"] = updateAttemptIdHex(g_updateDiagnostic.attemptId);
    doc["updateResult"] = updateResultName((UpdateResultCode)g_updateDiagnostic.result);
    doc["updateStage"] = updateStageName((UpdateStageCode)g_updateDiagnostic.stage);
    doc["updateError"] = updateErrorName((UpdateErrorCode)g_updateDiagnostic.error);
    doc["updateDurationMs"] = g_updateDiagnostic.durationMs;
    return true;
}

static void clearAcknowledgedDeviceDiagnostics() {
    if (g_updateDiagnosticLoaded && g_updateDiagnostic.reportPending != 0) {
        clearUpdateDiagnostic();
    }
    g_rtcGpio0StuckLow = false;
}

static bool reportUpdateDiagnosticNow() {
    if (!g_updateDiagnosticLoaded || g_updateDiagnostic.reportPending == 0) {
        return true;
    }
    if (WiFi.status() != WL_CONNECTED || !loadOrCreateDeviceKey()) {
        Serial.println("⚠️  更新结果暂时无法回传，将在下次唤醒补报");
        return false;
    }

    CloudApiClient cloudClient;
    HTTPClient http;
    const String url = getCloudApiOrigin() + "/api/device/update-result";
    if (!beginCloudApiRequest(http, cloudClient, url)) {
        Serial.println("⚠️  更新结果请求初始化失败，将在下次唤醒补报");
        return false;
    }
    http.setTimeout(CLOUD_API_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.addHeader("Content-Type", "application/json");
    http.addHeader(DEVICE_KEY_HEADER, g_deviceKey);

    JsonDocument doc;
    doc["deviceId"] = deviceId;
    appendDeviceDiagnosticTelemetry(doc);
    String requestBody;
    serializeJson(doc, requestBody);

    const int httpCode = http.POST(requestBody);
    if (httpCode != HTTP_CODE_OK && httpCode != HTTP_CODE_CREATED) {
        Serial.printf("⚠️  更新结果回传失败: HTTP %d，将在下次唤醒补报\n", httpCode);
        http.end();
        return false;
    }
    const int responseLength = http.getSize();
    if (responseLength > 2048) {
        Serial.println("⚠️  更新结果响应过大，保留本地待补报记录");
        http.end();
        return false;
    }
    const String response = http.getString();
    http.end();
    JsonDocument responseDoc;
    const DeserializationError jsonError = deserializeJson(responseDoc, response);
    if (jsonError || !responseDoc["success"].as<bool>() ||
        !responseDoc["diagnosticAccepted"].as<bool>()) {
        Serial.println("⚠️  云端未确认更新结果，保留本地待补报记录");
        return false;
    }

    Serial.printf("✅ 更新结果已回传: result=%s, stage=%s, error=%s\n",
                  updateResultName((UpdateResultCode)g_updateDiagnostic.result),
                  updateStageName((UpdateStageCode)g_updateDiagnostic.stage),
                  updateErrorName((UpdateErrorCode)g_updateDiagnostic.error));
    clearAcknowledgedDeviceDiagnostics();
    return true;
}

/**
 * 向云端查询设备状态
 */
DeviceStatusResponse queryDeviceStatus() {
    DeviceStatusResponse result = {false, false, false, 0, "", "", 0, "", ""};
    
    if (WiFi.status() != WL_CONNECTED) {
        result.error = "WiFi未连接";
        return result;
    }
    if (!loadOrCreateDeviceKey()) {
        result.error = "设备密钥不可用";
        Serial.println("❌ 设备密钥不可用，禁止发送无认证状态请求");
        return result;
    }
    
    CloudApiClient cloudClient;
    HTTPClient http;
    String url = getCloudApiOrigin() + "/api/device/status";
    
    Serial.printf("📡 查询设备状态: %s\n", url.c_str());
    
    if (!beginCloudApiRequest(http, cloudClient, url)) {
        result.error = "云端连接初始化失败";
        Serial.println("❌ 状态请求连接初始化失败");
        return result;
    }
    http.setTimeout(CLOUD_API_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    http.addHeader(DEVICE_KEY_HEADER, g_deviceKey);
    
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

    JsonDocument doc;
    doc["deviceId"] = deviceId;
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    doc["uptime_ms"] = (uint32_t)millis();
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["wakeType"] = wakeType;
    doc["wakeCause"] = wakeCauseText;
    uint32_t currentSleepSeconds = loadSleepInterval();
    if (currentSleepSeconds == 0) {
        currentSleepSeconds = DEFAULT_SLEEP_INTERVAL_SECONDS;
    }
    doc["currentSleepSeconds"] = currentSleepSeconds;
    const bool diagnosticIncluded = appendDeviceDiagnosticTelemetry(doc);
    String requestBody;
    serializeJson(doc, requestBody);
    Serial.printf("   上报状态: ip=%s, rssi=%d dBm, uptime=%lu ms, freeHeap=%lu, wakeType=%s, wakeCause=%s, currentSleepSeconds=%u\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.RSSI(),
                  (unsigned long)millis(),
                  (unsigned long)ESP.getFreeHeap(),
                  wakeType,
                  wakeCauseText,
                  currentSleepSeconds);
    
    const uint32_t statusRequestStartedAt = millis();
    int httpCode = http.POST(requestBody);
    if ((uint32_t)(millis() - statusRequestStartedAt) >= CLOUD_STATUS_TOTAL_TIMEOUT_MS) {
        result.error = "状态请求超过总时限";
        Serial.printf("❌ 状态请求超过绝对总时限: %u ms\n",
                      (unsigned)CLOUD_STATUS_TOTAL_TIMEOUT_MS);
        http.end();
        return result;
    }
    
    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
        const int declaredResponseLength = http.getSize();
        if (declaredResponseLength > (int)CLOUD_STATUS_MAX_RESPONSE_BYTES) {
            result.error = "状态响应超过8KB上限";
            Serial.printf("❌ 状态响应Content-Length过大: %d 字节（上限 %u）\n",
                          declaredResponseLength, (unsigned)CLOUD_STATUS_MAX_RESPONSE_BYTES);
            http.end();
            return result;
        }
        if (declaredResponseLength < -1) {
            result.error = "状态响应长度无效";
            Serial.printf("❌ 状态响应Content-Length无效: %d\n", declaredResponseLength);
            http.end();
            return result;
        }

        String response;
        const size_t reserveBytes = declaredResponseLength > 0
                                      ? (size_t)declaredResponseLength + 1
                                      : (size_t)CLOUD_STATUS_MAX_RESPONSE_BYTES + 1;
        if (!response.reserve((unsigned int)reserveBytes)) {
            result.error = "状态响应缓冲区分配失败";
            Serial.printf("❌ 无法为状态响应预留 %u 字节堆内存\n", (unsigned)reserveBytes);
            http.end();
            return result;
        }

        const uint32_t statusElapsedMs = (uint32_t)(millis() - statusRequestStartedAt);
        if (statusElapsedMs >= CLOUD_STATUS_TOTAL_TIMEOUT_MS) {
            result.error = "状态响应超过总时限";
            Serial.printf("❌ 状态响应读取前已耗尽 %u ms 总预算\n",
                          (unsigned)CLOUD_STATUS_TOTAL_TIMEOUT_MS);
            http.end();
            return result;
        }
        const uint32_t statusRemainingMs = CLOUD_STATUS_TOTAL_TIMEOUT_MS - statusElapsedMs;
        const uint32_t responseIdleTimeoutMs = statusRemainingMs < CLOUD_API_TIMEOUT_MS
                                                   ? statusRemainingMs
                                                   : CLOUD_API_TIMEOUT_MS;
        http.setTimeout((uint16_t)responseIdleTimeoutMs);
        LimitedStringStream responseStream(response, CLOUD_STATUS_MAX_RESPONSE_BYTES,
                                           statusRequestStartedAt, CLOUD_STATUS_TOTAL_TIMEOUT_MS);
        const int responseBytes = http.writeToStream(&responseStream);
        if (responseStream.totalBudgetExceeded() ||
            (uint32_t)(millis() - statusRequestStartedAt) >= CLOUD_STATUS_TOTAL_TIMEOUT_MS) {
            result.error = "状态响应超过总时限";
            Serial.printf("❌ 状态响应读取超过绝对总时限: %u ms\n",
                          (unsigned)CLOUD_STATUS_TOTAL_TIMEOUT_MS);
            http.end();
            return result;
        }
        if (responseStream.overflowed()) {
            result.error = "状态响应超过8KB上限";
            Serial.printf("❌ 未知长度/分块状态响应超过 %u 字节上限，已中止读取\n",
                          (unsigned)CLOUD_STATUS_MAX_RESPONSE_BYTES);
            http.end();
            return result;
        }
        if (responseStream.allocationFailed()) {
            result.error = "状态响应读取时内存不足";
            Serial.println("❌ 状态响应受限流式读取时内存分配失败");
            http.end();
            return result;
        }
        if (responseBytes < 0) {
            result.error = "状态响应读取失败";
            Serial.printf("❌ 状态响应读取失败: %s (%d)\n",
                          http.errorToString(responseBytes).c_str(), responseBytes);
            http.end();
            return result;
        }
        if (responseBytes == 0 || response.length() != (size_t)responseBytes) {
            result.error = "状态响应为空或长度不一致";
            Serial.printf("❌ 状态响应长度不一致: stream=%d, string=%u\n",
                          responseBytes, (unsigned)response.length());
            http.end();
            return result;
        }

        Serial.printf("✅ 云端响应: %s\n", response.c_str());
        
        JsonDocument respDoc;
        DeserializationError error = deserializeJson(respDoc, response);
        
        if (!error) {
            const bool apiSuccess = respDoc["success"].as<bool>();
            if (!apiSuccess) {
                result.error = respDoc["error"].is<String>()
                                   ? respDoc["error"].as<String>()
                                   : String("云端拒绝状态请求");
                Serial.printf("❌ 云端状态请求失败: %s\n", result.error.c_str());
                http.end();
                return result;
            }

            result.success = true;
            result.claimed = respDoc["claimed"].as<bool>();
            result.diagnosticAccepted = respDoc["diagnosticAccepted"].as<bool>();
            
            if (respDoc["imageVersion"].is<long long>()) {
                result.imageVersion = respDoc["imageVersion"].as<long long>();
            } else if (respDoc["imageVersion"].is<int>()) {
                // 兼容旧版本（int 类型）
                result.imageVersion = respDoc["imageVersion"].as<int>();
            }
            
            if (respDoc["imageUrl"].is<String>()) {
                result.imageUrl = respDoc["imageUrl"].as<String>();
            }

            if (respDoc["imageSha256"].is<String>()) {
                result.imageSha256 = respDoc["imageSha256"].as<String>();
                result.imageSha256.trim();
                if (result.imageSha256.length() > 0 && !isValidSha256Hex(result.imageSha256)) {
                    result.success = false;
                    result.error = "云端返回的imageSha256格式无效";
                    Serial.println("❌ 云端返回的图片SHA-256格式无效，拒绝下载");
                    http.end();
                    return result;
                }
            }

            if (respDoc["nextSleepSeconds"].is<uint32_t>()) {
                const uint32_t candidateSleepSeconds = respDoc["nextSleepSeconds"].as<uint32_t>();
                if (isValidSleepInterval(candidateSleepSeconds)) {
                    result.nextSleepSeconds = candidateSleepSeconds;
                } else {
                    Serial.printf("⚠️  云端nextSleepSeconds越界，忽略且不保存: %u 秒（允许 %u~%u）\n",
                                  candidateSleepSeconds,
                                  (unsigned)MIN_SLEEP_INTERVAL_SECONDS,
                                  (unsigned)MAX_SLEEP_INTERVAL_SECONDS);
                }
            } else if (!respDoc["nextSleepSeconds"].isNull()) {
                Serial.println("⚠️  云端nextSleepSeconds类型无效，忽略且不保存");
            }

            if (!result.claimed && respDoc["pairingCode"].is<String>()) {
                result.pairingCode = respDoc["pairingCode"].as<String>();
                result.pairingCode.trim();
            }

            if (!result.claimed && !isValidPairingCode(result.pairingCode)) {
                result.success = false;
                result.error = "认证响应缺少有效pairingCode";
                Serial.println("❌ 未绑定设备未收到有效配对码，拒绝降级为旧设备码绑定流程");
                http.end();
                return result;
            }

            if (result.nextSleepSeconds > 0) {
                if (saveSleepInterval(result.nextSleepSeconds)) {
                    Serial.printf("   云端下发唤醒间隔: %u 秒\n", result.nextSleepSeconds);
                }
            }

            if (diagnosticIncluded && result.diagnosticAccepted) {
                clearAcknowledgedDeviceDiagnostics();
                Serial.println("✅ 云端已确认上次待补报诊断");
            }
            
            Serial.printf("   绑定状态: %s\n", result.claimed ? "已绑定" : "未绑定");
            Serial.printf("   图片版本: %lld\n", result.imageVersion);
            if (!result.claimed) {
                Serial.printf("   配对码: %s\n", result.pairingCode.c_str());
            }
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
bool downloadImageToFlash(const String& imageUrl, const String& expectedSha256) {
    Serial.println("\n========== 开始下载图片 ==========");
    Serial.printf("   URL: %s\n", imageUrl.c_str());
    Serial.printf("   剩余内存: %d 字节\n", ESP.getFreeHeap());

    if (!isTrustedDeviceImageUrl(imageUrl)) {
        Serial.println("❌ 图片URL不是受信云端raw端点，拒绝发送设备密钥");
        setUpdateError(UPDATE_ERROR_DOWNLOAD_HTTP);
        return false;
    }
    if (!loadOrCreateDeviceKey()) {
        Serial.println("❌ 设备密钥不可用，禁止发送无认证图片请求");
        setUpdateError(UPDATE_ERROR_DOWNLOAD_HTTP);
        return false;
    }
    if (expectedSha256.length() > 0 && !isValidSha256Hex(expectedSha256)) {
        Serial.println("❌ 目标图片SHA-256格式无效，拒绝下载");
        setUpdateError(UPDATE_ERROR_SHA_MISMATCH);
        return false;
    }
    
    // 清除旧文件并创建新文件
    if (SPIFFS.exists(FLASH_TEMP_FILE)) {
        SPIFFS.remove(FLASH_TEMP_FILE);
    }
    
    flashTempFile = SPIFFS.open(FLASH_TEMP_FILE, "w");
    if (!flashTempFile) {
        Serial.println("❌ 无法创建Flash临时文件");
        setUpdateError(UPDATE_ERROR_SPIFFS_WRITE);
        return false;
    }
    flashTempFileOpen = true;
    flashTempFileSize = 0;
    
    const uint32_t downloadStartedAt = millis();
    CloudApiClient cloudClient;
    HTTPClient http;
    if (!beginCloudApiRequest(http, cloudClient, imageUrl)) {
        Serial.println("❌ 图片请求连接初始化失败");
        setUpdateError(UPDATE_ERROR_DOWNLOAD_HTTP);
        flashTempFile.close();
        flashTempFileOpen = false;
        SPIFFS.remove(FLASH_TEMP_FILE);
        return false;
    }
    
    http.setTimeout(CLOUD_DOWNLOAD_TIMEOUT_MS);
    // 禁止重定向，避免认证头被带到非预期主机。
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.addHeader(DEVICE_KEY_HEADER, g_deviceKey);
    const char* responseHeaderKeys[] = {"Transfer-Encoding"};
    http.collectHeaders(responseHeaderKeys, 1);
    
    int httpCode = http.GET();
    Serial.printf("   HTTP状态码: %d\n", httpCode);
    if ((uint32_t)(millis() - downloadStartedAt) >= CLOUD_DOWNLOAD_TOTAL_TIMEOUT_MS) {
        Serial.printf("❌ 图片请求超过绝对总时限: %u ms\n",
                      (unsigned)CLOUD_DOWNLOAD_TOTAL_TIMEOUT_MS);
        setUpdateError(UPDATE_ERROR_DOWNLOAD_TIMEOUT);
        http.end();
        flashTempFile.close();
        flashTempFileOpen = false;
        SPIFFS.remove(FLASH_TEMP_FILE);
        flashTempFileSize = 0;
        return false;
    }
    
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("❌ HTTP下载失败: %d\n", httpCode);
        if (httpCode == HTTP_CODE_CONFLICT) {
            setUpdateError(UPDATE_ERROR_VERSION_EXPIRED);
            g_retrySleepSeconds = WAKEUP_STUCK_LOW_RETRY_SECONDS;
        } else {
            setUpdateError(UPDATE_ERROR_DOWNLOAD_HTTP);
        }
        http.end();
        flashTempFile.close();
        flashTempFileOpen = false;
        SPIFFS.remove(FLASH_TEMP_FILE);
        return false;
    }

    String transferEncoding = http.header("Transfer-Encoding");
    transferEncoding.toLowerCase();
    if (transferEncoding.indexOf("chunked") >= 0) {
        Serial.println("❌ 图片响应使用chunked编码；当前严格流式校验不接受分块帧，拒绝下载");
        setUpdateError(UPDATE_ERROR_DOWNLOAD_HTTP);
        http.end();
        flashTempFile.close();
        flashTempFileOpen = false;
        SPIFFS.remove(FLASH_TEMP_FILE);
        flashTempFileSize = 0;
        return false;
    }
    
    int contentLength = http.getSize();
    Serial.printf("   内容长度: %d 字节 (%.2f KB)\n", contentLength, contentLength / 1024.0);

    // 设备端最小防护：如果云端返回了 Content-Length，但不是期望长度，直接判失败
    // 这样可以避免把“坏/半截数据”交给 EPD 驱动，导致 busy 卡死
    if (contentLength > 0 && contentLength != EPD_EXPECTED_CHARS) {
        Serial.printf("❌ 内容长度异常，期望 %d，实际 %d，放弃下载\n", EPD_EXPECTED_CHARS, contentLength);
        setUpdateError(UPDATE_ERROR_SIZE_MISMATCH);
        http.end();
        flashTempFile.close();
        flashTempFileOpen = false;
        SPIFFS.remove(FLASH_TEMP_FILE);
        flashTempFileSize = 0;
        return false;
    }
    
    // 流式下载，分块写入SPIFFS
    WiFiClient *stream = http.getStreamPtr();
    if (stream == nullptr) {
        Serial.println("❌ 无法获取图片响应数据流");
        setUpdateError(UPDATE_ERROR_DOWNLOAD_HTTP);
        http.end();
        flashTempFile.close();
        flashTempFileOpen = false;
        SPIFFS.remove(FLASH_TEMP_FILE);
        flashTempFileSize = 0;
        return false;
    }

    mbedtls_sha256_context shaContext;
    mbedtls_sha256_init(&shaContext);
    if (sha256Starts(&shaContext) != 0) {
        Serial.println("❌ SHA-256校验器初始化失败");
        setUpdateError(UPDATE_ERROR_SHA_MISMATCH);
        mbedtls_sha256_free(&shaContext);
        http.end();
        flashTempFile.close();
        flashTempFileOpen = false;
        SPIFFS.remove(FLASH_TEMP_FILE);
        flashTempFileSize = 0;
        return false;
    }

    uint8_t buffer[512];  // 512字节缓冲区
    int totalRead = 0;
    unsigned long lastDataTime = millis();
    bool invalidPayload = false;
    bool flashWriteFailed = false;
    bool idleTimedOut = false;
    bool totalTimedOut = false;
    bool oversizedPayload = false;
    bool shaFailed = false;
    
    while (http.connected() && (contentLength > 0 || contentLength == -1)) {
        if ((uint32_t)(millis() - downloadStartedAt) >= CLOUD_DOWNLOAD_TOTAL_TIMEOUT_MS) {
            totalTimedOut = true;
            Serial.printf("❌ 图片下载超过绝对总时限: %u ms\n",
                          (unsigned)CLOUD_DOWNLOAD_TOTAL_TIMEOUT_MS);
            break;
        }
        if (millis() - lastDataTime > CLOUD_DOWNLOAD_TIMEOUT_MS) {
            idleTimedOut = true;
            Serial.printf("❌ 下载连续 %u ms 无数据，判定空闲超时\n",
                          (unsigned)CLOUD_DOWNLOAD_TIMEOUT_MS);
            break;
        }
        
        size_t available = stream->available();
        if (available) {
            int bytesToRead = (available > sizeof(buffer)) ? sizeof(buffer) : available;
            int bytesRead = stream->readBytes(buffer, bytesToRead);

            if (bytesRead <= 0) {
                delay(10);
                continue;
            }

            lastDataTime = millis();

            if (totalRead + bytesRead > EPD_EXPECTED_CHARS) {
                Serial.printf("❌ 图片数据超长：已接收 %d，本块 %d，最大 %d\n",
                              totalRead, bytesRead, EPD_EXPECTED_CHARS);
                oversizedPayload = true;
                break;
            }

            // EPD 文本协议只允许 a~p；边下载边校验，禁止等长错误页/脏数据进入屏幕。
            for (int i = 0; i < bytesRead; i++) {
                if (buffer[i] < 'a' || buffer[i] > 'p') {
                    Serial.printf("❌ 图片数据含非法字符: offset=%d, value=0x%02X\n",
                                  totalRead + i, buffer[i]);
                    invalidPayload = true;
                    break;
                }
            }
            if (invalidPayload) {
                break;
            }

            if (sha256Update(&shaContext, buffer, (size_t)bytesRead) != 0) {
                Serial.println("❌ SHA-256流式计算失败");
                shaFailed = true;
                break;
            }

            // 直接写入Flash
            const size_t bytesWritten = flashTempFile.write(buffer, bytesRead);
            if (bytesWritten != (size_t)bytesRead) {
                Serial.printf("❌ SPIFFS写入不完整: 期望 %d，实际 %u\n",
                              bytesRead, (unsigned)bytesWritten);
                flashWriteFailed = true;
                break;
            }
            flashTempFileSize += (int)bytesWritten;
            totalRead += (int)bytesWritten;
            
            if (contentLength > 0) {
                contentLength -= bytesRead;
            }
            
            // 每64KB输出一次进度
            if (totalRead % 65536 == 0) {
                Serial.printf("   已下载: %.2f KB\n", totalRead / 1024.0);
            }

            // Content-Length未知时继续等到对端明确关闭；若再来任何数据，上方超长检查会拒绝。
            // 这避免只取前384000字节便把带尾随数据的响应误判为完整图片。
        } else {
            delay(10);
        }
    }

    if ((uint32_t)(millis() - downloadStartedAt) >= CLOUD_DOWNLOAD_TOTAL_TIMEOUT_MS) {
        totalTimedOut = true;
    }

    uint8_t actualSha256[32] = {0};
    if (!shaFailed && !totalTimedOut && sha256Finish(&shaContext, actualSha256) != 0) {
        Serial.println("❌ SHA-256计算收尾失败");
        shaFailed = true;
    }
    mbedtls_sha256_free(&shaContext);
    
    flashTempFile.flush();
    flashTempFile.close();
    flashTempFileOpen = false;
    
    http.end();

    setUpdateStage(UPDATE_STAGE_VERIFY);

    if (invalidPayload || flashWriteFailed || idleTimedOut || totalTimedOut || oversizedPayload || shaFailed) {
        if (invalidPayload) {
            setUpdateError(UPDATE_ERROR_CHARSET_INVALID);
        } else if (flashWriteFailed) {
            setUpdateError(UPDATE_ERROR_SPIFFS_WRITE);
        } else if (idleTimedOut || totalTimedOut) {
            setUpdateError(UPDATE_ERROR_DOWNLOAD_TIMEOUT);
        } else if (oversizedPayload) {
            setUpdateError(UPDATE_ERROR_SIZE_MISMATCH);
        } else {
            setUpdateError(UPDATE_ERROR_SHA_MISMATCH);
        }
        SPIFFS.remove(FLASH_TEMP_FILE);
        flashTempFileSize = 0;
        Serial.println("========== 下载失败 ==========");
        return false;
    }
    
    // 检查下载结果
    Serial.printf("✅ 下载完成: %d 字符 (%.2f KB)\n", flashTempFileSize, flashTempFileSize / 1024.0);
    Serial.printf("   期望大小: %d 字符\n", EPD_EXPECTED_CHARS);

    // 设备端最小防护：只要不是“完全匹配”，就视为失败并删除临时文件
    if (flashTempFileSize != EPD_EXPECTED_CHARS) {
        Serial.printf("❌ 下载不完整：期望 %d，实际 %d，删除临时文件并放弃本次刷新\n",
                      EPD_EXPECTED_CHARS, flashTempFileSize);
        SPIFFS.remove(FLASH_TEMP_FILE);
        flashTempFileSize = 0;
        setUpdateError(UPDATE_ERROR_SIZE_MISMATCH);
        Serial.println("========== 下载失败 ==========\n");
        return false;
    }

    const String actualSha256Hex = sha256ToHex(actualSha256);
    if (expectedSha256.length() > 0) {
        if (!actualSha256Hex.equalsIgnoreCase(expectedSha256)) {
            Serial.printf("❌ 图片SHA-256不匹配：期望 %s，实际 %s\n",
                          expectedSha256.c_str(), actualSha256Hex.c_str());
            SPIFFS.remove(FLASH_TEMP_FILE);
            flashTempFileSize = 0;
            setUpdateError(UPDATE_ERROR_SHA_MISMATCH);
            Serial.println("========== 下载失败 ==========\n");
            return false;
        }
        Serial.println("✅ 图片SHA-256校验通过");
    } else {
        Serial.printf("⚠️  云端未提供图片SHA-256，仅完成长度/字符校验；实际摘要=%s\n",
                      actualSha256Hex.c_str());
    }
    
    Serial.println("========== 下载完成 ==========\n");
    return true;
}

/**
 * 显示下载的图片（从Flash读取并刷新EPD）
 */
static void setBusyErrorForCurrentStage() {
    switch ((UpdateStageCode)g_updateDiagnostic.stage) {
        case UPDATE_STAGE_EPD_REFRESH:
            setUpdateError(UPDATE_ERROR_BUSY_REFRESH);
            break;
        case UPDATE_STAGE_EPD_POWER_OFF:
            setUpdateError(UPDATE_ERROR_BUSY_POWER_OFF);
            break;
        default:
            setUpdateError(UPDATE_ERROR_BUSY_POWER_ON);
            break;
    }
}

bool displayDownloadedImage() {
    Serial.println("📺 开始显示图片...");
    
    if (!SPIFFS.exists(FLASH_TEMP_FILE)) {
        Serial.println("❌ 临时文件不存在");
        setUpdateError(UPDATE_ERROR_SIZE_MISMATCH);
        return false;
    }

    // 设备端最小防护：显示前再做一次长度检查，避免 EPD 驱动因数据异常 busy 卡死
    {
        File f = SPIFFS.open(FLASH_TEMP_FILE, "r");
        if (!f) {
            Serial.println("❌ 无法打开临时文件");
            setUpdateError(UPDATE_ERROR_SPIFFS_WRITE);
            SPIFFS.remove(FLASH_TEMP_FILE);
            return false;
        }
        size_t sz = f.size();
        f.close();
        if ((int)sz != EPD_EXPECTED_CHARS) {
            Serial.printf("❌ 临时文件大小异常：期望 %d，实际 %d；跳过刷新并删除临时文件\n",
                          EPD_EXPECTED_CHARS, (int)sz);
            setUpdateError(UPDATE_ERROR_SIZE_MISMATCH);
            SPIFFS.remove(FLASH_TEMP_FILE);
            return false;
        }
    }
    
    // 初始化EPD
    if (EPD_dispIndex < 0 || EPD_dispIndex >= (sizeof(EPD_dispMass) / sizeof(EPD_dispMass[0]))) {
        EPD_dispIndex = 0;
    }
    ensureDisplayHardwareReady();
    setUpdateStage(UPDATE_STAGE_EPD_POWER_ON);
    EPD_7in3E_SetUpdateStageCallback(onEpdUpdateStage);
    EPD_7in3E_ClearBusyTimeout();
    EPD_7IN3E_ClearBusyTimeout();
    EPD_dispInit();
    if (EPD_7IN3E_LastBusyTimeout()) {
        Serial.println("❌ EPD初始化失败，阻止继续加载图片数据");
        setBusyErrorForCurrentStage();
        tryEnterEpdDeepSleep("云端图片初始化异常收尾");
        EPD_7in3E_SetUpdateStageCallback(nullptr);
        clearFlashTempFile();
        return false;
    }
    
    // 调用显示函数（从Flash读取）
    if (EPD_dispLoad != nullptr) {
        const bool loadCompleted = EPD_dispLoad();
        if (!loadCompleted || EPD_7in3E_LastBusyTimeout()) {
            Serial.println("❌ 图片显示未正常完成，请检查文件、BUSY线、屏幕供电和排线");
            if (EPD_7in3E_LastBusyTimeout()) {
                setBusyErrorForCurrentStage();
            } else {
                setUpdateError(UPDATE_ERROR_CHARSET_INVALID);
            }
            tryEnterEpdDeepSleep("云端图片刷新异常收尾");
            EPD_7in3E_SetUpdateStageCallback(nullptr);
            clearFlashTempFile();
            return false;
        }
        Serial.println("✅ 图片显示完成");
        setUpdateStage(UPDATE_STAGE_EPD_POWER_OFF);
        if (!tryEnterEpdDeepSleep("云端图片刷新")) {
            setUpdateError(UPDATE_ERROR_BUSY_POWER_OFF);
            EPD_7in3E_SetUpdateStageCallback(nullptr);
            clearFlashTempFile();
            return false;
        }
    } else {
        Serial.println("❌ EPD_dispLoad未设置");
        setUpdateError(UPDATE_ERROR_EPD_NOT_BOUND);
        EPD_7in3E_SetUpdateStageCallback(nullptr);
        clearFlashTempFile();
        return false;
    }
    
    // 清除临时文件
    EPD_7in3E_SetUpdateStageCallback(nullptr);
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
static void enterDeepSleepInternal(uint32_t overrideSleepSeconds, const char* reason) {
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
    if (reason != nullptr && reason[0] != '\0') {
        Serial.printf("   原因: %s\n", reason);
    }
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

    bool allowGpioWake = true;

    if (gpio_get_level(WAKEUP_GPIO) == 0) {
        Serial.println("⚠️  检测到GPIO0仍为低电平（按键可能未松开/无上拉），等待释放...");
        unsigned long startWait = millis();
        while (gpio_get_level(WAKEUP_GPIO) == 0 && (millis() - startWait) < WAKEUP_RELEASE_WAIT_MS) {
            delay(20);
        }
        if (gpio_get_level(WAKEUP_GPIO) == 0) {
            allowGpioWake = false;
            g_rtcGpio0StuckLow = true;
            Serial.println("⚠️  等待超时，GPIO0仍为低电平：本轮禁用GPIO唤醒，避免立即重唤耗电");
        } else {
            Serial.println("✅ GPIO0已恢复高电平，继续进入Deep-sleep");
        }
    }

    // 2. 配置GPIO0按键唤醒（低电平唤醒）
    // ESP32-C3使用esp_deep_sleep_enable_gpio_wakeup
    bool gpioWakeEnabled = false;
    if (allowGpioWake) {
        Serial.println("   配置GPIO0按键唤醒...");
        esp_err_t gpioWakeResult = esp_deep_sleep_enable_gpio_wakeup(
            1ULL << WAKEUP_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW);
        if (gpioWakeResult != ESP_OK) {
            Serial.printf("⚠️  GPIO0唤醒源配置失败: %s\n", esp_err_to_name(gpioWakeResult));
        } else {
            gpioWakeEnabled = true;
        }
    } else {
        Serial.println("   跳过GPIO0唤醒源，本轮仅使用定时唤醒");
    }
    
    // 3. 配置定时唤醒（故障恢复覆盖值 > 云端持久化值 > 默认值）
    uint32_t sleepSec = overrideSleepSeconds;
    if (sleepSec == 0) {
        sleepSec = loadSleepInterval();
    }
    if (sleepSec == 0) {
        sleepSec = DEFAULT_SLEEP_INTERVAL_SECONDS;
    }
    if (!allowGpioWake && sleepSec > WAKEUP_STUCK_LOW_RETRY_SECONDS) {
        sleepSec = WAKEUP_STUCK_LOW_RETRY_SECONDS;
        Serial.printf("⚠️  GPIO0持续为低，本轮将定时唤醒缩短为 %u 秒后复检\n", sleepSec);
    }
    esp_err_t timerWakeResult = esp_sleep_enable_timer_wakeup((uint64_t)sleepSec * 1000000ULL);
    if (timerWakeResult != ESP_OK) {
        Serial.printf("❌ 定时唤醒源配置失败: %s\n", esp_err_to_name(timerWakeResult));
        Serial.println("⚠️  为避免无定时唤醒源的永久休眠，执行受控重启");
        Serial.flush();
        delay(100);
        ESP.restart();
        return;
    }
    if (sleepSec >= 3600) {
        Serial.printf("   配置定时唤醒: %u 秒（%u 小时）\n", sleepSec, sleepSec / 3600);
    } else if (sleepSec >= 60) {
        Serial.printf("   配置定时唤醒: %u 秒（%u 分钟）\n", sleepSec, sleepSec / 60);
    } else {
        Serial.printf("   配置定时唤醒: %u 秒\n", sleepSec);
    }
    
    // 4. 打印信息
    Serial.println("\n✅ Deep-sleep配置完成:");
    if (gpioWakeEnabled) {
        Serial.println("   - GPIO0 按键唤醒（低电平）");
    } else {
        Serial.println("   - GPIO0 按键唤醒未启用（本轮仅靠定时器）");
    }
    Serial.printf("   - 定时唤醒: %u 秒后\n", sleepSec);
    Serial.println("   - 墨水屏将保持当前画面");
    Serial.println("\n💤 进入Deep-sleep...\n");
    Serial.flush();
    delay(100);
    
    // 5. 进入Deep-sleep
    esp_deep_sleep_start();
    
    // 不会执行到这里
}

void enterDeepSleep() {
    enterDeepSleepInternal(0, "正常唤醒周期结束");
}

void enterDeepSleepForRetry(uint32_t retrySeconds) {
    if (retrySeconds == 0) {
        retrySeconds = 300;
    }
    enterDeepSleepInternal(retrySeconds, "故障恢复，短周期重试");
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
    if (deviceId.length() == 0) {
        Serial.println("❌ 设备身份不可用，本次唤醒拒绝访问云端并进入Deep-sleep");
        g_shouldEnterDeepSleep = true;
        g_statusChecked = true;
        return;
    }
    Serial.printf("⭐ 设备ID: %s\n", deviceId.c_str());
    
    // 2. 读取本地状态
    deviceClaimed = loadClaimedStatus();
    localImageVersion = loadImageVersion();
    prepareUpdateDiagnosticForBoot();
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
        const bool deviceCodeDisplayed = displayDeviceCode(status.pairingCode);

        if (deviceCodeDisplayed) {
            Serial.println("✅ 设备码已显示，请通过网页绑定设备");
        } else {
            Serial.println("⚠️  设备码页未能显示，请根据串口日志排查屏幕或NVS");
        }
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
            g_targetImageSha256 = status.imageSha256;
            if (g_targetImageSha256.length() == 0) {
                Serial.println("⚠️  status未提供imageSha256，将以兼容模式仅校验长度和字符范围");
            }
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
    g_targetImageSha256 = "";
    g_currentUpdateError = UPDATE_ERROR_NONE;
    g_retrySleepSeconds = 0;

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
            beginUpdateDiagnostic(g_targetImageVersion);
            bool updateSucceeded = false;
            if (downloadImageToFlash(g_targetImageUrl, g_targetImageSha256)) {
                if (displayDownloadedImage()) {
                    setUpdateStage(UPDATE_STAGE_NVS_COMMIT);
                    if (saveImageVersion(g_targetImageVersion)) {
                        localImageVersion = g_targetImageVersion;
                        updateSucceeded = true;
                        Serial.printf("✅ 已更新到版本: %lld\n", localImageVersion);
                    } else {
                        setUpdateError(UPDATE_ERROR_NVS_SAVE);
                        Serial.println("⚠️  屏幕已刷新，但版本号未能持久化；下次唤醒将重新同步");
                    }
                } else {
                    Serial.println("❌ 显示失败，保留本地版本号，下次唤醒继续重试该图片");
                }
            } else {
                Serial.println("❌ 下载失败，本次不再重复尝试");
            }

            if (!updateSucceeded && g_currentUpdateError == UPDATE_ERROR_NONE) {
                setUpdateError(UPDATE_ERROR_DOWNLOAD_HTTP);
            }
            finishUpdateDiagnostic(updateSucceeded);
            reportUpdateDiagnosticNow();
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
        if (g_retrySleepSeconds > 0) {
            enterDeepSleepForRetry(g_retrySleepSeconds);
        } else {
            enterDeepSleep();
        }
    }

    // 正常不会走到这里
    delay(100);
}

#endif // HTTP_UPDATE_H
