# STM32F407 工程构建状态

## 当前结论

独立 STM32F407ZGT6 工程已建立，验证等级为 **Host Verified + ARM Build Verified**，不是 **Board Verified**。

## 本阶段验证结果

2026-08-15 完成深度低功耗软件闭环：增加 W25Q128 Eco 空闲自动休眠/按需唤醒、Network Suspend/Resume、六任务 Event Group 静默屏障、深睡确认与安全门禁、应用临界区 DWT 统计；STOP/Standby 平台 capability 保持默认关闭，不执行未经板端证明的深睡入口：

| 门禁 | 结果 | 关键证据 |
|---|---|---|
| Host Debug + CTest | PASS，11/11 | Deep Power 门禁/回退、嵌套临界区与回绕、W25 幂等休眠、Network Suspend/Resume、CLI 确认命令及全部既有用例 |
| ARM Debug | PASS | 三镜像及向量/分区/RTOS/Reliability/Deep-Power/业务门禁 |
| ARM Release | PASS | 三镜像及相同业务门禁；Debug-only 故障注入仍被裁除 |
| App A/B Debug | 228,176 B Flash / 102,648 B RAM | 每槽 Flash 87.04%，普通 RAM 78.31% |
| App A/B Release | 201,244/201,252 B Flash / 102,648 B RAM | 每槽 Flash 76.77%，普通 RAM 78.31% |
| Bootloader Release | 26,676 B Flash / 3,440 B RAM | Bootloader Flash 40.70% |
| Board | 未执行 | 未连接 ST-LINK、LCD/触摸、CLI 串口、ESP8266、Broker、传感器、存储或总线节点 |

本阶段手写源码继续使用 `-Wall -Wextra -Werror`，Host、ARM Debug 和 ARM Release 均无新增编译告警。

## 已完成

