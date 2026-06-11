/**
 ******************************************************************************
 * @file    Loader_esp32wf.ino
 * @author  Waveshare Team / Modified for Deep-sleep + HTTP Pull
 * @version V3.0.0
 * @date    23-January-2018 / Modified 2026-01-24
 * @brief   ESP32 E-Paper Deep-sleep + HTTP Pull Update
 *          设备绝大多数时间处于Deep-sleep，只有按键或定时唤醒后
 *          才联网HTTP拉取更新图片，刷新墨水屏后立即回到Deep-sleep
 *
 ******************************************************************************
*/

/* Includes ------------------------------------------------------------------*/
#include <WiFi.h>

/* WiFi配网功能 ------------------------------------------------------------------*/
#include "wifi_config.h"

/* HTTP更新功能（替代原MQTT） --------------------------------------------------*/
#include "http_update.h"

/* 全局变量定义（在头文件中声明为extern）----------------------------------------*/
Preferences preferences;  // NVS持久化存储（供wifi_config和http_update共享）
bool wifiConfigured = false;  // WiFi配网状态标志

/* ------------------------ 用户自定义：长按进入配网 ------------------------ */
#define WIFI_RECONFIG_HOLD_MS 3000  // 长按GPIO0进入"清除WiFi并AP配网"的阈值（ms）
#define WIFI_RECONFIG_POST_WAKE_CONFIRM_MS 1200  // GPIO唤醒后继续按住多长时间触发清配置+配网
#define WAKE_DEBUG_SERIAL_DELAY_MS 500  // 调试期给串口监视器留一点启动输出时间
RTC_DATA_ATTR uint32_t g_deepSleepBootCount = 0;

static const char* wakeCauseName(esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
      return "TIMER";
    case ESP_SLEEP_WAKEUP_GPIO:
      return "GPIO";
    case ESP_SLEEP_WAKEUP_EXT0:
      return "EXT0";
    case ESP_SLEEP_WAKEUP_EXT1:
      return "EXT1";
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      return "POWERON_OR_RESET";
    default:
      return "OTHER";
  }
}

/**
 * 判断是否为正常唤醒原因（按键/定时器/GPIO等）
 */
static bool isNormalWakeCause(esp_sleep_wakeup_cause_t cause) {
  return (cause == ESP_SLEEP_WAKEUP_TIMER ||
          cause == ESP_SLEEP_WAKEUP_GPIO ||
          cause == ESP_SLEEP_WAKEUP_EXT0 ||
          cause == ESP_SLEEP_WAKEUP_EXT1);
}

/**
 * 检测 GPIO0 是否被持续按住（低电平）达到指定时长
 * 注意：GPIO0 在本项目作为唤醒键（低电平），这里复用它作为"长按配网"入口。
 * 使用 http_update.h 中定义的 WAKEUP_GPIO (GPIO_NUM_0)
 */
static bool isWakeKeyHeldLow(uint32_t holdMs) {
  // WAKEUP_GPIO 在 http_update.h 中已定义为 GPIO_NUM_0
  const gpio_num_t wakeupPin = WAKEUP_GPIO;

  pinMode((int)wakeupPin, INPUT_PULLUP);
  gpio_pullup_en(wakeupPin);
  gpio_pulldown_dis(wakeupPin);

  // 必须从一开始就是低电平才算"按住"
  if (gpio_get_level(wakeupPin) != 0) {
    return false;
  }

  Serial.printf("🔎 GPIO0为低电平，开始判断是否持续按住 %lu ms...\n", (unsigned long)holdMs);
  uint32_t start = millis();
  while ((millis() - start) < holdMs) {
    if (gpio_get_level(wakeupPin) != 0) {
      Serial.println("ℹ️ GPIO0已松开：按键唤醒成立，但不是长按配网");
      return false;  // 中途松开
    }
    delay(10);
  }
  return true;  // 全程按住
}

