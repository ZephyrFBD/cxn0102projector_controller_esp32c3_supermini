// modify by Lyu on 2025/11/18
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include "esp_system.h"
#include "esp_log.h"
#include <string.h>
#include <EEPROM.h>
// 新增 WiFiManager & mDNS
#include <ESPAsyncWiFiManager.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>

// ---------------------- Pins & Device ----------------------
#define SDA_PIN 8
#define SCL_PIN 7
#define BUTTON_PIN 2
#define I2C_ADDRESS 0x77
#define COM_REQ_PIN 10 // GPIO10 用于 COM_REQ

// ---------------------- Fan PWM -----------------------------
#define FAN_PWM_PIN 12
#define FAN_PWM_CHANNEL 0
#define FAN_PWM_FREQ 25000     // 25kHz 静音风扇常用频率
#define FAN_PWM_RES 8          // 0~255
uint8_t fanPwmValue = 0;       // 当前 PWM

// ---------------------- SoftAP ------------------------------
const char* AP_SSID = "CXN0102_Web_Controller";
const char* AP_PASSWORD = "12345678"; // 必须 ≥ 8 字符
// ---------------------- Commands ----------------------------
static const char* commands[] = {
  "0100", // 1: Start Input
  "0200", // 2: Stop Input
  "0b0101", // 3: Reboot
  "0b0100", // 4: Shutdown
  "3200", // 5: Enter Optical Axis Adjustment
  "3300", // 6: Optical Axis +
  "3400", // 7: Optical Axis -
  "350100", // 8: Exit Optical Axis (No Save)
  "350101", // 9: Exit Optical Axis (Save)
  "3600", // 10: Enter Bi-Phase Adjustment
  "3700", // 11: Bi-Phase +
  "3800", // 12: Bi-Phase -
  "390100", // 13: Exit Bi-Phase (No Save)
  "390101", // 14: Exit Bi-Phase (Save)
  "4A", // 15: Flip Mode
  "5001", // 16: Test Image ON
  "5000", // 17: Test Image OFF
  "6000", // 18: Mute
  "6001", // 19: Unmute
  "7000", // 20: Keystone Vertical -
  "7001", // 21: Keystone Vertical +
  "7002", // 22: Keystone Horizontal -
  "7003", // 23: Keystone Horizontal +
  "8000", // 24: Color Temperature -
  "8001", // 25: Color Temperature +
  "4300", // 26: Set Brightness (默认值)
  "4500", // 27: Set Contrast
  "4700", // 28: Set Hue
  "4900", // 29: Set Saturation
  "4F00", // 30: Set Sharpness
};
// ---------------------- Web Server --------------------------
AsyncWebServer server(80);
AsyncWiFiManager wifiManager(&server, NULL);
// ---------------------- EEPROM Layout -----------------------
#define EEPROM_SIZE 128 // 扩展到128以存储SSID(32)和PWD(64)
#define ADDR_MAGIC 127
#define MAGIC_VALUE 0xA5
// Compact layout
#define ADDR_PAN 0 // int8_t [-30,30]
#define ADDR_TILT 1 // int8_t [-20,20]
#define ADDR_FLIP 2 // uint8_t [0..3]
#define ADDR_TXPOWER 3 // int8_t one of {78,76,74,68,60,52,44,34,28,20,8,-4}
#define ADDR_LANG 4 // uint8_t 0=en,1=zh
// EEPROM 地址扩展
#define ADDR_BRIGHTNESS 5
#define ADDR_CONTRAST 6
#define ADDR_HUE 7 // 旧:单一色调
#define ADDR_SATURATION 8 // 旧:单一饱和度
#define ADDR_SHARPNESS 9
#define ADDR_HUE_U 10 // 新增
#define ADDR_HUE_V 11 // 新增
#define ADDR_SAT_U 12 // 新增
#define ADDR_SAT_V 13 // 新增
// 新增：SSID (32 bytes)
#define ADDR_SSID 14
// 新增：PWD (64 bytes)
#define ADDR_PWD 46
// 新增：WiFi 配网启用标志地址（0=AP only,1=enable STA）
#define ADDR_WIFI_FLAG 110

#define ADDR_FAN_MODE 120   // uint8_t fanMode 


// ---------------------- WiFi Scan --------------------------
bool scanningWiFi = false;
unsigned long scanStartTime = 0;
const unsigned long SCAN_TIMEOUT = 10000; // 10秒扫描超时
String wifiScanResults = "[]";
bool scanRequested = false; // 新增：标记是否用户请求扫描
// ---------------------- Persisted State ---------------------
int panV = 0;
int tiltV = 0;
int flipV = 0;
int txPowerV = 34; // default 8.5dBm
uint8_t langV = 1; // 0=en,1=zh
uint8_t brightnessV = 128;
uint8_t contrastV = 128;
uint8_t hueV = 128;
uint8_t saturationV = 128;
uint8_t sharpnessV = 128;
uint8_t hueU = 128;
uint8_t hueV2 = 128;
uint8_t satU = 128;
uint8_t satV = 128;
String savedSSID = "";
String savedPWD = "";
uint8_t fanMode = 3;   // 3=Auto（默认）, 0=Silent, 1=Normal, 2=Aggressive

