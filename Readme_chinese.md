# cxn0102projector_controller_esp32c3_supermini
接线图和页面说明：![click here](/v3.2/Esp32c3supermini驱动小宝光机接线图.pdf)

> **🚀 重要提示：**
> 本项目已 **完全开源**！你可以自由下载、编译并刷写固件到你的 ESP32-C3 SuperMini，无需购买许可证。

---

## 🔥 如何下载并刷写二进制文件

请按照以下详细步骤，使用 **ESP32 Flash Download Tool** 将二进制文件刷入你的 ESP32-C3 开发板。

### 1️⃣ 下载 ESP32 Flash Download Tool

- **官方下载：**  
  请访问 [Espressif 文档](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32/production_stage/tools/flash_download_tool.html) 获取该工具。

- **仓库下载：**  
  该工具也可在本项目目录中找到：  
  📂 `cxn0102projector_controller_esp32c3_supermini/download_tool`

### 2️⃣ 获取正确的二进制文件

- 从本仓库的开源资源中下载与你设备匹配的 **.bin** 文件。

### 3️⃣ 设置刷写工具

- **打开 ESP32 Flash Download Tool。**
- **配置以下设置：**
  - **芯片类型：** `ESP32-C3`
  - **固件模式：** `Development`
  - **接口：** `UART`

### 4️⃣ 配置并刷写固件

- **使用默认设置：**  
  大多数情况下，无需修改默认配置。

- **刷写地址：**  
  将二进制文件烧录到芯片地址 **`0x0`**。

- **开始刷写：**  
  点击 **“Start Flash”** 或 **“Download”**，然后等待过程完成。

### 5️⃣ 验证并重启

- 刷写完成后，你应会看到 **“Flash Successful”** 的提示信息。
- **重启你的 ESP32-C3 SuperMini** 以应用新固件。

---

## 🔥 连接方式
![点击查看](Esp32c3supermini驱动小宝光机接线图.pdf)

## 🆕 更新与版本历史

### **🔹 v3.3（测试版）**
- ✅ **新增：** 将用户设置存储到 **EEPROM**，重启后仍能保持配置  
- ✅ **改进：** 多设置保存时的稳定性优化

### **🔹 v3.2（测试版）**
- ✅ **新增：** gpio2 按键用于关机

### **🔹 v3.1（最新发布版）**
- ✅ **新增：** 中文支持
- ✅ **改进：** 接线布局调整

📸 **截图：**  
![ESP32 Flash 工具](v3.1/CXN0102v3.1.png)

### **🔹 v3.0**
- ✅ **新增：** 自定义 I2C 命令  
- ✅ **新增：** WiFi 发射功率设置  

📸 **截图：**  
![ESP32 Flash 工具](v3.0/CXN0102%20Controller%20v3.0%20(Author%20vx_samzhangxian)%20-%20Google%20Chrome%202_15_2025%2012_36_12%20PM.png)

### **以前的版本**
- **v2.0, v2.2, v2.3** 可在仓库中找到  
  📩 **如需帮助，请联系我！**

---

📌 本项目已 **完全开源**，你可以自由探索、修改和贡献。  
🎉 **刷写愉快，开发愉快！**
