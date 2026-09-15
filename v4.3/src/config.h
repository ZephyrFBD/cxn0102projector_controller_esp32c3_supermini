// config.h — 硬件引脚、风扇 PWM 参数、通用小工具
#pragma once
#include <Arduino.h>

// ---------------------- Pins & Device ----------------------
#define SDA_PIN 8
#define SCL_PIN 7
#define BUTTON_PIN 2
#define I2C_ADDRESS 0x77
#define COM_REQ_PIN 10 // GPIO10 用于 COM_REQ

// ---------------------- Fan PWM -----------------------------
#define FAN_PWM_PIN 6
#define FAN_PWM_CHANNEL 0
#define FAN_PWM_FREQ 25000     // 25kHz 静音风扇常用频率
#define FAN_PWM_RES 8          // 0~255

// ---------------------- Utils -------------------------------
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int mapi(int x, int in_min, int in_max, int out_min, int out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
