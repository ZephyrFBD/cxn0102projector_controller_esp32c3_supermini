// network.cpp — WiFi 模式管理、强制门户、多网络连接、mDNS
#include "network.h"
#include "settings.h"    // wifiConfigured, txPowerV, saveSettings
#include "wifi_store.h"  // wifiNets[], wifiNetCount
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>

// ---------------------- SoftAP / Captive --------------------
static const char* AP_SSID = "CXN0102_Web_Controller_Silver";
static const char* AP_PASSWORD = "12345678"; // 必须 ≥ 8 字符
static const byte DNS_PORT = 53;
static const IPAddress AP_IP(192, 168, 4, 1);
static DNSServer dnsServer;
static bool dnsActive = false;

const char* getAPSSID() {
  return AP_SSID;
}

// ---------------------- STA connect state -------------------
static unsigned long connectStartTime = 0;
static bool waitingForWiFi = false;
static int  curNetIdx = 0;       // 当前尝试的网络下标
static int  cycleCount = 0;      // 已完整轮询的次数
static const unsigned long PER_NET_TIMEOUT = 12000; // 每个网络尝试 12 秒
static const int MAX_CYCLES = 2;                    // 全部网络尝试 2 轮仍失败 -> 回 AP

// ---------------------- TX Power ----------------------------
void setTxPower(int powerLevel) {
  wifi_power_t txPower;
  switch (powerLevel) {
    case 78: txPower = WIFI_POWER_19_5dBm; break;
    case 76: txPower = WIFI_POWER_19dBm; break;
    case 74: txPower = WIFI_POWER_18_5dBm; break;
    case 68: txPower = WIFI_POWER_17dBm; break;
    case 60: txPower = WIFI_POWER_15dBm; break;
    case 52: txPower = WIFI_POWER_13dBm; break;
    case 44: txPower = WIFI_POWER_11dBm; break;
    case 34: txPower = WIFI_POWER_8_5dBm; break;
    case 28: txPower = WIFI_POWER_7dBm; break;
    case 20: txPower = WIFI_POWER_5dBm; break;
    case 8:  txPower = WIFI_POWER_2dBm; break;
    case -4: txPower = WIFI_POWER_MINUS_1dBm; break;
    default: txPower = WIFI_POWER_8_5dBm; break;
  }
  if (!WiFi.setTxPower(txPower)) Serial.println("[WiFi] Failed to apply TX power");
}

// ---------------------- mDNS --------------------------------
static void enableMDNS() {
  MDNS.end(); // 幂等
  if (MDNS.begin("cxn0102")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("[mDNS] Started: http://cxn0102.local");
  } else {
    Serial.println("[mDNS] Failed to start");
  }
}

// ---------------------- AP ----------------------------------
void startAPMode() {
  waitingForWiFi = false;
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  bool apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD, 1, 0, 8);
  if (apStarted) {
    setTxPower(txPowerV); // 模式重建后重新应用用户保存的功率
    Serial.println("[WiFi] AP started successfully");
    Serial.print("[WiFi] SSID: ");       Serial.println(AP_SSID);
    Serial.print("[WiFi] IP address: "); Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("[WiFi] Failed to start AP!");
  }
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", AP_IP);
  dnsActive = true;
  enableMDNS();
  Serial.println("[WiFi] Captive portal active");
  Serial.println("[WiFi] >>> 入口 / Entry: http://192.168.4.1  |  http://cxn0102.local");
}

// ---------------------- STA ---------------------------------
static void tryNet(int i) {
  WiFi.disconnect();
  WiFi.begin(wifiNets[i].ssid.c_str(), wifiNets[i].pwd.c_str());
  connectStartTime = millis();
  waitingForWiFi = true;
  Serial.printf("[WiFi] Trying net %d/%d: %s\n", i + 1, wifiNetCount, wifiNets[i].ssid.c_str());
}

void startSTAMode() {
  dnsServer.stop(); dnsActive = false;
  WiFi.mode(WIFI_STA);
  // 跟随路由器 DTIM 进行 modem sleep，保留在线控制。
  if (!WiFi.setSleep(true)) Serial.println("[WiFi] Failed to enable modem sleep");
  setTxPower(txPowerV);
  WiFi.setAutoReconnect(true);
  // 凭证已由 wifi_store（NVS）自行管理；关闭 SDK 持久化，
  // 避免多网络轮询时每次 WiFi.begin 都写 flash（磨损）
  WiFi.persistent(false);
  if (wifiNetCount == 0) {
    Serial.println("[WiFi] No saved networks, fallback to AP.");
    wifiConfigured = false;
    saveSettings();
    startAPMode();
    return;
  }
  curNetIdx = 0;
  cycleCount = 0;
  tryNet(0);
}

static void onConnected() {
  waitingForWiFi = false;
  curNetIdx = 0; cycleCount = 0; // 复位重试计数，避免日后掉线时过早回退 AP
  dnsServer.stop(); dnsActive = false;
  Serial.print("[WiFi] Connected: "); Serial.print(WiFi.SSID());
  Serial.print("  IP: ");             Serial.println(WiFi.localIP());
  WiFi.softAPdisconnect(true);
  enableMDNS();
  Serial.print("[WiFi] >>> 入口 / Entry: http://cxn0102.local  |  http://");
  Serial.println(WiFi.localIP());
}

// STA 连接状态机：依次尝试已保存网络，整体失败 MAX_CYCLES 轮后回 AP
static void wifiConnectFSM() {
  if (!waitingForWiFi) return;
  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) { onConnected(); return; }

  bool failed  = (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL);
  bool timeout = (millis() - connectStartTime > PER_NET_TIMEOUT);
  if (!failed && !timeout) return; // 仍在尝试当前网络

  // 当前网络失败 -> 下一个
  curNetIdx++;
  if (curNetIdx >= wifiNetCount) {
    curNetIdx = 0;
    cycleCount++;
    if (cycleCount >= MAX_CYCLES) {
      // 所有网络多轮都连不上 -> 回 AP 以便重新配置（避免被锁在外面）
      waitingForWiFi = false;
      Serial.println("[WiFi] All saved networks unreachable, fallback to AP.");
      wifiConfigured = false;
      saveSettings();
      startAPMode();
      return;
    }
  }
  tryNet(curNetIdx);
}

void networkLoop() {
  // 强制门户 DNS（仅 AP 模式激活）
  if (dnsActive) dnsServer.processNextRequest();

  // STA 连接状态机
  wifiConnectFSM();

  // 已连上后若掉线，尝试快速重连（仅在非连接中状态触发）
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 10000) {
    if (wifiConfigured && !waitingForWiFi && WiFi.getMode() == WIFI_STA && WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] STA dropped, reconnecting...");
      WiFi.reconnect();
      connectStartTime = millis();
      waitingForWiFi = true;
    }
    lastCheck = millis();
  }
}