// WiFi state
bool wifiConfigured = false; // 从 EEPROM 读取，是否启用 STA 模式尝试（1=启用）
unsigned long connectStartTime = 0; // 连接开始时间（用于 1 分钟超时）
bool waitingForWiFi = false;
// ---------------------- Notify Handling ---------------------
volatile bool notifyPending = false;
uint8_t notifyBuffer[32];
uint8_t notifyLength = 0;
unsigned long lastNotifyTime = 0;
// ---------------------- Device Info Cache -------------------
struct DeviceInfo {
  int temperature = -1;
  int muteThreshold = 60;
  int stopThreshold = 65;
  unsigned long runtime = 0;
  String firmwareVersion = "Unknown";
  String parameterVersion = "Unknown";
  String dataVersion = "Unknown";
  String lotNumber = "Unknown";
  String serialNumber = "Unknown";
  bool infoValid = false;
  unsigned long lastUpdate = 0; // 新增：最后更新时间
};
DeviceInfo deviceInfo;
volatile bool infoRequestPending = false;
uint8_t expectedResponseCmd = 0;
// ---------------------- Utils -------------------------------
static inline int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int map(int x, int in_min, int in_max, int out_min, int out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
void saveSettings() {
  EEPROM.write(ADDR_PAN, (int8_t)panV);
  EEPROM.write(ADDR_TILT, (int8_t)tiltV);
  EEPROM.write(ADDR_FLIP, (uint8_t)flipV);
  EEPROM.write(ADDR_TXPOWER, (int8_t)txPowerV);
  EEPROM.write(ADDR_LANG, (uint8_t)langV);
  EEPROM.write(ADDR_BRIGHTNESS, brightnessV);
  EEPROM.write(ADDR_CONTRAST, contrastV);
  EEPROM.write(ADDR_HUE, hueV);
  EEPROM.write(ADDR_SATURATION, saturationV);
  EEPROM.write(ADDR_SHARPNESS, sharpnessV);
  EEPROM.write(ADDR_HUE_U, hueU);
  EEPROM.write(ADDR_HUE_V, hueV2);
  EEPROM.write(ADDR_SAT_U, satU);
  EEPROM.write(ADDR_SAT_V, satV);
  EEPROM.write(ADDR_WIFI_FLAG, wifiConfigured ? 1 : 0);
  EEPROM.write(ADDR_FAN_MODE, fanMode);
  // 保存SSID
  for (int i = 0; i < 32; i++) {
    EEPROM.write(ADDR_SSID + i, i < savedSSID.length() ? savedSSID[i] : 0);
  }
  // 保存PWD
  for (int i = 0; i < 64; i++) {
    EEPROM.write(ADDR_PWD + i, i < savedPWD.length() ? savedPWD[i] : 0);
  }
  EEPROM.write(ADDR_MAGIC, MAGIC_VALUE);
  EEPROM.commit();
  Serial.println("[EEPROM] Settings saved.");
}
void loadSettings() {
  uint8_t magic = EEPROM.read(ADDR_MAGIC);
  if (magic != MAGIC_VALUE) {
    // First boot defaults
    panV = 0; tiltV = 0; flipV = 0; txPowerV = 34; langV = 1;
    brightnessV = 128; contrastV = 128; hueV = 128; saturationV = 128; sharpnessV = 128;
    hueU = 128; hueV2 = 128; satU = 128; satV = 128;
    wifiConfigured = false;
    savedSSID = "";
    savedPWD = "";
    fanMode = 3;
    saveSettings();
    Serial.println("[EEPROM] Initialized defaults.");
    return;
  }
  panV = (int8_t)EEPROM.read(ADDR_PAN);
  tiltV = (int8_t)EEPROM.read(ADDR_TILT);
  flipV = (uint8_t)EEPROM.read(ADDR_FLIP);
  txPowerV = (int8_t)EEPROM.read(ADDR_TXPOWER);
  langV = (uint8_t)EEPROM.read(ADDR_LANG);
  brightnessV = EEPROM.read(ADDR_BRIGHTNESS);
  contrastV = EEPROM.read(ADDR_CONTRAST);
  hueV = EEPROM.read(ADDR_HUE);
  saturationV = EEPROM.read(ADDR_SATURATION);
  sharpnessV = EEPROM.read(ADDR_SHARPNESS);
  hueU = EEPROM.read(ADDR_HUE_U);
  hueV2 = EEPROM.read(ADDR_HUE_V);
  satU = EEPROM.read(ADDR_SAT_U);
  satV = EEPROM.read(ADDR_SAT_V);
  wifiConfigured = EEPROM.read(ADDR_WIFI_FLAG) == 1;
  fanMode = EEPROM.read(ADDR_FAN_MODE);
  // 加载SSID
  savedSSID = "";
  for (int i = 0; i < 32; i++) {
    char c = EEPROM.read(ADDR_SSID + i);
    if (c == 0) break;
    savedSSID += c;
  }
  // 加载PWD
  savedPWD = "";
  for (int i = 0; i < 64; i++) {
    char c = EEPROM.read(ADDR_PWD + i);
    if (c == 0) break;
    savedPWD += c;
  }
  // Basic sanitization
  panV = clamp(panV, -30, 30);
  tiltV = clamp(tiltV, -20, 20);
  if (flipV < 0 || flipV > 3) flipV = 0;
  brightnessV = clamp(brightnessV, 0, 255);
  contrastV = clamp(contrastV, 0, 255);
  hueV = clamp(hueV, 0, 255);
  saturationV = clamp(saturationV, 0, 255);
  sharpnessV = clamp(sharpnessV, 0, 255);
  hueU = clamp(hueU, 0, 255);
  hueV2 = clamp(hueV2, 0, 255);
  satU = clamp(satU, 0, 255);
  satV = clamp(satV, 0, 255);
  Serial.printf("[EEPROM] Loaded: pan=%d tilt=%d flip=%d txPower=%d lang=%u brightness=%u contrast=%u hue=%u hueU=%u hueV=%u saturation=%u satU=%u satV=%u sharpness=%u wifiConfigured=%d ssid=%s\n",
                panV, tiltV, flipV, txPowerV, langV, brightnessV, contrastV, hueV, hueU, hueV2, saturationV, satU, satV, sharpnessV, wifiConfigured, savedSSID.c_str());
}
// ---------------------- WiFi Tx Power -----------------------
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
      case 8: txPower = WIFI_POWER_2dBm; break;
      case -4: txPower = WIFI_POWER_MINUS_1dBm; break;
      default: txPower = WIFI_POWER_8_5dBm; break;
  }
  WiFi.setTxPower(txPower);
}
// ---------------------- I2C Helpers -------------------------
void sendKeystoneAndFlip(int pan, int tilt, int flip) {
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(0x26); // Set Video Output Position Information
  Wire.write(0x09); // Size
  Wire.write(pan & 0xFF);
  Wire.write(tilt & 0xFF);
  Wire.write(flip & 0xFF);
  for (int i = 0; i < 6; i++) Wire.write((i == 0) ? 0x64 : 0x00); // Fixed values
  uint8_t error = Wire.endTransmission();
  if (error) {
    Serial.printf("I2C error while sending keystone command: %d\n", error);
  } else {
    Serial.println("Keystone and Flip command sent successfully.");
  }
}
void sendI2CCommand(const char* cmd) {
  Wire.beginTransmission(I2C_ADDRESS);
  for (int i = 0; i < (int)strlen(cmd); i += 2) {
    char byteStr[3] = {cmd[i], cmd[i + 1], '\0'};
    uint8_t byteVal = (uint8_t)strtol(byteStr, NULL, 16);
    Wire.write(byteVal);
  }
  uint8_t error = Wire.endTransmission();
  if (error) {
    Serial.printf("I2C error: %d\n", error);
  } else {
    Serial.println("Command sent successfully.");
  }
}
// ---------------------- WiFi Scan Functions -----------------
void startWiFiScan() {
  if (scanningWiFi) return;
 
  Serial.println("[WiFi] Starting WiFi scan...");
  scanningWiFi = true;
  scanStartTime = millis();
  scanRequested = false; // 重置请求标志
 
  // 切换到AP+STA模式进行扫描
  WiFi.mode(WIFI_AP_STA);
  WiFi.scanNetworks(true); // 异步扫描
}
void processWiFiScan() {
  if (!scanningWiFi) return;
 
  int scanResult = WiFi.scanComplete();
  if (scanResult == WIFI_SCAN_RUNNING) {
    // 检查超时
    if (millis() - scanStartTime > SCAN_TIMEOUT) {
      WiFi.scanDelete();
      scanningWiFi = false;
      Serial.println("[WiFi] Scan timeout");
      wifiScanResults = "[]";
     
      // 扫描完成后恢复AP模式
      WiFi.mode(WIFI_AP);
      Serial.println("[WiFi] Restored AP mode after scan");
    }
    return;
  }
 
  scanningWiFi = false;
 
  if (scanResult == WIFI_SCAN_FAILED) {
    Serial.println("[WiFi] Scan failed");
    wifiScanResults = "[]";
    // 恢复AP模式
    WiFi.mode(WIFI_AP);
    Serial.println("[WiFi] Restored AP mode after failed scan");
    return;
  }
 
  // 构建扫描结果JSON
  String json = "[";
  for (int i = 0; i < scanResult; ++i) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"encryption\":" + String(WiFi.encryptionType(i)) + ",";
    json += "\"channel\":" + String(WiFi.channel(i));
    json += "}";
  }
  json += "]";
 
  wifiScanResults = json;
  Serial.printf("[WiFi] Scan completed: %d networks found\n", scanResult);
 
  // 清理扫描结果
  WiFi.scanDelete();
 
  // 扫描完成后恢复AP模式
  WiFi.mode(WIFI_AP);
  Serial.println("[WiFi] Restored AP mode after scan completion");
}
// ---------------------- WiFi helpers ------------------------
void startAPMode() {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(
      IPAddress(192, 168, 4, 1),
      IPAddress(192, 168, 4, 1),
      IPAddress(255, 255, 255, 0)
    );
    bool apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD, 1, 0, 8);
    if (apStarted) {
      Serial.println("[WiFi] AP started successfully");
      Serial.print("[WiFi] SSID: "); Serial.println(AP_SSID);
      Serial.print("[WiFi] IP address: "); Serial.println(WiFi.softAPIP());
      Serial.print("[WiFi] MAC address: "); Serial.println(WiFi.softAPmacAddress());
    } else {
      Serial.println("[WiFi] Failed to start AP!");
    }
    // 启动时扫描并缓存
    startWiFiScan();
}
void startSTAMode() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
   
    // 使用保存的SSID和PWD尝试连接
    if (savedSSID.length() > 0) {
      WiFi.begin(savedSSID.c_str(), savedPWD.c_str());
      connectStartTime = millis();
      waitingForWiFi = true;
      Serial.printf("[WiFi] Trying to connect to SSID: %s\n", savedSSID.c_str());
    } else {
      Serial.println("[WiFi] No saved credentials, fallback to AP.");
      wifiConfigured = false;
      saveSettings();
      startAPMode();
    }
}
void enableMDNS() {
    if (MDNS.begin("cxn0102")) {
      MDNS.addService("http", "tcp", 80);
      Serial.println("[mDNS] Started: cxn0102.local");
    } else {
      Serial.println("[mDNS] Failed to start");
    }
}
void checkWiFiReconnectFallback() {
    if (waitingForWiFi) {
        wl_status_t status = WiFi.status();

        if (status == WL_CONNECTED) {
            waitingForWiFi = false;
            Serial.print("[WiFi] Connected. IP: ");
            Serial.println(WiFi.localIP());

            WiFi.softAPdisconnect(true);
            enableMDNS();
        }

        // 只有找不到 SSID 才回到 AP（需求）
        else if (status == WL_NO_SSID_AVAIL) {
            waitingForWiFi = false;
            wifiConfigured = false;
            saveSettings();
            Serial.println("[WiFi] Saved SSID not found, fallback to AP.");
            startAPMode();
        }

        // 连接失败：继续尝试，不切 AP
        else if (status == WL_CONNECT_FAILED) {
            Serial.println("[WiFi] Connection failed, retrying...");
            WiFi.disconnect();
            WiFi.begin(savedSSID.c_str(), savedPWD.c_str());
            connectStartTime = millis();
        }

        // 超时 1 分钟：继续保持 STA，不切 AP，但可重新尝试
        else if (millis() - connectStartTime > 60000) {
            Serial.println("[WiFi] Connect timeout, retrying STA...");
            WiFi.disconnect();
            WiFi.begin(savedSSID.c_str(), savedPWD.c_str());
            connectStartTime = millis();
        }
    }
}

