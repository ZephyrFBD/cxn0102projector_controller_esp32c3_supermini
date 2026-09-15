// fan.cpp — 风扇 PWM 控制实现
#include "fan.h"
#include "config.h"
#include "settings.h"   // fanMode, fanCustom*, saveSettings
#include "projector.h"  // deviceInfo.temperature
#include <math.h>
#include <Preferences.h>

uint8_t fanPwmValue = 0;

bool fanIsDisabled() { return fanMode == 7; }

// ---------------------- PID (mode 6) ------------------------
uint8_t pidTarget = 55;
float   pidKp = 8.0, pidKi = 0.6, pidKd = 1.5;
static float pidIntegral = 0, pidLastErr = 0;
static bool  pidInited = false;

void fanPidLoad() {
  Preferences p;
  p.begin("fanpid", true);
  pidTarget = p.getUChar("t", 55);
  pidKp = p.getFloat("kp", 8.0);
  pidKi = p.getFloat("ki", 0.6);
  pidKd = p.getFloat("kd", 1.5);
  p.end();
  if (pidTarget < 30 || pidTarget > 90) pidTarget = 55;
  Serial.printf("[Fan] PID loaded: target=%u Kp=%.2f Ki=%.2f Kd=%.2f\n", pidTarget, pidKp, pidKi, pidKd);
}
void fanPidSave() {
  Preferences p;
  p.begin("fanpid", false);
  p.putUChar("t", pidTarget);
  p.putFloat("kp", pidKp);
  p.putFloat("ki", pidKi);
  p.putFloat("kd", pidKd);
  p.end();
}
void fanPidReset() { pidIntegral = 0; pidLastErr = 0; pidInited = false; }

// 离散 PID（采样间隔固定，dt 折入增益）；温度高于目标 -> 加大风扇
static void pidCompute() {
  float err = (float)deviceInfo.temperature - (float)pidTarget; // >0 太热
  if (!pidInited) { pidLastErr = err; pidInited = true; }
  float deriv = err - pidLastErr;
  pidIntegral += err;
  float out = pidKp * err + pidKi * pidIntegral + pidKd * deriv;
  // 抗积分饱和：输出顶到边界时撤回本次积分
  if (out > 255) { out = 255; pidIntegral -= err; }
  else if (out < 0) { out = 0; pidIntegral -= err; }
  pidLastErr = err;
  fanPwmValue = (uint8_t)constrain((int)lround(out), 0, 255);
  ledcWrite(FAN_PWM_CHANNEL, fanPwmValue);
  Serial.printf("[Fan] PID Temp=%d target=%u err=%.1f I=%.1f PWM=%u\n",
                deviceInfo.temperature, pidTarget, err, pidIntegral, fanPwmValue);
}

// ---------------------- Auto-tune (IMC-PID, 加热→冷却两阶段) ----------------------
// 阶段1: 风扇降到最低，等待投影机自身发热超过目标温度（> target+2）
// 阶段2: 风扇升到高速，温度下降 -> 辨识 FOPDT -> IMC 计算 PID。
enum { AT_IDLE = 0, AT_HEAT = 1, AT_COOL = 2, AT_DONE = 3 };
static uint8_t atState = AT_IDLE;
static const uint8_t AT_PWM_HEAT = 30;     // 加热阶段用低转速（最小冷却）
static const uint8_t AT_PWM_STEP = 220;    // 冷却阶跃用高速
static uint8_t atPwmBase = 0;             // 冷却阶跃前 PWM
static float   atTempStart = 0;           // 冷却阶跃开始前温度
static unsigned long atStartMs = 0;        // 冷却阶段开始时刻
#define AT_MAX_WAIT 1800                   // 加热等待上限 30 分钟
#define AT_MAX_SAMPLES 64                 // 冷却阶段最多 64 个采样
static int8_t  atSamples[AT_MAX_SAMPLES];
static uint8_t atSampleN = 0;

