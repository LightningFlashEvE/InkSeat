/**
 ******************************************************************************
 * @file    wifi_config.h
 * @brief   WiFi配网功能：AP热点模式 + Web配网页面
 *          支持通过Web页面配置WiFi，完成后自动连接
 ******************************************************************************
*/

#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_heap_caps.h"
#include "device_identity.h"

bool displayProvisioningScreen(const String& apSSID, const String& deviceCode, const String& wifiQrPayload);
String getProvisioningApPassword();
void initConfigServer();
void dbgSetEpdActive(bool active);
void EPD_ProvisioningYield(void);
void ensureApWebServices();
void processApProvisioningLoop();
bool isApWebServicesReady();
uint32_t apModeElapsedMs();

// 配网相关配置

#ifndef PROVISIONING_RENDER_AP_SCREEN
#define PROVISIONING_RENDER_AP_SCREEN 1
#endif
// 留空=开放热点（与历史可用版本一致）；若需加密可设 >=8 字符密码
#ifndef PROVISIONING_AP_PSK
#define PROVISIONING_AP_PSK ""
#endif
constexpr bool provisioningApPskIsPrintableAscii(const char* value, size_t length) {
    return length == 0 ||
           ((unsigned char)value[0] >= 0x20 && (unsigned char)value[0] <= 0x7E &&
            provisioningApPskIsPrintableAscii(value + 1, length - 1));
}
static_assert(
    sizeof(PROVISIONING_AP_PSK) == 1 ||
    (sizeof(PROVISIONING_AP_PSK) >= 9 && sizeof(PROVISIONING_AP_PSK) <= 64),
    "PROVISIONING_AP_PSK must be empty or contain 8-63 characters"
);
static_assert(
    provisioningApPskIsPrintableAscii(PROVISIONING_AP_PSK, sizeof(PROVISIONING_AP_PSK) - 1),
    "PROVISIONING_AP_PSK must contain printable ASCII characters so it can be shown on screen"
);
#ifndef PROVISIONING_AP_CHANNEL
#define PROVISIONING_AP_CHANNEL 1
#endif
#ifndef PROVISIONING_AP_MAX_CONN
#define PROVISIONING_AP_MAX_CONN 4
#endif
#ifndef PROVISIONING_DHCP_GRACE_MS
#define PROVISIONING_DHCP_GRACE_MS 4000
#endif
#define CONFIG_NAMESPACE "wifi_cfg"   // Preferences命名空间
#define CONFIG_SSID_KEY "ssid"        // WiFi SSID存储键
#define CONFIG_PASSWORD_KEY "pwd"     // WiFi密码存储键
#define CONFIG_CONFIGURED_KEY "cfg"   // 配网标志位存储键
#define CONFIG_MAX_SSID_BYTES 32       // IEEE 802.11 SSID 最大长度（字节）
#define CONFIG_MAX_PASSWORD_BYTES 64   // ESP32 WiFi 密码/PSK 最大长度（字节）

// 全局变量（WebServer 延后到 initConfigServer 再分配，避免启动时打碎堆）
static WebServer* configServerInstance = nullptr;
DNSServer dnsServer;

static WebServer& getConfigServer() {
    if (configServerInstance == nullptr) {
        configServerInstance = new WebServer(80);
    }
    return *configServerInstance;
}
extern Preferences preferences;  // 在Loader_esp32wf.ino中定义
extern bool wifiConfigured;
bool apModeStarted = false;
bool provisioningScreenAttempted = false;
static bool g_apWebServicesStarted = false;
static volatile bool g_staIpAssigned = false;
static uint32_t g_apModeStartMs = 0;
static volatile uint32_t g_staConnectMs = 0;
static uint32_t g_webStartMs = 0;
static volatile bool g_pendingWebAfterEpd = false;
static volatile bool g_staConnectedEventPending = false;
static volatile bool g_staDisconnectedEventPending = false;
static volatile bool g_staIpEventPending = false;
static volatile uint32_t g_staEventMacSuffix = 0;
static volatile uint32_t g_staEventIpRaw = 0;
String savedSSID = "";
String savedPassword = "";
const byte DNS_PORT = 53;
String provisioningApSSID = "";
String provisioningDeviceCode = "";
#ifndef PROVISIONING_ENABLE_CAPTIVE_DNS
#define PROVISIONING_ENABLE_CAPTIVE_DNS 1
#endif

