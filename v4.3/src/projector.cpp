// projector.cpp — I2C 通信、设备信息解析、Notify 处理
#include "projector.h"
#include "config.h"
#include "settings.h"
#include "thermal_guard.h"
#include <Wire.h>
#include <math.h>

// ---------------------- Globals -----------------------------
DeviceInfo deviceInfo;
volatile bool deviceInfoUpdatePending = false;
bool inputStarted = false; // 运行时状态：Start 后 true，Stop/Shutdown 后 false

const char* const commands[] = {
  "0100",   // 1: Start Input
  "0200",   // 2: Stop Input
  "0b0101", // 3: Reboot
  "0b0100", // 4: Shutdown
  "3200",   // 5: Enter Optical Axis Adjustment
  "3300",   // 6: Optical Axis +
  "3400",   // 7: Optical Axis -
  "350100", // 8: Exit Optical Axis (No Save)
  "350101", // 9: Exit Optical Axis (Save)
  "3600",   // 10: Enter Bi-Phase Adjustment
  "3700",   // 11: Bi-Phase +
  "3800",   // 12: Bi-Phase -
  "390100", // 13: Exit Bi-Phase (No Save)
  "390101", // 14: Exit Bi-Phase (Save)
  "4A",     // 15: Flip Mode
  "5001",   // 16: Test Image ON
  "5000",   // 17: Test Image OFF
  "6000",   // 18: Mute
  "6001",   // 19: Unmute
  "7000",   // 20: Keystone Vertical -
  "7001",   // 21: Keystone Vertical +
  "7002",   // 22: Keystone Horizontal -
  "7003",   // 23: Keystone Horizontal +
  "8000",   // 24: Color Temperature -
  "8001",   // 25: Color Temperature +
  "4300",   // 26: Set Brightness
  "4500",   // 27: Set Contrast
  "4700",   // 28: Set Hue
  "4900",   // 29: Set Saturation
  "4F00",   // 30: Set Sharpness
};
const int COMMANDS_COUNT = sizeof(commands) / sizeof(commands[0]);

// ---------------------- Notify state ------------------------
static volatile bool notifyPending = false;
static uint8_t notifyBuffer[32];
static uint8_t notifyLength = 0;

static void IRAM_ATTR handleCOM_REQ_ISR() { notifyPending = true; }

// ---------------------- I2C init ----------------------------
void projectorInitI2C() {
  pinMode(COM_REQ_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(COM_REQ_PIN), handleCOM_REQ_ISR, RISING);
  Wire.begin(SDA_PIN, SCL_PIN);
}

// ---------------------- I2C primitives ----------------------
void sendKeystoneAndFlip(int pan, int tilt, int flip) {
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(0x26); // Set Video Output Position Information
  Wire.write(0x09); // Size
  Wire.write(pan & 0xFF);
  Wire.write(tilt & 0xFF);
  Wire.write(flip & 0xFF);
  for (int i = 0; i < 6; i++) Wire.write((i == 0) ? 0x64 : 0x00); // Fixed values
  uint8_t error = Wire.endTransmission();
  if (error) Serial.printf("I2C error while sending keystone command: %d\n", error);
  else       Serial.println("Keystone and Flip command sent successfully.");
}

void sendI2CCommand(const char* cmd) {
  Wire.beginTransmission(I2C_ADDRESS);
  for (int i = 0; i < (int)strlen(cmd); i += 2) {
    char byteStr[3] = {cmd[i], cmd[i + 1], '\0'};
    uint8_t byteVal = (uint8_t)strtol(byteStr, NULL, 16);
    Wire.write(byteVal);
  }
  uint8_t error = Wire.endTransmission();
  if (error) Serial.printf("I2C error: %d\n", error);
  else       Serial.println("Command sent successfully.");
}

void sendTestPattern(uint8_t pattern, uint8_t generalSetting,
                     uint8_t bgR, uint8_t bgG, uint8_t bgB,
                     uint8_t fgR, uint8_t fgG, uint8_t fgB) {
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(0xA3); // Output Test Picture
  Wire.write(0x11); // OP0: Size = 17 bytes
  Wire.write(pattern);        // OP1
  Wire.write(generalSetting); // OP2
  Wire.write(bgR); Wire.write(bgG); Wire.write(bgB); // OP3-5 背景
  Wire.write(fgR); Wire.write(fgG); Wire.write(fgB); // OP6-8 前景
  for (int i = 0; i < 9; i++) Wire.write(0x00);      // OP9-17 保留
  uint8_t error = Wire.endTransmission();
  if (error) Serial.printf("I2C error while sending test pattern: %d\n", error);
  else       Serial.printf("Test pattern command sent: pattern=0x%02X\n", pattern);
}