// FOPDT + IMC-PID：K=对象增益幅值(°C/PWM), tau=时间常数(s), theta=滞后(s)
static void computeIMCPid(float K, float tau, float theta) {
  float L = theta, T = tau;
  float lambda = fmax(0.5f * T, 5.0f);    // 调优因子，平衡响应速度与鲁棒
  float Kc = T / (K * (L + lambda));      // PWM/°C
  float Ti = fmin(L + T, 300.0f);
  float Td = fmax(L * T / (L + T), 0.5f);
  pidKp = fmax(1.0f, fmin(50.0f, Kc));
  pidKi = fmax(0.01f, fmin(5.0f, Kc / Ti));
  pidKd = fmax(0.0f, fmin(20.0f, Kc * Td));
  Serial.printf("[Fan][AT] K=%.3f tau=%.0fs theta=%.0fs -> Kp=%.2f Ki=%.3f Kd=%.2f\n",
                K, tau, theta, pidKp, pidKi, pidKd);
}

static void finishTune(int Tend, unsigned long elapsedSec, bool aborted) {
  float dT = atTempStart - (float)Tend;            // 冷却后的温降（正）
  int   dP = (int)AT_PWM_STEP - (int)atPwmBase;    // 增加的冷却量（正）
  if (aborted || dT < 3.0f || dP < 20) {
    pidKp = 8.0f; pidKi = 0.6f; pidKd = 1.5f;      // 辨识不足 -> 保守默认
    Serial.printf("[Fan][AT] weak/aborted (dT=%.1f dP=%d) -> defaults\n", dT, dP);
  } else {
    float K = dT / (float)dP;                      // 对象增益幅值 °C/PWM
    // 63% 温降时间 -> τ
    float t63 = atTempStart - dT * 0.632f;
    float tau = (float)elapsedSec * 0.6f;          // 缺省估计
    for (int i = 1; i < atSampleN; i++) {
      if (atSamples[i] <= t63) { tau = (float)elapsedSec * i / atSampleN; break; }
    }
    if (tau < 10) tau = 10;
    float theta = fmax(5.0f, tau * 0.2f);
    computeIMCPid(K, tau, theta);
  }
  fanPidSave();
  fanPidReset();
  atState = AT_DONE;
  Serial.printf("[Fan][AT] done. Kp=%.2f Ki=%.3f Kd=%.2f\n", pidKp, pidKi, pidKd);
}

void fanPidAutoTune() {
  if (fanIsDisabled()) {
    setFanPwmValue(0);
    Serial.println("[Fan][AT] blocked: fan output is disabled");
    return;
  }
  if (highTempNeedsFanFull()) {
    setFanPwmValue(255);
    Serial.println("[Fan][AT] blocked by user high-temperature protection");
    return;
  }
  if (atState == AT_HEAT || atState == AT_COOL) return; // 已在进行
  if (deviceInfo.temperature < 0) return;  // 无有效温度
  atStartMs = millis();
  atSampleN  = 0;
  fanPwmValue = AT_PWM_HEAT;              // 先降到最低，让投影机自己发热
  ledcWrite(FAN_PWM_CHANNEL, fanPwmValue);
  atState = AT_HEAT;
  Serial.printf("[Fan][AT] start HEAT phase: fan=%u, T=%.0f°C, waiting for T >= target+2\n",
               AT_PWM_HEAT, (float)deviceInfo.temperature);
}

