//注意接线
#include <Wire.h>
//#include <Arduino.h>
#include <WiFi.h>
#include <AsyncEventSource.h>
#include <WebSerial.h>

#define I2C_ADDRESS 0x77
#define MAX_DEVICES 1

AsyncWebServer server(80);

const char* ssid = "CXN0102_Web_Controller"; // WiFi AP SSID
const char* password = "12345678"; // WiFi AP Password

unsigned long last_print_time = millis();

// I2C 命令表
const char* commands[] = {
  "0100", // 1: start-input
  "0200", // 2: stop-input

  "3200", // 3: EasyOpticalAxisEnter
  "3300", // 4: OpticalAxisPlus
  "3400", // 5: OpticalAxisMinus
  "350100", // 6: EasyOpticalAxisExit-without saving
  "350101", // 7: EasyOpticalAxisExit-with saving

  "3600", // 8: Easy Bi-Phase Adjustment
  "3700", // 9: Bi-PhasePlus
  "3800", // 10: Bi-PhaseMinus
  "390100", // 11: Easy Bi-Phase Adjustment-without saving
  "390101", // 12: Easy Bi-Phase Adjustment-with saving

  "0b0100",//13: shutdown
  "0b0101",//14: reboot!
  "0300",//15: mute
  "2609e2ec01640000000000",
  "2609000000640000000000",

  "43011f",
  "4301e1",
};

const char* getCommandDescription(int index) {
  switch (index) {
    // HDMI Control
    case 1: return "Start Input";
    case 2: return "Stop Input";

    // OpticalAxis/Bi-Phase Adjustment
    case 3: return "EasyOpticalAxisEnter";
    case 4: return "OpticalAxisPlus";
    case 5: return "OpticalAxisMinus";
    case 6: return "EasyOpticalAxisExit-without saving";
    case 7: return "EasyOpticalAxisExit-with saving";

    case 8: return "Easy Bi-Phase Adjustment";
    case 9: return "Bi-PhasePlus";
    case 10: return "Bi-PhaseMinus";
    case 11: return "Easy Bi-Phase Adjustment-without saving";
    case 12: return "Easy Bi-Phase Adjustment-with saving";

    //power control
    case 13: return "Shutdown";
    case 14: return "Reboot";
    case 15: return "MUTE/UNMUTE";
    default: return "Unknown Command";
  }
}

void sendI2CCommand(const char* command) {
  for (int i = 0; i < strlen(command); i += 2) {
    char byteStr[3] = { command[i], command[i + 1], '\0' };
    uint8_t byteValue = (uint8_t)strtol(byteStr, NULL, 16);
    Wire.write(byteValue);
  }
}

void printCommandList() {
  /*
  WebSerial.println("\nCommand List:");
  for (int i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
    String cmd = String(i + 1);
    cmd += ": ";
    cmd += getCommandDescription(i + 1);
    WebSerial.println(cmd);
  }
  */
}

void handleCommand(String command) {
  int commandIndex = command.toInt();
  if (commandIndex >= 1 && commandIndex <= 20) {
    const char* cmd = commands[commandIndex - 1];
    const char* description = getCommandDescription(commandIndex);
    WebSerial.print("Sending command: ");
    WebSerial.println(cmd);
    WebSerial.println(description);
    printCommandList();

    // 发送 I2C 命令
    Wire.beginTransmission(I2C_ADDRESS);
    sendI2CCommand(cmd);
    Wire.endTransmission();

    // 等待并读取 I2C 设备的返回数据
    delay(100);
    Wire.requestFrom(I2C_ADDRESS, 32);
    String response = "";
    while (Wire.available()) {
      char c = Wire.read();
      response += c;
      WebSerial.println(c, HEX);
    }
  } else {
    WebSerial.print("Invalid command number");
  }
}


void setup() {
  Wire.begin();
  WiFi.softAP(ssid, password);
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "Hi! This is cxn0102 control website. You can access webserial interface at http://" + WiFi.softAPIP().toString() + "/webserial\nCommands:\n  \"0100\", // 1: start-input\n  \"0200\", // 2: stop-input\n\n  \"3200\", // 3: EasyOpticalAxisEnter\n  \"3300\", // 4: OpticalAxisPlus\n  \"3400\", // 5: OpticalAxisMinus\n  \"350100\", // 6: EasyOpticalAxisExit-without saving\n  \"350101\", // 7: EasyOpticalAxisExit-with saving\n\n  \"3600\", // 8: Easy Bi-Phase Adjustment\n  \"3700\", // 9: Bi-PhasePlus\n  \"3800\", // 10: Bi-PhaseMinus\n  \"390100\", // 11: Easy Bi-Phase Adjustment-without saving\n  \"390101\", // 12: Easy Bi-Phase Adjustment-with saving\n\n  \"0b0100\",//13: shutdown\n  \"0b0101\",//14: reboot!\n  \"0300\",//15: mute\n");
  });

  WebSerial.begin(&server);
  WebSerial.onMessage([&](uint8_t *data, size_t len) {
    String command = "";
    for (size_t i = 0; i < len; i++) {
      command += char(data[i]);
    }
    WebSerial.println("Received: " + command);
    handleCommand(command);
  });

  server.begin();
  Serial.print("Ready");
}

void loop() {

}