static uint32_t getWiFiReconfigHoldMs(esp_sleep_wakeup_cause_t cause) {
  if (cause == ESP_SLEEP_WAKEUP_GPIO ||
      cause == ESP_SLEEP_WAKEUP_EXT0 ||
      cause == ESP_SLEEP_WAKEUP_EXT1) {
    return WIFI_RECONFIG_POST_WAKE_CONFIRM_MS;
  }
  return WIFI_RECONFIG_HOLD_MS;
}

static void printWakeDebug(esp_sleep_wakeup_cause_t cause) {
  pinMode((int)WAKEUP_GPIO, INPUT_PULLUP);
  gpio_pullup_en(WAKEUP_GPIO);
  gpio_pulldown_dis(WAKEUP_GPIO);
  int wakePinLevel = gpio_get_level(WAKEUP_GPIO);

  Serial.println("---------- WAKE DEBUG ----------");
  Serial.printf("原因: %s (%d)\n", wakeCauseName(cause), (int)cause);
  Serial.printf("GPIO0当前电平: %d (%s)\n", wakePinLevel, wakePinLevel == 0 ? "LOW/按下" : "HIGH/释放");
  Serial.printf("Deep-sleep启动计数: %lu\n", (unsigned long)g_deepSleepBootCount);

  if (cause == ESP_SLEEP_WAKEUP_GPIO || cause == ESP_SLEEP_WAKEUP_EXT0 || cause == ESP_SLEEP_WAKEUP_EXT1) {
    Serial.println("✅ 检测到按键/外部信号唤醒，准备连接WiFi并检查云端更新");
  } else if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("✅ 检测到定时唤醒，准备连接WiFi并检查云端更新");
  } else {
    Serial.println("ℹ️ 本次为上电/复位启动");
  }
  Serial.println("--------------------------------");
  Serial.flush();
}

