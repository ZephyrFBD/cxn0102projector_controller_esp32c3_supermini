// fan.h — 风扇 PWM 控制与温度曲线
#pragma once
#include <Arduino.h>

// 温度-PWM 曲线  格式: {min_temp, max_temp, min_pwm, max_pwm, 曲线系数}
// 曲线系数: 1.0=线性, >1.0=高温陡升, <1.0=低温早启
struct FanCurve {
  int temp_min;
  int temp_max;
  uint8_t pwm_min;
  uint8_t pwm_max;
  float curve_factor;
};

extern uint8_t fanPwmValue; // 当前 PWM (0~255)
bool fanIsDisabled();       // 无风扇设备模式：PWM 始终保持 0，但不影响温度采样

// 模式6=PID：闭环把温度稳定在目标值（NVS 持久化）
extern uint8_t pidTarget;             // 目标温度 °C
extern float   pidKp, pidKi, pidKd;   // PID 增益

void fanInit();                       // ledcSetup + attach
void fanPidLoad();                    // 从 NVS 载入 PID 配置
void fanPidSave();                    // 保存 PID 配置到 NVS
void fanPidReset();                   // 切换到 PID 模式时复位积分/微分
void fanPidAutoTune();                // 触发一次 PID 自整定（状态机）
void fanPidCancelTune();             // 取消进行中的自整定，恢复正常风扇控制
void fanPidTuneLoop();               // 在 10s 采样周期内推进整定状态机
bool  fanPidIsTuning();              // 整定是否在进行中
uint8_t fanPidTuneState();           // 0=idle 1=heat 2=cool 3=done
int   fanPidTuneElapsed();           // 整定已进行的秒数（未整定时 0）
int   fanPidTuneRemaining();        // 预估剩余秒数
FanCurve getFanCurve(uint8_t mode);   // 各模式曲线的唯一定义来源（PID 模式不适用）
void adjustFanSpeed();                // 按当前温度+模式刷新 PWM
void setFanModeInternal(uint8_t mode);
void setFanPwmValue(uint8_t v);
