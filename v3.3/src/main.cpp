#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include "esp_system.h"
#include "esp_log.h"
#include <string.h>
#include <EEPROM.h>

// ---------------------- Pins & Device ----------------------
#define SDA_PIN        8
#define SCL_PIN        7
#define BUTTON_PIN     2
#define I2C_ADDRESS    0x77
#define COM_REQ_PIN    10  // GPIO10 用于 COM_REQ

// ---------------------- SoftAP ------------------------------
const char* AP_SSID     = "CXN0102_Web_Controller";
const char* AP_PASSWORD = "12345678"; // 必须 ≥ 8 字符

// ---------------------- Commands ----------------------------
static const char* commands[] = {
  "0100",      // 1: Start Input
  "0200",      // 2: Stop Input
  "0b0101",    // 3: Reboot
  "0b0100",    // 4: Shutdown
  "3200",      // 5: Enter Optical Axis Adjustment
  "3300",      // 6: Optical Axis +
  "3400",      // 7: Optical Axis -
  "350100",    // 8: Exit Optical Axis (No Save)
  "350101",    // 9: Exit Optical Axis (Save)
  "3600",      // 10: Enter Bi-Phase Adjustment
  "3700",      // 11: Bi-Phase +
  "3800",      // 12: Bi-Phase -
  "390100",    // 13: Exit Bi-Phase (No Save)
  "390101",    // 14: Exit Bi-Phase (Save)
  "4A",        // 15: Flip Mode
  "5001",      // 16: Test Image ON
  "5000",      // 17: Test Image OFF
  "6000",      // 18: Mute
  "6001",      // 19: Unmute
  "7000",      // 20: Keystone Vertical -
  "7001",      // 21: Keystone Vertical +
  "7002",      // 22: Keystone Horizontal -
  "7003",      // 23: Keystone Horizontal +
  "8000",      // 24: Color Temperature -
  "8001",      // 25: Color Temperature +
};

// ---------------------- Web Server --------------------------
AsyncWebServer server(80);

// ---------------------- EEPROM Layout -----------------------
#define EEPROM_SIZE   64
#define ADDR_MAGIC    63
#define MAGIC_VALUE   0xA5

// Compact layout
#define ADDR_PAN      0   // int8_t   [-30,30]
#define ADDR_TILT     1   // int8_t   [-20,20]
#define ADDR_FLIP     2   // uint8_t  [0..3]
#define ADDR_TXPOWER  3   // int8_t   one of {78,76,74,68,60,52,44,34,28,20,8,-4}
#define ADDR_LANG     4   // uint8_t  0=en,1=zh

// ---------------------- Persisted State ---------------------
int panV  = 0;
int tiltV = 0;
int flipV = 0;
int txPowerV = 34; // default 8.5dBm
uint8_t langV = 0; // 0=en,1=zh

// ---------------------- Utils -------------------------------
static inline int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void saveSettings() {
  EEPROM.write(ADDR_PAN,     (int8_t)panV);
  EEPROM.write(ADDR_TILT,    (int8_t)tiltV);
  EEPROM.write(ADDR_FLIP,    (uint8_t)flipV);
  EEPROM.write(ADDR_TXPOWER, (int8_t)txPowerV);
  EEPROM.write(ADDR_LANG,    (uint8_t)langV);
  EEPROM.write(ADDR_MAGIC,   MAGIC_VALUE);
  EEPROM.commit();
  Serial.println("[EEPROM] Settings saved.");
}

void loadSettings() {
  uint8_t magic = EEPROM.read(ADDR_MAGIC);
  if (magic != MAGIC_VALUE) {
    // First boot defaults
    panV = 0; tiltV = 0; flipV = 0; txPowerV = 34; langV = 0;
    saveSettings();
    Serial.println("[EEPROM] Initialized defaults.");
    return;
  }
  panV     = (int8_t)EEPROM.read(ADDR_PAN);
  tiltV    = (int8_t)EEPROM.read(ADDR_TILT);
  flipV    = (uint8_t)EEPROM.read(ADDR_FLIP);
  txPowerV = (int8_t)EEPROM.read(ADDR_TXPOWER);
  langV    = (uint8_t)EEPROM.read(ADDR_LANG);

  // Basic sanitization
  panV  = clamp(panV,  -30, 30);
  tiltV = clamp(tiltV, -20, 20);
  if (flipV < 0 || flipV > 3) flipV = 0;

  Serial.printf("[EEPROM] Loaded: pan=%d tilt=%d flip=%d txPower=%d lang=%u\n",
                panV, tiltV, flipV, txPowerV, langV);
}