/* Entry point ----------------------------------------------------------------*/
void setup()
{
    // Serial port initialization
    Serial.begin(115200);
    delay(WAKE_DEBUG_SERIAL_DELAY_MS);

    // 打印启动信息
    Serial.println();
    Serial.println("========================================");
    Serial.println("  ESP32 E-Paper Deep-sleep 模式");
    Serial.println("  Version 3.0.0");
    Serial.println("========================================");
    Serial.printf("  剩余内存: %d 字节\n", ESP.getFreeHeap());
    Serial.println("  显示硬件初始化: 按需延后");
    Serial.println("========================================\n");

    // 读取唤醒原因
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (isNormalWakeCause(cause)) {
        g_deepSleepBootCount++;
    }
    printWakeDebug(cause);
    Serial.printf("⏰ wakeup cause = %s (%d)\n", wakeCauseName(cause), (int)cause);

    // 先读一次"是否已配网"（不改变现有逻辑，仅用于门控）
    bool alreadyConfigured = checkWiFiConfigured();
    Serial.printf("📦 本地WiFi配置: %s\n", alreadyConfigured ? "已存在" : "不存在");

    // 1) 长按 GPIO0 进入"清除WiFi + AP配网"
    //    - 适用于：从 deep-sleep 按键唤醒后继续按住不放
    //    - 也适用于：上电/复位后按住 GPIO0（若硬件允许）
    uint32_t reconfigHoldMs = getWiFiReconfigHoldMs(cause);
    if (reconfigHoldMs != WIFI_RECONFIG_HOLD_MS) {
        Serial.printf("ℹ️ 检测到按键唤醒：继续按住GPIO0 %lu ms 可清除WiFi并进入二维码配网\n",
                      (unsigned long)reconfigHoldMs);
    }

    if (isWakeKeyHeldLow(reconfigHoldMs)) {
        Serial.println("🧹 检测到长按GPIO0：清除WiFi配置并进入AP配网模式");
        clearWiFiConfig();       // 清除NVS WiFi信息
        startAPMode();
        wifiConfigured = false;
        if (apModeStarted) {
            Serial.println("⏳ 等待配网中...（AP模式）");
        }
        return;  // AP模式下不进入Deep-sleep
    }

    // 2) 复位/上电等"非正常唤醒"，且已配网：连接WiFi查询云端
    //    - 如果云端 claimed=true：直接回睡
    //    - 如果云端 claimed=false：显示设备码，等待配网绑定
    if (!isNormalWakeCause(cause) && alreadyConfigured) {
        Serial.println("🔄 非按键/非定时唤醒（复位），但已配网，连接WiFi查询云端...");
        Serial.println("ℹ️ 检测到本地已有WiFi配置，本次启动优先尝试联网，不会直接广播AP热点");
        if (initWiFiConfig()) {
            // WiFi连接成功，查询云端 claimed 状态
            HTTP_UPDATE__setup();
            HTTP_UPDATE__loop();
        } else {
            // WiFi连接失败时 initWiFiConfig() 会打开AP修复入口（不清除旧配置）
            if (apModeStarted) {
                Serial.println("📱 WiFi连接失败，已打开AP修复入口");
                Serial.println("   如需修改WiFi，请连接热点并访问: http://192.168.4.1");
                Serial.println("⏳ 等待配网中...（AP模式）");
            } else {
                Serial.println("❌ WiFi连接失败，且AP修复入口未能启动");
            }
        }
        return;
    }

    // WiFi配网初始化
    Serial.println("📶 WiFi配网初始化...");

    bool openApOnSavedWiFiFailure = !alreadyConfigured || !isNormalWakeCause(cause);
    bool wifiConnected = initWiFiConfig(openApOnSavedWiFiFailure);

    if (!wifiConnected) {
        // 未连接：可能进入AP配网，也可能因已有配置但临时连接失败而直接回睡
        Serial.println();
        if (apModeStarted) {
            Serial.println("📱 设备已进入AP配网模式");
            Serial.println("   请按以下步骤操作：");
            Serial.println("   1. 扫描屏幕二维码或连接WiFi热点");
            Serial.println("   2. 访问 http://192.168.4.1");
            Serial.println("   3. 输入WiFi名称和密码");
            Serial.println("   4. 点击连接，设备将自动重启");
            Serial.println();
            Serial.println("⏳ 等待配网中...（AP模式）");
        } else {
            if (alreadyConfigured) {
                Serial.println("⚠️  已保存WiFi但本次连接失败，保留配置并进入Deep-sleep，下次唤醒再试");
                enterDeepSleep();
            } else {
                Serial.println("❌ 当前未能进入AP配网模式，请查看前面的AP启动日志");
            }
        }
        // 注意：AP配网模式下不进入Deep-sleep，保持Web服务器运行
        return;
    }

    // WiFi已连接，执行HTTP更新检查
    Serial.println();
    Serial.println("✅ WiFi已连接，开始HTTP更新检查...");

    // HTTP更新模式初始化：本次唤醒只做一次“是否需要更新”的判定
    HTTP_UPDATE__setup();

    // 为了避免进入 loop 后再做一次兜底，这里直接调用一次 loop 处理：
    // - 需要更新：执行下载+刷新，然后 deep-sleep
    // - 不需要更新：直接 deep-sleep
    HTTP_UPDATE__loop();

    // 正常情况下不会执行到这里（deep-sleep 后不会返回）
    Serial.println("⚠️  仍在运行：未进入Deep-sleep（异常路径）");
}

/* The main loop -------------------------------------------------------------*/
void loop()
{
    if (wifiConfigured) {
        // WiFi已配置，正常情况下不会执行到这里
        // 因为setup()中的HTTP_UPDATE__setup()会进入Deep-sleep
        // 如果执行到这里，尝试重新进入Deep-sleep
        HTTP_UPDATE__loop();
    } else {
        processApProvisioningLoop();
    }
}
