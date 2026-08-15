# STM32F407 FreeRTOS Industrial Gateway and A/B OTA

本工程面向野火 STM32F407 霸天虎 V2（STM32F407ZGT6），实现工业多协议采集、FreeRTOS 多任务架构、W25Q128 持久化、低功耗和 A/B OTA。

当前验证等级为 **Host Verified + ARM Build Verified**，尚未执行真实开发板验证。

> 这是可复现的学习与简历项目，不是可直接部署的工业安全产品。MQTT/HTTP
> 当前使用明文传输，OTA 的 CRC32/SHA-256 提供完整性校验但不提供发布者身份认证。

## 推荐阅读

1. `docs/stm32f407_industrial_gateway_design.md`：完整架构与设计取舍。
2. `docs/architecture_audit_and_delivery.md`：最终架构审计、证据和剩余风险。
3. `docs/ota_update_design_and_validation.md`：A/B OTA 安装、trial 与回滚链路。
4. `docs/reliability_power_board_validation.md`：可靠性、低功耗和上板验收清单。

## 工程结构

- `firmware/platform/stm32f407`：CubeMX 生成层与手写 F407 平台适配。
- `firmware/app/core|devices|domain|protocols|subsystems|rtos|ui`：对象化设备接口、系统上下文、工业测点模型、协议中间件、子系统门面、FreeRTOS 调度骨架与 LVGL 页面状态机。
- `firmware/bootloader`：裸机 A/B Bootloader 状态机、镜像安装、校验与跳转逻辑。
- `firmware/common`：Bootloader/Application 共享的 Metadata、分区表、镜像格式和校验算法。
- `firmware/linker`：Bootloader、App A、App B 独立链接脚本。
- `third_party/freertos_kernel`：FreeRTOS V11.3.0 与 Cortex-M4F port。
- `third_party/lvgl`：固定到 LVGL v9.5.0，使用项目内 `lv_conf.h` 做 MCU 尺寸裁剪。
- `third_party/stm32f4_hal`：从 STM32CubeF4 V1.28.3 固化的 I2C HAL 最小子集，用于手写 PB6/PB7 I2C1 端口。
- `tests`：Host C/Python 单元测试和 F407 ELF/产物门禁。
- `tools`：F407 Slot A/B 固件打包与 Manifest 生成工具。
- `docs/stm32f407_industrial_gateway_design.md`：完整系统设计方案。

## 构建

依赖 CMake 3.20+、Ninja、Python 3 和 Host C 编译器。ARM 固件固定使用
Arm GNU Toolchain 14.3.1；将 `arm-none-eabi-*` 加入 `PATH`，或设置
`ARM_GNU_TOOLCHAIN_ROOT` 指向工具链根目录。VS Code 配置不包含本机绝对路径。

Debug：

```powershell
cmake --preset debug
cmake --build --preset debug --parallel
```

Release：

```powershell
cmake --preset release
cmake --build --preset release --parallel
```

Host 测试：

```powershell
cmake --preset host-debug
cmake --build --preset host-debug --parallel
ctest --preset host-debug
```

VS Code 默认构建任务为 `F407: Build All`。`F407 App A - ST-LINK` 与 `F407 Bootloader - ST-LINK` 调试配置已经预留，但只有完成板端验证后才能标记为 Board Verified。

## Flash 布局

| 区域 | 起始地址 | 物理容量 | 说明 |
|---|---:|---:|---|
| Bootloader | `0x08000000` | 64 KB | Sector 0-3，裸机运行 |
| Metadata A | `0x08010000` | 64 KB | Sector 4 |
| App A body | `0x08020000` | 256 KB | Sector 5-6 |
| App A descriptor | `0x08060000` | 128 KB | Sector 7，独立提交 |
| App B body | `0x08080000` | 256 KB | Sector 8-9 |
| App B descriptor | `0x080C0000` | 128 KB | Sector 10，独立提交 |
| Metadata B | `0x080E0000` | 128 KB | Sector 11 |

构建生成 `f407_bootloader`、`f407_app_a`、`f407_app_b` 的 `.elf`、`.bin`、`.hex`、`.map`、`.lst` 和 `.size.json`。默认门禁检查向量地址、Flash/RAM 边界、Bootloader 不链接 FreeRTOS，以及 App 链接关键 RTOS 原语。

## 当前实现边界