// ---------------------- Picture Quality ---------------------
void projectorSetBrightness(uint8_t v) {
  int val = mapi(v, 0, 255, -31, 31);
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(0x43); Wire.write(0x01); Wire.write((int8_t)val);
  Wire.endTransmission();
}
void projectorSetContrast(uint8_t v) {
  int val = mapi(v, 0, 255, -15, 15);
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(0x45); Wire.write(0x01); Wire.write((int8_t)val);
  Wire.endTransmission();
}
void projectorSetHue(uint8_t u8, uint8_t v8) {
  int u = mapi(u8, 0, 255, -15, 15), v = mapi(v8, 0, 255, -15, 15);
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(0x47); Wire.write(0x02); Wire.write((int8_t)u); Wire.write((int8_t)v);
  uint8_t error = Wire.endTransmission();
  Serial.printf("[PQ] Set Hue U/V: %d/%d, I2C error=%d\n", u, v, error);
}
void projectorSetSaturation(uint8_t u8, uint8_t v8) {
  int u = mapi(u8, 0, 255, -15, 15), v = mapi(v8, 0, 255, -15, 15);
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(0x49); Wire.write(0x02); Wire.write((int8_t)u); Wire.write((int8_t)v);
  Wire.endTransmission();
}
void projectorSetSharpness(uint8_t v) {
  int val = mapi(v, 0, 255, 0, 8);
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(0x4F); Wire.write(0x01); Wire.write((uint8_t)val);
  Wire.endTransmission();
}
void projectorFactoryReset() {
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(0x08); Wire.write(0x00);
  Wire.endTransmission();
}
void projectorSaveAll() {
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(0x07); // 保存所有参数
  Wire.write(0x05); // OP0
  Wire.write(0x00); // OP1
  Wire.write(0x00); // OP2
  Wire.write(0x01); // OP3:保存输出位置
  Wire.write(0x01); // OP4:保存光轴/双相位
  Wire.write(0x01); // OP5:保存画质信息
  Wire.endTransmission();
}

// ---------------------- Parse helpers -----------------------
static String parseVersion(const uint8_t* data, uint8_t startIndex) {
  String version = "";
  for (int i = 0; i < 4; i++) {
    if (data[startIndex + i] >= 32 && data[startIndex + i] <= 126) {
      version += (char)data[startIndex + i];
    } else {
      char hex[3];
      sprintf(hex, "%02X", data[startIndex + i]);
      version += hex;
    }
  }
  return version;
}
// LOT/Serial: 每 4 字节为一组，按文档大端格式 VV WW XX YY 还原
static String parseGroupedHex(const uint8_t* data, uint8_t startIndex, int groups) {
  String out = "";
  for (int g = 0; g < groups; g++) {
    int base = startIndex + (g * 4);
    char buffer[9];
    sprintf(buffer, "%02X%02X%02X%02X", data[base + 3], data[base + 2], data[base + 1], data[base]);
    out += String(buffer);
    if (g < groups - 1) out += "-";
  }
  return out;
}

