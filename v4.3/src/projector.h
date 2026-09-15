// projector.h — 与 CXN0102 模块的 I2C 通信、设备信息、Notify、测试图案、画质
#pragma once
#include <Arduino.h>

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
  unsigned long lastUpdate = 0;
};
extern DeviceInfo deviceInfo;
extern volatile bool deviceInfoUpdatePending; // 由 HTTP 回调置位、loop 异步处理
extern bool inputStarted;                     // 光机输入是否已 Start（运行中）

// 命令表（index 见 .cpp 注释）
extern const char* const commands[];
extern const int COMMANDS_COUNT;

void projectorInitI2C();                       // Wire.begin + COM_REQ 中断
void sendI2CCommand(const char* cmd);          // hex 字符串
void sendKeystoneAndFlip(int pan, int tilt, int flip);
void sendTestPattern(uint8_t pattern, uint8_t generalSetting = 0x00,
                     uint8_t bgR = 0x00, uint8_t bgG = 0x00, uint8_t bgB = 0x00,
                     uint8_t fgR = 0xFF, uint8_t fgG = 0xFF, uint8_t fgB = 0xFF);

// 画质（输入 0~255，内部映射到模块范围并下发 I2C）
void projectorSetBrightness(uint8_t v);
void projectorSetContrast(uint8_t v);
void projectorSetHue(uint8_t u, uint8_t v);
void projectorSetSaturation(uint8_t u, uint8_t v);
void projectorSetSharpness(uint8_t v);
void projectorFactoryReset();
void projectorSaveAll();

// 设备信息 / Notify
void processNotify();
bool requestTemperature();
void requestAllDeviceInfo();

// 温度历史（最近 ~1 小时）
extern const int TEMP_SAMPLE_SEC;     // 采样间隔（秒）
void   recordTempSample();            // 记录一次当前温度
String tempHistoryJson();             // 旧->新 的温度数组 JSON

// ESP32-C3 内置结温传感器与高温关机运行状态。
// 芯片传感器用于趋势和保护，不等同于环境温度或外壳温度。
extern float esp32Temperature;
extern bool esp32TemperatureValid;
extern unsigned long esp32TemperatureAt;
void sampleEsp32Temperature();
String esp32TempHistoryJson();
bool evaluateHighTempShutdown();       // 连续两次超限后仅返回一次 true
bool highTempNeedsFanFull();
bool highTempShutdownTripped();
String highTempShutdownReason();
uint8_t highTempModuleCount();
uint8_t highTempEsp32Count();
void resetHighTempRuntimeState();