// ---------------------- Notify Handling ---------------------
void handleCOM_REQ_ISR() {
  notifyPending = true;
  lastNotifyTime = millis();
}
String bytesToHexString(const uint8_t* data, uint8_t length) {
  String result = "";
  for (int i = 0; i < length; i++) {
    char hex[3];
    sprintf(hex, "%02X", data[i]);
    result += hex;
  }
  return result;
}
// 修复版本信息解析函数
String parseVersion(const uint8_t* data, uint8_t startIndex) {
  String version = "";
  for (int i = 0; i < 4; i++) {
    if (data[startIndex + i] >= 32 && data[startIndex + i] <= 126) {
      version += (char)data[startIndex + i];
    } else {
      // 如果字符不可打印，显示为十六进制
      char hex[3];
      sprintf(hex, "%02X", data[startIndex + i]);
      version += hex;
    }
  }
  return version;
}
// 修复LOT Number解析函数 - 根据文档格式
String parseLOTNumber(const uint8_t* data, uint8_t startIndex) {
  // LOT Number包含3个4字节数据: LOT0, LOT1, LOT2
  // 每个4字节格式: VV WW XX YY (大端格式)
  String lot = "";
  for (int lotIndex = 0; lotIndex < 3; lotIndex++) {
    int base = startIndex + (lotIndex * 4);
    char buffer[9];
    // 按照文档格式: VV WW XX YY
    sprintf(buffer, "%02X%02X%02X%02X",
            data[base + 3], // VV部分
            data[base + 2], // WW部分
            data[base + 1], // XX部分
            data[base]); // YY部分
    lot += String(buffer);
    if (lotIndex < 2) lot += "-";
  }
  return lot;
}
// 修复Serial Number解析函数 - 根据文档格式
String parseSerialNumber(const uint8_t* data, uint8_t startIndex) {
  // Serial Number包含2个4字节数据: Serial0, Serial1
  // 每个4字节格式: VV WW XX YY (大端格式)
  String serial = "";
  for (int serialIndex = 0; serialIndex < 2; serialIndex++) {
    int base = startIndex + (serialIndex * 4);
    char buffer[9];
    // 按照文档格式: VV WW XX YY
    sprintf(buffer, "%02X%02X%02X%02X",
            data[base + 3], // VV部分
            data[base + 2], // WW部分
            data[base + 1], // XX部分
            data[base]); // YY部分
    serial += String(buffer);
    if (serialIndex < 1) serial += "-";
  }
  return serial;
}
void processNotify() {
  if (!notifyPending) return;
  // 读取 Notify 数据
  Wire.requestFrom((uint8_t)I2C_ADDRESS, (uint8_t)32);
  notifyLength = 0;
  while (Wire.available() && notifyLength < 32) {
    notifyBuffer[notifyLength++] = Wire.read();
  }
  // 处理 Notify 数据
  if (notifyLength >= 3) {
    uint8_t cmd = notifyBuffer[0];
    uint8_t size = notifyBuffer[1];
    uint8_t result = notifyBuffer[2];
  
    Serial.printf("[NOTIFY] CMD: 0x%02X, Size: %d, Result: 0x%02X, Data: ", cmd, size, result);
    for (int i = 0; i < notifyLength; i++) {
      Serial.printf("%02X ", notifyBuffer[i]);
    }
    Serial.println();
  
    switch (cmd) {
      case 0x00: // Boot Completed Notify
        Serial.println("[NOTIFY] Boot Completed");
        if (result != 0x00) {
          Serial.printf("[NOTIFY] Boot error: 0x%02X\n", result);
        }
        break;
      
      case 0x10: // Emergency Notify
        Serial.printf("[NOTIFY] Emergency: 0x%02X\n", result);
        break;
      
      case 0x11: // Temperature Emergency and Recovery Notify
        if (result == 0x80 || result == 0x81) {
          Serial.printf("[NOTIFY] Temperature Emergency: 0x%02X\n", result);
        } else {
          Serial.printf("[NOTIFY] Temperature Recovery: 0x%02X\n", result);
        }
        break;
      
      case 0x12: // Command Emergency Notify
        Serial.printf("[NOTIFY] Command Error: 0x%02X\n", result);
        break;
      
      case 0xA0: // Get Temperature response - 根据第一部分文档
        if (notifyLength >= 6) {
          if (result == 0x00) {
            deviceInfo.temperature = notifyBuffer[3]; // OP2: 温度数据 (0x00-0x64)
            deviceInfo.muteThreshold = notifyBuffer[4]; // OP3: 静音阈值
            deviceInfo.stopThreshold = notifyBuffer[5]; // OP4: 系统停止阈值
            deviceInfo.infoValid = true;
            Serial.printf("[NOTIFY] Temperature: %d°C, Mute Threshold: %d°C, Stop Threshold: %d°C\n",
                         deviceInfo.temperature, deviceInfo.muteThreshold, deviceInfo.stopThreshold);
          } else {
            Serial.printf("[NOTIFY] Get Temperature abnormal: 0x%02X\n", result);
            deviceInfo.temperature = -1; // 标记为无效
          }
        } else {
          Serial.println("[NOTIFY] Incomplete Get Temperature response");
        }
        break;
      
      case 0xA1: // Get Time response - 根据第二部分文档修复
        if (notifyLength >= 7) {
          if (result == 0x00) {
            // 根据文档，运行时间是小端格式：OP2=DD, OP3=CC, OP4=BB, OP5=AA
            deviceInfo.runtime = ((unsigned long)notifyBuffer[6] << 24) | // OP5: AA部分
                                 ((unsigned long)notifyBuffer[5] << 16) | // OP4: BB部分
                                 ((unsigned long)notifyBuffer[4] << 8) | // OP3: CC部分
                                 notifyBuffer[3]; // OP2: DD部分
            Serial.printf("[NOTIFY] Runtime: %lu seconds (%lu hours %lu minutes)\n",
                         deviceInfo.runtime, deviceInfo.runtime/3600, (deviceInfo.runtime%3600)/60);
          } else {
            Serial.printf("[NOTIFY] Get Time abnormal: 0x%02X\n", result);
          }
        } else {
          Serial.println("[NOTIFY] Incomplete Get Time response");
        }
        break;
      
      case 0xA2: // Get Version response - 根据第二部分文档修复
        if (notifyLength >= 14) {
          if (result == 0x00) {
            // 根据文档解析版本信息
            // OP2-OP5: 固件版本 (4字节ASCII)
            // OP6-OP9: 参数版本 (4字节ASCII)
            // OP10-OP13: 数据版本 (4字节ASCII)
            deviceInfo.firmwareVersion = parseVersion(notifyBuffer, 3); // OP2-OP5
            deviceInfo.parameterVersion = parseVersion(notifyBuffer, 7); // OP6-OP9
            deviceInfo.dataVersion = parseVersion(notifyBuffer, 11); // OP10-OP13
           
            Serial.printf("[NOTIFY] Firmware Version: %s, Parameter Version: %s, Data Version: %s\n",
                        deviceInfo.firmwareVersion.c_str(),
                        deviceInfo.parameterVersion.c_str(),
                        deviceInfo.dataVersion.c_str());
          } else {
            Serial.printf("[NOTIFY] Get Version abnormal: 0x%02X\n", result);
          }
        } else {
          Serial.println("[NOTIFY] Incomplete Get Version response");
        }
        break;
      case 0xB2: // Get LOT Number response - 根据第三部分文档修复
        if (notifyLength >= 15) {
          if (result == 0x00) {
            // LOT Number包含3个4字节数据: LOT0, LOT1, LOT2
            // 数据从OP2开始: OP2-OP5=LOT0, OP6-OP9=LOT1, OP10-OP13=LOT2
            deviceInfo.lotNumber = parseLOTNumber(notifyBuffer, 3);
            Serial.printf("[NOTIFY] LOT Number: %s\n", deviceInfo.lotNumber.c_str());
          } else {
            Serial.printf("[NOTIFY] Get LOT Number abnormal: 0x%02X\n", result);
          }
        } else {
          Serial.println("[NOTIFY] Incomplete Get LOT Number response");
        }
        break;
      
      case 0xB4: // Get Serial Number response - 根据第三部分文档修复
        if (notifyLength >= 11) {
          if (result == 0x00) {
            // Serial Number包含2个4字节数据: Serial0, Serial1
            // 数据从OP2开始: OP2-OP5=Serial0, OP6-OP9=Serial1
            deviceInfo.serialNumber = parseSerialNumber(notifyBuffer, 3);
            Serial.printf("[NOTIFY] Serial Number: %s\n", deviceInfo.serialNumber.c_str());
          } else {
            Serial.printf("[NOTIFY] Get Serial Number abnormal: 0x%02X\n", result);
          }
        } else {
          Serial.println("[NOTIFY] Incomplete Get Serial Number response");
        }
        break;
      
      default:
        Serial.printf("[NOTIFY] Unknown command: 0x%02X\n", cmd);
        break;
    }
  } else {
    Serial.println("[NOTIFY] Invalid notify data length");
  }
  notifyPending = false;
}
// ---------------------- Info Request Helpers ----------------
bool sendInfoRequestAndRead(uint8_t cmd, uint8_t* response, uint8_t expectedLength) {
  // Send request
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(cmd);
  Wire.write(0x00); // OP0=0
  uint8_t error = Wire.endTransmission();
  if (error) {
    Serial.printf("[INFO] I2C error sending request 0x%02X: %d\n", cmd, error);
    return false;
  }
 
  // Wait for response
  delay(50); // 增加延迟以确保响应可用
 
  // Read response
  Wire.requestFrom((uint8_t)I2C_ADDRESS, expectedLength);
  uint8_t readLength = 0;
  while (Wire.available() && readLength < expectedLength) {
    response[readLength++] = Wire.read();
  }
 
  if (readLength != expectedLength) {
    Serial.printf("[INFO] Incomplete response for 0x%02X: expected %d, got %d\n", cmd, expectedLength, readLength);
    return false;
  }
 
  // Check CMD and Result
  if (response[0] != cmd || response[2] != 0x00) {
    Serial.printf("[INFO] Invalid response for 0x%02X: CMD=0x%02X Result=0x%02X\n", cmd, response[0], response[2]);
    return false;
  }
 
  return true;
}
bool requestTemperature() {
  uint8_t response[6];
  if (!sendInfoRequestAndRead(0xA0, response, 6)) {
    return false;
  }
 
  deviceInfo.temperature = response[3];
  deviceInfo.muteThreshold = response[4];
  deviceInfo.stopThreshold = response[5];
  deviceInfo.lastUpdate = millis();
 
  Serial.printf("[INFO] Temperature: %d°C, Mute: %d°C, Stop: %d°C\n",
               deviceInfo.temperature, deviceInfo.muteThreshold, deviceInfo.stopThreshold);
  return true;
}
bool requestRuntime() {
  uint8_t response[7];
  if (!sendInfoRequestAndRead(0xA1, response, 7)) {
    return false;
  }
 
  deviceInfo.runtime = ((unsigned long)response[6] << 24) |
                      ((unsigned long)response[5] << 16) |
                      ((unsigned long)response[4] << 8) |
                      response[3];
  deviceInfo.lastUpdate = millis();
 
  Serial.printf("[INFO] Runtime: %lu seconds (%lu hours %lu minutes)\n",
               deviceInfo.runtime, deviceInfo.runtime/3600, (deviceInfo.runtime%3600)/60);
  return true;
}
bool requestVersion() {
  uint8_t response[14];
  if (!sendInfoRequestAndRead(0xA2, response, 14)) {
    return false;
  }
 
  deviceInfo.firmwareVersion = parseVersion(response, 3);
  deviceInfo.parameterVersion = parseVersion(response, 7);
  deviceInfo.dataVersion = parseVersion(response, 11);
  deviceInfo.lastUpdate = millis();
 
  Serial.printf("[INFO] Firmware: %s, Parameter: %s, Data: %s\n",
               deviceInfo.firmwareVersion.c_str(),
               deviceInfo.parameterVersion.c_str(),
               deviceInfo.dataVersion.c_str());
  return true;
}
bool requestLOTNumber() {
  uint8_t response[15];
  if (!sendInfoRequestAndRead(0xB2, response, 15)) {
    return false;
  }
 
  deviceInfo.lotNumber = parseLOTNumber(response, 3);
  deviceInfo.lastUpdate = millis();
  Serial.printf("[INFO] LOT Number: %s\n", deviceInfo.lotNumber.c_str());
  return true;
}
bool requestSerialNumber() {
  uint8_t response[11];
  if (!sendInfoRequestAndRead(0xB4, response, 11)) {
    return false;
  }
 
  deviceInfo.serialNumber = parseSerialNumber(response, 3);
  deviceInfo.lastUpdate = millis();
  Serial.printf("[INFO] Serial Number: %s\n", deviceInfo.serialNumber.c_str());
  return true;
}
void requestAllDeviceInfo() {
  Serial.println("[INFO] ===== Requesting all device information () =====");
 
  // 依次请求所有信息，增加延迟避免冲突
  if (requestTemperature()) {
    delay(400);
  }
  if (requestRuntime()) {
    delay(400);
  }
  if (requestVersion()) {
    delay(400);
  }
  if (requestLOTNumber()) {
    delay(400);
  }
  if (requestSerialNumber()) {
    delay(400);
  }
 
  Serial.println("[INFO] ===== All device info requests completed =====");
}