// ---------------------- Notify ------------------------------
void processNotify() {
  if (!notifyPending) return;
  // 先清标志再读：若处理期间 ISR 再次触发，不会丢失新的 Notify
  notifyPending = false;
  Wire.requestFrom((uint8_t)I2C_ADDRESS, (uint8_t)32);
  notifyLength = 0;
  while (Wire.available() && notifyLength < 32) notifyBuffer[notifyLength++] = Wire.read();

  if (notifyLength >= 3) {
    uint8_t cmd = notifyBuffer[0];
    uint8_t size = notifyBuffer[1];
    uint8_t result = notifyBuffer[2];

    Serial.printf("[NOTIFY] CMD: 0x%02X, Size: %d, Result: 0x%02X, Data: ", cmd, size, result);
    for (int i = 0; i < notifyLength; i++) Serial.printf("%02X ", notifyBuffer[i]);
    Serial.println();

    switch (cmd) {
      case 0x00: // Boot Completed
        Serial.println("[NOTIFY] Boot Completed");
        if (result != 0x00) Serial.printf("[NOTIFY] Boot error: 0x%02X\n", result);
        break;
      case 0x10: // Emergency
        Serial.printf("[NOTIFY] Emergency: 0x%02X\n", result);
        break;
      case 0x11: // Temperature Emergency / Recovery
        if (result == 0x80 || result == 0x81) Serial.printf("[NOTIFY] Temperature Emergency: 0x%02X\n", result);
        else                                  Serial.printf("[NOTIFY] Temperature Recovery: 0x%02X\n", result);
        break;
      case 0x12: // Command Emergency
        Serial.printf("[NOTIFY] Command Error: 0x%02X\n", result);
        break;
      case 0xA0: // Get Temperature
        if (notifyLength >= 6) {
          if (result == 0x00) {
            deviceInfo.temperature = notifyBuffer[3] == 0xFF ? -1 : notifyBuffer[3];
            deviceInfo.muteThreshold = notifyBuffer[4];
            deviceInfo.stopThreshold = notifyBuffer[5];
            deviceInfo.infoValid = deviceInfo.temperature >= 0;
            if (deviceInfo.infoValid) deviceInfo.lastUpdate = millis();
            Serial.printf("[NOTIFY] Temperature: %d°C, Mute Threshold: %d°C, Stop Threshold: %d°C\n",
                          deviceInfo.temperature, deviceInfo.muteThreshold, deviceInfo.stopThreshold);
          } else {
            Serial.printf("[NOTIFY] Get Temperature abnormal: 0x%02X\n", result);
            deviceInfo.temperature = -1;
          }
        } else Serial.println("[NOTIFY] Incomplete Get Temperature response");
        break;
      case 0xA1: // Get Time
        if (notifyLength >= 7) {
          if (result == 0x00) {
            deviceInfo.runtime = ((unsigned long)notifyBuffer[6] << 24) |
                                 ((unsigned long)notifyBuffer[5] << 16) |
                                 ((unsigned long)notifyBuffer[4] << 8) |
                                 notifyBuffer[3];
            Serial.printf("[NOTIFY] Runtime: %lu seconds (%lu hours %lu minutes)\n",
                          deviceInfo.runtime, deviceInfo.runtime / 3600, (deviceInfo.runtime % 3600) / 60);
          } else Serial.printf("[NOTIFY] Get Time abnormal: 0x%02X\n", result);
        } else Serial.println("[NOTIFY] Incomplete Get Time response");
        break;
      case 0xA2: // Get Version
        if (notifyLength >= 14) {
          if (result == 0x00) {
            deviceInfo.firmwareVersion = parseVersion(notifyBuffer, 3);
            deviceInfo.parameterVersion = parseVersion(notifyBuffer, 7);
            deviceInfo.dataVersion = parseVersion(notifyBuffer, 11);
            Serial.printf("[NOTIFY] Firmware Version: %s, Parameter Version: %s, Data Version: %s\n",
                          deviceInfo.firmwareVersion.c_str(), deviceInfo.parameterVersion.c_str(), deviceInfo.dataVersion.c_str());
          } else Serial.printf("[NOTIFY] Get Version abnormal: 0x%02X\n", result);
        } else Serial.println("[NOTIFY] Incomplete Get Version response");
        break;
      case 0xB2: // Get LOT Number
        if (notifyLength >= 15) {
          if (result == 0x00) {
            deviceInfo.lotNumber = parseGroupedHex(notifyBuffer, 3, 3);
            Serial.printf("[NOTIFY] LOT Number: %s\n", deviceInfo.lotNumber.c_str());
          } else Serial.printf("[NOTIFY] Get LOT Number abnormal: 0x%02X\n", result);
        } else Serial.println("[NOTIFY] Incomplete Get LOT Number response");
        break;
      case 0xB4: // Get Serial Number
        if (notifyLength >= 11) {
          if (result == 0x00) {
            deviceInfo.serialNumber = parseGroupedHex(notifyBuffer, 3, 2);
            Serial.printf("[NOTIFY] Serial Number: %s\n", deviceInfo.serialNumber.c_str());
          } else Serial.printf("[NOTIFY] Get Serial Number abnormal: 0x%02X\n", result);
        } else Serial.println("[NOTIFY] Incomplete Get Serial Number response");
        break;
      default:
        Serial.printf("[NOTIFY] Unknown command: 0x%02X\n", cmd);
        break;
    }
  } else {
    Serial.println("[NOTIFY] Invalid notify data length");
  }
}

