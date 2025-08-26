# cxn0102projector_controller_esp32c3_supermini
- 中文：[Readme_chinese.md](Readme_chinese.md)
- connections pdf（接线图和页面说明）：[click here](/v3.2/Esp32c3supermini驱动小宝光机接线图.pdf)

> **🚀 Important Notice:**  
> This project is **now fully open-source**! You can freely download, compile, and flash the firmware onto your ESP32-C3 SuperMini without a license.

---
> **Connections:**  
![ESP32 Connect](/figures/Esp32c3_supermini_wiring.png)
## 🔥 How to Download and Flash the Binary Files

Follow the detailed steps below to flash the binary files onto your ESP32-C3 development board using the **ESP32 Flash Download Tool**.

### 1️⃣ Download the ESP32 Flash Download Tool

- **Official Download:**  
  Visit the [Espressif Documentation](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32/production_stage/tools/flash_download_tool.html) to get the tool.

- **Repository Download:**  
  The tool is also available in this project's directory:  
  📂 `cxn0102projector_controller_esp32c3_supermini/download_tool`

### 2️⃣ Obtain the Correct Binary File

- Download the **.bin** file that matches your device from the open-source resources in this repository.

### 3️⃣ Set Up the Flashing Tool

- **Open the ESP32 Flash Download Tool.**
- **Configure the following settings:**
  - **Chip Type:** `ESP32-C3`
  - **Firmware Mode:** `Development`
  - **Interface:** `UART`

### 4️⃣ Configure and Flash the Firmware

- **Use Default Settings:**  
  No need to modify default configurations in most cases.

- **Flash Address:**  
  Burn the binary file to the chip at address **`0x0`**.

- **Start Flashing:**  
  Click **"Start Flash"** or **"Download"**, then wait for the process to complete.

### 5️⃣ Verify and Restart

- After flashing is complete, you should see a **"Flash Successful"** message.
- **Restart your ESP32-C3 SuperMini** to apply the new firmware.

---

## 🆕 Updates & Version History

### **🔹 v3.3 (Latest Test)**
- ✅ **Added:** Store user settings to **EEPROM**, so configurations persist across reboots.
- ✅ **Improved:** Stability when saving multiple settings.

### **🔹 v3.2 (Latest Release)**
- ✅ **Added:** gpio2 button for shutdown

### **🔹 v3.1**
- ✅ **Added:** Chinese language support
- ✅ **Changed wiring layout**

📸 **Screenshot:**  
![ESP32 Flash Tool](figures/v3.1.png)

### **🔹 v3.0**
- ✅ **Added:** Custom I2C Commands  
- ✅ **Added:** WiFi Transmit Power Settings  

📸 **Screenshot:**  
![ESP32 Flash Tool](figures/v3.0.png)

### **Previous Versions**
- **v2.0, v2.2, v2.3** are available in the repository.  
  📩 **Contact me if you need additional help!**

---

📌 This project is now **completely open-source**, so you can freely explore, modify, and contribute.  
🎉 **Happy flashing and coding!**
