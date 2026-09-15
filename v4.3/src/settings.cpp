// settings.cpp — EEPROM 持久化实现
#include "settings.h"
#include "config.h"
#include <EEPROM.h>

// ---------------------- EEPROM Layout -----------------------
#define EEPROM_SIZE 128 // 扩展到128以存储SSID(32)和PWD(64)
#define ADDR_MAGIC 127
#define MAGIC_VALUE 0xA5
#define ADDR_PAN 0        // int8_t [-30,30]
#define ADDR_TILT 1       // int8_t [-20,20]
#define ADDR_FLIP 2       // uint8_t [0..3]
#define ADDR_TXPOWER 3    // int8_t
#define ADDR_LANG 4       // uint8_t 0=en,1=zh
#define ADDR_BRIGHTNESS 5
#define ADDR_CONTRAST 6
#define ADDR_HUE 7        // 旧:单一色调
#define ADDR_SATURATION 8 // 旧:单一饱和度
#define ADDR_SHARPNESS 9
#define ADDR_HUE_U 10
#define ADDR_HUE_V 11
#define ADDR_SAT_U 12
#define ADDR_SAT_V 13
#define ADDR_SSID 14      // 32 bytes
#define ADDR_PWD 46       // 64 bytes
#define ADDR_WIFI_FLAG 110
#define ADDR_HIGH_TEMP_ENABLED 111
#define ADDR_HIGH_TEMP_MODULE  112
#define ADDR_HIGH_TEMP_ESP32   113
#define ADDR_FAN_MODE 120
#define ADDR_FAN_CUSTOM_START 121
#define ADDR_FAN_CUSTOM_MAX   122
#define ADDR_FAN_CUSTOM_MINPWM 123
#define ADDR_FAN_CUSTOM_MAXPWM 124
#define ADDR_AUTO_START 125

// ---------------------- Globals -----------------------------
int panV = 0, tiltV = 0, flipV = 0, txPowerV = 34; // 默认 8.5dBm
uint8_t langV = 1;
uint8_t brightnessV = 128, contrastV = 128, hueV = 128, saturationV = 128, sharpnessV = 128;
uint8_t hueU = 128, hueV2 = 128, satU = 128, satV = 128;
String savedSSID = "", savedPWD = "";
bool wifiConfigured = false;
bool autoStartOnBoot = false; // 默认关闭（开机不自动 Start，需手动打开开关）
bool highTempShutdownEnabled = false; // 安全功能默认关闭，须由用户确认阈值后启用
uint8_t highTempModuleThreshold = 70;
uint8_t highTempEsp32Threshold = 85;
uint8_t fanMode = 3;
uint8_t fanCustomStartTemp = 25, fanCustomMaxTemp = 70, fanCustomMinPwm = 50, fanCustomMaxPwm = 200;

// 仅当值变化时才写入，避免无谓的 flash 扇区提交（降低磨损）
static bool eepromDirty = false;
static inline void ew(int addr, uint8_t val) {
  if (EEPROM.read(addr) != val) {
    EEPROM.write(addr, val);
    eepromDirty = true;
  }
}

void eepromInit() { EEPROM.begin(EEPROM_SIZE); }

