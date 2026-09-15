// main.cpp — CXN0102 Controller V4.3 (low-power revision)
// 模块划分:
//   config.h      硬件引脚/PWM/工具
//   settings.*    EEPROM 持久化设置
//   projector.*   I2C 通信、设备信息、Notify、测试图案、画质
//   fan.*         风扇 PWM 与温度曲线
//   network.*     WiFi AP/STA、强制门户、扫描、mDNS
//   web_server.*  HTTP 路由
#include <Arduino.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include "config.h"
#include "settings.h"
#include "wifi_store.h"
#include "projector.h"
#include "fan.h"
#include "network.h"
#include "web_server.h"

void setup() {
  Serial.begin(115200);
  delay(5000); // 等待串口/模块上电稳定
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.printf("CXN0102 Controller V4.3 starting... CPU=%u MHz\n", getCpuFrequencyMhz());

  eepromInit();
  loadSettings();
  wifiStoreLoad(); // 载入多 WiFi 列表（含从旧单网络迁移）

  // 根据 wifiConfigured 决定启动 AP 还是尝试 STA
  if (wifiConfigured) {
    Serial.println("[WiFi] wifiConfigured flag set -> attempt STA");
    // 注意：不要在此处 softAPdisconnect(true)——startSTAMode() 在无已存网络时
    // 会回退到 AP 模式，此处再关 AP 会把设备锁在网络之外。
    // WiFi.mode(WIFI_STA)（startSTAMode 内部）已确保不会双模式。
    startSTAMode();
  } else {
    startAPMode();
    delay(3000); // 等待 AP 稳定
    Serial.println("[WiFi] AP should be stable now");
  }

  // I2C + 风扇
  projectorInitI2C();
  fanInit();
  fanPidLoad();                // 载入 PID 配置（NVS）
  setFanModeInternal(fanMode); // 应用加载的 fanMode

  // SPIFFS（必须在 setupRoutes 之前）
  if (!SPIFFS.begin(true)) Serial.println("[SPIFFS] Mount failed! HTML will not be served.");
  else                     Serial.println("[SPIFFS] Mounted OK");

  // 应用上次保存的设置
  setTxPower(txPowerV);
  sendKeystoneAndFlip(panV, tiltV, flipV);

  setupRoutes();
  server.begin();
  Serial.println("HTTP Server started.");

  // 启动后拉取设备信息并自动开始输入（取决于开关）
  delay(2000);
  requestAllDeviceInfo();
  sampleEsp32Temperature();
  recordTempSample();
  if (autoStartOnBoot) {
    sendI2CCommand(commands[0]);
    inputStarted = true;
    Serial.println("[Boot] Auto-start enabled — sending Start command.");
  } else {
    Serial.println("[Boot] Auto-start disabled — skipping Start command.");
  }
}

void loop() {
  networkLoop();   // 强制门户 DNS + 扫描收尾 + STA 断线重连
  processNotify(); // 处理模块主动上报

  // 每 TEMP_SAMPLE_SEC 秒：刷新温度 -> 记录历史 -> 调整风扇（自动/曲线/PID 都在此响应）
  static unsigned long lastTempUpdate = 0;
  if (millis() - lastTempUpdate > (unsigned long)TEMP_SAMPLE_SEC * 1000) {
    requestTemperature();
    sampleEsp32Temperature();
    recordTempSample();
    const bool thermalShutdownNow = evaluateHighTempShutdown();
    if (highTempNeedsFanFull()) {
      if (fanPidIsTuning()) fanPidCancelTune();
      setFanPwmValue(255); // 有风扇时全速；无风扇模式保持 PWM=0，第二次连续超限仍关机
    } else {
      fanPidTuneLoop();                        // 自整定状态机（在 10s 采样周期内推进）
      if (!fanPidIsTuning()) adjustFanSpeed(); // 整定进行中时不要覆盖阶跃 PWM
    }
    if (thermalShutdownNow) {
      // inputStarted may be stale after an ESP32-only reboot, so always attempt Stop first.
      sendI2CCommand(commands[1]); // Stop
      delay(100);
      sendI2CCommand(commands[3]);   // Shutdown；控制器不能切断光机供电
      inputStarted = false;
      Serial.printf("[THERMAL] Stop/Shutdown sequence sent; fan PWM=%u (%s).\n",
                    fanPwmValue, fanIsDisabled() ? "disabled" : "thermal full output");
    }
    lastTempUpdate = millis();
  }

  // 页面请求触发的全量设备信息刷新（在 loop 执行，避免阻塞 HTTP 回调）
  if (deviceInfoUpdatePending) {
    deviceInfoUpdatePending = false;
    requestAllDeviceInfo();
  }

  // 网页触发的延迟系统动作（重启 / 切回 AP），避免在 async_tcp 任务里直接执行
  processPendingAction();

  // 按钮：真正的长按（≥1.5s）才触发 停止 + 关机，避免误碰即关机
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50); // 消抖
    if (digitalRead(BUTTON_PIN) == LOW) {
      unsigned long pressStart = millis();
      bool longPress = false;
      while (digitalRead(BUTTON_PIN) == LOW) {
        if (millis() - pressStart >= 1500) { longPress = true; break; }
        delay(10);
      }
      if (longPress) {
        sendI2CCommand(commands[1]); // Stop
        delay(100);
        sendI2CCommand(commands[3]); // Shutdown
        inputStarted = false;
        Serial.println("Shutdown command sent via button long-press.");
        while (digitalRead(BUTTON_PIN) == LOW) { delay(10); } // 等待松开
      }
    }
  }

  delay(10);
}