// ---------------------- Info request ------------------------
static bool sendInfoRequestAndRead(uint8_t cmd, uint8_t* response, uint8_t expectedLength) {
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(cmd);
  Wire.write(0x00); // OP0=0
  uint8_t error = Wire.endTransmission();
  if (error) { Serial.printf("[INFO] I2C error sending request 0x%02X: %d\n", cmd, error); return false; }

  delay(50); // 等待响应可用

  Wire.requestFrom((uint8_t)I2C_ADDRESS, expectedLength);
  uint8_t readLength = 0;
  while (Wire.available() && readLength < expectedLength) response[readLength++] = Wire.read();

  if (readLength != expectedLength) {
    Serial.printf("[INFO] Incomplete response for 0x%02X: expected %d, got %d\n", cmd, expectedLength, readLength);
    return false;
  }
  if (response[0] != cmd || response[2] != 0x00) {
    Serial.printf("[INFO] Invalid response for 0x%02X: CMD=0x%02X Result=0x%02X\n", cmd, response[0], response[2]);
    return false;
  }
  return true;
}

bool requestTemperature() {
  uint8_t response[6];
  if (!sendInfoRequestAndRead(0xA0, response, 6)) {
    deviceInfo.temperature = -1;
    deviceInfo.infoValid = false;
    return false;
  }
  if (response[3] == 0xFF) {
    deviceInfo.temperature = -1;
    deviceInfo.infoValid = false;
    Serial.println("[INFO] Module temperature unavailable (0xFF)");
    return false;
  }
  deviceInfo.temperature = response[3];
  deviceInfo.muteThreshold = response[4];
  deviceInfo.stopThreshold = response[5];
  deviceInfo.lastUpdate = millis();
  deviceInfo.infoValid = true;
  Serial.printf("[INFO] Temperature: %d°C, Mute: %d°C, Stop: %d°C\n",
                deviceInfo.temperature, deviceInfo.muteThreshold, deviceInfo.stopThreshold);
  return true;
}
static bool requestRuntime() {
  uint8_t response[7];
  if (!sendInfoRequestAndRead(0xA1, response, 7)) return false;
  deviceInfo.runtime = ((unsigned long)response[6] << 24) | ((unsigned long)response[5] << 16) |
                       ((unsigned long)response[4] << 8) | response[3];
  deviceInfo.lastUpdate = millis();
  Serial.printf("[INFO] Runtime: %lu seconds (%lu hours %lu minutes)\n",
                deviceInfo.runtime, deviceInfo.runtime / 3600, (deviceInfo.runtime % 3600) / 60);
  return true;
}
static bool requestVersion() {
  uint8_t response[14];
  if (!sendInfoRequestAndRead(0xA2, response, 14)) return false;
  deviceInfo.firmwareVersion = parseVersion(response, 3);
  deviceInfo.parameterVersion = parseVersion(response, 7);
  deviceInfo.dataVersion = parseVersion(response, 11);
  deviceInfo.lastUpdate = millis();
  Serial.printf("[INFO] Firmware: %s, Parameter: %s, Data: %s\n",
                deviceInfo.firmwareVersion.c_str(), deviceInfo.parameterVersion.c_str(), deviceInfo.dataVersion.c_str());
  return true;
}
static bool requestLOTNumber() {
  uint8_t response[15];
  if (!sendInfoRequestAndRead(0xB2, response, 15)) return false;
  deviceInfo.lotNumber = parseGroupedHex(response, 3, 3);
  deviceInfo.lastUpdate = millis();
  Serial.printf("[INFO] LOT Number: %s\n", deviceInfo.lotNumber.c_str());
  return true;
}
static bool requestSerialNumber() {
  uint8_t response[11];
  if (!sendInfoRequestAndRead(0xB4, response, 11)) return false;
  deviceInfo.serialNumber = parseGroupedHex(response, 3, 2);
  deviceInfo.lastUpdate = millis();
  Serial.printf("[INFO] Serial Number: %s\n", deviceInfo.serialNumber.c_str());
  return true;
}
void requestAllDeviceInfo() {
  Serial.println("[INFO] ===== Requesting all device information =====");
  // 无论成败都留出间隔，失败时立刻连发反而更容易继续失败
  requestTemperature();  delay(200);
  requestRuntime();      delay(200);
  requestVersion();      delay(200);
  requestLOTNumber();    delay(200);
  requestSerialNumber();
  Serial.println("[INFO] ===== All device info requests completed =====");
}

