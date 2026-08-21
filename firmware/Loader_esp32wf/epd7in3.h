/**
  ******************************************************************************
  * @file    epd7in3.h
  * @brief   7.3 inch E6 (7-color) e-Paper driver - 适配层，调用官方Demo驱动
  ******************************************************************************
  */

// 引入官方Demo驱动
#include "EPD_7in3e.h"
#include "DEV_Config.h"  // 用于底层SPI函数
#include <SPIFFS.h>
#include <FS.h>

// 如果FLASH_TEMP_FILE未定义，则定义它（避免包含顺序问题）
#ifndef FLASH_TEMP_FILE
#define FLASH_TEMP_FILE "/temp_image.bin"
#endif

// 这里不直接包含 buff.h，避免在同一个编译单元里重复定义全局变量
// 只做前向声明，真正的定义仍在 buff.h 中，由 http_update.h 在主线流程中包含
extern int  Buff__bufInd;
extern char Buff__bufArr[];
int Buff__getByte(int index);
int Buff__getWord(int index);

// 全局图像缓冲区声明（在 http_update.h 中定义）
extern UBYTE globalImageBuffer[];
// GLOBAL_IMAGE_BUFFER_SIZE 已在 http_update.h 中定义，这里不再重复定义

static const uint32_t EPD7IN3_BUSY_INIT_TIMEOUT_MS = 10000;
static const uint32_t EPD7IN3_BUSY_REFRESH_TIMEOUT_MS = 180000;
static bool g_epd7in3BusyTimeout = false;
static const char *g_epd7in3LastBusyPhase = "none";
typedef void (*EpdUpdateStageCallback)(const char *stage);
static EpdUpdateStageCallback g_epdUpdateStageCallback = nullptr;

static void EPD_7in3E_SetUpdateStageCallback(EpdUpdateStageCallback callback)
{
    g_epdUpdateStageCallback = callback;
}

static void EPD_7in3E_NotifyUpdateStage(const char *stage)
{
    if (g_epdUpdateStageCallback != nullptr) {
        g_epdUpdateStageCallback(stage);
    }
}

static void EPD_7in3E_ClearBusyTimeout()
{
    g_epd7in3BusyTimeout = false;
    g_epd7in3LastBusyPhase = "none";
}

static bool EPD_7in3E_LastBusyTimeout()
{
    return g_epd7in3BusyTimeout;
}

static const char *EPD_7in3E_LastBusyPhase()
{
    return g_epd7in3LastBusyPhase;
}

static bool EPD_7in3E_WaitBusy(uint32_t timeoutMs, const char *phase)
{
    uint32_t start = millis();
    while (!DEV_Digital_Read(EPD_BUSY_PIN)) {
        if ((millis() - start) >= timeoutMs) {
            g_epd7in3BusyTimeout = true;
            g_epd7in3LastBusyPhase = phase != nullptr ? phase : "unknown";
            Serial.printf("❌ EPD BUSY等待超时（%s）: %lu ms\n",
                          phase, (unsigned long)timeoutMs);
            return false;
        }
        delay(1);
    }
    return true;
}

// 适配函数：调用官方Demo的初始化
int EPD_7in3E_init()
{
    Serial.print("\r\nEPD7in3E6 (使用官方Demo驱动)");
    EPD_7IN3E_Init();  // 调用官方Demo的初始化函数
    return 0;
}

// 适配函数：调用官方Demo的显示函数
void EPD_7in3E_Show(void)
{
    EPD_7IN3E_Show();  // 调用官方Demo的显示函数
}

// 适配函数：调用官方Demo的清屏函数
void EPD_7in3E_Clear(byte color)
{
    EPD_7IN3E_Clear((UBYTE)color);  // 调用官方Demo的清屏函数
}

