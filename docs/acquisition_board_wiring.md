# F407 采集模块接线与板级约束

## 验证状态

本文记录已进入源码和 ARM 构建门禁的板级映射，不代表真实开发板已经验证。当前状态为 **ARM Build Verified / Board Unverified**。

## 引脚分配

| 模块信号 | STM32F407 引脚 | 开发板约束 |
|---|---|---|
| ADS1115 SCL | PB6 / I2C1_SCL | 外接采集总线；需核对模块或外部上拉 |
| ADS1115 SDA | PB7 / I2C1_SDA | ADS1115 默认 7 位地址 `0x48` |
| SHT30 SCL | PB6 / I2C1_SCL | SHT30 默认 7 位地址 `0x44` |
| SHT30 SDA | PB7 / I2C1_SDA | 与 ADS1115 共用总线，不共用设备地址 |
| MAX31865 CS | PA4 | 软件片选，空闲为高电平；不使用摄像头接口复用功能 |
| MAX31865 SCK | PB13 / SPI2_SCK | CubeMX 已配置的标准 SPI2 引脚 |
| MAX31865 SDO | PB14 / SPI2_MISO | 模块输出接 MCU 输入 |
| MAX31865 SDI | PB15 / SPI2_MOSI | MCU 输出接模块输入 |

所有模块使用 3.3 V 逻辑并与开发板共地。接线前应核对购买模块的供电和电平转换电路，不能仅凭模块名称假设其接口为 3.3 V。

## 为什么采用这组引脚

- 霸天虎板载 CAN1 收发器使用 PB8/PB9，因此外接 I2C1 从早期方案的 PB8/PB9 移到 PB6/PB7，避免采集和 CAN 同时启用时发生复用冲突。
- SPI2 直接使用 CubeMX 基线 `PB13/PB14/PB15`，CS 选用 PA4；不再运行时重映射。这样释放 PD3 给 GT911 软件 I2C SDA。
- 引脚选择同时进入源码、接线文档和 ARM 构建门禁，后续调整必须三处同步，不能只修改 `.ioc`。

CubeMX 6.18 的命令行生成器无法稳定保存当前 I2C/SPI 组合。工程采用以下可审计方式：

1. CubeMX 生成 SPI2 Mode 1、2.625 MHz 基线，但不生成 I2C1。
2. `f407_acquisition_port.c` 使用 STM32 HAL 在 PB6/PB7 上初始化 I2C1 为 400 kHz。
3. `f407_acquisition_port.c` 直接使用 `MX_SPI2_Init()` 产生的 PB13/PB14/PB15，不再改写 GPIO Alternate Function。
4. ARM 构建门禁同时检查 `.ioc` 参数、PB6/PB7 I2C、PB13/PB14/PB15 SPI2 和 PA4 CS。

## I2C 地址与电气检查

| 设备 | 7 位地址 |
|---|---:|
| ADS1115 | `0x48` |
| SHT30 | `0x44` |
PB6/PB7 外接总线不能假设继承板载 PB8/PB9 总线的上拉。连接模块前应确认模块是否自带上拉，并测量等效阻值；缺少上拉会导致总线无法释放为高电平，多个模块上拉并联后阻值过小则会增加低电平灌电流并恶化波形。

## 首次上板检查顺序

1. 断电检查 3.3 V/GND、PB13/PB14/PB15 与 SPI 输入输出方向。
2. 不接传感器启动 App，确认系统降级启动且 RTOS 不进入 Panic。
3. 用示波器或逻辑分析仪确认 PB6/PB7 为 400 kHz I2C、SPI2 为 Mode 1。
4. 依次接入 SHT30、ADS1115、MAX31865，每次只增加一个变量。
5. 验证设备断开、CRC 错误、PT100 开路/短路时仅对应测点降级。
6. 测量 4 mA、12 mA、20 mA 和 PT100 多温点误差，再决定校准系数。

## 依据

- [野火 STM32F407 霸天虎 V1/V2 底板说明](https://doc.embedfire.com/stm32_products/must_read/zh/latest/doc/introduction_of_stm32/STM32/ebf_stm32f407_batianhu_v1_v2/stm32f407_batianhu_v1_v2.html)
- [野火 STM32F407 SPI 引脚复用表](https://doc.embedfire.com/mcu/stm32/f407batianhu/std/zh/latest/book/SPI.html)
- [TI ADS1115 Datasheet](https://www.ti.com/lit/ds/symlink/ads1115.pdf)
- [Analog Devices MAX31865 Datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX31865.pdf)
- [Sensirion SHT3x-DIS Datasheet](https://sensirion.com/media/documents/213E6A3B/63A5A569/Datasheet_SHT3x_DIS.pdf)