// ---------------------- WiFi Tx Power -----------------------
void setTxPower(int powerLevel) {
  wifi_power_t txPower;
  switch (powerLevel) {
      case 78: txPower = WIFI_POWER_19_5dBm; break;
      case 76: txPower = WIFI_POWER_19dBm; break;
      case 74: txPower = WIFI_POWER_18_5dBm; break;
      case 68: txPower = WIFI_POWER_17dBm; break;
      case 60: txPower = WIFI_POWER_15dBm; break;
      case 52: txPower = WIFI_POWER_13dBm; break;
      case 44: txPower = WIFI_POWER_11dBm; break;
      case 34: txPower = WIFI_POWER_8_5dBm; break;
      case 28: txPower = WIFI_POWER_7dBm; break;
      case 20: txPower = WIFI_POWER_5dBm; break;
      case 8:  txPower = WIFI_POWER_2dBm; break;
      case -4: txPower = WIFI_POWER_MINUS_1dBm; break;
      default: txPower = WIFI_POWER_8_5dBm; break;
  }
  WiFi.setTxPower(txPower);
}

// ---------------------- I2C Helpers -------------------------
void sendKeystoneAndFlip(int pan, int tilt, int flip) {
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(0x26);  // Set Video Output Position Information
  Wire.write(0x09);  // Size
  Wire.write(pan & 0xFF);
  Wire.write(tilt & 0xFF);
  Wire.write(flip & 0xFF);
  for (int i = 0; i < 6; i++) Wire.write((i == 0) ? 0x64 : 0x00);  // Fixed values
  uint8_t error = Wire.endTransmission();
  if (error) {
    Serial.printf("I2C error while sending keystone command: %d\n", error);
  } else {
    Serial.println("Keystone and Flip command sent successfully.");
  }
}

void sendI2CCommand(const char* cmd) {
  Wire.beginTransmission(I2C_ADDRESS);
  for (int i = 0; i < (int)strlen(cmd); i += 2) {
    char byteStr[3] = {cmd[i], cmd[i + 1], '\0'};
    uint8_t byteVal = (uint8_t)strtol(byteStr, NULL, 16);
    Wire.write(byteVal);
  }
  uint8_t error = Wire.endTransmission();
  if (error) {
    Serial.printf("I2C error: %d\n", error);
  } else {
    Serial.println("Command sent successfully.");
  }
}