// ---------------------- Temperature history -----------------
const int TEMP_SAMPLE_SEC = 10;         // 每 10 秒采样一次
#define TEMP_HIST_N 360                   // 360 点 × 10 秒 = 1 小时
static int8_t tempHist[TEMP_HIST_N];
static int tempHistCount = 0;
static int tempHistHead = 0;             // 下一个写入位置
static int16_t esp32TempHist[TEMP_HIST_N]; // 0.1 °C, INT16_MIN = invalid
float esp32Temperature = NAN;
bool esp32TemperatureValid = false;
unsigned long esp32TemperatureAt = 0;
static cxnthermal::Guard thermalGuard;

void sampleEsp32Temperature() {
  float value = temperatureRead();
  esp32TemperatureValid = isfinite(value) && value >= -40.0f && value <= 150.0f;
  esp32Temperature = esp32TemperatureValid ? value : NAN;
  if (esp32TemperatureValid) esp32TemperatureAt = millis();
  Serial.printf("[THERMAL] ESP32-C3 die: %s\n",
                esp32TemperatureValid ? (String(esp32Temperature, 1) + " C").c_str() : "invalid");
}

void recordTempSample() {
  int t = deviceInfo.temperature;
  tempHist[tempHistHead] = (int8_t)(t < -1 ? -1 : (t > 127 ? 127 : t));
  esp32TempHist[tempHistHead] = esp32TemperatureValid
      ? (int16_t)constrain((int)lroundf(esp32Temperature * 10.0f), -400, 1500)
      : INT16_MIN;
  tempHistHead = (tempHistHead + 1) % TEMP_HIST_N;
  if (tempHistCount < TEMP_HIST_N) tempHistCount++;
}

String tempHistoryJson() {
  String s = "[";
  for (int i = 0; i < tempHistCount; i++) {
    int idx = (tempHistHead - tempHistCount + i + TEMP_HIST_N * 2) % TEMP_HIST_N;
    if (i > 0) s += ",";
    s += String((int)tempHist[idx]);
  }
  s += "]";
  return s;
}

String esp32TempHistoryJson() {
  String s = "[";
  for (int i = 0; i < tempHistCount; i++) {
    int idx = (tempHistHead - tempHistCount + i + TEMP_HIST_N * 2) % TEMP_HIST_N;
    if (i > 0) s += ",";
    if (esp32TempHist[idx] == INT16_MIN) s += "null";
    else s += String(esp32TempHist[idx] / 10.0f, 1);
  }
  s += "]";
  return s;
}

void resetHighTempRuntimeState() {
  thermalGuard.reset();
}

bool evaluateHighTempShutdown() {
  if (!highTempShutdownEnabled) {
    return thermalGuard.update(false, false, 0, highTempModuleThreshold,
                               false, 0, highTempEsp32Threshold);
  }
  const unsigned long now = millis();
  const bool moduleFresh = deviceInfo.temperature >= 0 && deviceInfo.infoValid &&
                           now - deviceInfo.lastUpdate <= (unsigned long)TEMP_SAMPLE_SEC * 3000UL;
  const bool espFresh = esp32TemperatureValid && now - esp32TemperatureAt <= (unsigned long)TEMP_SAMPLE_SEC * 3000UL;
  const bool trigger = thermalGuard.update(true, moduleFresh, deviceInfo.temperature,
                                           highTempModuleThreshold, espFresh, esp32Temperature,
                                           highTempEsp32Threshold);
  if (trigger) {
    Serial.printf("[THERMAL] AUTO SHUTDOWN: %s temperature exceeded threshold twice\n",
                  thermalGuard.source == cxnthermal::Source::Module ? "module" : "esp32");
  }
  return trigger;
}

bool highTempNeedsFanFull() {
  return thermalGuard.fanFull(highTempShutdownEnabled);
}
bool highTempShutdownTripped() { return thermalGuard.latched; }
String highTempShutdownReason() { return thermalGuard.source == cxnthermal::Source::Module ? "module" : thermalGuard.source == cxnthermal::Source::Esp32 ? "esp32" : ""; }
uint8_t highTempModuleCount() { return thermalGuard.moduleCount; }
uint8_t highTempEsp32Count() { return thermalGuard.esp32Count; }
