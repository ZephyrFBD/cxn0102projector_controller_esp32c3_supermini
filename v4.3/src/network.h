// network.h — WiFi AP/STA、强制门户、mDNS、TX 功率
#pragma once
#include <Arduino.h>

void setTxPower(int powerLevel);
const char* getAPSSID(); // 当前固件配置的设备热点名称
void startAPMode();
void startSTAMode();   // 依次尝试已保存的网络
void networkLoop();    // loop() 中调用：DNS 门户 + STA 连接状态机