- Bootloader 已接入 Metadata 双副本、W25Q128 staging 校验、inactive slot Sector 擦写、descriptor-last 提交、trial/rollback 决策和 App 跳转。
- App 已创建十个静态任务、十五条业务 Queue、Storage Queue Set、Event Group、Snapshot/Config Mutex 和 ISR-to-Task Notification 调度路径；Power Command Queue 保持深度低功耗控制器由 Supervisor 单独拥有。
- 已实现 HAL-free 的 I2C/SPI 端口对象、设备健康统计和统一工业测点模型；ADS1115、MAX31865、SHT30 均采用实例结构体、常量 Ops 表和包装 API。
- Acquisition Subsystem 已通过 Composition Root 装配，并由 100 ms FreeRTOS 任务驱动；三类传感器使用独立采样周期，单设备通信或传感器故障只降低对应测点质量码，其他设备继续采集。
- ADS1115 Single-shot、MAX31865 Bias/One-shot/Fault/Callendar-Van Dusen 换算、SHT30 Single-shot/CRC 已通过 Host Mock 测试，并已链接进入 F407 App A/B。
- F407 HAL 采集端口已接入启动链：外接 I2C1 使用 PB6/PB7，为板载 CAN1 的 PB8/PB9 让出引脚；MAX31865 使用 PA4 CS 与 CubeMX 标准 SPI2 PB13/PB14/PB15。启动失败会降级运行并按设备周期自动重试。
- 采集板端接线与共享引脚约束见 `docs/acquisition_board_wiring.md`；当前仅为 ARM Build Verified，不声称传感器已在真实硬件运行。
- 已实现 HAL-free 的 `rs485_bus_t`、`can_bus_t`、Modbus RTU Master 和轻量 CAN 测点协议，并由 Fieldbus Subsystem 统一封装；协议 CRC、超时重试、异常响应、字序缩放、CAN 编解码和 bus-off 恢复已通过 Host 测试。
- Modbus Task 以 1 s 周期轮询，CAN Task 由 RX/错误中断通过 Task Notification 唤醒；两者都发布到 Measurement Queue，Data Hub 再通过独立 CAN TX Queue 扇出，Task 不直接解析设备寄存器或协议帧。
- F407 端口使用 USART2 PA2/PA3 + PC0 DE 实现 RS485 DMA/IDLE 链路，使用 CAN1 PB8/PB9 实现 250 kbit/s 总线；接线、跳帽、终端电阻和板级约束见 `docs/fieldbus_board_wiring.md`。
- 已实现 `relay_t`、`w25q128_t`、`storage_media_t` 设备对象以及 Alarm/Storage Subsystem；连续采样、滞回、数据质量安全态、日志/告警环形记录、配置 A/B 副本和 CRC 校验均已通过 Host 测试。
- Data Hub 先执行告警状态机，再把遥测和告警分别扇出到 Storage Queue；Storage Task 通过 Queue Set 唤醒并按告警、配置、普通日志的优先级消费，单独拥有 W25Q128。
- Application 与 Bootloader 共用 W25Q128 分区常量，保留原 OTA staging/metadata 起始地址和语义；新增配置、故障、告警、运行日志区域不改动 A/B OTA 状态机。
- F407 Application 使用 SPI1 PB3/PB4/PB5 + PG6 CS 访问板载 W25Q128，PG2 作为外接低压继电器模块的 active-high DO；完整约束见 `docs/control_storage_board_wiring.md`。
- 已实现 `esp8266_t`、`network_transport_t` 和 `network_subsystem_t` 对象边界，以及 Raw TCP MQTT 3.1.1 CONNECT/CONNACK、QoS 1 PUBLISH/PUBACK/DUP、Keep Alive、指数退避重连和 OTA 网络租约；告警使用独立队列并优先于可覆盖的最新遥测。
- F407 网络端口使用 USART3 PB10/PB11、DMA TX 与 Receive-to-IDLE DMA RX；ISR 只通过 Task Notification 唤醒唯一所有者 Network Task。接线、凭据配置、安全边界和板端验收见 `docs/network_mqtt_board_wiring.md`。
- 已实现 HAL-free `http_client_raw_t`、`ota_manager_t` 和 `ota_staging_t`。`ota check` 获取并验证 Manifest，`ota start` 流式下载到 W25Q128 并校验完整包 CRC32/SHA256，`ota apply` 才显式提交 staging metadata 与内部 Boot Metadata；本阶段不自动复位。
- OTA Task 通过指针 Queue 请求 Network/Storage Task，并通过 Task Notification 接收完成回执；Network Task 继续独占 ESP8266，Storage Task 继续独占 W25Q128。每次只传 256 B 下载块、每个请求只擦一个 4 KB sector，等待期间继续更新 OTA/Storage 心跳并响应取消。
- `tools/pack_firmware.py` 与 `tools/gen_manifest.py` 使用 F407 的 `0x08020000/0x08080000` 链接地址生成 `image_header + raw_app.bin` 包和包级摘要 Manifest；详细流程见 `docs/ota_update_design_and_validation.md`。
- 已接入 LVGL v9.5.0、`display_device_t`/`input_device_t` Ops、六页 Page Ops 状态机和唯一所有者 UI Task；Data Hub 只通过长度 1 的 Snapshot Queue 发送不可变快照，CLI 页面命令通过独立 Queue 进入 UI Task。
- 已实现 HAL-free `lcd_bus_t`、`lcd_controller_t` 和 `gt911_t`：LCD 读取 ID 后分派 ILI9806G/NT35510 Ops，未知型号返回 `ERR_UNSUPPORTED`；GT911 使用 PD7/PD3 软件 I2C、PD6 Reset 和 PG8 EXTI，ISR 只通知 UI Task。
- F407 BSP 已接入 FSMC Bank3 LCD、Bank4 1 MB SRAM、PF9/TIM14 低有效背光和 PF11 Reset。SRAM 自检通过时使用两块 `800x10` RGB565 缓冲，失败时退回内部 `800x6` 单缓冲并记录降级。
- 触摸活动会刷新 Power Manager 活动时间并短时持有 `PM_LOCK_UI_ACTIVE`；Active/Eco 背光为 80%/20%，Suspend/Resume 调用 LCD 与 GT911 的休眠/唤醒接口。
- 已接入 USART1 PA9/PA10 Circular DMA + IDLE CLI，ISR 只填充环形缓冲并发送 Task Notification；CLI Task 支持系统、RTOS、传感器、告警、MQTT、Storage、Config、UI 和 OTA 诊断命令。
- Config Subsystem 维护 active/staged 配置、revision 和 request ID；Storage Task 使用 Config Mutex 串行完成运行态 Alarm 重配置、W25Q128 保存和提交，持久化失败会回滚旧运行配置。
- LCD/GT911 驱动、LVGL 页面刷新、双缓冲降级、UI 电源状态、CLI 和 Config 事务已通过 Host 测试；复核时已修正 NT35510 扩展寄存器的逐索引命令格式。控制器 ID、FSMC 时序、背光极性、触摸方向和功耗仍为 Board Unverified。
- 已接入对象化 Watchdog、Supervisor、Power Manager 和 boot confirmation：八个关键任务全部健康时才刷新 IWDG，trial 槽连续健康 2 秒后才事务提交 `boot_ok`；Power Lock 已覆盖 OTA、Flash、网络、Modbus、CAN 和活动告警。
- Storage Task 在 Eco 模式连续空闲 5 s、无 OTA 活动且无待处理成员时自动让 W25Q128 进入 Deep Power-down；任何日志、告警、配置或 OTA 请求都先由同一任务唤醒 Flash，再执行业务。休眠/唤醒幂等、计数和失败状态已通过 Host 测试。
- 已实现 HAL-free `deep_power_controller_t` 和六任务 Event Group 静默屏障：Acquisition、Modbus、CAN、Network、Storage、UI 只在各自任务内 Suspend/Resume；控制器继续检查确认令牌、全部 ACK、Power Lock 和 IWDG 窗口。F407 的 STOP/Standby capability 默认关闭，`power stop/standby` 返回 `ERR_UNSUPPORTED`，禁止在 RTC/EXTI 和时钟恢复尚未上板确认时强行进入深睡。
- CLI 已增加 Stack/Queue 高水位、DWT/FreeRTOS CPU 千分比、100 ms 采集周期抖动/释放超期、应用临界区最长周期、Supervisor、Power Lock、IWDG 剩余窗口和 Slot/Fault/OTA 诊断。Host 11/11 与 ARM Debug/Release 门禁通过，真实喂狗、trial 回滚、周期测量和功耗仍为 Board Unverified；详细验收步骤见 `docs/reliability_power_board_validation.md` 和 `docs/deep_power_design_and_validation.md`。
- 已接入 `fault_recorder_t`、RTC 备份域异常 cookie、复位后 `fault show/clear` 和 Debug-only HardFault/Watchdog 注入；Storage Task 会把有效 cookie 以双层 CRC 记录追加到 W25Q128 Crash 环形区，并按 sequence + CRC 去重。跨扇区重挂载与损坏回退已通过 Host 测试，真实掉电保持仍为 Board Unverified；该故障归档功能不占用 OTA staging/metadata 分区。
- 在线 OTA 的解析、状态机、Flash 顺序写约束、打包工具和目标构建为 Host Verified + ARM Build Verified；ESP8266 实际 HTTP 下载、W25Q128 staging、显式 apply 后重启安装、trial/rollback 仍保持 Board Unverified。
- 所有实机行为，包括 W25Q128 JEDEC ID、RS485 DMA/IDLE 波形、CAN 收发与 bus-off、A/B OTA 和回滚，目前均保持 Board Unverified。