// ---------------------- HTML Page ---------------------------
String buildMainPageHtml() {
  String page;
  page += "<!DOCTYPE html><html><head>";
  page += "<meta charset='utf-8'/>";
  page += "<meta name='viewport' content='width=device-width,initial-scale=1'/>";
  page += "<title>CXN0102 Controller v3.3 (Author vx:samzhangxian)</title>";
  page += R"===(<style>
      body {
        font-family: Arial, system-ui, -apple-system;
        text-align: center;
        background-color: #121212;
        color: #ffffff;
        margin: 0; padding: 0 8px 32px;
      }
      h1, h2 { font-weight: 600; }
      .button {
        font-size: 1.0rem;
        padding: 0.5rem 1rem;
        margin: 0.25rem;
        background-color: #333333;
        color: #ffffff;
        border: none;
        border-radius: 6px;
        cursor: pointer;
      }
      .button:active { transform: translateY(1px); }
      .grid { display: grid; gap: 8px; justify-content: center; }
      .slider-container { margin: 1rem auto; max-width: 520px; }
      label { display: inline-block; margin: 0.25rem 0.5rem; }
      input[type='range'] { width: 220px; vertical-align: middle; }
      select {
        background-color: #333333;
        color: #ffffff;
        border: none;
        border-radius: 6px;
        padding: 6px 8px;
      }
      #statusIndicator {
        width: 14px; height: 14px; border-radius: 50%;
        background-color: red; position: fixed; top: 10px; left: 10px;
        box-shadow: 0 0 10px rgba(0,0,0,0.5);
      }
      .topbar { position: fixed; top: 8px; right: 8px; }
      .card { background:#1c1c1c; border-radius:10px; padding:12px; margin:10px auto; max-width:820px; }
      .muted { opacity:.8; font-size:.9rem; }
    </style>)===";

  page += "</head><body>";

  // Status dot
  page += "<div id='statusIndicator'></div>";

  // Lang selector (top-right)
  page += "<div class='topbar'><select id='langSelect' onchange='onLangChange()'>";
  page += "<option value='en'>English</option>";
  page += "<option value='zh'>中文</option>";
  page += "</select></div>";

  page += "<h1 id='headerH1'>CXN0102 Controller</h1>";

  // Basic controls
  page += "<div class='card'><h2 id='basicControlsHeader'>Basic Controls</h2>";
  page += "<div class='grid'>";
  page += "<button id='btnStartInput' class='button' onclick='sendCommand(1)'>Start Input</button>";
  page += "<button id='btnStopInput' class='button' onclick='sendCommand(2)'>Stop Input</button>";
  page += "<button id='btnReboot' class='button' onclick='sendCommand(3)'>Reboot</button>";
  page += "<button id='btnShutdown' class='button' onclick='sendCommand(4)'>Shutdown</button>";
  page += "</div></div>";

  // Optical Axis
  page += "<div class='card'><h2 id='opticalAxisHeader'>Optical Axis Adjustment</h2>";
  page += "<div class='grid'>";
  page += "<button id='btnOpticalEnter' class='button' onclick='sendCommand(5)'>Enter/Next</button>";
  page += "<button id='btnOpticalPlus' class='button' onclick='sendCommand(6)'>+</button>";
  page += "<button id='btnOpticalMinus' class='button' onclick='sendCommand(7)'>-</button>";
  page += "<button id='btnOpticalExit' class='button' onclick='sendCommand(9)'>Exit (Save)</button>";
  page += "</div></div>";

  // Bi-Phase
  page += "<div class='card'><h2 id='biPhaseHeader'>Bi-Phase Adjustment</h2>";
  page += "<div class='grid'>";
  page += "<button id='btnBiPhaseEnter' class='button' onclick='sendCommand(10)'>Enter/Next</button>";
  page += "<button id='btnBiPhasePlus' class='button' onclick='sendCommand(11)'>+</button>";
  page += "<button id='btnBiPhaseMinus' class='button' onclick='sendCommand(12)'>-</button>";
  page += "<button id='btnBiPhaseExit' class='button' onclick='sendCommand(14)'>Exit (Save)</button>";
  page += "</div></div>";

  // Keystone + Flip
  page += "<div class='card'><h2 id='keystoneHeader'>Keystone Adjustment</h2>";
  page += "<div class='slider-container'>";
  page += "<label id='labelHorizontal'>Horizontal: <input type='range' min='-30' max='30' value='0' id='pan'></label>";
  page += "<label id='labelVertical'>Vertical: <input type='range' min='-20' max='20' value='0' id='tilt'></label>";
  page += "<label id='labelFlipMode'>Flip Mode: <select id='flip'>"
          "<option id='optionFlipNone' value='0'>None</option>"
          "<option id='optionFlipHorizontal' value='1'>Horizontal</option>"
          "<option id='optionFlipVertical' value='2'>Vertical</option>"
          "<option id='optionFlipBoth' value='3'>Both</option>"
          "</select></label>";
  page += "<button id='btnKeystoneApply' class='button' onclick='applyKeystone()'>Apply</button>";
  page += "</div></div>";

  // Custom I2C
  page += "<div class='card'><h2 id='customI2CHeader'>Custom I2C Command(eg.0b0100 for shutdown)</h2>";
  page += "<input type='text' id='customCmd' placeholder='Enter hex command'/> ";
  page += "<button id='btnSendCustom' class='button' onclick='sendCustomCommand()'>Send</button>";
  page += "<div class='muted' id='customHint'></div>";
  page += "</div>";

  // WiFi TX Power
  page += "<div class='card'><h2 id='wifiTxHeader'>WiFi Transmit Power</h2>";
  page += "<label id='labelSelectPower' for='txPower'>Select Power (dBm):</label> ";
  page += "<select id='txPower'>";
  page += "<option id='option78' value='78'>19.5 dBm (≈90mW)</option>";
  page += "<option id='option76' value='76'>19 dBm (≈79mW)</option>";
  page += "<option id='option74' value='74'>18.5 dBm (≈71mW)</option>";
  page += "<option id='option68' value='68'>17 dBm (≈50mW)</option>";
  page += "<option id='option60' value='60'>15 dBm (≈32mW)</option>";
  page += "<option id='option52' value='52'>13 dBm (≈20mW)</option>";
  page += "<option id='option44' value='44'>11 dBm (≈12mW)</option>";
  page += "<option id='option34' value='34'>8.5 dBm (≈7mW)</option>";
  page += "<option id='option28' value='28'>7 dBm (≈5mW)</option>";
  page += "<option id='option20' value='20'>5 dBm (≈3mW)</option>";
  page += "<option id='option8' value='8'>2 dBm (≈1.6mW)</option>";
  page += "<option id='optionMinus4' value='-4'>-1 dBm (≈0.8mW)</option>";
  page += "</select> ";
  page += "<button id='btnTxApply' class='button' onclick='applyTx()'>Apply</button>";
  page += "</div>";

  // Scripts: connection check, commands, persistence via backend
  page += R"===(<script>
    // --- connection indicator ---
    function checkConnection() {
      fetch('/ping').then(r => {
        document.getElementById('statusIndicator').style.backgroundColor = r.ok ? 'green' : 'red';
      }).catch(()=>{document.getElementById('statusIndicator').style.backgroundColor='red';});
    }
    setInterval(checkConnection, 1000);

    // --- commands ---
    function sendCommand(cmd) {
      fetch(`/command?cmd=${cmd}`).then(r=>r.text()).then(console.log);
    }

    // --- custom I2C ---
    function sendCustomCommand() {
      const cmd = document.getElementById('customCmd').value.trim();
      if (!cmd) { alert('Please enter a command!'); return; }
      fetch(`/custom_command?cmd=${cmd}`).then(resp => resp.text()).then(t => alert(t));
    }

    // --- keystone ---
    async function applyKeystone() {
      const pan = document.getElementById('pan').value;
      const tilt = document.getElementById('tilt').value;
      const flip = document.getElementById('flip').value;
      // 发送动作 + 持久化
      await fetch(`/keystone?pan=${pan}&tilt=${tilt}&flip=${flip}`);
      await fetch(`/set_settings?pan=${pan}&tilt=${tilt}&flip=${flip}`);
    }

    // --- tx power ---
    async function applyTx() {
      const power = document.getElementById('txPower').value;
      await fetch(`/set_tx_power?power=${power}`);      // 应用
      await fetch(`/set_settings?txPower=${power}`);    // 持久化
      alert('Transmit Power applied.');
    }

    // --- language pack ---
    var languages = {
      "en": {
        "title": "CXN0102 Controller v3.2 (Author vx:samzhangxian)",
        "h1": "CXN0102 Controller",
        "basicControls": "Basic Controls",
        "startInput": "Start Input",
        "stopInput": "Stop Input",
        "reboot": "Reboot",
        "shutdown": "Shutdown",
        "opticalAxisAdjustment": "Optical Axis Adjustment",
        "enter": "Enter/Next",
        "exitSave": "Exit (Save)",
        "biPhaseAdjustment": "Bi-Phase Adjustment",
        "keystoneAdjustment": "Keystone Adjustment",
        "horizontal": "Horizontal:",
        "vertical": "Vertical:",
        "flipMode": "Flip Mode:",
        "none": "None",
        "horizontalOption": "Horizontal",
        "verticalOption": "Vertical",
        "both": "Both",
        "apply": "Apply",
        "customI2C": "Custom I2C Command (eg.0b0100 for shutdown)",
        "enterHexCmd": "Enter hex command",
        "send": "Send",
        "wifiTransmitPower": "WiFi Transmit Power",
        "selectPower": "Select Power (dBm):",
        "option78": "19.5 dBm (≈90mW)",
        "option76": "19 dBm (≈79mW)",
        "option74": "18.5 dBm (≈71mW)",
        "option68": "17 dBm (≈50mW)",
        "option60": "15 dBm (≈32mW)",
        "option52": "13 dBm (≈20mW)",
        "option44": "11 dBm (≈12mW)",
        "option34": "8.5 dBm (≈7mW)",
        "option28": "7 dBm (≈5mW)",
        "option20": "5 dBm (≈3mW)",
        "option8": "2 dBm (≈1.6mW)",
        "optionMinus4": "-1 dBm (≈0.8mW)"
      },
      "zh": {
        "title": "CXN0102 控制器 v3.2 (作者 vx:samzhangxian)",
        "h1": "CXN0102 控制器",
        "basicControls": "基本控制功能",
        "startInput": "开始输入",
        "stopInput": "停止输入",
        "reboot": "重启",
        "shutdown": "关机",
        "opticalAxisAdjustment": "光轴调整",
        "enter": "进入/切换下一项",
        "exitSave": "退出（保存）",
        "biPhaseAdjustment": "双相位调整",
        "keystoneAdjustment": "梯形校正",
        "horizontal": "水平:",
        "vertical": "垂直:",
        "flipMode": "翻转模式:",
        "none": "无",
        "horizontalOption": "水平",
        "verticalOption": "垂直",
        "both": "双向",
        "apply": "应用",
        "customI2C": "自定义 I2C 命令（例如：0b0100 关机）",
        "enterHexCmd": "输入十六进制命令",
        "send": "发送",
        "wifiTransmitPower": "WiFi 发射功率",
        "selectPower": "选择功率 (dBm):",
        "option78": "19.5 dBm (约90毫瓦)",
        "option76": "19 dBm (约79毫瓦)",
        "option74": "18.5 dBm (约71毫瓦)",
        "option68": "17 dBm (约50毫瓦)",
        "option60": "15 dBm (约32毫瓦)",
        "option52": "13 dBm (约20毫瓦)",
        "option44": "11 dBm (约12毫瓦)",
        "option34": "8.5 dBm (约7毫瓦)",
        "option28": "7 dBm (约5毫瓦)",
        "option20": "5 dBm (约3毫瓦)",
        "option8": "2 dBm (约1.6毫瓦)",
        "optionMinus4": "-1 dBm (约0.8毫瓦)"
      }
    };

    function switchLanguage() {
      var lang = document.getElementById('langSelect').value;
      var dict = languages[lang];
      document.title = dict.title;
      document.getElementById('headerH1').innerText = dict.h1;

      document.getElementById('basicControlsHeader').innerText = dict.basicControls;
      document.getElementById('btnStartInput').innerText = dict.startInput;
      document.getElementById('btnStopInput').innerText = dict.stopInput;
      document.getElementById('btnReboot').innerText = dict.reboot;
      document.getElementById('btnShutdown').innerText = dict.shutdown;

      document.getElementById('opticalAxisHeader').innerText = dict.opticalAxisAdjustment;
      document.getElementById('btnOpticalEnter').innerText = dict.enter;
      document.getElementById('btnOpticalExit').innerText = dict.exitSave;

      document.getElementById('biPhaseHeader').innerText = dict.biPhaseAdjustment;
      document.getElementById('btnBiPhaseEnter').innerText = dict.enter;
      document.getElementById('btnBiPhaseExit').innerText = dict.exitSave;

      document.getElementById('keystoneHeader').innerText = dict.keystoneAdjustment;

      var labelH = document.getElementById('labelHorizontal');
      labelH.childNodes[0].nodeValue = dict.horizontal + " ";
      var labelV = document.getElementById('labelVertical');
      labelV.childNodes[0].nodeValue = dict.vertical + " ";
      var labelF = document.getElementById('labelFlipMode');
      labelF.childNodes[0].nodeValue = dict.flipMode + " ";

      document.getElementById('optionFlipNone').innerText = dict.none;
      document.getElementById('optionFlipHorizontal').innerText = dict.horizontalOption;
      document.getElementById('optionFlipVertical').innerText = dict.verticalOption;
      document.getElementById('optionFlipBoth').innerText = dict.both;
      document.getElementById('btnKeystoneApply').innerText = dict.apply;

      document.getElementById('customI2CHeader').innerText = dict.customI2C;
      document.getElementById('customCmd').placeholder = dict.enterHexCmd;
      document.getElementById('btnSendCustom').innerText = dict.send;

      document.getElementById('wifiTxHeader').innerText = dict.wifiTransmitPower;
      document.getElementById('labelSelectPower').innerText = dict.selectPower;
      document.getElementById('option78').innerText = dict.option78;
      document.getElementById('option76').innerText = dict.option76;
      document.getElementById('option74').innerText = dict.option74;
      document.getElementById('option68').innerText = dict.option68;
      document.getElementById('option60').innerText = dict.option60;
      document.getElementById('option52').innerText = dict.option52;
      document.getElementById('option44').innerText = dict.option44;
      document.getElementById('option34').innerText = dict.option34;
      document.getElementById('option28').innerText = dict.option28;
      document.getElementById('option20').innerText = dict.option20;
      document.getElementById('option8').innerText = dict.option8;
      document.getElementById('optionMinus4').innerText = dict.optionMinus4;
      document.getElementById('btnTxApply').innerText = dict.apply;
    }

    async function onLangChange() {
      const lang = document.getElementById('langSelect').value;
      await fetch(`/set_lang?lang=${lang}`);
      switchLanguage();
    }

    // --- bootstrap: load persisted settings ---
    async function loadSettings() {
      try {
        const r = await fetch('/get_settings');
        const s = await r.json();
        document.getElementById('pan').value      = s.pan;
        document.getElementById('tilt').value     = s.tilt;
        document.getElementById('flip').value     = s.flip;
        document.getElementById('txPower').value  = s.txPower;
        document.getElementById('langSelect').value = s.lang;
        switchLanguage();
      } catch(e) {
        console.error(e);
      }
    }

    document.addEventListener('DOMContentLoaded', loadSettings);
  </script>)===";

  page += "</body></html>";
  return page;
}