void saveSettings() {
  eepromDirty = false;
  ew(ADDR_PAN, (uint8_t)(int8_t)panV);
  ew(ADDR_TILT, (uint8_t)(int8_t)tiltV);
  ew(ADDR_FLIP, (uint8_t)flipV);
  ew(ADDR_TXPOWER, (uint8_t)(int8_t)txPowerV);
  ew(ADDR_LANG, (uint8_t)langV);
  ew(ADDR_BRIGHTNESS, brightnessV);
  ew(ADDR_CONTRAST, contrastV);
  ew(ADDR_HUE, hueV);
  ew(ADDR_SATURATION, saturationV);
  ew(ADDR_SHARPNESS, sharpnessV);
  ew(ADDR_HUE_U, hueU);
  ew(ADDR_HUE_V, hueV2);
  ew(ADDR_SAT_U, satU);
  ew(ADDR_SAT_V, satV);
  ew(ADDR_WIFI_FLAG, wifiConfigured ? 1 : 0);
  ew(ADDR_HIGH_TEMP_ENABLED, highTempShutdownEnabled ? 1 : 0);
  ew(ADDR_HIGH_TEMP_MODULE, highTempModuleThreshold);
  ew(ADDR_HIGH_TEMP_ESP32, highTempEsp32Threshold);
  ew(ADDR_FAN_MODE, fanMode);
  ew(ADDR_FAN_CUSTOM_START,  fanCustomStartTemp);
  ew(ADDR_FAN_CUSTOM_MAX,    fanCustomMaxTemp);
  ew(ADDR_FAN_CUSTOM_MINPWM, fanCustomMinPwm);
  ew(ADDR_FAN_CUSTOM_MAXPWM, fanCustomMaxPwm);
  ew(ADDR_AUTO_START, autoStartOnBoot ? 1 : 0);
  for (int i = 0; i < 32; i++) ew(ADDR_SSID + i, i < (int)savedSSID.length() ? savedSSID[i] : 0);
  for (int i = 0; i < 64; i++) ew(ADDR_PWD + i,  i < (int)savedPWD.length()  ? savedPWD[i]  : 0);
  ew(ADDR_MAGIC, MAGIC_VALUE);
  // 只有真正有字节变化时才 commit（一次 commit = 一次 flash 扇区写）
  if (eepromDirty) {
    EEPROM.commit();
    Serial.println("[EEPROM] Settings saved.");
  } else {
    Serial.println("[EEPROM] No change, skip commit.");
  }
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
    autoStartOnBoot = false;
    highTempShutdownEnabled = false;
    highTempModuleThreshold = 70;
    highTempEsp32Threshold = 85;
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
  highTempShutdownEnabled = EEPROM.read(ADDR_HIGH_TEMP_ENABLED) == 1;
  highTempModuleThreshold = EEPROM.read(ADDR_HIGH_TEMP_MODULE);
  highTempEsp32Threshold = EEPROM.read(ADDR_HIGH_TEMP_ESP32);
  fanMode = EEPROM.read(ADDR_FAN_MODE);
  fanCustomStartTemp = EEPROM.read(ADDR_FAN_CUSTOM_START);
  fanCustomMaxTemp   = EEPROM.read(ADDR_FAN_CUSTOM_MAX);
  fanCustomMinPwm    = EEPROM.read(ADDR_FAN_CUSTOM_MINPWM);
  fanCustomMaxPwm    = EEPROM.read(ADDR_FAN_CUSTOM_MAXPWM);
  autoStartOnBoot    = EEPROM.read(ADDR_AUTO_START) == 1;
  // 阈值合法性校验
  if (fanMode > 7) fanMode = 3; // 0~7 之外（含 EEPROM 0xFF）回落到 Auto
  if (fanCustomStartTemp < 10 || fanCustomStartTemp > 80) fanCustomStartTemp = 25;
  if (fanCustomMaxTemp < 20   || fanCustomMaxTemp > 100)  fanCustomMaxTemp   = 70;
  if (fanCustomMaxTemp <= fanCustomStartTemp) { fanCustomStartTemp = 25; fanCustomMaxTemp = 70; } // 温度区间倒挂/相等 -> 复位
  if (fanCustomMaxPwm  == 0)                              fanCustomMaxPwm    = 200;
  if (fanCustomMinPwm  > fanCustomMaxPwm) { fanCustomMinPwm = 50; fanCustomMaxPwm = 200; }       // PWM 区间倒挂 -> 复位
  if (highTempModuleThreshold < 45 || highTempModuleThreshold > 100) highTempModuleThreshold = 70;
  if (highTempEsp32Threshold < 55 || highTempEsp32Threshold > 110) highTempEsp32Threshold = 85;
  // 加载 SSID / PWD
  savedSSID = "";
  for (int i = 0; i < 32; i++) { char c = EEPROM.read(ADDR_SSID + i); if (c == 0) break; savedSSID += c; }
  savedPWD = "";
  for (int i = 0; i < 64; i++) { char c = EEPROM.read(ADDR_PWD + i); if (c == 0) break; savedPWD += c; }
  // Basic sanitization
  panV = clampi(panV, -30, 30);
  tiltV = clampi(tiltV, -20, 20);
  if (flipV < 0 || flipV > 3) flipV = 0;
  brightnessV = clampi(brightnessV, 0, 255);
  contrastV = clampi(contrastV, 0, 255);
  hueV = clampi(hueV, 0, 255);
  saturationV = clampi(saturationV, 0, 255);
  sharpnessV = clampi(sharpnessV, 0, 255);
  hueU = clampi(hueU, 0, 255);
  hueV2 = clampi(hueV2, 0, 255);
  satU = clampi(satU, 0, 255);
  satV = clampi(satV, 0, 255);
  Serial.printf("[EEPROM] Loaded: pan=%d tilt=%d flip=%d txPower=%d lang=%u brightness=%u contrast=%u hue=%u hueU=%u hueV=%u saturation=%u satU=%u satV=%u sharpness=%u wifiConfigured=%d ssid=%s\n",
                panV, tiltV, flipV, txPowerV, langV, brightnessV, contrastV, hueV, hueU, hueV2, saturationV, satU, satV, sharpnessV, wifiConfigured, savedSSID.c_str());
}

void clearEEPROM() {
  for (int i = 0; i < EEPROM_SIZE; i++) EEPROM.write(i, 0);
  EEPROM.commit();
}
