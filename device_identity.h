/**
 ******************************************************************************
 * @file    device_identity.h
 * @brief   Shared, fail-closed MAC address and device-code derivation
 ******************************************************************************
 */

#ifndef DEVICE_IDENTITY_H
#define DEVICE_IDENTITY_H

#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include "esp_err.h"
#include "esp_mac.h"
#include "esp_wifi.h"

// 0 = full MAC (12 hex digits), 1 = first 3 bytes, 2 = last 3 bytes.
#ifndef DEVICE_ID_MODE
#define DEVICE_ID_MODE 2
#endif

static inline bool deviceIdentityMacIsAllZero(const uint8_t mac[6]) {
    return mac[0] == 0 && mac[1] == 0 && mac[2] == 0 &&
           mac[3] == 0 && mac[4] == 0 && mac[5] == 0;
}

static inline bool readDeviceIdentityMac(uint8_t mac[6], const char** source) {
    if (mac == nullptr) {
        return false;
    }

    memset(mac, 0, 6);
    if (esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY) == ESP_OK &&
        !deviceIdentityMacIsAllZero(mac)) {
        if (source != nullptr) {
            *source = "ESP_MAC_EFUSE_FACTORY";
        }
        return true;
    }

    uint8_t candidate[6] = {0};
    WiFi.macAddress(candidate);
    if (!deviceIdentityMacIsAllZero(candidate)) {
        memcpy(mac, candidate, sizeof(candidate));
        if (source != nullptr) {
            *source = "WiFi.macAddress";
        }
        return true;
    }

    memset(candidate, 0, sizeof(candidate));
    if (esp_wifi_get_mac(WIFI_IF_STA, candidate) == ESP_OK &&
        !deviceIdentityMacIsAllZero(candidate)) {
        memcpy(mac, candidate, sizeof(candidate));
        if (source != nullptr) {
            *source = "WIFI_IF_STA";
        }
        return true;
    }

    memset(candidate, 0, sizeof(candidate));
    if (esp_wifi_get_mac(WIFI_IF_AP, candidate) == ESP_OK &&
        !deviceIdentityMacIsAllZero(candidate)) {
        memcpy(mac, candidate, sizeof(candidate));
        if (source != nullptr) {
            *source = "WIFI_IF_AP";
        }
        return true;
    }

    memset(mac, 0, 6);
    if (source != nullptr) {
        *source = nullptr;
    }
    return false;
}

static inline bool deriveDeviceIdentity(String& deviceId) {
    deviceId = "";

    uint8_t mac[6] = {0};
    const char* source = nullptr;
    if (!readDeviceIdentityMac(mac, &source)) {
        Serial.println("❌ 无法读取有效设备MAC（所有读取均失败或返回全零），拒绝生成设备码");
        return false;
    }

    Serial.printf("🔍 设备MAC（%s）: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  source == nullptr ? "unknown" : source,
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.printf("   DEVICE_ID_MODE = %d\n", DEVICE_ID_MODE);

    char buffer[13] = {0};
#if DEVICE_ID_MODE == 1
    snprintf(buffer, sizeof(buffer), "%02X%02X%02X",
             mac[0], mac[1], mac[2]);
#elif DEVICE_ID_MODE == 2
    snprintf(buffer, sizeof(buffer), "%02X%02X%02X",
             mac[3], mac[4], mac[5]);
#else
    snprintf(buffer, sizeof(buffer), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#endif

    deviceId = String(buffer);
    Serial.printf("   设备码: %s\n", deviceId.c_str());
    return true;
}

#endif  // DEVICE_IDENTITY_H
