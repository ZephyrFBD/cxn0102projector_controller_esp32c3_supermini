# cxn0102projector_controller_esp32c3_supermini

> **Important Notice:**  
> The binary files provided in this project require a purchased license.  
> If you have your own ESP32-C3 SuperMini development board, you can get a 50% discount.  
> The project source code will be open-sourced in the near future.

## Purchase License

- **Purchase Link:**  
  [Click here to purchase the license](https://m.tb.cn/h.TJZHCVa?tk=7TBWeStUB3q)
  
- **Contact:**  
  If you have any questions, please email: `Zephyr142024@gmail.com`

---

## How to Download and Flash the Binary Files

Follow these detailed steps to flash the binary files to your ESP32-C3 development board using the ESP32 Flash Download Tool.

### 1. Download the ESP32 Flash Download Tool

- **Online Download:**  
  Visit the [Espressif Documentation](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32/production_stage/tools/flash_download_tool.html) to download the tool.

- **Repository Download:**  
  Alternatively, you can find the tool in this project's directory at:  
  `cxn0102projector_controller_esp32c3_supermini/download_tool`

### 2. Obtain the Appropriate Binary File

- Download the binary file (.bin) that is suitable for your device from the provided resources.

### 3. Prepare the Flashing Tool

- **Launch the Tool:**  
  Open the ESP32 Flash Download Tool.

- **Select Parameters:**  
  In the tool, configure the following settings:
  - **Chip Type:** ESP32-C3
  - **Firmware:** development
  - **Interface:** UART

### 4. Configure and Flash the Firmware

- **Default Settings:**  
  Use the default settings in the tool, which typically do not require any changes.

- **Flash Address:**  
  Flash the binary file to the chip starting at address `0x0`.

- **Start Flashing:**  
  Click the "Start Flash" or "Download" button and wait until the process completes.

### 5. Complete the Flashing Process

- Once the flashing process is finished, the tool will display a "Flash Successful" message.
- Restart your development board to ensure the new firmware is running correctly.

---

If you have any questions or need further assistance, please feel free to contact me via email. Happy flashing!

---

Feel free to update this document as the project evolves.

### 6. Updates
- **V3.0**
- adds Custom I2C Command, WiFi Transmit Power settings
- [Uploading CXN<!DOCTYPE html>
<!-- saved from url=(0019)http://192.168.4.1/ -->
<html><head><meta http-equiv="Content-Type" content="text/html; charset=UTF-8"><title>CXN0102 Controller v3.0 (Author vx:samzhangxian)</title><style>
      body {
        font-family: Arial;
        text-align: center;
        background-color: #121212; /* 深色背景 */
        color: #ffffff;           /* 浅色文字 */
      }
      .button {
        font-size: 1.2rem;
        padding: 0.5rem 1rem;
        margin: 0.2rem;
        background-color: #333333; /* 按钮也使用深色背景 */
        color: #ffffff;            /* 按钮文字与背景对比 */
        border: none;
        border-radius: 4px;       /* 可选：让按钮稍微圆角 */
      }
      .slider-container {
        margin: 1rem;
      }
      /* 使滑块在暗色背景下可见度更好（可选） */
      input[type='range'] {
        width: 200px;
      }
      select {
        background-color: #333333;
        color: #ffffff;
      }
    </style></head><body><h1>CXN0102 Controller</h1><style>
  #statusIndicator {
      width: 20px;
      height: 20px;
      border-radius: 50%;
      background-color: red; /* 默认红色（断开状态） */
      position: fixed;
      top: 10px;
      left: 10px;
      box-shadow: 0 0 10px rgba(0, 0, 0, 0.5);
  }
  </style><div id="statusIndicator" style="background-color: green;"></div><h2>Basic Controls</h2><button class="button" onclick="sendCommand(1)">Start Input</button><button class="button" onclick="sendCommand(2)">Stop Input</button><button class="button" onclick="sendCommand(3)">Reboot</button><button class="button" onclick="sendCommand(4)">Shutdown</button><h2>Optical Axis Adjustment</h2><button class="button" onclick="sendCommand(5)">Enter</button><button class="button" onclick="sendCommand(6)">+</button><button class="button" onclick="sendCommand(7)">-</button><button class="button" onclick="sendCommand(9)">Exit (Save)</button><h2>Bi-Phase Adjustment</h2><button class="button" onclick="sendCommand(10)">Enter</button><button class="button" onclick="sendCommand(11)">+</button><button class="button" onclick="sendCommand(12)">-</button><button class="button" onclick="sendCommand(14)">Exit (Save)</button><h2>Keystone Adjustment</h2><div class="slider-container"><label>Horizontal: <input type="range" min="-30" max="30" value="0" id="pan"></label><label>Vertical: <input type="range" min="-20" max="20" value="0" id="tilt"></label><label>Flip Mode: <select id="flip"><option value="0">None</option><option value="1">Horizontal</option><option value="2">Vertical</option><option value="3">Both</option></select></label><button class="button" onclick="updateKeystone()">Apply</button></div><h2>Custom I2C Command(eg.0b0100 for shutdown)</h2><input type="text" id="customCmd" placeholder="Enter hex command"><button class="button" onclick="sendCustomCommand()">Send</button><script>
      function sendCustomCommand() {
        const cmd = document.getElementById('customCmd').value.trim();
        if (!cmd) {
          alert('Please enter a command!');
          return;
        }
        fetch(`/custom_command?cmd=${cmd}`)
          .then(resp => resp.text())
          .then(alert);
      }
    </script><h2>WiFi Transmit Power</h2><label for="txPower">Select Power (dBm):</label><select id="txPower"><option value="78">19.5 dBm (≈90mW)</option><option value="76">19 dBm (≈79mW)</option><option value="74">18.5 dBm (≈71mW)</option><option value="68">17 dBm (≈50mW)</option><option value="60">15 dBm (≈32mW)</option><option value="52">13 dBm (≈20mW)</option><option value="44">11 dBm (≈12mW)</option><option value="34">8.5 dBm (≈7mW)</option><option value="28">7 dBm (≈5mW)</option><option value="20">5 dBm (≈3mW)</option><option value="8">2 dBm (≈1.6mW)</option><option value="-4">-1 dBm (≈0.8mW)</option></select><button class="button" onclick="updateTxPower()">Apply</button><script>
      // 检查连接状态
      function checkConnection() {
          fetch('/ping')
              .then(response => {
                  if (response.ok) {
                      document.getElementById('statusIndicator').style.backgroundColor = 'green'; // 连接正常
                  } else {
                      document.getElementById('statusIndicator').style.backgroundColor = 'red'; // 连接失败
                  }
              })
              .catch(() => {
                  document.getElementById('statusIndicator').style.backgroundColor = 'red'; // 服务器无响应
              });
      }
      setInterval(checkConnection, 1000); // 每秒检查一次连接状态

      // 发送 I2C 命令
      function sendCommand(cmd) {
          fetch(`/command?cmd=${cmd}`)
              .then(resp => resp.text())
              .then(console.log);
      }

      // 更新梯形校正设置并保存到 localStorage
      function updateKeystone() {
          const pan = document.getElementById('pan').value;
          const tilt = document.getElementById('tilt').value;
          const flip = document.getElementById('flip').value;
          localStorage.setItem('keystonePan', pan);
          localStorage.setItem('keystoneTilt', tilt);
          localStorage.setItem('keystoneFlip', flip);
          fetch(`/keystone?pan=${pan}&tilt=${tilt}&flip=${flip}`)
              .then(resp => resp.text())
              .then(console.log);
      }

      // 更新 WiFi 发射功率设置并保存到 localStorage
      function updateTxPower() {
          const power = document.getElementById('txPower').value;
          localStorage.setItem('txPower', power);
          fetch(`/set_tx_power?power=${power}`)
              .then(resp => resp.text())
              .then(alert);
      }

      // 页面加载时自动恢复并应用之前保存的设置
      document.addEventListener('DOMContentLoaded', function() {
          // 恢复梯形校正设置
          const savedPan = localStorage.getItem('keystonePan');
          const savedTilt = localStorage.getItem('keystoneTilt');
          const savedFlip = localStorage.getItem('keystoneFlip');
          if (savedPan !== null && savedTilt !== null && savedFlip !== null) {
              document.getElementById('pan').value = savedPan;
              document.getElementById('tilt').value = savedTilt;
              document.getElementById('flip').value = savedFlip;
              updateKeystone();
          }
          // 恢复 WiFi 发射功率设置
          const savedTxPower = localStorage.getItem('txPower');
          if (savedTxPower !== null) {
              document.getElementById('txPower').value = savedTxPower;
              updateTxPower();
          }
      });
    </script></body></html>0102 Controller v3.0 (Author vx_samzhangxian).html…]()

- 
v2.0,v2.2,v2.3 is not shown here,please contact me if needed
