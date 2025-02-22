# cxn0102projector_controller_esp32c3_supermini
接线图和页面说明：![click here](/v3.2/Esp32c3supermini驱动小宝光机接线图.pdf)
> **🚀 重要提示：**
>  建议固件和接线方法：
>  v3.1
> 本项目中提供的二进制文件需要购买许可证。  
> 如果你拥有自己的 **ESP32-C3 SuperMini** 开发板，可享受 **50% 折扣**（仅 2.41 美元）。  
> 该项目 **可能很快开源**。

## 🛒 购买许可证

- **购买链接：**  
  👉 [点击此处购买许可证](https://m.tb.cn/h.TJZHCVa?tk=7TBWeStUB3q)
  
- **联系方式：**  
  📩 如有任何疑问，请发送邮件至：**`Zephyr142024@gmail.com`**

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

- 从提供的资源中下载与你设备匹配的 **.bin** 文件。

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
### **🔹 v3.1（最新版本）**
- ✅ **新增：** 中文支持
- ✅ **新增：** 更改接线布局!!!!

📸 **截图：**  
![ESP32 Flash 工具](v3.1/CXN0102v3.1.png)

### **🔹 v3.0**
- ✅ **新增：** 自定义 I2C 命令  
- ✅ **新增：** WiFi 发射功率设置  

📸 **截图：**  
![ESP32 Flash 工具](v3.0/CXN0102%20Controller%20v3.0%20(Author%20vx_samzhangxian)%20-%20Google%20Chrome%202_15_2025%2012_36_12%20PM.png)

### **以前的版本**
- **v2.0, v2.2, v2.3** 未在此展示。  
  📩 **如需这些版本，请联系我！**

---

📌 如果你遇到任何问题或需要帮助，欢迎通过邮件联系我们。  
🎉 **刷写愉快！**
