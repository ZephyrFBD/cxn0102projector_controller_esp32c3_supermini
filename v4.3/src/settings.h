// settings.h — EEPROM 持久化设置（几何/画质/WiFi/风扇）
#pragma once
#include <Arduino.h>

// 几何 / 发射功率 / 语言
extern int panV, tiltV, flipV, txPowerV;
extern uint8_t langV;                 // 0=en,1=zh
// 画质
extern uint8_t brightnessV, contrastV, hueV, saturationV, sharpnessV;
extern uint8_t hueU, hueV2, satU, satV;
// WiFi 凭证 / 模式标志
extern String savedSSID, savedPWD;
extern bool wifiConfigured;           // 1=尝试 STA, 0=AP only
// 启动行为
extern bool autoStartOnBoot;         // 开机自动发送 Start 命令
// 高温自动关机（同时监控光机温度与 ESP32-C3 芯片温度）
extern bool highTempShutdownEnabled;
extern uint8_t highTempModuleThreshold, highTempEsp32Threshold;
// 风扇
extern uint8_t fanMode;               // 0=Silent,1=Normal,2=Aggressive,3=Auto,4=Full,5=Custom,6=PID,7=Off/No fan
extern uint8_t fanCustomStartTemp, fanCustomMaxTemp, fanCustomMinPwm, fanCustomMaxPwm;

void eepromInit();    // EEPROM.begin
void loadSettings();
void saveSettings();  // 仅在有字节变化时 commit（降低 flash 磨损）
void clearEEPROM();
