# CXN0102 Controller V4.3

[Back to English project README](../README.md) · [返回中文说明](../Readme_chinese.md)

V4.3 is the current ESP32-C3 PlatformIO source release. It includes the bilingual SPIFFS web interface, projector controls and status reads, Wi-Fi management, one-hour projector/ESP32 temperature histories, configurable thermal shutdown, fan/PID control, and a persistent fan-off mode for heatsink-only devices.

## Build / 构建

```bash
pio run
pio run -t buildfs
```

Upload from PlatformIO / 使用 PlatformIO 上传：

```bash
pio run -t upload
pio run -t uploadfs
```

Build output is intentionally excluded from Git. The checked-in release binaries are under [`bin/`](bin/).

## Prebuilt images / 预编译固件

The easiest option is [`bin/v4.3_merged_0x0.bin`](bin/v4.3_merged_0x0.bin), flashed at `0x0`.

最简单的方式是将 [`bin/v4.3_merged_0x0.bin`](bin/v4.3_merged_0x0.bin) 刷写到 `0x0`。

For separate flashing / 分文件刷写：

| Image / 文件 | Address / 地址 |
|---|---:|
| `bootloader.bin` | `0x0` |
| `partitions.bin` | `0x8000` |
| `firmware.bin` | `0x10000` |
| `spiffs.bin` | `0x290000` |

Check all files with [`bin/SHA256SUMS.txt`](bin/SHA256SUMS.txt).

## Hardware configuration / 硬件配置

| Pin | Function / 功能 |
|---|---|
| GPIO8 | I²C SDA |
| GPIO7 | I²C SCL |
| GPIO10 | COM_REQ input; use an external pull-up / COM_REQ 输入，使用外部上拉 |
| GPIO2 | Active-low long-press shutdown button / 低电平长按关机按钮 |
| GPIO6 | 25 kHz fan PWM / 25 kHz 风扇 PWM |

Default AP / 默认热点：

- SSID: `CXN0102_Web_Controller_Silver`
- Password / 密码: `12345678`
- Address / 地址: `http://192.168.4.1/`

## Safety / 安全

- Thermal shutdown is disabled by default. Configure and test suitable thresholds for the actual enclosure.
- Fan Off mode disables PWM only; temperature recording and thermal shutdown stay active.
- ESP32-C3 die temperature is not ambient or enclosure temperature.
- Software shutdown cannot physically remove projector power.
- The web interface has no authentication; keep the controller on a trusted network.

- 高温自动关机默认关闭，请根据实际外壳设置并验证阈值。
- 关闭风扇模式只关闭 PWM，温度记录和高温关机保持运行。
- ESP32-C3 芯片温度不等于环境或外壳温度。
- 软件关机不能物理切断光机电源。
- 网页没有身份验证，请只在可信网络中使用。

Detailed power, thermal and validation notes / 详细功耗、温度和验证说明：[`POWER_V4.3.md`](POWER_V4.3.md).