- CubeMX `.ioc` 固定 STM32F407ZGT6、25 MHz HSE、168 MHz SYSCLK、SWD、USART1 DMA、SPI1 W25Q128、SPI2、USART2 DMA、USART3 DMA、CAN1、RS485 DE、MAX31865 CS 和板载 RGB LED。外接 I2C1 由 BSP 独立初始化，避免 CubeMX 6.18 生成器对该引脚组合的限制。
- CubeMX 生成代码隔离在 `firmware/platform/stm32f407/cubemx/generated`，手写业务代码不进入生成目录。
- CMake + Ninja 独立构建 `f407_bootloader`、`f407_app_a`、`f407_app_b`。
- App A/B 使用 FreeRTOS V11.3.0 ARM_CM4F port、硬浮点 ABI 和静态任务/内核对象。
- 建立十任务调度骨架、十五条业务 Queue、Storage Queue Set、Event Group、Snapshot/Config Mutex 和 ISR-to-Task Notification 等待路径；低功耗命令通过专用 Queue 交给 Supervisor，CLI 不直接改写控制器。
- 建立 C 对象接口 `gateway_device_t + gateway_device_ops_t`，调用者只依赖公开包装函数。
- 建立 HAL-free I2C/SPI 端口对象以及 ADS1115、MAX31865、SHT30 强类型设备对象，包含 Init/Sample/Suspend/Resume/Self-test 和健康统计。
- 实现 Acquisition Subsystem、Composition Root 和统一四测点数据模型；三类设备按独立周期采样，单设备故障不阻断其他设备。
- Acquisition FreeRTOS Task 已调用子系统并将质量编码测点投入 Measurement Queue，发送失败计入 `measurement_publish_drops`；`periodic_timing_monitor_t` 记录 100 ms 释放周期、抖动、提前/延迟峰值和 2 ms 容差外的超期次数。
- Host Mock 覆盖 ADS1115 电流换算、MAX31865 CVD 温度换算、SHT30 CRC、独立采样周期、Suspend/Resume 和通信故障隔离。
- F407 HAL 端口已将外接 I2C1 绑定到 PB6/PB7，为板载 CAN1 的 PB8/PB9 让出引脚；SPI2 直接使用 CubeMX 的 PB13/PB14/PB15 Mode 1/2.625 MHz 配置，PA4 作为 MAX31865 CS，PD3 留给 GT911 SDA。
- 建立 `rs485_bus_t`、`can_bus_t` 对象边界，使用常量 Ops 表和包装 API 隔离协议层与 STM32 HAL；建立 `modbus_rtu_master_t`、轻量 CAN 测点协议和 `fieldbus_subsystem_t` Facade。
- Modbus RTU Master 支持 `0x03/0x04`、CRC16、U16/S16/U32/S32、字序、定点缩放、异常响应、有限重试和通信质量降级；CAN 支持标准帧编解码、RX FIFO 排空、发送统计及 bus-off 延时恢复。
- Modbus Task 使用 `vTaskDelayUntil()` 周期轮询并发布到 MPSC Measurement Queue；CAN ISR 只通过 Direct-to-Task Notification 唤醒 CAN Task，Data Hub 通过独立 CAN TX Queue 扇出，协议解析和寄存器访问均不进入 Task 文件。
- F407 RS485 端口使用 USART2 PA2/PA3、PC0 DE、DMA TX 和 Receive-to-IDLE DMA RX；CAN1 使用板载收发器实际连接的 PB8/PB9，速率为 250 kbit/s。
- Host Mock 已覆盖 Modbus CRC、解析、缩放、异常、超时重试、CRC 错误、CAN 正负值编解码及 bus-off 恢复；ARM 门禁检查现场总线对象、HAL、DMA/IDLE、通知和引脚重映射符号。
- 建立 `relay_t`、`w25q128_t` 和 `storage_media_t` 对象边界；Alarm/Storage Subsystem 只依赖公开包装 API，不直接依赖 STM32 HAL。
- 告警状态机支持高低限、连续样本确认、恢复滞回、数据质量告警、唯一事件 ID 和 Relay 安全态；Data Hub 将同一测点产生的遥测、告警、UI 快照和 CAN/网络消息分别扇出。
- Storage Task 使用 Queue Set 等待日志、告警、配置和 OTA storage 请求；一次唤醒后聚合就绪成员，OTA 擦写仍由同一个 Storage Task 执行，W25Q128 保持单任务所有权。
- W25Q128 Storage 实现 JEDEC ID、分页编程、4 KB 擦除、Ready 轮询和 Deep Power-down API；持久化层实现 128 B CRC 记录、扇区代际环形区和配置 A/B 副本回退。
- 外部 Flash 分区由 Application 与 Bootloader 共用：OTA staging/metadata 地址保持不变，新增 Config A/B、Crash、Alarm Log 和 Runtime Log 区域。
- Host Mock 覆盖告警连续样本/滞回/安全态、W25Q128 SPI 命令与跨页编程、日志/Crash 扇区轮转、配置 A/B 重启恢复、故障去重和 CRC 损坏回退；ARM 门禁检查 PG2 Relay、SPI1 W25Q128、周期监视与 Crash 归档关键符号。
- 建立 `esp8266_t`、`network_transport_t` 和 `network_subsystem_t` 三层对象边界，串口 AT 设备、TCP Transport、MQTT/重连/队列策略互不反向依赖。
- Raw TCP MQTT 3.1.1 支持 CONNECT/CONNACK、PUBLISH QoS 1、PUBACK、PINGREQ/PINGRESP 和 DISCONNECT；单条在途消息使用 Packet ID，超时或重连后设置 DUP 并有限重发。
- Data Hub 将普通遥测写入长度 1 的覆盖队列，将告警写入独立长度 8 队列；Network Task 先接收告警，再接收最新遥测，子系统内部继续保证告警优先。
- OTA Task 通过控制 Queue 获取/释放网络租约，并通过专用指针 Queue + Task Notification 调用 Network/Storage Task；它不直接访问 ESP8266 或 W25Q128。租约获取时 MQTT 断开并保留在途消息，释放后重连并用原 Packet ID + DUP 重发。
- HAL-free `http_client_raw_t` 支持 `http://host[:port]/path`、HTTP/1.0/1.1 200、`Content-Length` 和流式 body，明确拒绝 HTTPS、chunked、缺失长度、非 200 和超长 Header。
- `ota_manager_t` 依次执行 Manifest 解析/适用性检查、inactive slot 检查、256 B 分块下载、完整包 CRC32/SHA256 校验和显式 pending 提交；包头内部继续校验 raw App body，形成两层摘要。
- `ota_staging_t` 强制从 offset 0 顺序写入、逐块读回验证、每次擦除一个 4 KB sector，并在完整 package 到齐后才允许写 236 B staging metadata。
- CLI 使用 `ota check`、`ota start`、`ota apply`、`ota cancel` 和详细 `ota status`。`start` 只下载并验证，`apply` 才提交 pending；代码不会自动复位，避免误触导致服务中断。
- F407 Python 工具使用 Slot A `0x08020000`、Slot B `0x08080000` 和 256 KB body 上限，生成 `image_header + raw_app.bin` 与包级 Manifest；已从当前 Release Slot B 产物重新生成并回读 201,400 B 示例包。
- F407 网络端口使用 USART3 PB10/PB11、DMA1 Stream1 RX、Stream3 TX 和 Receive-to-IDLE；USART2/USART3 共用集中 HAL UART 回调分发，ISR 仅发送 Task Notification。
- Host Mock 覆盖 MQTT 编解码、告警优先、PUBACK、DUP 重发、OTA 租约恢复、ESP8266 AT 命令和分片 `+IPD`；ARM 门禁检查网络对象、USART3 引脚/DMA、HAL 和通知符号。
- 固化 LVGL v9.5.0，按 256 KB App 槽裁剪为 RGB565、单字体、Label/Button 和简单软件渲染；UI Task 是唯一 `lv_...` API 所有者，`q_ui_snapshot` 保留最新系统快照，`q_ui_command` 串行化 CLI 页面跳转。
- 建立 `display_device_t`、`input_device_t`、`cli_transport_t` 函数指针对象；UI Page Ops 实现 Monitor、Menu、Point、Alarms、Devices、Parameters 六页 create/enter/leave/update/destroy 生命周期。
- USART1 PA9/PA10 使用 DMA2 Stream2 Circular RX、Stream7 TX 和 IDLE 回调；ISR 只把字节写入 SPSC 环形缓冲并发送 Task Notification，CLI Task 负责行缓冲、解析和诊断输出。
- CLI 支持 `status`、`rtos`、`sensor list`、`alarm list/ack`、`mqtt/storage status`、`config show/set`、`ui page` 和 `ota status/check/start/apply/cancel`。Host Mock 已覆盖解析、传输和命令分派。
- Config Subsystem 使用 active/staged 快照、单调 revision 和 request ID；Storage Task 在 `mtx_config` 下拒绝活动告警期改参，完成 Alarm 运行态重配置、W25Q128 持久化和提交，写入失败时回滚旧运行配置。
- Host Mock 使用真实 LVGL 渲染器验证页面刷新与 Flush Ops，覆盖配置提交和活动告警拒绝重配置；ARM 门禁检查 LVGL/UI/CLI/Config 符号、USART1 DMA/IDLE 路由、`q_ui_command` 和 `mtx_config`。
- 建立 HAL-free `lcd_bus_t`、`lcd_controller_t`、`lcd_controller_ops_t` 和 `gt911_t`；ILI9806G/NT35510 通过 `0xD3` ID 分派，未知控制器返回 `ERR_UNSUPPORTED`。复核时已将 NT35510 扩展寄存器修正为 `0xF000/0xF001/...` 逐索引写入，并增加 Host 反向断言。
- F407 BSP 接入 FSMC Bank3 LCD、Bank4 IS62WV51216、PF11 Reset 和 PF9/TIM14_CH1 低有效 PWM；SRAM 自检通过时使用两块 `800x10` RGB565 缓冲，失败时退回内部 `800x6` 单缓冲并记录降级。
- GT911 使用 PD7/PD3 GPIO 模拟 I2C、PD6 Reset、PG8 falling-edge EXTI；ISR 仅置位并调用 `xTaskNotifyFromISR()`，UI Task 被提前唤醒后完成寄存器读取和 LVGL 输入处理。
- UI Task 将触摸活动写入 Power Manager 并短时持有 `PM_LOCK_UI_ACTIVE`；Active/Eco 背光分别为 80%/20%，Suspend/Resume 通过设备 Ops 关闭背光并控制 LCD/GT911 休眠恢复。
- 建立 `watchdog_device_t`、`supervisor_subsystem_t`、`power_manager_t` 和 `boot_confirmation_t` 对象；平台寄存器、任务健康策略、功耗仲裁和 Metadata 事务通过函数指针/Facade 分层。
- 服务装配完成后、Scheduler 启动前开启 12 s IWDG；Scheduler 启动后由 Supervisor 每 100 ms 检查 Acquisition/Data Hub/Modbus/CAN/Network/OTA/Storage/UI 心跳，只有全部健康才刷新 IWDG。
- trial `boot_ok` 改为八任务连续健康 2 s 后，由 Supervisor 向 OTA Queue 投递内部 `CONFIRM_BOOT`，OTA Task 执行事务提交；它与远端 `ota check` 不再共用命令语义。仅允许当前运行槽等于 Metadata `pending_slot`，正常已确认槽不重复写 Flash，失败时锁存故障并停止喂狗。
- Power Lock 使用引用计数并接入 OTA Lease、Storage/Metadata 写入、Network、Modbus、CAN 在线监听和活动告警；Tickless 入口同时检查 Active 锁、最小 5 ms 空闲和 IWDG 剩余窗口。
- Storage Task 在 Eco 模式空闲 5 s 后自动调用 W25Q128 Deep Power-down，并在消费任意 Queue Set 成员前唤醒；该路径不新增共享 Flash 调用者。ESP8266 初始化和重连继续配置 `AT+SLEEP=2`，Network Suspend/Resume 会保留 QoS 1 在途消息的 DUP 恢复语义。
- `deep_power_controller_t` 使用 `deep_power_platform_ops_t` 隔离平台入口，要求显式 `CONFIRM`、六任务 ACK、Power Lock 与 IWDG 窗口全部满足。F407 `F407_STOP_PERIODIC_ENABLED` 与 `F407_STANDBY_SHIPPING_ENABLED` 当前均为 0，平台端口明确返回 `ERR_UNSUPPORTED`。
- Acquisition、Modbus、CAN、Network、Storage、UI 在自己的任务上下文完成 Suspend/Resume；CAN 静默时释放长期监控锁，恢复成功后重新获取。应用自有临界区通过 DWT 记录最长周期、嵌套深度和配对错误。
- 新增 `fault_recorder_t + fault_recorder_ops_t`，使用 magic、版本、序号和 CRC32 封装异常记录；应用层不直接访问 STM32 备份域寄存器。
- Cortex-M 异常包装根据 `EXC_RETURN` 选择 MSP/PSP，捕获 R0-R3、R12、LR、PC、xPSR、CFSR、HFSR、MMFAR、BFAR 和当前任务 token，以 19 个 word 写入 RTC Backup Register，magic 最后提交。
- FreeRTOS 启用 Trace Facility 与 Run Time Stats，DWT `CYCCNT` 提供计数基准；Supervisor 每秒计算十任务、Idle 和其他内核任务的窗口 CPU 千分比，并继续采样 Stack/Queue 高水位。
- CLI 新增 `rtos runtime`、`fault show/clear` 和 `fault inject hardfault|watchdog CONFIRM`。注入只在 Debug 构建开放；Release 门禁反向检查平台注入符号不存在。
- Host Reliability/Power Mock 覆盖嵌套锁、Tickless、心跳 stale、trial 确认、故障记录 CRC/损坏/清除和注入确认令牌；ARM 门禁检查 RTC cookie、DWT、`uxTaskGetSystemState()`、异常包装及 Debug/Release 差异。
- App 启动时执行 Composition Root 装配；部分设备缺失不会阻止 RTOS 启动，各设备在自己的采样周期重试初始化并可自动恢复。
- F407 A/B 分区按真实 Sector 擦除粒度设计，Descriptor A/B 独占 Sector 7/10。
- Bootloader 已接入 Metadata 双副本、W25Q128 staging、inactive slot 写入、descriptor-last 提交、trial/rollback 和受控 App 跳转。
- 构建门禁检查三张向量表、Flash 边界、每镜像六类产物、Bootloader/FreeRTOS 隔离、App 关键符号以及采集、现场总线、告警、Relay 和 W25Q128 路由。

