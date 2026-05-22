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
// 只做前向声明，真正的定义仍在 buff.h 中，由其它文件（如 mqtt_config.h）包含
extern int  Buff__bufInd;
extern char Buff__bufArr[];
int Buff__getByte(int index);
int Buff__getWord(int index);

// 全局图像缓冲区声明（在mqtt_config.h中定义）
extern UBYTE globalImageBuffer[];
// GLOBAL_IMAGE_BUFFER_SIZE 已在 mqtt_config.h 中定义，这里不再重复定义

static const uint32_t EPD7IN3_BUSY_INIT_TIMEOUT_MS = 10000;
static const uint32_t EPD7IN3_BUSY_REFRESH_TIMEOUT_MS = 180000;
static bool g_epd7in3BusyTimeout = false;

static void EPD_7in3E_ClearBusyTimeout()
{
    g_epd7in3BusyTimeout = false;
}

static bool EPD_7in3E_LastBusyTimeout()
{
    return g_epd7in3BusyTimeout;
}

static bool EPD_7in3E_WaitBusy(uint32_t timeoutMs, const char *phase)
{
    uint32_t start = millis();
    while (!DEV_Digital_Read(EPD_BUSY_PIN)) {
        if ((millis() - start) >= timeoutMs) {
            g_epd7in3BusyTimeout = true;
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
// 这个函数会被EPD_dispLoad调用，用于MQTT模式的数据加载
void EPD_load_7in3E_from_buff()
{
    // FLASH_TEMP_FILE已在mqtt_config.h中定义为宏
    
    // 计算需要的缓冲区大小（4bit格式）
    int packedWidth = (EPD_7IN3E_WIDTH + 1) / 2;  // 400字节/行
    int totalBytes = packedWidth * EPD_7IN3E_HEIGHT;
    
    Serial.printf("📥 从Flash读取图像数据: 需要 %d 字节\n", totalBytes);
    Serial.printf("   当前剩余内存: %d 字节\n", ESP.getFreeHeap());
    Serial.printf("   使用流式处理（行缓冲区）\n");
    
    // 打开Flash临时文件
    File file = SPIFFS.open(FLASH_TEMP_FILE, "r");
    if (!file) {
        Serial.println("❌ 无法打开Flash临时文件");
        Serial.println("   可能原因：DOWNLOAD命令未执行或文件未创建");
        return;
    }
    
    int fileSize = file.size();
    Serial.printf("📁 Flash文件大小: %d 字符 (%.2f KB)\n", fileSize, fileSize / 1024.0);
    
    // 计算期望的文件大小：800x480 4bit格式 = 192000字节 = 384000字符
    int expectedChars = (EPD_7IN3E_WIDTH / 2) * EPD_7IN3E_HEIGHT * 2;  // 400 * 480 * 2 = 384000
    Serial.printf("   期望大小: %d 字符 (%.2f KB)\n", expectedChars, expectedChars / 1024.0);
    
    if (fileSize == 0) {
        Serial.println("❌ Flash文件为空！");
        Serial.println("   可能原因：DOWNLOAD命令未正确执行或数据未写入");
        file.close();
        return;
    }
    
    if (fileSize < expectedChars) {
        Serial.printf("⚠️  警告：文件大小不完整！期望 %d 字符，实际 %d 字符，缺少 %d 字符\n", 
                      expectedChars, fileSize, expectedChars - fileSize);
        Serial.println("   可能原因：HTTP下载不完整或网络中断");
        Serial.println("   底部区域将显示为白色");
    } else if (fileSize > expectedChars) {
        Serial.printf("⚠️  警告：文件大小超出！期望 %d 字符，实际 %d 字符，多出 %d 字符\n", 
                      expectedChars, fileSize, fileSize - expectedChars);
        Serial.println("   将只读取前 384000 字符");
    } else {
        Serial.println("✅ 文件大小正确");
    }
    
    // 使用行缓冲区（400字节），避免大内存分配
    UBYTE *rowBuffer = (UBYTE *)malloc(packedWidth);
    if (!rowBuffer) {
        Serial.printf("❌ 行缓冲区分配失败！需要 %d 字节，但只有 %d 字节可用\n", 
                      packedWidth, ESP.getFreeHeap());
        file.close();
        return;
    }
    
    Serial.printf("✅ 行缓冲区分配成功: %d 字节\n", packedWidth);
    
    // 优化：减少日志输出
    // Serial.println("   初始化EPD（如果未初始化）...");
    EPD_7in3E_ClearBusyTimeout();
    EPD_7IN3E_ClearBusyTimeout();
    EPD_7IN3E_Init();
    if (EPD_7IN3E_LastBusyTimeout()) {
        g_epd7in3BusyTimeout = true;
        Serial.println("❌ EPD初始化失败，跳过本次图片刷新");
        file.close();
        free(rowBuffer);
        return;
    }
    
    // 发送显示命令（0x10）- 开始写入图像数据
    // 优化：减少日志输出
    // Serial.println("   开始发送图像数据到EPD...");
    DEV_Digital_Write(EPD_DC_PIN, 0);  // 命令模式
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x10);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    
    // 逐行处理：从Flash读取、转换、直接发送到显示驱动
    int charIdx = 0;
    int totalBytesRead = 0;
    
    int missingDataCount = 0;  // 统计缺失数据的次数
    int invalidCharCount = 0;  // 统计无效字符的次数
    
    for (int row = 0; row < EPD_7IN3E_HEIGHT; row++) {
        // 读取一行数据（packedWidth字节，需要2*packedWidth个字符）
        for (int col = 0; col < packedWidth; col++) {
            // 读取两个字符组成一个字节
            if (charIdx >= fileSize || !file.available()) {
                // 数据不足，用白色填充
                rowBuffer[col] = 0x11;  // 两个白色像素
                missingDataCount++;
                continue;
            }
            
            char c1 = file.read();
            charIdx++;
            
            if (charIdx >= fileSize || !file.available()) {
                // 只有一个字符，用白色填充
                rowBuffer[col] = 0x11;
                missingDataCount++;
                continue;
            }
            
            char c2 = file.read();
            charIdx++;
            
            // 优化：简化字符验证（只检查范围，不打印日志，提升速度）
            if (c1 < 'a' || c1 > 'p' || c2 < 'a' || c2 > 'p') {
                // 无效字符，用白色填充
                rowBuffer[col] = 0x11;
                invalidCharCount++;
                // 优化：只在最后统计时报告，不逐行打印
                continue;
            }
            
            // 将两个字符转换为字节
            int low = (c1 - 'a') & 0x0F;
            int high = (c2 - 'a') & 0x0F;
            
            // 打包成字节：高4bit是high，低4bit是low
            rowBuffer[col] = (UBYTE)((high << 4) | low);
            totalBytesRead++;
        }
        
        // 优化：使用批量SPI传输函数，大幅提升速度（减少CS切换次数）
        DEV_Digital_Write(EPD_DC_PIN, 1);  // 数据模式
        DEV_SPI_Write_nByte(rowBuffer, packedWidth);  // 批量发送整行（400字节）
        
        // 优化：减少进度日志输出频率（从每50行改为每100行）
        if ((row + 1) % 100 == 0) {
            Serial.printf("   进度: %d/%d 行 (%.1f%%)\n", row + 1, EPD_7IN3E_HEIGHT, 
                          (row + 1) * 100.0 / EPD_7IN3E_HEIGHT);
        }
    }
    
    file.close();
    free(rowBuffer);
    
    Serial.printf("✅ 已读取并发送 %d 字节，准备刷新显示\n", totalBytesRead);
    if (missingDataCount > 0) {
        Serial.printf("⚠️  警告：有 %d 个字节因数据不足被填充为白色\n", missingDataCount);
    }
    if (invalidCharCount > 0) {
        Serial.printf("⚠️  警告：有 %d 个字节因无效字符被填充为白色\n", invalidCharCount);
    }
    
    // 刷新显示：需要完整的TurnOnDisplay流程
    // 参考EPD_7IN3E_TurnOnDisplay的实现
    // 优化：减少日志输出
    // Serial.println("   执行完整的显示刷新流程...");
    
    // 1. 发送命令0x04（上电）
    DEV_Digital_Write(EPD_DC_PIN, 0);  // 命令模式
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x04);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    
    // 等待BUSY（优化：减少日志输出）
    // Serial.println("   等待BUSY（上电）...");
    if (!EPD_7in3E_WaitBusy(EPD7IN3_BUSY_INIT_TIMEOUT_MS, "上电")) {
        return;
    }
    
    // 2. 发送命令0x06（设置显示模式）并发送数据
    DEV_Digital_Write(EPD_DC_PIN, 0);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x06);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    
    // 发送数据：0x6F, 0x1F, 0x17, 0x49
    DEV_Digital_Write(EPD_DC_PIN, 1);  // 数据模式
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
    
    // 3. 发送命令0x12（显示刷新）并发送数据0x00
    DEV_Digital_Write(EPD_DC_PIN, 0);  // 命令模式
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x12);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    
    DEV_Digital_Write(EPD_DC_PIN, 1);  // 数据模式
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x00);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    
    // 等待BUSY（显示刷新）（优化：减少日志输出）
    // Serial.println("   等待BUSY（显示刷新）...");
    if (!EPD_7in3E_WaitBusy(EPD7IN3_BUSY_REFRESH_TIMEOUT_MS, "显示刷新")) {
        return;
    }
    
    // 4. 发送命令0x02（断电）
    DEV_Digital_Write(EPD_DC_PIN, 0);  // 命令模式
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x02);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    
    DEV_Digital_Write(EPD_DC_PIN, 1);  // 数据模式
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x00);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    
    // 等待BUSY（断电）（优化：减少日志输出）
    // Serial.println("   等待BUSY（断电）...");
    if (EPD_7in3E_WaitBusy(EPD7IN3_BUSY_INIT_TIMEOUT_MS, "断电")) {
        Serial.println("✅ 显示完成");
    } else {
        Serial.println("❌ 显示流程未正常完成");
    }
}