#ifndef EPD_STRUCTURED_DEBUG
#define EPD_STRUCTURED_DEBUG 0
#endif

static volatile bool g_dbgEpdActive = false;

static void dbgApLog(const char* hypothesisId, const char* location, const char* message,
                     uint32_t d1 = 0, uint32_t d2 = 0) {
#if EPD_STRUCTURED_DEBUG
    Serial.printf(
        "{\"component\":\"ap\",\"event\":\"%s\",\"location\":\"%s\","
        "\"message\":\"%s\",\"data\":{\"ms\":%lu,\"d1\":%lu,\"d2\":%lu,\"epd\":%u,"
        "\"heap\":%u,\"sta\":%u,\"mode\":%d,\"apIp\":\"%s\"},\"timestamp\":%lu}\n",
        hypothesisId, location, message,
        (unsigned long)millis(), (unsigned long)d1, (unsigned long)d2,
        g_dbgEpdActive ? 1u : 0u, (unsigned)ESP.getFreeHeap(),
        (unsigned)WiFi.softAPgetStationNum(), (int)WiFi.getMode(),
        WiFi.softAPIP().toString().c_str(), (unsigned long)millis());
#else
    (void)hypothesisId;
    (void)location;
    (void)message;
    (void)d1;
    (void)d2;
#endif
}

static void tryStartApWebServicesAfterEpd();

void dbgSetEpdActive(bool active) {
    g_dbgEpdActive = active;
    dbgApLog("H1", "dbgSetEpdActive", active ? "epd_begin" : "epd_end", active ? 1u : 0u, 0);
    if (!active) {
        tryStartApWebServicesAfterEpd();
    }
}

/**
 * 检查配网状态
 */
bool checkWiFiConfigured() {
    if (!preferences.begin(CONFIG_NAMESPACE, true)) {  // 只读模式
        // NVS命名空间不存在（第一次使用），这是正常的
        preferences.end();
        return false;
    }
    bool configured = preferences.getBool(CONFIG_CONFIGURED_KEY, false);
    if (configured) {
        savedSSID = preferences.getString(CONFIG_SSID_KEY, "");
        savedPassword = preferences.getString(CONFIG_PASSWORD_KEY, "");
    }
    preferences.end();
    return configured && savedSSID.length() > 0;
}

/**
 * 保存WiFi配置
 */
bool saveWiFiConfig(const String& ssid, const String& password) {
    if (!preferences.begin(CONFIG_NAMESPACE, false)) {  // 读写模式
        preferences.end();
        Serial.println("⚠️  NVS命名空间打开失败，无法保存WiFi配置");
        return false;
    }

    // 先撤销配置有效标志，避免任一字符串写入失败后仍使用半套配置。
    bool ok = preferences.putBool(CONFIG_CONFIGURED_KEY, false) == sizeof(uint8_t);
    if (ok) {
        const size_t ssidWritten = preferences.putString(CONFIG_SSID_KEY, ssid);
        ok = ssidWritten == ssid.length();
    }
    if (ok) {
        const size_t passwordWritten = preferences.putString(CONFIG_PASSWORD_KEY, password);
        // Preferences::putString() 成功写入空字符串时返回 0，需要通过键和值回读确认。
        ok = password.length() > 0
                 ? passwordWritten == password.length()
                 : (preferences.isKey(CONFIG_PASSWORD_KEY) &&
                    preferences.getString(CONFIG_PASSWORD_KEY, "__missing__").length() == 0);
    }
    if (ok) {
        ok = preferences.putBool(CONFIG_CONFIGURED_KEY, true) == sizeof(uint8_t);
    }
    preferences.end();

    if (!ok) {
        Serial.println("❌ WiFi配置写入NVS失败，保留AP模式供用户重试");
        return false;
    }

    savedSSID = ssid;
    savedPassword = password;
    Serial.println("✅ WiFi配置已保存并校验");
    return true;
}