void fanPidTuneLoop() {
  if (fanIsDisabled()) {
    atState = AT_IDLE;
    setFanPwmValue(0);
    return;
  }
  if (atState == AT_IDLE || atState == AT_DONE) return;
  if (deviceInfo.temperature < 0) return;

  int T = deviceInfo.temperature;

  // 热保护（最高优先级）：整定期间 loop 跳过了 adjustFanSpeed，热保护在此独立兜底。
  // 一旦逼近模块停机阈值，立即全速并中止整定，避免加热阶段把投影机烤到自我保护停机。
  if (deviceInfo.stopThreshold > 0 && T >= deviceInfo.stopThreshold - 3) {
    fanPwmValue = 255;
    ledcWrite(FAN_PWM_CHANNEL, fanPwmValue);
    Serial.printf("[Fan][AT] THERMAL SAFETY T=%d (stop=%d) -> abort tune, PWM=255\n",
                  T, deviceInfo.stopThreshold);
    finishTune(T, fanPidTuneElapsed(), true); // 中止，回落保守默认增益
    return;
  }

  if (atState == AT_HEAT) {
    unsigned long el = (millis() - atStartMs) / 1000;
    // 加热目标 = pidTarget+2，但绝不超过安全上限（停机阈值下方留 5℃ 余量）
    int heatGoal = (int)pidTarget + 2;
    if (deviceInfo.stopThreshold > 0 && heatGoal > deviceInfo.stopThreshold - 5)
      heatGoal = deviceInfo.stopThreshold - 5;
    // 温度已达到加热目标，进入冷却阶跃
    if (T >= heatGoal) {
      atPwmBase   = fanPwmValue;
      atTempStart = T;
      atSampleN   = 0;
      atStartMs   = millis();
      fanPwmValue = AT_PWM_STEP;
      ledcWrite(FAN_PWM_CHANNEL, fanPwmValue);
      atState = AT_COOL;
      Serial.printf("[Fan][AT] switch to COOL: fan %u->%u, T=%.0f\n", atPwmBase, AT_PWM_STEP, (float)T);
    } else if (el >= AT_MAX_WAIT) {
      // 30 分钟都没热到目标，放弃
      Serial.printf("[Fan][AT] heat timeout (%lus). Target=%u, T=%.0f\n", el, pidTarget, (float)T);
      finishTune(T, el, true);
    }
    return;
  }

  // AT_COOL: 记录冷却阶段温降
  if (atState == AT_COOL) {
    unsigned long el = (millis() - atStartMs) / 1000;
    if (atSampleN < AT_MAX_SAMPLES) atSamples[atSampleN++] = (int8_t)T;
    if (T >= 85) { finishTune(T, el, true); return; } // 极端高温保护
    // 平稳判定：最近 3 个采样变化≤1℃ 且已降温≥4℃
    bool plateau = (atSampleN >= 6 && abs(T - atSamples[atSampleN - 3]) <= 1 && (atTempStart - T) >= 4);
    if (plateau || atSampleN >= AT_MAX_SAMPLES || el >= 480) finishTune(T, el, false);
  }
}

bool fanPidIsTuning()    { return atState == AT_HEAT || atState == AT_COOL; }
uint8_t fanPidTuneState(){ return atState; }   // 0=idle, 1=heat, 2=cool, 3=done
// 当前阶段已耗时（秒）：atStartMs 在进入 COOL 时重置，故返回的是本阶段的秒数
int  fanPidTuneElapsed() {
  if (atState == AT_IDLE || atState == AT_DONE) return 0;
  return (int)((millis() - atStartMs) / 1000);
}
// 冷却阶段预估剩余时间
int  fanPidTuneRemaining(){
  if (atState == AT_HEAT) return -1; // 加热阶段，无法预估
  if (atState != AT_COOL) return 0;
  float dT = atTempStart - (float)deviceInfo.temperature;
  if (dT < 1) return 300;
  float rate = (float)fanPidTuneElapsed() / fmax(dT, 0.5f);
  return (int)(rate * 3.0f); // 预估再降 3 度需要的时间
}

void fanPidCancelTune() {
  if (atState == AT_HEAT || atState == AT_COOL) {
    atState = AT_IDLE;
    Serial.println("[Fan][AT] cancelled by user");
    fanPidReset();    // 整定期间积分/微分状态已失效，先复位再恢复控制
    adjustFanSpeed(); // 立即恢复正常风扇控制（PID/曲线）
  }
}

void fanInit() {
  ledcSetup(FAN_PWM_CHANNEL, FAN_PWM_FREQ, FAN_PWM_RES);
  ledcAttachPin(FAN_PWM_PIN, FAN_PWM_CHANNEL);
  ledcWrite(FAN_PWM_CHANNEL, 0);
}

FanCurve getFanCurve(uint8_t mode) {
  switch (mode) {
    case 0: return {25, 50, 30, 120, 0.5}; // Silent: 低温早启、平缓、上限低
    case 1: return {25, 70, 60, 220, 1.2}; // Normal
    case 2: return {25, 65, 80, 255, 1.8}; // Aggressive: 高温陡升
    case 5: return {(int)fanCustomStartTemp, (int)fanCustomMaxTemp,  // Custom: 用户自定义、线性
                    fanCustomMinPwm, fanCustomMaxPwm, 1.0};
    case 3:
    default: return {25, 70, 50, 200, 1.0}; // Auto: 平衡、线性
  }
}