void adjustFanSpeed() {
  if (fanMode == 4 || deviceInfo.temperature == -1) return;
  
  int temp = deviceInfo.temperature;
  
  // 各模式的温度-PWM曲线参数
  // 格式: {min_temp, max_temp, min_pwm, max_pwm, 曲线系数}
  struct FanCurve {
    int temp_min;
    int temp_max;
    uint8_t pwm_min;
    uint8_t pwm_max;
    float curve_factor; // 曲线陡峭程度 (1.0=线性, >1.0=加速, <1.0=减速)
  };
  
  FanCurve curve;
  
  switch (fanMode) {
    case 0: // Silent: 静音模式，低温就慢慢启动，平缓曲线
      curve = {25, 50, 30, 120, 0.5}; // 平缓曲线，温度低就启动，最大PWM不高
      break;
    case 1: // Normal: 正常模式
      curve = {25, 70, 60, 220, 1.2}; 
      break;
    case 2: // Aggressive: 激进模式，低温启动，陡峭曲线
      curve = {25, 65, 80, 255, 1.8}; 
      break;
    case 3: // Auto: 自动模式（平衡）
    default:
      curve = {25, 70, 50, 200, 1.0}; 
      break;
  }
  
  // 限制温度在有效范围内
  temp = constrain(temp, curve.temp_min, curve.temp_max);
  
  // 计算归一化位置 (0.0 ~ 1.0)
  float normalized = (float)(temp - curve.temp_min) / (float)(curve.temp_max - curve.temp_min);
  
  // 应用曲线函数
  float curved;
  if (curve.curve_factor == 1.0) {
    curved = normalized; // 线性
  } else if (curve.curve_factor > 1.0) {
    curved = pow(normalized, curve.curve_factor); // 陡峭曲线
  } else {
    curved = 1.0 - pow(1.0 - normalized, 1.0 / curve.curve_factor); // 平缓曲线
  }
  
  // 计算PWM值
  fanPwmValue = (uint8_t)(curve.pwm_min + curved * (curve.pwm_max - curve.pwm_min));
  
  // 确保PWM在有效范围内
  fanPwmValue = constrain(fanPwmValue, curve.pwm_min, curve.pwm_max);
  
  ledcWrite(FAN_PWM_CHANNEL, fanPwmValue);
  
  Serial.printf("[Fan] Mode=%d Temp=%d°C PWM=%u (Curve: %d-%d°C -> %d-%d PWM, factor=%.1f)\n", 
                fanMode, temp, fanPwmValue, 
                curve.temp_min, curve.temp_max, 
                curve.pwm_min, curve.pwm_max, 
                curve.curve_factor);
}