/**
 * 清除WiFi配置
 */
bool clearWiFiConfig() {
    if (!preferences.begin(CONFIG_NAMESPACE, false)) {
        preferences.end();
        Serial.println("⚠️  NVS命名空间打开失败，无法确认WiFi配置已清除");
        return false;
    }

    bool ok = preferences.putBool(CONFIG_CONFIGURED_KEY, false) == sizeof(uint8_t);
    if (preferences.isKey(CONFIG_SSID_KEY)) {
        ok = preferences.remove(CONFIG_SSID_KEY) && ok;
    }
    if (preferences.isKey(CONFIG_PASSWORD_KEY)) {
        ok = preferences.remove(CONFIG_PASSWORD_KEY) && ok;
    }
    preferences.end();
    savedSSID = "";
    savedPassword = "";

    if (!ok) {
        Serial.println("⚠️  WiFi配置未能完整清除，请检查NVS状态");
        return false;
    }
    Serial.println("🗑️  WiFi配置已清除");
    return true;
}

// AP 配网和云端请求共用同一套 MAC 读取与设备码派生规则。
String getDeviceIdForAP() {
    String derivedId;
    if (!deriveDeviceIdentity(derivedId)) {
        return "";
    }
    return derivedId;
}
String escapeWifiQrField(const String& input) {
    String escaped = "";
    escaped.reserve(input.length() + 8);
    for (size_t i = 0; i < input.length(); i++) {
        char c = input.charAt(i);
        if (c == '\\' || c == ';' || c == ',' || c == ':') {
            escaped += '\\';
        }
        escaped += c;
    }
    return escaped;
}

String getProvisioningApPassword() {
    if (PROVISIONING_AP_PSK[0] == '\0' || strlen(PROVISIONING_AP_PSK) < 8) {
        return String("");
    }
    return String(PROVISIONING_AP_PSK);
}

String getProvisioningWifiQrPayload() {
    if (provisioningApSSID.length() == 0) {
        return "";
    }
    const String ssidField = escapeWifiQrField(provisioningApSSID);
    const String password = getProvisioningApPassword();
    if (password.length() >= 8) {
        return "WIFI:T:WPA;S:" + ssidField + ";P:" + escapeWifiQrField(password) + ";;";
    }
    return "WIFI:T:nopass;S:" + ssidField + ";;";
}

static void onProvisioningWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_AP_STACONNECTED: {
            const uint8_t* mac = info.wifi_ap_staconnected.mac;
            g_staConnectMs = millis();
            g_staIpAssigned = false;
            g_pendingWebAfterEpd = true;
            g_staEventMacSuffix = ((uint32_t)mac[4] << 8) | mac[5];
            g_staConnectedEventPending = true;
            break;
        }
        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
            g_staDisconnectedEventPending = true;
            break;
        case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED: {
            g_staIpAssigned = true;
            g_pendingWebAfterEpd = true;
            g_staEventIpRaw = info.wifi_ap_staipassigned.ip.addr;
            g_staIpEventPending = true;
            break;
        }
        default:
            break;
    }
}