void adjustFanSpeed() {
  // 模式7用于只有散热片、没有风扇的硬件。它只关闭 PWM，温度采样和关机保护仍在主循环运行。
  if (fanIsDisabled()) {
    if (fanPwmValue != 0) Serial.println("[Fan] Output disabled -> PWM=0");
    fanPwmValue = 0;
    ledcWrite(FAN_PWM_CHANNEL, 0);
    return;
  }
  if (highTempNeedsFanFull()) {
    if (fanPwmValue != 255) Serial.println("[Fan] User high-temperature protection -> PWM=255");
    fanPwmValue = 255;
    ledcWrite(FAN_PWM_CHANNEL, fanPwmValue);
    return;
  }
  if (deviceInfo.temperature == -1) return;
  // 热保护：温度逼近模块停机阈值时，无视当前模式强制全速，避免投影机自我保护停机
  if (deviceInfo.stopThreshold > 0 && deviceInfo.temperature >= deviceInfo.stopThreshold - 3) {
    if (fanPwmValue != 255)
      Serial.printf("[Fan] THERMAL SAFETY T=%d (stop=%d) -> PWM=255\n", deviceInfo.temperature, deviceInfo.stopThreshold);
    fanPwmValue = 255;
    ledcWrite(FAN_PWM_CHANNEL, fanPwmValue);
    return;
  }
  // 模式4=Full 由 setFanModeInternal 直接写 PWM=255，跳过曲线计算
  if (fanMode == 4) return;
  if (fanMode == 6) { pidCompute(); return; } // PID 闭环

  int temp = deviceInfo.temperature;
  FanCurve curve = getFanCurve(fanMode);

  // 曲线合法性兜底（自定义参数可能被设成倒挂/相等，避免除零与 NaN）
  if (curve.temp_max <= curve.temp_min) curve.temp_max = curve.temp_min + 1;
  if (curve.pwm_max < curve.pwm_min)    curve.pwm_max = curve.pwm_min;
  if (!(curve.curve_factor > 0.0f))     curve.curve_factor = 1.0f;

  temp = constrain(temp, curve.temp_min, curve.temp_max);
  float normalized = (float)(temp - curve.temp_min) / (float)(curve.temp_max - curve.temp_min);

  float curved;
  if (curve.curve_factor == 1.0)      curved = normalized;                                  // 线性
  else if (curve.curve_factor > 1.0)  curved = pow(normalized, curve.curve_factor);         // 陡峭
  else                                curved = 1.0 - pow(1.0 - normalized, 1.0 / curve.curve_factor); // 平缓

  fanPwmValue = (uint8_t)(curve.pwm_min + curved * (curve.pwm_max - curve.pwm_min));
  fanPwmValue = constrain(fanPwmValue, curve.pwm_min, curve.pwm_max);
  ledcWrite(FAN_PWM_CHANNEL, fanPwmValue);

  Serial.printf("[Fan] Mode=%d Temp=%d°C PWM=%u (Curve: %d-%d°C -> %d-%d PWM, factor=%.1f)\n",
                fanMode, temp, fanPwmValue, curve.temp_min, curve.temp_max,
                curve.pwm_min, curve.pwm_max, curve.curve_factor);
}

void setFanModeInternal(uint8_t mode) {
  atState = AT_IDLE; // 切换模式时取消进行中的自整定，避免卡在阶跃 PWM
  fanMode = mode;
  saveSettings();
  if (fanMode == 7) {
    fanPwmValue = 0;
    ledcWrite(FAN_PWM_CHANNEL, 0);
    Serial.println("[Fan] Disabled / no-fan device mode, PWM=0");
  } else if (fanMode == 4) {
    fanPwmValue = 255;
    ledcWrite(FAN_PWM_CHANNEL, fanPwmValue);
    Serial.println("[Fan] Full mode PWM=255");
  } else {
    if (fanMode == 6) fanPidReset(); // 进入 PID 模式时复位积分
    adjustFanSpeed(); // 基于当前温度设置初值
    Serial.printf("[Fan] Mode=%d enabled, PWM will adjust based on temp\n", fanMode);
  }
}

void setFanPwmValue(uint8_t v) {
  // 所有调用（包括高温保护）都必须尊重无风扇模式。
  fanPwmValue = fanIsDisabled() ? 0 : v;
  ledcWrite(FAN_PWM_CHANNEL, fanPwmValue);
  Serial.printf("[Fan] PWM set to %u\n", fanPwmValue);
}