// ---------------------- Setup -------------------------------
void setup() {
  Serial.begin(9600);
  delay(5000); // 保留原延时
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  ESP_LOGI("MAIN", "Running secure firmware...");
  Serial.println("starting wifi");

  EEPROM.begin(EEPROM_SIZE);
  loadSettings();

  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.println("started");
  pinMode(COM_REQ_PIN, INPUT);
  Wire.begin(SDA_PIN, SCL_PIN);

  // 应用上次保存的设置
  setTxPower(txPowerV);
  sendKeystoneAndFlip(panV, tiltV, flipV);

  // ---------------------- Routes ---------------------------
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", buildMainPageHtml());
  });

  // Commands by index
  server.on("/command", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("cmd")) {
      request->send(400, "text/plain", "Missing cmd parameter");
      return;
    }
    String cmdStr = request->getParam("cmd")->value();
    int cmdIndex = cmdStr.toInt();
    if (cmdIndex < 1 || cmdIndex > (int)(sizeof(commands) / sizeof(commands[0]))) {
      request->send(400, "text/plain", "Invalid command index");
      return;
    }
    sendI2CCommand(commands[cmdIndex - 1]);
    request->send(200, "text/plain", "Command executed");
  });

  // Keystone (also persist)
  server.on("/keystone", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("pan") || !request->hasParam("tilt") || !request->hasParam("flip")) {
      request->send(400, "text/plain", "Missing parameters");
      return;
    }
    panV  = clamp(request->getParam("pan")->value().toInt(),  -30, 30);
    tiltV = clamp(request->getParam("tilt")->value().toInt(), -20, 20);
    flipV = request->getParam("flip")->value().toInt();
    if (flipV < 0 || flipV > 3) flipV = 0;

    sendKeystoneAndFlip(panV, tiltV, flipV);
    saveSettings();
    request->send(200, "text/plain", "Keystone and Flip updated");
  });

  // Custom command (with validation)
  server.on("/custom_command", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("cmd")) {
      request->send(400, "text/plain", "Missing cmd parameter");
      return;
    }
    String customCmd = request->getParam("cmd")->value();
    if (customCmd.length() % 2 != 0 || customCmd.length() > 50) {
      request->send(400, "text/plain", "Invalid command format");
      return;
    }
    // Hex check
    for (size_t i = 0; i < customCmd.length(); ++i) {
      char c = customCmd[i];
      bool ok = (c >= '0' && c <= '9') || (c>='a'&&c<='f') || (c>='A'&&c<='F');
      if (!ok) {
        request->send(400, "text/plain", "Invalid hex content");
        return;
      }
    }
    sendI2CCommand(customCmd.c_str());
    request->send(200, "text/plain", "Custom command sent");
  });

  // Set Tx Power (apply & persist)
  server.on("/set_tx_power", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("power")) {
        request->send(400, "text/plain", "Missing power parameter");
        return;
    }
    txPowerV = request->getParam("power")->value().toInt();
    setTxPower(txPowerV);
    saveSettings();
    request->send(200, "text/plain", "Transmit Power set to " + String(txPowerV / 4.0) + " dBm");
  });

  // Ping indicator
  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "OK");
  });

  // Get all persisted settings (for page bootstrap)
  server.on("/get_settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"pan\":" + String(panV) + ",";
    json += "\"tilt\":" + String(tiltV) + ",";
    json += "\"flip\":" + String(flipV) + ",";
    json += "\"txPower\":" + String(txPowerV) + ",";
    json += "\"lang\":\"" + String((langV==0)?"en":"zh") + "\"";
    json += "}";
    request->send(200, "application/json", json);
  });

  // Set settings (any subset). Apply where relevant & persist.
  server.on("/set_settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool changed = false;
    if (request->hasParam("pan"))  { panV  = clamp(request->getParam("pan")->value().toInt(), -30, 30); changed=true; }
    if (request->hasParam("tilt")) { tiltV = clamp(request->getParam("tilt")->value().toInt(), -20, 20); changed=true; }
    if (request->hasParam("flip")) { flipV = request->getParam("flip")->value().toInt(); if (flipV<0||flipV>3) flipV=0; changed=true; }
    if (request->hasParam("txPower")) { txPowerV = request->getParam("txPower")->value().toInt(); setTxPower(txPowerV); changed=true; }
    if (request->hasParam("lang")) {
      String l = request->getParam("lang")->value();
      langV = (l == "zh") ? 1 : 0;
      changed = true;
    }
    if (changed) {
      // Apply geometry if changed
      sendKeystoneAndFlip(panV, tiltV, flipV);
      saveSettings();
    }
    request->send(200, "text/plain", "OK");
  });

  // Set language explicitly
  server.on("/set_lang", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("lang")) {
      request->send(400, "text/plain", "Missing lang");
      return;
    }
    String l = request->getParam("lang")->value();
    langV = (l == "zh") ? 1 : 0;
    saveSettings();
    request->send(200, "text/plain", "Lang updated");
  });

  server.begin();
  Serial.println("HTTP Server started.");

  // Auto send Start Input after boot (as original behavior)
  sendI2CCommand(commands[0]);
  Serial.println("Start Input command sent automatically after 5 seconds.");
}

// ---------------------- Loop -------------------------------
void loop() {
  // 按钮按下（低电平） -> Stop + Shutdown
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50);
    if (digitalRead(BUTTON_PIN) == LOW) {
      sendI2CCommand(commands[1]); // Stop
      delay(100);
      sendI2CCommand(commands[3]); // Shutdown 0b0100
      Serial.println("Shutdown command sent via button press.");
      while (digitalRead(BUTTON_PIN) == LOW) { delay(10); }
    }
  }
  delay(10);
}