static void processPendingProvisioningWiFiEvents() {
    if (g_staConnectedEventPending) {
        g_staConnectedEventPending = false;
        const uint32_t macSuffix = g_staEventMacSuffix;
        Serial.printf("📱 设备已关联热点, STA #%u（DHCP 宽限 %u ms，暂不启 Web）\n",
                      (unsigned)WiFi.softAPgetStationNum(),
                      (unsigned)PROVISIONING_DHCP_GRACE_MS);
        dbgApLog("H1", "wifi_event_main", "sta_connected",
                 macSuffix, WiFi.softAPgetStationNum());
    }

    if (g_staIpEventPending) {
        g_staIpEventPending = false;
        const uint32_t ipRaw = g_staEventIpRaw;
        const IPAddress clientIp(ipRaw);
        Serial.printf("📱 热点客户端 IP: %s\n", clientIp.toString().c_str());
        dbgApLog("H2", "wifi_event_main", "sta_ip_assigned", clientIp[3], 0);
    }

    if (g_staDisconnectedEventPending) {
        g_staDisconnectedEventPending = false;
        const uint8_t stationCount = WiFi.softAPgetStationNum();
        Serial.printf("📴 设备已断开热点, STA #%u\n", (unsigned)stationCount);
        if (stationCount == 0) {
            g_staConnectMs = 0;
            g_staIpAssigned = false;
            g_pendingWebAfterEpd = false;
        }
        dbgApLog("H1", "wifi_event_main", "sta_disconnected", 0, stationCount);
    }
}

static void registerProvisioningWiFiEvents() {
    static bool registered = false;
    if (registered) {
        return;
    }
    WiFi.onEvent(onProvisioningWiFiEvent);
    registered = true;
}

String getProvisioningApSSID() {
    return provisioningApSSID;
}

String getProvisioningDeviceCode() {
    return provisioningDeviceCode;
}

/**
 * 启动AP热点模式
 */
bool startAPMode() {
    Serial.println("📡 启动AP热点模式...");
    provisioningScreenAttempted = false;
    g_apWebServicesStarted = false;
    g_staIpAssigned = false;
    g_staConnectMs = 0;
    g_webStartMs = 0;
    g_pendingWebAfterEpd = false;
    g_staConnectedEventPending = false;
    g_staDisconnectedEventPending = false;
    g_staIpEventPending = false;
    g_apModeStartMs = millis();
    registerProvisioningWiFiEvents();

    WiFi.persistent(false);
    WiFi.disconnect(false, true);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(150);

    String deviceCode = getDeviceIdForAP();
    if (deviceCode.length() == 0) {
        apModeStarted = false;
        provisioningDeviceCode = "";
        provisioningApSSID = "";
        Serial.println("❌ 无法读取设备身份，拒绝启动不可寻址的配网热点");
        return false;
    }

    String apSSID = "EPD-" + deviceCode;
    provisioningDeviceCode = deviceCode;
    provisioningApSSID = apSSID;

#if PROVISIONING_RENDER_AP_SCREEN
    if (!provisioningScreenAttempted) {
        provisioningScreenAttempted = true;
        Serial.println("🖥️ 在启动 WiFi 热点前刷新配网二维码（malloc→显示→free）...");
        if (!displayProvisioningScreen(provisioningApSSID,
                                       provisioningDeviceCode,
                                       getProvisioningWifiQrPayload())) {
            Serial.println("⚠️  配网页显示失败，仍继续启动热点");
        }
    }
#endif

    if (!WiFi.mode(WIFI_AP)) {
        Serial.println("⚠️  无法切换到 WIFI_AP，继续尝试 softAP");
    }
    delay(200);
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);

    const IPAddress apIp(192, 168, 4, 1);
    const IPAddress apGw(192, 168, 4, 1);
    const IPAddress apMask(255, 255, 255, 0);
    if (!WiFi.softAPConfig(apIp, apGw, apMask)) {
        Serial.println("⚠️  softAPConfig 失败");
    }

    Serial.printf("   AP名称: %s\n", apSSID.c_str());
    const String apPassword = getProvisioningApPassword();
    if (apPassword.length() >= 8) {
        Serial.println("   AP密码: 已设置（不在串口输出明文）");
    } else {
        Serial.println("   AP密码: 无密码");
    }

    bool apStarted = false;
    if (apPassword.length() >= 8) {
        apStarted = WiFi.softAP(apSSID.c_str(), apPassword.c_str(),
                                PROVISIONING_AP_CHANNEL, 0, PROVISIONING_AP_MAX_CONN);
    } else {
        apStarted = WiFi.softAP(apSSID.c_str());
    }

    if (!apStarted) {
        Serial.println("⚠️  首次启动AP失败，重置WiFi后重试一次...");
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        delay(200);
        WiFi.mode(WIFI_AP);
        delay(200);
        if (apPassword.length() >= 8) {
            apStarted = WiFi.softAP(apSSID.c_str(), apPassword.c_str(),
                                    PROVISIONING_AP_CHANNEL, 0, PROVISIONING_AP_MAX_CONN);
        } else {
            apStarted = WiFi.softAP(apSSID.c_str());
        }
    }

    if (!apStarted) {
        apModeStarted = false;
        provisioningDeviceCode = "";
        provisioningApSSID = "";
        Serial.println("❌ AP热点启动失败");
        Serial.println("   请检查供电、电源稳定性、天线以及串口中的 WiFi 初始化错误日志");
        return false;
    }

    delay(200);
    IPAddress IP = WiFi.softAPIP();
    apModeStarted = true;

    Serial.print("   AP IP地址: ");
    Serial.println(IP);
    Serial.printf("   当前WiFi模式: %d\n", static_cast<int>(WiFi.getMode()));
    Serial.printf("   AP信道: %d\n", WiFi.channel());
    Serial.printf("   启动后剩余堆内存: %u 字节（画布已释放，Web/DNS 延后）\n",
                  (unsigned)ESP.getFreeHeap());
    Serial.printf("   最大连续块: %u 字节\n",
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    Serial.println("   请连接热点；关联成功后再启动配网 Web");
    // #region agent log
    dbgApLog("H4", "startAPMode", "ap_ready_minimal", IP[3], WiFi.channel());
    // #endregion
    return true;
}