void setFanModeInternal(uint8_t mode) {
  fanMode = mode;
  saveSettings();
  if (fanMode == 4) {
    fanPwmValue = 255;
    ledcWrite(FAN_PWM_CHANNEL, fanPwmValue);
    Serial.printf("[Fan] Full mode PWM=255\n");
  } else {
    adjustFanSpeed(); // 设置初始基于当前温度
    Serial.printf("[Fan] Mode=%d enabled, PWM will adjust based on temp\n", fanMode);
  }
}
void setFanPwmValue(uint8_t v) {
  fanPwmValue = v;
  ledcWrite(FAN_PWM_CHANNEL, fanPwmValue);
  Serial.printf("[Fan] PWM set to %u\n", fanPwmValue);
}

// ---------------------- Test Pattern Function ---------------
void sendTestPattern(uint8_t pattern, uint8_t generalSetting = 0x00,
                    uint8_t bgR = 0x00, uint8_t bgG = 0x00, uint8_t bgB = 0x00,
                    uint8_t fgR = 0xFF, uint8_t fgG = 0xFF, uint8_t fgB = 0xFF) {
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(0xA3); // Output Test Picture
  Wire.write(0x11); // OP0: Size = 17 bytes
  // OP1: Test pattern
  Wire.write(pattern);
  // OP2: General purpose setting
  Wire.write(generalSetting);
  // OP3-OP5: Background color (R, G, B)
  Wire.write(bgR);
  Wire.write(bgG);
  Wire.write(bgB);
  // OP6-OP8: Foreground color (R, G, B)
  Wire.write(fgR);
  Wire.write(fgG);
  Wire.write(fgB);
  // OP9-OP17: Reserved (0x00)
  for (int i = 0; i < 9; i++) {
    Wire.write(0x00);
  }
  uint8_t error = Wire.endTransmission();
  if (error) {
    Serial.printf("I2C error while sending test pattern: %d\n", error);
  } else {
    Serial.printf("Test pattern command sent: pattern=0x%02X\n", pattern);
  }
}
// ---------------------- Web Routes Setup --------------------
void setupRoutes() {
  // 初始化 SPIFFS
  if(!SPIFFS.begin(true)){
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    File file = SPIFFS.open("/web_interface.html", "r");
    if (!file) {
      request->send(404, "text/plain", "HTML file not found");
      return;
    }
    
    String htmlContent;
    while(file.available()){
      htmlContent += file.readString();
    }
    file.close();
    
    request->send(200, "text/html", htmlContent);
  });
 
  // Commands by index
  server.on("/command", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("cmd")) {
      request->send(400, "text/plain", "Missing cmd parameter");
      return;
    }
    String cmdStr = request->getParam("cmd")->value();
    int cmdIndex = cmdStr.toInt();
    if (cmdIndex < 1 || cmdIndex > (int)(sizeof(commands) / sizeof(commands[0]))) {
      request->send(400, "text/plain", "Invalid command index");
      return;
    }
    sendI2CCommand(commands[cmdIndex - 1]);
    request->send(200, "text/plain", "Command executed");
  });
 
  // Keystone (also persist)
  server.on("/keystone", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("pan") || !request->hasParam("tilt") || !request->hasParam("flip")) {
      request->send(400, "text/plain", "Missing parameters");
      return;
    }
    panV = clamp(request->getParam("pan")->value().toInt(), -30, 30);
    tiltV = clamp(request->getParam("tilt")->value().toInt(), -20, 20);
    flipV = request->getParam("flip")->value().toInt();
    if (flipV < 0 || flipV > 3) flipV = 0;
    sendKeystoneAndFlip(panV, tiltV, flipV);
    saveSettings();
    request->send(200, "text/plain", "Keystone and Flip updated");
  });
 
  // Custom command (with validation)
  server.on("/custom_command", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("cmd")) {
      request->send(400, "text/plain", "Missing cmd parameter");
      return;
    }
    String customCmd = request->getParam("cmd")->value();
    if (customCmd.length() % 2 != 0 || customCmd.length() > 50) {
      request->send(400, "text/plain", "Invalid command format");
      return;
    }
    // Hex check
    for (size_t i = 0; i < customCmd.length(); ++i) {
      char c = customCmd[i];
      bool ok = (c >= '0' && c <= '9') || (c>='a'&&c<='f') || (c>='A'&&c<='F');
      if (!ok) {
        request->send(400, "text/plain", "Invalid hex content");
        return;
      }
    }
    sendI2CCommand(customCmd.c_str());
    request->send(200, "text/plain", "Custom command sent");
  });
 
  // Test Pattern
  server.on("/test_pattern", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("pattern")) {
      request->send(400, "text/plain", "Missing pattern parameter");
      return;
    }
    uint8_t pattern = request->getParam("pattern")->value().toInt();
    sendTestPattern(pattern);
    request->send(200, "text/plain", "Test pattern command sent");
  });
 
  // Set Tx Power (apply & persist)
  server.on("/set_tx_power", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("power")) {
        request->send(400, "text/plain", "Missing power parameter");
        return;
    }
    txPowerV = request->getParam("power")->value().toInt();
    setTxPower(txPowerV);
    saveSettings();
    request->send(200, "text/plain", "Transmit Power set to " + String(txPowerV / 4.0) + " dBm");
  });
 
  // Ping indicator
  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "OK");
  });
 
  // Get all persisted settings (for page bootstrap)
  server.on("/get_settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"pan\":" + String(panV) + ",";
    json += "\"tilt\":" + String(tiltV) + ",";
    json += "\"flip\":" + String(flipV) + ",";
    json += "\"txPower\":" + String(txPowerV) + ",";
    json += "\"lang\":\"" + String((langV==0)?"en":"zh") + "\",";
    json += "\"brightness\":" + String(brightnessV) + ",";
    json += "\"contrast\":" + String(contrastV) + ",";
    json += "\"hueU\":" + String(hueU) + ",";
    json += "\"hueV\":" + String(hueV2) + ",";
    json += "\"satU\":" + String(satU) + ",";
    json += "\"satV\":" + String(satV) + ",";
    json += "\"sharpness\":" + String(sharpnessV) + ",";
    json += "\"fanMode\":" + String(fanMode);
    json += "}";
    request->send(200, "application/json", json);
  });
 
  // Set settings (any subset). Apply where relevant & persist.
  server.on("/set_settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool changed = false;
    if (request->hasParam("pan")) { panV = clamp(request->getParam("pan")->value().toInt(), -30, 30); changed=true; }
    if (request->hasParam("tilt")) { tiltV = clamp(request->getParam("tilt")->value().toInt(), -20, 20); changed=true; }
    if (request->hasParam("flip")) { flipV = request->getParam("flip")->value().toInt(); if (flipV<0||flipV>3) flipV=0; changed=true; }
    if (request->hasParam("txPower")) { txPowerV = request->getParam("txPower")->value().toInt(); setTxPower(txPowerV); changed=true; }
    if (request->hasParam("lang")) {
      String l = request->getParam("lang")->value();
      langV = (l == "zh") ? 1 : 0;
      changed = true;
    }
    if (changed) {
      // Apply geometry if changed
      sendKeystoneAndFlip(panV, tiltV, flipV);
      saveSettings();
    }
    request->send(200, "text/plain", "OK");
  });
 
  // Set language explicitly
  server.on("/set_lang", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("lang")) {
      request->send(400, "text/plain", "Missing lang");
      return;
    }
    String l = request->getParam("lang")->value();
    langV = (l == "zh") ? 1 : 0;
    saveSettings();
    request->send(200, "text/plain", "Lang updated");
  });
 
  // Set Picture Quality (via I2C)
  server.on("/set_pq", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool pqChanged = false;
    if (request->hasParam("brightness")) {
      brightnessV = request->getParam("brightness")->value().toInt();
      int val = map(brightnessV, 0, 255, -31, 31);
      Wire.beginTransmission(I2C_ADDRESS);
      Wire.write(0x43); // Set Brightness
      Wire.write(0x01);
      Wire.write((int8_t)val);
      Wire.endTransmission();
      pqChanged = true;
    }
    if (request->hasParam("contrast")) {
      contrastV = request->getParam("contrast")->value().toInt();
      int val = map(contrastV, 0, 255, -15, 15);
      Wire.beginTransmission(I2C_ADDRESS);
      Wire.write(0x45); // Set Contrast
      Wire.write(0x01);
      Wire.write((int8_t)val);
      Wire.endTransmission();
      pqChanged = true;
    }
    if (request->hasParam("hueU") && request->hasParam("hueV")) {
      hueU = request->getParam("hueU")->value().toInt();
      hueV2 = request->getParam("hueV")->value().toInt();
      int u = map(hueU, 0, 255, -15, 15);
      int v = map(hueV2, 0, 255, -15, 15);
      Wire.beginTransmission(I2C_ADDRESS);
      Wire.write(0x47); // Set Hue
      Wire.write(0x02); // OP0=2
      Wire.write((int8_t)u); // OP1: U
      Wire.write((int8_t)v); // OP2: V
      uint8_t error = Wire.endTransmission();
      Serial.printf("[PQ] Set Hue U/V: %d/%d, I2C error=%d\n", u, v, error);
      pqChanged = true;
    }
    if (request->hasParam("satU") && request->hasParam("satV")) {
      satU = request->getParam("satU")->value().toInt();
      satV = request->getParam("satV")->value().toInt();
      int u = map(satU, 0, 255, -15, 15);
      int v = map(satV, 0, 255, -15, 15);
      Wire.beginTransmission(I2C_ADDRESS);
      Wire.write(0x49); // Set Saturation
      Wire.write(0x02); // OP0=2
      Wire.write((int8_t)u); // OP1: U
      Wire.write((int8_t)v); // OP2: V
      Wire.endTransmission();
      pqChanged = true;
    }
    if (request->hasParam("sharpness")) {
      sharpnessV = request->getParam("sharpness")->value().toInt();
      int val = map(sharpnessV, 0, 255, 0, 8);
      Wire.beginTransmission(I2C_ADDRESS);
      Wire.write(0x4F); // Set Sharpness
      Wire.write(0x01);
      Wire.write((uint8_t)val);
      Wire.endTransmission();
      pqChanged = true;
    }
    if (pqChanged) {
      saveSettings();
    }
    request->send(200, "text/plain", "PQ updated");
  });
 
  // 恢复出厂设置
  server.on("/factory_reset", HTTP_GET, [](AsyncWebServerRequest *request) {
    Wire.beginTransmission(I2C_ADDRESS);
    Wire.write(0x08); // 恢复出厂设置
    Wire.write(0x00);
    Wire.endTransmission();
    request->send(200, "text/plain", "Factory reset command sent.");
  });
 
  // 保存所有参数
  server.on("/save_all", HTTP_GET, [](AsyncWebServerRequest *request) {
    Wire.beginTransmission(I2C_ADDRESS);
    Wire.write(0x07); // 保存所有参数
    Wire.write(0x05); // OP0
    Wire.write(0x00); // OP1
    Wire.write(0x00); // OP2
    Wire.write(0x01); // OP3:保存输出位置
    Wire.write(0x01); // OP4:保存光轴/双相位
    Wire.write(0x01); // OP5:保存画质信息
    Wire.endTransmission();
    request->send(200, "text/plain", "Save all command sent.");
  });
 
  // 更新设备信息路由
