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
![Image Description](v3.0/CXN0102 Controller v3.0 (Author vx_samzhangxian) - Google Chrome 2_15_2025 12_36_12 PM.png)
v2.0,v2.2,v2.3 is not shown here,please contact me if needed