static void tryStartApWebServicesAfterEpd() {
    if (!g_pendingWebAfterEpd || g_apWebServicesStarted || !apModeStarted) {
        return;
    }
    if (g_staIpAssigned) {
        g_pendingWebAfterEpd = false;
        ensureApWebServices();
        return;
    }
    if (g_staConnectMs > 0 &&
        (millis() - g_staConnectMs) >= PROVISIONING_DHCP_GRACE_MS) {
        g_pendingWebAfterEpd = false;
        ensureApWebServices();
    }
}

void ensureApWebServices() {
    if (g_apWebServicesStarted || !apModeStarted) {
        return;
    }
    if (g_dbgEpdActive) {
        g_pendingWebAfterEpd = true;
        // #region agent log
        dbgApLog("H6", "ensureApWebServices", "deferred_epd", ESP.getFreeHeap(),
                 WiFi.softAPgetStationNum());
        // #endregion
        return;
    }
    // #region agent log
    dbgApLog("H6", "ensureApWebServices", "before", ESP.getFreeHeap(), WiFi.softAPgetStationNum());
    // #endregion
    const IPAddress IP = WiFi.softAPIP();
#if PROVISIONING_ENABLE_CAPTIVE_DNS
    dnsServer.stop();
    dnsServer.start(DNS_PORT, "*", IP);
#endif
    initConfigServer();
    g_apWebServicesStarted = true;
    g_webStartMs = millis();
    Serial.println("✅ 配网 Web + Captive Portal DNS 已启动（DHCP 完成后）");
    Serial.println("   请访问: http://192.168.4.1");
    // #region agent log
    dbgApLog("H6", "ensureApWebServices", "after", ESP.getFreeHeap(), WiFi.softAPgetStationNum());
    // #endregion
}

/**
 * 配网页面HTML
 */