server.on("/get_device_info", HTTP_GET, [](AsyncWebServerRequest *request) {
  // 使用新的固定版本获取设备信息
  requestAllDeviceInfo();
 
  // 返回当前缓存的信息
  String json = "{";
  json += "\"temperature\":{\"current\":" + String(deviceInfo.temperature) +
          ",\"lower\":" + String(deviceInfo.muteThreshold) +
          ",\"upper\":" + String(deviceInfo.stopThreshold) + "},";
  json += "\"runtime\":" + String(deviceInfo.runtime) + ",";
  json += "\"version\":{\"firmware\":\"" + deviceInfo.firmwareVersion +
          "\",\"parameter\":\"" + deviceInfo.parameterVersion +
          "\",\"data\":\"" + deviceInfo.dataVersion + "\"},";
  json += "\"lot_number\":\"" + deviceInfo.lotNumber + "\",";
  json += "\"serial_number\":\"" + deviceInfo.serialNumber + "\"";
  json += "}";
 
  Serial.println("[WEB] Sending device info: " + json);
  request->send(200, "application/json", json);
});
// 更新温度信息路由
server.on("/get_temperature", HTTP_GET, [](AsyncWebServerRequest *request) {
  requestTemperature();
 
  String json = "{\"temperature\":" + String(deviceInfo.temperature) +
                ",\"mute_threshold\":" + String(deviceInfo.muteThreshold) +
                ",\"stop_threshold\":" + String(deviceInfo.stopThreshold) + "}";
  request->send(200, "application/json", json);
});
 
  // 获取通知信息
  server.on("/get_notifications", HTTP_GET, [](AsyncWebServerRequest *request) {
    // 这里可以返回系统状态信息
    String json = "{\"notifications\":[]}";
    request->send(200, "application/json", json);
  });
 
  // Clear EEPROM
  server.on("/clear_eeprom", HTTP_GET, [](AsyncWebServerRequest *request) {
    for (int i = 0; i < EEPROM_SIZE; i++) {
      EEPROM.write(i, 0);
    }
    EEPROM.commit();
    request->send(200, "text/plain", "EEPROM cleared. Please restart the device.");
  });
  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "ok");
  });
  // reboot route used by UI
  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "rebooting");
    delay(100);
    ESP.restart();
  });
  // WiFi扫描路由
  server.on("/wifi_scan", HTTP_GET, [](AsyncWebServerRequest *request) {
    // 只有在用户请求时才扫描
    scanRequested = true;
    if (!scanningWiFi) {
      startWiFiScan();
    }
   
    // 返回当前扫描状态或结果
    String json = "{";
    json += "\"scanning\":" + String(scanningWiFi ? "true" : "false") + ",";
    json += "\"networks\":" + wifiScanResults;
    json += "}";
   
    request->send(200, "application/json", json);
  });
 
  // 获取WiFi列表路由
  server.on("/wifi_list", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", wifiScanResults);
  });
 
  // set wifi mode: /set_wifi_mode?mode=sta 或 mode=ap
  server.on("/set_wifi_mode", HTTP_GET, [](AsyncWebServerRequest *request){
      if (!request->hasParam("mode")) {
        request->send(400, "text/plain", "Missing mode");
        return;
      }
      String mode = request->getParam("mode")->value();
      if (mode == "sta") {
        wifiConfigured = true;
        saveSettings();
        request->send(200, "text/plain", "STA enabled, rebooting...");
        delay(100);
        ESP.restart();
      } else {
        wifiConfigured = false;
        saveSettings();
        startAPMode();
        request->send(200, "text/plain", "AP only mode enabled.");
      }
  });
  // wifi_connect: 保存 ssid/pwd, 不立即连接
  server.on("/wifi_connect", HTTP_GET, [](AsyncWebServerRequest *request){
      if (!request->hasParam("ssid")) {
        request->send(400, "text/plain", "Missing ssid");
        return;
      }
      String ssid = request->getParam("ssid")->value();
      String pwd = request->hasParam("pwd") ? request->getParam("pwd")->value() : "";
      // 验证输入
      if (ssid.length() == 0 || ssid.length() > 32) {
        request->send(400, "text/plain", "Invalid SSID");
        return;
      }
     
      if (pwd.length() > 64) {
        request->send(400, "text/plain", "Password too long");
        return;
      }
      // 保存凭证
      savedSSID = ssid;
      savedPWD = pwd;
      saveSettings();
      String resp = "Credentials saved for: " + ssid + ". Switch to STA mode to connect.";
      request->send(200, "text/plain", resp);
  });
 
  // 断开WiFi连接
  server.on("/wifi_disconnect", HTTP_GET, [](AsyncWebServerRequest *request) {
    WiFi.disconnect(true);
    wifiConfigured = false;
    savedSSID = "";
    savedPWD = "";
    saveSettings();
    waitingForWiFi = false;
   
    // 回到AP模式
    startAPMode();
   
    request->send(200, "text/plain", "Disconnected and returned to AP mode");
  });
  // wifi_status: 返回当前 WiFi 模式/连接状态
  server.on("/wifi_status", HTTP_GET, [](AsyncWebServerRequest *request){
      String json = "{";
      json += "\"mode\":\"" + String(wifiConfigured ? "sta" : "ap") + "\",";
      json += "\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
      json += "\"ip\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "\",";
      json += "\"ssid\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "CXN0102_Web_Controller") + "\",";
      json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
      json += "\"lang\":\"" + String(langV == 1 ? "zh" : "en") + "\"";
      json += "}";
      request->send(200, "application/json", json);
  });
  server.on("/set_fan", HTTP_GET, [](AsyncWebServerRequest *request){  // Changed to /set_fan?mode=
    if (!request->hasParam("mode")) {  // Also change param name to "mode" for consistency
      request->send(400, "text/plain", "missing mode");
      return;
    }
    uint8_t mode = request->getParam("mode")->value().toInt();
    setFanModeInternal(mode);
    request->send(200, "text/plain", "OK");
  });
}
// ---------------------- Setup -------------------------------
void setup() {
  Serial.begin(115200);
  delay(5000); // 保留原延时
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  ESP_LOGI("MAIN", "CXN0102 Controller V4.2 ");
  Serial.println("Starting WiFi.");
  EEPROM.begin(EEPROM_SIZE);
  loadSettings();
  // 根据 wifiConfigured 决定启动 AP 还是 尝试 STA
  if (wifiConfigured) {
    Serial.println("[WiFi] wifiConfigured flag set -> attempt STA");
    startSTAMode();
    // 明确关闭AP，避免双模式
    WiFi.softAPdisconnect(true);
  } else {
    startAPMode();
    // 等待AP稳定
    delay(3000);
    Serial.println("[WiFi] AP should be stable now");
  }
  // COM_REQ 引脚与 I2C
  pinMode(COM_REQ_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(COM_REQ_PIN), handleCOM_REQ_ISR, RISING);
  Wire.begin(SDA_PIN, SCL_PIN);

  // --- Fan PWM init ---
  ledcSetup(FAN_PWM_CHANNEL, FAN_PWM_FREQ, FAN_PWM_RES);
  ledcAttachPin(FAN_PWM_PIN, FAN_PWM_CHANNEL);
  setFanModeInternal(fanMode);  // 应用加载的fanMode
  Serial.println("[Fan] PWM initialized on pin 12.");

  // 应用上次保存的设置
  setTxPower(txPowerV);
  sendKeystoneAndFlip(panV, tiltV, flipV);
  // 设置路由（包含我们新增的 WiFi 路由）
  setupRoutes();
  server.begin();
  Serial.println("HTTP Server started.");
 
  // 延迟后请求设备信息（使用修复版本）
  delay(2000);
  requestAllDeviceInfo(); // 改为使用修复版本
  // Auto send Start Input after boot（保留原行为）
  sendI2CCommand(commands[0]);
  Serial.println("Start Input command sent automatically.");
}
// ---------------------- Loop -------------------------------
void loop() {
  // 处理 Notify（保留原有逻辑，但主要信息通过新方法获取）
  processNotify();
  // 定期更新设备信息（每60秒，使用修复版本）
  static unsigned long lastInfoUpdate = 0;
  if (millis() - lastInfoUpdate > 60000) {
    requestTemperature(); // 使用修复版本
    lastInfoUpdate = millis();
  }
  // 按钮处理
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50);
    if (digitalRead(BUTTON_PIN) == LOW) {
      sendI2CCommand(commands[1]); // Stop
      delay(100);
      sendI2CCommand(commands[3]); // Shutdown
      Serial.println("Shutdown command sent via button press.");
      while (digitalRead(BUTTON_PIN) == LOW) { delay(10); }
    }
  }
  // 处理WiFi扫描（只有在用户请求时才处理）
  if (scanRequested) {
    processWiFiScan();
  }
  // WiFi状态监控
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 10000) { // 每10秒检查一次
    if (wifiConfigured && WiFi.status() != WL_CONNECTED && !waitingForWiFi) {
      // STA模式但未连接，尝试重新连接
      Serial.println("[WiFi] STA disconnected, attempting reconnect...");
      WiFi.reconnect();
      connectStartTime = millis();
      waitingForWiFi = true;
    }
    lastWiFiCheck = millis();
  }
  // WiFi 状态检查（用于 1 分钟回退等）
  checkWiFiReconnectFallback();

  // 自动风扇调整（每60秒检查一次）
  static unsigned long lastFanCheck = 0;
  if (millis() - lastFanCheck > 60000) {
    adjustFanSpeed();
    lastFanCheck = millis();
  }

  delay(10);
}