// 适配函数：从Flash加载数据到7.3E6（使用流式处理，避免大内存分配）
// 这个函数会被EPD_dispLoad调用，用于HTTP Pull下载后的图片刷新
bool EPD_load_7in3E_from_buff()
{
    // FLASH_TEMP_FILE由 http_update.h 定义；单独包含时上方会提供默认值
    const int packedWidth = (EPD_7IN3E_WIDTH + 1) / 2;  // 400字节/行
    const int totalBytes = packedWidth * EPD_7IN3E_HEIGHT;
    const int expectedChars = totalBytes * 2;

    Serial.printf("📥 从Flash读取图像数据: 需要 %d 字节\n", totalBytes);
    Serial.printf("   当前剩余内存: %d 字节\n", ESP.getFreeHeap());
    Serial.println("   使用流式处理（行缓冲区）");

    File file = SPIFFS.open(FLASH_TEMP_FILE, "r");
    if (!file) {
        Serial.println("❌ 无法打开Flash临时文件");
        return false;
    }

    const int fileSize = file.size();
    Serial.printf("📁 Flash文件大小: %d 字符 (%.2f KB)\n", fileSize, fileSize / 1024.0);
    Serial.printf("   期望大小: %d 字符 (%.2f KB)\n", expectedChars, expectedChars / 1024.0);
    if (fileSize != expectedChars) {
        Serial.printf("❌ Flash文件大小异常：期望 %d 字符，实际 %d 字符\n",
                      expectedChars, fileSize);
        file.close();
        return false;
    }

    UBYTE* rowBuffer = (UBYTE*)malloc(packedWidth);
    if (rowBuffer == nullptr) {
        Serial.printf("❌ 行缓冲区分配失败！需要 %d 字节，但只有 %d 字节可用\n",
                      packedWidth, ESP.getFreeHeap());
        file.close();
        return false;
    }

    EPD_7in3E_ClearBusyTimeout();
    EPD_7in3E_NotifyUpdateStage("epd_power_on");
    EPD_7IN3E_ClearBusyTimeout();
    EPD_7IN3E_Init();
    if (EPD_7IN3E_LastBusyTimeout()) {
        g_epd7in3BusyTimeout = true;
        Serial.println("❌ EPD初始化失败，跳过本次图片刷新");
        file.close();
        free(rowBuffer);
        return false;
    }

    DEV_Digital_Write(EPD_DC_PIN, 0);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x10);
    DEV_Digital_Write(EPD_CS_PIN, 1);

    int charIdx = 0;
    int totalBytesRead = 0;
    bool payloadValid = true;

    for (int row = 0; row < EPD_7IN3E_HEIGHT && payloadValid; row++) {
        for (int col = 0; col < packedWidth; col++) {
            const int first = file.read();
            const int second = file.read();
            if (first < 0 || second < 0) {
                Serial.printf("❌ Flash读取中断: 字符偏移 %d\n", charIdx);
                payloadValid = false;
                break;
            }
            charIdx += 2;

            const char c1 = (char)first;
            const char c2 = (char)second;
            if (c1 < 'a' || c1 > 'p' || c2 < 'a' || c2 > 'p') {
                Serial.printf("❌ Flash图像含非法字符: 字符偏移 %d\n", charIdx - 2);
                payloadValid = false;
                break;
            }

            const int low = (c1 - 'a') & 0x0F;
            const int high = (c2 - 'a') & 0x0F;
            rowBuffer[col] = (UBYTE)((high << 4) | low);
            totalBytesRead++;
        }

        if (!payloadValid) {
            break;
        }

        DEV_Digital_Write(EPD_DC_PIN, 1);
        DEV_SPI_Write_nByte(rowBuffer, packedWidth);
        if ((row + 1) % 100 == 0) {
            Serial.printf("   进度: %d/%d 行 (%.1f%%)\n", row + 1, EPD_7IN3E_HEIGHT,
                          (row + 1) * 100.0 / EPD_7IN3E_HEIGHT);
        }
    }

    file.close();
    free(rowBuffer);

    if (!payloadValid || charIdx != expectedChars || totalBytesRead != totalBytes) {
        Serial.printf("❌ 图像数据未完整加载: chars=%d/%d, bytes=%d/%d\n",
                      charIdx, expectedChars, totalBytesRead, totalBytes);
        return false;
    }
    Serial.printf("✅ 已读取并发送 %d 字节，准备刷新显示\n", totalBytesRead);

    DEV_Digital_Write(EPD_DC_PIN, 0);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x04);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    EPD_7in3E_NotifyUpdateStage("epd_power_on");
    if (!EPD_7in3E_WaitBusy(EPD7IN3_BUSY_INIT_TIMEOUT_MS, "上电")) {
        return false;
    }

    DEV_Digital_Write(EPD_DC_PIN, 0);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x06);
    DEV_Digital_Write(EPD_CS_PIN, 1);

    DEV_Digital_Write(EPD_DC_PIN, 1);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x6F);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x1F);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x17);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x49);
    DEV_Digital_Write(EPD_CS_PIN, 1);

    DEV_Digital_Write(EPD_DC_PIN, 0);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x12);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    DEV_Digital_Write(EPD_DC_PIN, 1);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x00);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    EPD_7in3E_NotifyUpdateStage("epd_refresh");
    if (!EPD_7in3E_WaitBusy(EPD7IN3_BUSY_REFRESH_TIMEOUT_MS, "显示刷新")) {
        return false;
    }

    DEV_Digital_Write(EPD_DC_PIN, 0);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x02);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    DEV_Digital_Write(EPD_DC_PIN, 1);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x00);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    EPD_7in3E_NotifyUpdateStage("epd_power_off");
    if (!EPD_7in3E_WaitBusy(EPD7IN3_BUSY_INIT_TIMEOUT_MS, "断电")) {
        Serial.println("❌ 显示流程未正常完成");
        return false;
    }

    Serial.println("✅ 显示完成");
    return true;
}