String getConfigPageHTML() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>设备配网</title>";
    html += "<style>";
    html += ":root{color-scheme:light;--bg:#f7f9fb;--text:#17202a;--muted:#65758b;--line:#d5dde6;--accent:#2563eb;--ok:#17633a;--bad:#a22a22;}";
    html += "*{box-sizing:border-box}body{margin:0;padding:24px;background:var(--bg);color:var(--text);font-family:'Microsoft YaHei',Arial,sans-serif;line-height:1.5}";
    html += "main{width:min(100%,420px);margin:8vh auto 0}h1{margin:0 0 8px;font-size:28px;line-height:1.2}p{margin:0 0 22px;color:var(--muted)}";
    html += "form{display:grid;gap:16px}label{display:block;margin-bottom:6px;font-size:14px;font-weight:700}";
    html += "input,button{width:100%;min-height:44px;border-radius:8px;font:inherit}input{border:1px solid var(--line);padding:10px 12px;background:#fff;color:var(--text)}";
    html += "input:focus{outline:2px solid rgba(37,99,235,.18);border-color:var(--accent)}button{border:0;background:var(--accent);color:#fff;font-weight:700}";
    html += ".status{margin-top:16px;padding:12px;border:1px solid var(--line);border-radius:8px;text-align:center;font-size:14px}.success{color:var(--ok);background:#edf8f1}.error{color:var(--bad);background:#fff0f0}.progress{color:#1d4ed8;background:#eff6ff}";
    html += ".footer{margin-top:14px;text-align:center;font-size:12px;color:var(--muted)}";
    html += "</style></head><body>";
    html += "<main>";
    html += "<h1>设备配网</h1>";
    html += "<p>请配置可以联网的 WiFi</p>";
    html += "<form id='wifiForm' onsubmit='return submitConfig(event)'>";
    html += "<div class='form-group'>";
    html += "<label for='ssid'>WiFi名称 (SSID):</label>";
    html += "<input type='text' id='ssid' name='ssid' maxlength='32' required placeholder='请输入WiFi名称'>";
    html += "</div>";
    html += "<div class='form-group'>";
    html += "<label for='password'>WiFi密码:</label>";
    html += "<input type='password' id='password' name='password' maxlength='64' placeholder='请输入WiFi密码（可选）'>";
    html += "</div>";
    html += "<button type='submit'>连接WiFi</button>";
    html += "</form>";
    html += "<div id='status'></div>";
    html += "<div class='footer'>提交后设备会保存配置并自动重启</div>";
    html += "<script>";
    html += "function submitConfig(e) {";
    html += "  e.preventDefault();";
    html += "  var ssid = document.getElementById('ssid').value;";
    html += "  var password = document.getElementById('password').value;";
    html += "  var statusDiv = document.getElementById('status');";
    html += "  statusDiv.innerHTML = '<div class=\"status progress\">正在保存配置，请稍候...</div>';";
    html += "  fetch('/config', {";
    html += "    method: 'POST',";
    html += "    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },";
    html += "    body: 'ssid=' + encodeURIComponent(ssid) + '&password=' + encodeURIComponent(password)";
    html += "  }).then(response => response.text())";
    html += "    .then(data => {";
    html += "      if (data.includes('success')) {";
    html += "        statusDiv.innerHTML = '<div class=\"status success\">✅ 配置成功，设备正在重启并尝试连接 WiFi...</div>';";
    html += "        setTimeout(() => { statusDiv.innerHTML += '<div class=\"footer\">如果连接失败，请重新连接设备热点后再次配置</div>'; }, 2000);";
    html += "      } else {";
    html += "        statusDiv.innerHTML = '<div class=\"status error\">❌ 配置失败: ' + data + '</div>';";
    html += "      }";
    html += "    }).catch(err => {";
    html += "      statusDiv.innerHTML = '<div class=\"status error\">❌ 请求失败: ' + err + '</div>';";
    html += "    });";
    html += "  return false;";
    html += "}";
    html += "</script></main></body></html>";
    return html;
}

/**
 * 处理根路径请求（配网页面）
 */
void handleRoot() {
    getConfigServer().sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    getConfigServer().sendHeader("Pragma", "no-cache");
    getConfigServer().sendHeader("Expires", "0");
    getConfigServer().send(200, "text/html", getConfigPageHTML());
}

void sendNoCacheHeaders() {
    getConfigServer().sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    getConfigServer().sendHeader("Pragma", "no-cache");
    getConfigServer().sendHeader("Expires", "0");
}