## 当前边界

- Bootloader 的 W25Q128 初始化失败只影响 OTA staging，不阻断有效内部 App 启动；Application 存储启动失败时继续采集和告警，由 Storage Task 周期重试挂载，队列数据受有限容量约束。
- ADS1115、MAX31865、SHT30 的协议驱动、RTOS 数据链和 HAL 端口均已接入，但尚未连接模块或验证真实波形、器件 ID、错误电气条件和采样精度。
- MAX31865 已恢复到 PB13/PB14/PB15，不再占用 PD3/PC2/PC3；完整约束见 `acquisition_board_wiring.md`。
- RS485 Modbus 与 CAN 的对象层、协议层、FreeRTOS 数据链和 F407 HAL 端口已接线，但未连接真实收发器、从站或 CAN 节点，仍为 Board Unverified。
- PG2 是外接继电器模块的 3.3 V 逻辑控制信号，不直接驱动线圈或市电；具体开发板版本的排针位置、模块极性和上电安全态仍需板端核对。
- ESP8266 使用局域网明文 MQTT 演示边界，SSID、密码和 Broker 地址仍是占位配置；PB10/PB11 排针可用性、模块固件 AT 命令兼容性、供电、DMA/IDLE 波形、断网重连与 Broker 去重均为 Board Unverified。
- 在线 OTA 目前只支持明文 HTTP/1.x `Content-Length`，不支持 HTTPS、chunked、Range 续传或签名认证；Host 摘要校验不等同于发布者身份认证。真实 ESP8266 长包、Flash 写入、apply、重启安装和回滚仍为 Board Unverified。
- LCD/GT911 对象层、LVGL 双缓冲选择、六页状态机、CLI 和配置事务为 Host Verified + ARM Build Verified；真实屏幕批次 ID、FSMC 时序、背光极性、触摸方向、SRAM 稳定性和 USART1 DMA/IDLE 波形尚未确认，仍不标记为 Board Verified。
- 外部 SRAM 成功路径在目标代码中提供两块 16,000 B 缓冲；实际 SRAM 自检尚未上板。失败路径使用 9,600 B 内部缓冲，Host 已验证选择与降级状态。
- Tickless Idle、Power Lock、IWDG 门禁、UI 背光、ESP8266 Modem-sleep 配置和 W25Q128 自动 Deep Power-down 已接线；`planned_ms` 仍只是预算。STOP/Standby 的平台 capability 默认关闭，RTC/EXTI 唤醒、深睡后时钟树恢复和整板功耗仍需板端实现与验证。
- Supervisor/IWDG 与健康后 `boot_ok` 的软件顺序为 Host Verified + ARM Build Verified；真实 IWDG 精度、任务卡死复位、trial 确认和回滚仍是 Board Unverified。
- F407 HardFault 备份域 cookie、CPU 运行时间百分比、故障记录回读、Crash 归档、采集周期统计和 Debug-only 注入入口为 Host Verified + ARM Build Verified；真实异常、IWDG 注入、任务 token 对应关系、掉电保持和统计准确度仍为 Board Unverified。
- `fault show` 优先显示当前 RTC cookie；RTC 已清除或无有效记录时回退显示 W25Q128 最新归档。`fault clear` 只清 RTC cookie，不擦除持久化历史。
- 未连接 ST-LINK、未下载固件、未测 W25Q128 JEDEC ID、未验证 RS485 DMA/IDLE 和 CAN 波形，也未执行 A/B OTA 与回滚。

## 构建命令

```powershell
cmake --preset debug
cmake --build --preset debug --parallel
cmake --preset release
cmake --build --preset release --parallel
```

## 后续板端验收顺序（当前暂缓）

1. 上板读取 LCD/GT911 ID，验证 FSMC 时序、SRAM、触摸方向、背光极性和 Active/Eco/Suspend 功耗。
2. 使用 Debug 固件验证 HardFault cookie、Watchdog 注入、`rtos runtime`、`rtos timing` 和复位后 `fault show/clear`，再用 Release 固件确认注入不可用。
3. 验证 RTC cookie 归档后的断电保持、Crash 扇区轮转与损坏回退，并用逻辑分析仪校准采集周期统计。
4. 使用 ST-LINK 完成启动、CLI、显示、传感器、现场总线、Relay、W25Q128、MQTT、IWDG、`ota check/start/apply/cancel`、A/B 安装、异常回滚与功耗实测证据。
