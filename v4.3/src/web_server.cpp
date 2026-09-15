// web_server.cpp — HTTP 路由注册
#include "web_server.h"
#include "config.h"
#include "settings.h"
#include "projector.h"
#include "fan.h"
#include "network.h"
#include "wifi_store.h"
#include <WiFi.h>
#include <SPIFFS.h>

AsyncWebServer server(80);

// ---------------------- Deferred actions ---------------------
enum : uint8_t { PA_NONE = 0, PA_REBOOT = 1, PA_START_AP = 2 };
static volatile uint8_t pendingAction = PA_NONE;
static volatile unsigned long pendingActionAt = 0;
static void scheduleAction(uint8_t a, unsigned long delayMs = 300) {
  pendingActionAt = millis() + delayMs; // 留出时间把 HTTP 响应发完
  pendingAction = a;
}
void processPendingAction() {
  if (pendingAction == PA_NONE) return;
  if ((long)(millis() - pendingActionAt) < 0) return;
  uint8_t a = pendingAction;
  pendingAction = PA_NONE;
  if (a == PA_REBOOT)        ESP.restart();
  else if (a == PA_START_AP) startAPMode();
}

void setupRoutes() {
  // 主页（SPIFFS 流式发送）
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!SPIFFS.exists("/web_interface.html")) {
      request->send(404, "text/plain", "web_interface.html not found in SPIFFS");
      return;
    }
    request->send(SPIFFS, "/web_interface.html", "text/html");
  });

  // 按索引执行命令
  server.on("/command", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("cmd")) { request->send(400, "text/plain", "Missing cmd parameter"); return; }
    int cmdIndex = request->getParam("cmd")->value().toInt();
    if (cmdIndex < 1 || cmdIndex > COMMANDS_COUNT) {
      request->send(400, "text/plain", "Invalid command index");
      return;
    }
    if (highTempShutdownTripped() && cmdIndex == 1) {
      request->send(423, "text/plain", "Start blocked: thermal shutdown is latched until settings reset or reboot");
      return;
    }
    sendI2CCommand(commands[cmdIndex - 1]);
    if (cmdIndex == 1)                                          inputStarted = true;  // Start Input
    else if (cmdIndex == 2 || cmdIndex == 3 || cmdIndex == 4)   inputStarted = false; // Stop / Reboot / Shutdown
    request->send(200, "text/plain", "Command executed");
  });

  // 梯形校正（下发 I2C 并持久化）
  server.on("/keystone", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("pan") || !request->hasParam("tilt") || !request->hasParam("flip")) {
      request->send(400, "text/plain", "Missing parameters");
      return;
    }
    panV = clampi(request->getParam("pan")->value().toInt(), -30, 30);
    tiltV = clampi(request->getParam("tilt")->value().toInt(), -20, 20);
    flipV = request->getParam("flip")->value().toInt();
    if (flipV < 0 || flipV > 3) flipV = 0;
    sendKeystoneAndFlip(panV, tiltV, flipV);
    saveSettings();
    request->send(200, "text/plain", "Keystone and Flip updated");
  });

  // 自定义 I2C（带校验）
  server.on("/custom_command", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (highTempShutdownTripped()) { request->send(423, "text/plain", "Custom command blocked by thermal shutdown"); return; }
    if (!request->hasParam("cmd")) { request->send(400, "text/plain", "Missing cmd parameter"); return; }
    String customCmd = request->getParam("cmd")->value();
    if (customCmd.length() % 2 != 0 || customCmd.length() > 50) {
      request->send(400, "text/plain", "Invalid command format");
      return;
    }
    for (size_t i = 0; i < customCmd.length(); ++i) {
      char c = customCmd[i];
      bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
      if (!ok) { request->send(400, "text/plain", "Invalid hex content"); return; }
    }
    sendI2CCommand(customCmd.c_str());
    request->send(200, "text/plain", "Custom command sent");
  });

  // 测试图案
  server.on("/test_pattern", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("pattern")) { request->send(400, "text/plain", "Missing pattern parameter"); return; }
    sendTestPattern((uint8_t)request->getParam("pattern")->value().toInt());
    request->send(200, "text/plain", "Test pattern command sent");
  });

  // 发射功率（应用并持久化）
  server.on("/set_tx_power", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("power")) { request->send(400, "text/plain", "Missing power parameter"); return; }
    txPowerV = request->getParam("power")->value().toInt();
    setTxPower(txPowerV);
    saveSettings();
    request->send(200, "text/plain", "Transmit Power set to " + String(txPowerV / 4.0) + " dBm");
  });

  // 读取全部持久化设置（页面引导用）
  server.on("/get_settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"pan\":" + String(panV) + ",";
    json += "\"tilt\":" + String(tiltV) + ",";
    json += "\"flip\":" + String(flipV) + ",";
    json += "\"txPower\":" + String(txPowerV) + ",";
    json += "\"lang\":\"" + String((langV == 0) ? "en" : "zh") + "\",";
    json += "\"brightness\":" + String(brightnessV) + ",";
    json += "\"contrast\":" + String(contrastV) + ",";
    json += "\"hueU\":" + String(hueU) + ",";
    json += "\"hueV\":" + String(hueV2) + ",";
    json += "\"satU\":" + String(satU) + ",";
    json += "\"satV\":" + String(satV) + ",";
    json += "\"sharpness\":" + String(sharpnessV) + ",";
    json += "\"fanMode\":" + String(fanMode) + ",";
    json += "\"fanCustomStartTemp\":" + String(fanCustomStartTemp) + ",";
    json += "\"fanCustomMaxTemp\":"   + String(fanCustomMaxTemp) + ",";
    json += "\"fanCustomMinPwm\":"    + String(fanCustomMinPwm) + ",";
    json += "\"fanCustomMaxPwm\":"    + String(fanCustomMaxPwm) + ",";
    json += "\"pidTarget\":" + String(pidTarget) + ",";
    json += "\"pidKp\":" + String(pidKp, 2) + ",";
    json += "\"pidKi\":" + String(pidKi, 2) + ",";
    json += "\"pidKd\":" + String(pidKd, 2) + ",";
    json += "\"autoStartOnBoot\":" + String(autoStartOnBoot ? "true" : "false");
    json += ",\"highTempShutdownEnabled\":" + String(highTempShutdownEnabled ? "true" : "false");
    json += ",\"highTempModuleThreshold\":" + String(highTempModuleThreshold);
    json += ",\"highTempEsp32Threshold\":" + String(highTempEsp32Threshold);
    json += "}";
    request->send(200, "application/json", json);
  });

  // 设置任意子集并持久化（含几何应用）
  server.on("/set_settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool changed = false;
    if (request->hasParam("pan"))  { panV = clampi(request->getParam("pan")->value().toInt(), -30, 30); changed = true; }
    if (request->hasParam("tilt")) { tiltV = clampi(request->getParam("tilt")->value().toInt(), -20, 20); changed = true; }
    if (request->hasParam("flip")) { flipV = request->getParam("flip")->value().toInt(); if (flipV < 0 || flipV > 3) flipV = 0; changed = true; }
    if (request->hasParam("txPower")) { txPowerV = request->getParam("txPower")->value().toInt(); setTxPower(txPowerV); changed = true; }
    if (request->hasParam("lang")) { langV = (request->getParam("lang")->value() == "zh") ? 1 : 0; changed = true; }
    if (changed) {
      sendKeystoneAndFlip(panV, tiltV, flipV);
      saveSettings();
    }
    request->send(200, "text/plain", "OK");
  });

  // 显式设置语言
  server.on("/set_lang", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("lang")) { request->send(400, "text/plain", "Missing lang"); return; }
    langV = (request->getParam("lang")->value() == "zh") ? 1 : 0;
    saveSettings();
    request->send(200, "text/plain", "Lang updated");
  });

  // 开机自动发送 Start 命令开关
  server.on("/set_auto_start", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("v")) { request->send(400, "text/plain", "Missing v"); return; }
    autoStartOnBoot = (request->getParam("v")->value() == "1" || request->getParam("v")->value() == "true");
    saveSettings();
    request->send(200, "text/plain", autoStartOnBoot ? "Auto-start ON" : "Auto-start OFF");
  });

  // 用户高温关机阈值。连续两次有效采样超限才执行，避免单点毛刺误关机。
  server.on("/set_high_temp_shutdown", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("enabled") || !request->hasParam("module") || !request->hasParam("esp32")) {
      request->send(400, "text/plain", "Missing enabled/module/esp32"); return;
    }
    String enabled = request->getParam("enabled")->value();
    int moduleLimit = request->getParam("module")->value().toInt();
    int esp32Limit = request->getParam("esp32")->value().toInt();
    if ((enabled != "0" && enabled != "1") || moduleLimit < 45 || moduleLimit > 100 ||
        esp32Limit < 55 || esp32Limit > 110) {
      request->send(400, "text/plain", "Invalid limits: module 45-100, esp32 55-110"); return;
    }
    highTempShutdownEnabled = enabled == "1";
    highTempModuleThreshold = (uint8_t)moduleLimit;
    highTempEsp32Threshold = (uint8_t)esp32Limit;
    resetHighTempRuntimeState();
    saveSettings();
    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/get_thermal_status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"esp32_temperature\":" + String(esp32TemperatureValid ? String(esp32Temperature, 1) : "null") + ",";
    json += "\"esp32_valid\":" + String(esp32TemperatureValid ? "true" : "false") + ",";
    json += "\"enabled\":" + String(highTempShutdownEnabled ? "true" : "false") + ",";
    json += "\"module_threshold\":" + String(highTempModuleThreshold) + ",";
    json += "\"esp32_threshold\":" + String(highTempEsp32Threshold) + ",";
    json += "\"module_over_count\":" + String(highTempModuleCount()) + ",";
    json += "\"esp32_over_count\":" + String(highTempEsp32Count()) + ",";
    json += "\"tripped\":" + String(highTempShutdownTripped() ? "true" : "false") + ",";
    json += "\"reason\":\"" + highTempShutdownReason() + "\"";
    json += "}";
    request->send(200, "application/json", json);
  });

  // 画质（应用 I2C 并持久化）
  server.on("/set_pq", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool pqChanged = false;
    if (request->hasParam("brightness")) { brightnessV = request->getParam("brightness")->value().toInt(); projectorSetBrightness(brightnessV); pqChanged = true; }
    if (request->hasParam("contrast"))   { contrastV   = request->getParam("contrast")->value().toInt();   projectorSetContrast(contrastV);     pqChanged = true; }
    if (request->hasParam("hueU") && request->hasParam("hueV")) {
      hueU = request->getParam("hueU")->value().toInt();
      hueV2 = request->getParam("hueV")->value().toInt();
      projectorSetHue(hueU, hueV2);
      pqChanged = true;
    }
    if (request->hasParam("satU") && request->hasParam("satV")) {
      satU = request->getParam("satU")->value().toInt();
      satV = request->getParam("satV")->value().toInt();
      projectorSetSaturation(satU, satV);
      pqChanged = true;
    }
    if (request->hasParam("sharpness")) { sharpnessV = request->getParam("sharpness")->value().toInt(); projectorSetSharpness(sharpnessV); pqChanged = true; }
    if (pqChanged) saveSettings();
    request->send(200, "text/plain", "PQ updated");
  });

  // 恢复出厂设置
  server.on("/factory_reset", HTTP_GET, [](AsyncWebServerRequest *request) {
    projectorFactoryReset();
    request->send(200, "text/plain", "Factory reset command sent.");
  });

  // 保存所有参数（模块侧）
  server.on("/save_all", HTTP_GET, [](AsyncWebServerRequest *request) {
    projectorSaveAll();
    request->send(200, "text/plain", "Save all command sent.");
  });

  // 设备信息（返回缓存，触发 loop 异步刷新，避免阻塞 TCP 栈）
  server.on("/get_device_info", HTTP_GET, [](AsyncWebServerRequest *request) {
    deviceInfoUpdatePending = true;
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
    request->send(200, "application/json", json);
  });

  // 温度（返回缓存）—— 兼容旧前端/外部调用
  server.on("/get_temperature", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"temperature\":" + String(deviceInfo.temperature) +
                  ",\"mute_threshold\":" + String(deviceInfo.muteThreshold) +
                  ",\"stop_threshold\":" + String(deviceInfo.stopThreshold) + "}";
    request->send(200, "application/json", json);
  });

  // 清空 EEPROM
  server.on("/clear_eeprom", HTTP_GET, [](AsyncWebServerRequest *request) {
    clearEEPROM();
    request->send(200, "text/plain", "EEPROM cleared. Please restart the device.");
  });

  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "ok");
  });

  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "rebooting");
    scheduleAction(PA_REBOOT); // 在 loop 中重启，让响应先发完
  });

  // 已保存网络列表（仅 SSID，不含密码）
  server.on("/wifi_saved", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", "{\"networks\":" + wifiStoreListJson() + "}");
  });

  // 新增/更新一个网络（手动输入 SSID）
  server.on("/wifi_add", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("ssid")) { request->send(400, "text/plain", "Missing ssid"); return; }
    String ssid = request->getParam("ssid")->value();
    String pwd = request->hasParam("pwd") ? request->getParam("pwd")->value() : "";
    if (ssid.length() == 0 || ssid.length() > 32) { request->send(400, "text/plain", "Invalid SSID"); return; }
    if (pwd.length() > 64) { request->send(400, "text/plain", "Password too long"); return; }
    if (!wifiStoreAdd(ssid, pwd)) { request->send(400, "text/plain", "Storage full (max 8)"); return; }
    request->send(200, "text/plain", "Saved: " + ssid);
  });

  // 删除一个网络
  server.on("/wifi_remove", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("ssid")) { request->send(400, "text/plain", "Missing ssid"); return; }
    bool ok = wifiStoreRemove(request->getParam("ssid")->value());
    request->send(200, "text/plain", ok ? "Removed" : "Not found");
  });

  // 切换 WiFi 模式
  server.on("/set_wifi_mode", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("mode")) { request->send(400, "text/plain", "Missing mode"); return; }
    if (request->getParam("mode")->value() == "sta") {
      if (wifiNetCount == 0) { request->send(400, "text/plain", "No saved networks"); return; }
      wifiConfigured = true;
      saveSettings();
      request->send(200, "text/plain", "STA enabled, rebooting...");
      scheduleAction(PA_REBOOT); // 在 loop 中重启，让响应先发完
    } else {
      wifiConfigured = false;
      saveSettings();
      request->send(200, "text/plain", "AP only mode enabled.");
      scheduleAction(PA_START_AP); // WiFi 模式切换放到 loop，避免 async_tcp 任务内重入 LwIP
    }
  });

  // WiFi 状态
  server.on("/wifi_status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"mode\":\"" + String(wifiConfigured ? "sta" : "ap") + "\",";
    json += "\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
    json += "\"ip\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "\",";
    // AP 模式也返回实际配置值，前端不应复制/硬编码热点名称。
    json += "\"ssid\":\"" + jsonEscape(WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String(getAPSSID())) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
    json += "\"lang\":\"" + String(langV == 1 ? "zh" : "en") + "\"";
    json += "}";
    request->send(200, "application/json", json);
  });

  // 实时风扇状态 + 当前模式算法曲线
  server.on("/get_fan_status", HTTP_GET, [](AsyncWebServerRequest *request) {
    uint8_t pct = (uint8_t)((uint16_t)fanPwmValue * 100 / 255);
    String json = "{";
    json += "\"pwm\":"   + String(fanPwmValue) + ",";
    json += "\"percent\":" + String(pct) + ",";
    json += "\"mode\":"  + String(fanMode) + ",";
    json += "\"temperature\":" + String(deviceInfo.temperature) + ",";
    json += "\"started\":" + String(inputStarted ? "true" : "false") + ",";
    if (fanMode == 7) {
      json += "\"full\":false,\"disabled\":true";
    } else if (fanMode == 4) {
      json += "\"full\":true";
    } else if (fanMode == 6) {
      json += "\"full\":false,\"pid\":true,\"target\":" + String(pidTarget);
      json += ",\"tuning\":" + String(fanPidIsTuning() ? "true" : "false");
      json += ",\"tuneState\":" + String(fanPidTuneState());
      json += ",\"tuneElapsed\":" + String(fanPidTuneElapsed());
      json += ",\"tuneRemaining\":" + String(fanPidTuneRemaining());
    } else {
      FanCurve c = getFanCurve(fanMode);
      json += "\"full\":false,";
      json += "\"tmin\":"   + String(c.temp_min) + ",";
      json += "\"tmax\":"   + String(c.temp_max) + ",";
      json += "\"pmin\":"   + String(c.pwm_min) + ",";
      json += "\"pmax\":"   + String(c.pwm_max) + ",";
      json += "\"factor\":" + String(c.curve_factor, 2);
    }
    json += "}";
    request->send(200, "application/json", json);
  });

  // 设置自定义风扇阈值
  server.on("/set_fan_thresholds", HTTP_GET, [](AsyncWebServerRequest *request) {
    // 先读入候选值，整体校验通过后再提交，避免出现倒挂区间
    uint8_t st = fanCustomStartTemp, mt = fanCustomMaxTemp;
    uint8_t lp = fanCustomMinPwm,    hp = fanCustomMaxPwm;
    bool changed = false;
    if (request->hasParam("start_temp")) { st = (uint8_t)clampi(request->getParam("start_temp")->value().toInt(), 10, 80); changed = true; }
    if (request->hasParam("max_temp"))   { mt = (uint8_t)clampi(request->getParam("max_temp")->value().toInt(), 20, 100); changed = true; }
    if (request->hasParam("min_pwm"))    { lp = (uint8_t)clampi(request->getParam("min_pwm")->value().toInt(), 0, 255); changed = true; }
    if (request->hasParam("max_pwm"))    { hp = (uint8_t)clampi(request->getParam("max_pwm")->value().toInt(), 0, 255); changed = true; }
    if (mt <= st) { request->send(400, "text/plain", "max_temp must be > start_temp"); return; }
    if (hp < lp)  { request->send(400, "text/plain", "max_pwm must be >= min_pwm");    return; }
    if (changed) {
      fanCustomStartTemp = st; fanCustomMaxTemp = mt;
      fanCustomMinPwm = lp;    fanCustomMaxPwm = hp;
      saveSettings();
      if (fanMode == 5) adjustFanSpeed(); // 实时生效
    }
    request->send(200, "text/plain", "OK");
  });

  // 设置风扇模式
  server.on("/set_fan", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("mode")) { request->send(400, "text/plain", "missing mode"); return; }
    int m = request->getParam("mode")->value().toInt();
    if (m < 0 || m > 7) { request->send(400, "text/plain", "invalid mode (0-7)"); return; }
    setFanModeInternal((uint8_t)m);
    request->send(200, "text/plain", "OK");
  });

  // 设置 PID 配置（目标温度 + 增益），持久化到 NVS
  server.on("/set_fan_pid", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool changed = false;
    if (request->hasParam("target")) { pidTarget = (uint8_t)clampi(request->getParam("target")->value().toInt(), 30, 90); changed = true; }
    if (request->hasParam("kp")) { pidKp = request->getParam("kp")->value().toFloat(); changed = true; }
    if (request->hasParam("ki")) { pidKi = request->getParam("ki")->value().toFloat(); changed = true; }
    if (request->hasParam("kd")) { pidKd = request->getParam("kd")->value().toFloat(); changed = true; }
    if (changed) {
      fanPidSave();
      // 整定进行中不要扰动阶跃 PWM（pidCompute 会覆盖采样用的固定转速，破坏辨识）
      if (!fanPidIsTuning()) {
        fanPidReset();               // 目标/增益变化后复位积分，重新收敛
        if (fanMode == 6) adjustFanSpeed();
      }
    }
    request->send(200, "text/plain", "OK");
  });

  // 触发 PID 自整定（状态机，开始阶跃响应采集）
  server.on("/fan_auto_tune", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (fanMode != 6) { request->send(400, "text/plain", "Switch to PID mode first"); return; }
    if (fanPidIsTuning()) { request->send(400, "text/plain", "Already tuning"); return; }
    fanPidAutoTune();
    request->send(200, "text/plain", "Auto-tune started");
  });

  // 取消进行中的自整定
  server.on("/fan_tune_cancel", HTTP_GET, [](AsyncWebServerRequest *request) {
    fanPidCancelTune();
    request->send(200, "text/plain", "Cancelled");
  });

  // 温度历史（最近 ~1 小时，360 点 × 10s）
  server.on("/get_temp_history", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"interval\":" + String(TEMP_SAMPLE_SEC) +
                  ",\"temps\":" + tempHistoryJson() +
                  ",\"esp32\":" + esp32TempHistoryJson() + "}";
    request->send(200, "application/json", json);
  });

  // 强制门户 / Captive portal：AP 模式跳转 192.168.4.1；STA 模式跳转自身 LAN IP
  // 防止手机 OS 做连通性检测时长时间挂起（ STA 下若路由器无 internet ，
  // 外网请求超时可达 30s，期间页面加载被阻塞）
  server.onNotFound([](AsyncWebServerRequest *request) {
    if (!wifiConfigured) {
      request->redirect("http://192.168.4.1/");
    } else {
      String redirectUrl = "http://" + WiFi.localIP().toString() + "/";
      request->redirect(redirectUrl);
    }
  });
}