void handleCaptivePortal() {
    sendNoCacheHeaders();
    getConfigServer().send(200, "text/html", getConfigPageHTML());
}

void handleNotFoundCaptivePortal() {
    if (apModeStarted) {
        handleCaptivePortal();
        return;
    }
    getConfigServer().send(404, "text/plain", "Not Found");
}

/**
 * 处理WiFi配置提交
 */
void handleConfig() {
    if (getConfigServer().method() != HTTP_POST) {
        getConfigServer().send(405, "text/plain", "Method Not Allowed");
        return;
    }

    String ssid = getConfigServer().arg("ssid");
    String password = getConfigServer().arg("password");

    if (ssid.length() == 0) {
        getConfigServer().send(400, "text/plain", "SSID不能为空");
        return;
    }
    if (ssid.length() > CONFIG_MAX_SSID_BYTES) {
        getConfigServer().send(400, "text/plain", "SSID不能超过32字节");
        return;
    }
    if (password.length() > CONFIG_MAX_PASSWORD_BYTES) {
        getConfigServer().send(400, "text/plain", "WiFi密码不能超过64字节");
        return;
    }

    Serial.println("📝 收到WiFi配置:");
    Serial.printf("   SSID: %s\n", ssid.c_str());
    Serial.printf("   密码: %s\n", password.length() > 0 ? "***" : "(无密码)");

    // 保存配置
    if (!saveWiFiConfig(ssid, password)) {
        getConfigServer().send(500, "text/plain", "保存失败，请重试");
        return;
    }

    getConfigServer().send(200, "text/plain", "success");

    // 延迟后重启
    Serial.println("⏳ 3秒后重启并连接WiFi...");
    delay(3000);
    ESP.restart();
}

/**
 * 初始化Web服务器（AP模式）
 */
void initConfigServer() {
    (void)getConfigServer();
    getConfigServer().on("/", HTTP_GET, handleRoot);
    getConfigServer().on("/config", HTTP_POST, handleConfig);
    getConfigServer().on("/hotspot-detect.html", HTTP_GET, handleCaptivePortal);
    getConfigServer().on("/library/test/success.html", HTTP_GET, handleCaptivePortal);
    getConfigServer().on("/generate_204", HTTP_GET, handleCaptivePortal);
    getConfigServer().on("/gen_204", HTTP_GET, handleCaptivePortal);
    getConfigServer().on("/ncsi.txt", HTTP_GET, handleCaptivePortal);
    getConfigServer().on("/connecttest.txt", HTTP_GET, handleCaptivePortal);
    getConfigServer().onNotFound(handleNotFoundCaptivePortal);
    getConfigServer().begin();
    Serial.println("✅ Web配网服务器已启动（含Captive Portal）");
}

/**
 * 连接WiFi（使用保存的配置）
 */
bool connectWiFi() {
    if (!checkWiFiConfigured()) {
        Serial.println("⚠️  未检测到WiFi配置，将进入AP配网模式");
        return false;
    }

    Serial.println("📶 使用保存的WiFi配置连接...");
    Serial.printf("   SSID: %s\n", savedSSID.c_str());

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, true);
    delay(100);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
#if PROVISIONING_ENABLE_CAPTIVE_DNS
        dnsServer.stop();
#endif
        apModeStarted = false;
        g_apWebServicesStarted = false;
        g_staIpAssigned = false;
        g_staConnectMs = 0;
        g_webStartMs = 0;
        g_pendingWebAfterEpd = false;
        g_staConnectedEventPending = false;
        g_staDisconnectedEventPending = false;
        g_staIpEventPending = false;
        provisioningApSSID = "";
        provisioningDeviceCode = "";
        Serial.println("");
        Serial.println("✅ WiFi连接成功");
        Serial.print("   IP地址: ");
        Serial.println(WiFi.localIP());
        return true;
    } else {
        Serial.println("");
        Serial.println("❌ WiFi连接失败");
        Serial.printf("   WiFi状态码: %d\n", WiFi.status());
        return false;
    }
}

/**
 * WiFi配网初始化
 * @param openApOnSavedWiFiFailure 已保存WiFi但连接失败时是否打开AP修复入口
 * 返回值：true=已连接WiFi，false=未连接；可能已进入AP，也可能按策略回睡
 */
bool initWiFiConfig(bool openApOnSavedWiFiFailure = true) {
    // 检查配网状态
    bool hasSavedConfig = checkWiFiConfigured();
    if (hasSavedConfig) {
        // 尝试连接WiFi
        if (connectWiFi()) {
            wifiConfigured = true;
            return true;
        } else {
            // 连接失败不清除配置：可能只是路由器暂时不可用；长按GPIO0才会主动清除配置
            Serial.println("⚠️  WiFi连接失败，保留原配置");
            wifiConfigured = false;
            if (!openApOnSavedWiFiFailure) {
                Serial.println("   本次唤醒不打开AP，稍后进入Deep-sleep，下次唤醒再重试");
                apModeStarted = false;
                return false;
            }
            Serial.println("   打开AP修复入口，但不会删除已保存的WiFi配置");
        }
    } else {
        Serial.println("⚠️  未检测到WiFi配置，将进入AP配网模式");
    }

    // 进入AP配网模式（startAPMode 内已启动 Web 配网服务器）
    if (!startAPMode()) {
        Serial.println("❌ AP配网模式未能启动，当前不会广播配置热点");
    }
    wifiConfigured = false;
    return false;
}

/**
 * AP模式循环处理（需要在loop中调用）
 */
void handleAPMode() {
    if (!wifiConfigured && apModeStarted && g_apWebServicesStarted) {
#if PROVISIONING_ENABLE_CAPTIVE_DNS
        dnsServer.processNextRequest();
#endif
        getConfigServer().handleClient();
    }
}

void processApProvisioningLoop() {
    if (!apModeStarted || wifiConfigured) {
        return;
    }

    // WiFi事件任务只写标志；日志、Web与DNS初始化全部在主循环执行。
    processPendingProvisioningWiFiEvents();

    const uint32_t sinceAp = apModeElapsedMs();
    if (!g_apWebServicesStarted && !g_dbgEpdActive) {
        if (g_staIpAssigned) {
            ensureApWebServices();
        } else if (g_staConnectMs > 0 && WiFi.softAPgetStationNum() > 0) {
            const uint32_t sinceSta = millis() - g_staConnectMs;
            if (sinceSta >= PROVISIONING_DHCP_GRACE_MS) {
                // #region agent log
                dbgApLog("H7", "processAp", "dhcp_grace_elapsed",
                         sinceSta, ESP.getFreeHeap());
                // #endregion
                Serial.printf("ℹ️ DHCP 宽限 %u ms 已到，启动配网 Web（堆: %u）\n",
                              (unsigned)sinceSta, (unsigned)ESP.getFreeHeap());
                ensureApWebServices();
            }
        } else if (sinceAp >= 30000 && WiFi.softAPgetStationNum() == 0) {
            Serial.println("ℹ️ 30s 内无终端关联，仍启动配网 Web（可手动打开 192.168.4.1）");
            ensureApWebServices();
        }
    }

    handleAPMode();
}

void EPD_ProvisioningYield(void) {
    if (!apModeStarted) {
        return;
    }
    handleAPMode();
    static uint32_t lastYieldLogMs = 0;
    const uint32_t now = millis();
    if (now - lastYieldLogMs >= 5000) {
        lastYieldLogMs = now;
        // #region agent log
        dbgApLog("H3", "EPD_ProvisioningYield", "yield_tick", WiFi.softAPgetStationNum(), 0);
        // #endregion
    }
}

bool isApWebServicesReady() {
    return g_apWebServicesStarted;
}

uint32_t apModeElapsedMs() {
    return millis() - g_apModeStartMs;
}

#endif // WIFI_CONFIG_H
