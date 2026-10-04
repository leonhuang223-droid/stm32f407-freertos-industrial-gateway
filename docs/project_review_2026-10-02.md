# 项目审阅报告

> 本文保留修改前的审阅结果、行号和验证记录。报告中的 8 项问题已完成代码修复；最新架构、修复对应与 13/13 测试结果见 [RTOS 架构调整与问题修复记录](runtime_architecture_refactor_2026-10-02.md)。硬件相关修复仍需开发板验收。

审阅日期：2026-10-02（Asia/Shanghai）

基线：`e78305b`（`feat: deliver STM32F407 industrial gateway`）

## 结论

工程已形成较完整的设备接口、子系统、FreeRTOS 调度、A/B OTA、构建与验证文档体系。本次复核中现有 Host 测试全部通过，ARM Debug/Release 构建及产物检查通过，但发现 8 项需要修复的问题：5 项 P1、3 项 P2。其中 5 项已通过独立 Host 程序复现，另外 3 项由任务、HAL 驱动及配置源码交叉确认，尚未在开发板上复现。

最优先处理 CAN 接收中断、网络阻塞与看门狗窗口、USART3 连续接收，以及 Storage 错误回执。当前测试通过不足以证明这几条任务与硬件协作链路正确。

此次没有修改固件、原有测试、构建配置或用户已有的 `.vscode/settings.json` 修改。新增本报告，复现程序放在 Git 忽略的 `build/review/` 下，没有提交 Git，也没有烧录开发板。

## 1. [P1] CAN 接收中断未解除挂起条件，可能阻止任务运行

位置：[f407_fieldbus_port.c:360](../firmware/platform/stm32f407/src/f407_fieldbus_port.c#L360)。

`f407_can_enable_interrupts()` 开启 `CAN_IT_RX_FIFO0_MSG_PENDING`，但 `HAL_CAN_RxFifo0MsgPendingCallback()` 只通知任务，没有读出 FIFO，也没有禁用该通知。真正的 `HAL_CAN_GetRxMessage()` 要等 CAN Task 执行。

项目内 HAL 驱动的说明明确要求二选一：在回调中取消息，或先关闭通知，等任务取完再开启。参见 [stm32f4xx_hal_can.c:103](../firmware/platform/stm32f407/cubemx/generated/stm32f407_gateway/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_can.c#L103)；其 IRQ 实现只调用回调，并未替调用者读出消息。CAN RX IRQ 优先级为 6，PendSV/SysTick 为 15，FIFO 持续非空时会反复进入 CAN IRQ，任务通知不能自行消除这个条件。

**触发与影响：** 收到一帧 CAN 数据就可能形成中断风暴，CAN Task 无法运行来排空 FIFO；在调度器启动前，`owner_task` 还为零，接收中断同样已经开启。

**建议：** 回调屏蔽 RX pending 通知并唤醒固定所有者；任务排空 FIFO 后重新开启，处理好重开时新帧到达的竞争。也可在 ISR 中只搬运帧至静态接收队列，将协议解析留给任务。

**证据等级：** 源码与本地 HAL 驱动契约确认；未执行实板中断复现。

## 2. [P1] 网络连接的合法阻塞时间超过心跳与 IWDG 窗口

位置：[f407_board_config.h:52](../firmware/platform/stm32f407/include/f407_board_config.h#L52)、[f407_reliability_port.c:375](../firmware/platform/stm32f407/src/f407_reliability_port.c#L375)、[app_rtos.c:1239](../firmware/app/rtos/src/app_rtos.c#L1239)。

ESP8266 加入 AP / 建立 TCP 的等待窗口为 15,000 ms；MQTT CONNACK 等待为 3,000 ms。Network Task 超时仅 2,500 ms，启动宽限为 3,500 ms，IWDG 为 12,000 ms。启动时 Network Task 同步执行 `network_subsystem_start()`，执行结束前不更新心跳；重连时同样可能同步等待。

**触发与影响：** AP 无响应、TCP 连接缓慢或 Broker 不返回 CONNACK时，正常的连接等待被当作任务失活；Supervisor 停止刷新 IWDG。启动等待 15 s 时，12 s 的 IWDG 窗口已先到期，离线降级和指数退避可能被反复复位打断。

**复现：** 让其他任务每 100 ms 进展、Network 尚在启动等待，使用相同的关键任务掩码和启动宽限。模拟至 12,000 ms，`watchdog_refreshes=0`，`stale_task_mask=0x20`，对应 Network。

**建议：** 把 AT 加入、TCP 连接与 MQTT 握手改为有期限、分步执行的状态机，区分“设备离线”和“任务无进展”。临时调整窗口时必须覆盖总阻塞时间并与 IWDG 一起设计，单独放宽 Network 超时仍无法覆盖启动时的 15 s 等待。

**证据等级：** Host 监控逻辑模拟复现；未测量实板 IWDG 精度。

## 3. [P1] USART3 按需开启普通 DMA，异步数据没有连续接收通道

位置：[f407_network_port.c:92](../firmware/platform/stm32f407/src/f407_network_port.c#L92)、[usart.c:286](../firmware/platform/stm32f407/cubemx/generated/stm32f407_gateway/Core/Src/usart.c#L286)。

USART3 RX 使用 `DMA_NORMAL`。只有调用 `network_serial_read()` 时才启用 Receive-to-IDLE DMA，接收完成回调只发通知，不重启 DMA，也没有 ISR 接收环形缓冲。`network_serial_flush()` 还会主动中止 RX；串口发送等待期间没有常驻接收器。

**触发与影响：** ESP8266 在命令发送期间返回 AT 响应，或 HTTP 下载期间在 STM32 写 Flash、处理其他任务时继续发送 `+IPD`，均可能落入未启用 DMA 的空档。连续数据还会在每个 256 B 普通 DMA 块完成后等待任务重新装载。该链路没有启用硬件流控，数据可能丢失或产生 ORE，表现为随机握手/下载失败。

**建议：** 类似 CLI 端口，使用常驻 Circular DMA 和静态环形缓冲，由 ISR 更新接收位置，Network Task 消费字节；明确满缓冲行为与错误恢复。普通 AT 命令发送不应停止接收通道。

**证据等级：** BSP、生成 DMA 配置及任务调度链确认；实际丢字节窗口需用连续串口流或逻辑分析仪验证。

## 4. [P1] `+IPD` 要求整帧进入 768 B AT 缓冲，较大合法载荷接收失败

位置：[esp8266.c:100](../firmware/app/devices/src/esp8266.c#L100)、[esp8266.c:132](../firmware/app/devices/src/esp8266.c#L132)、[esp8266.h:14](../firmware/app/devices/include/esp8266.h#L14)。

`extract_ipd()` 等到整段 payload 到齐后才把数据移入 TCP 缓冲，而 AT 缓冲只有 768 B。载荷长度却按 1,024 B TCP 缓冲上限解析；没有通过 AT 配置限制服务器数据块大小。应用每次读取 256 B 不会限制 ESP8266 的 `+IPD` 帧长度。

**触发与影响：** 输入 `+IPD,800:` 加 800 B 载荷，按 256 B 分块交付后，AT 缓冲达到 768 B，下一次 ingest 返回 `ERR_NO_MEMORY`，应用获得 0 B。HTTP 固件下载会失败。另一个相关缺口是：不完整 `+IPD` 在收到一些字节后就被 `esp8266_tcp_receive()` 作为 `ERR_TIMEOUT` 返回，而 HTTP 调用方立即视为失败，并不像当前 ESP 单元测试那样自行循环重试。

**建议：** 将 `+IPD` 解析改为“解析长度后逐段搬运”的流式状态机，允许消费者释放 TCP 缓冲空间；只有实际等待期限耗尽时才返回超时。使用真实长度及不同串口分片方式做端到端 HTTP 测试。

**证据等级：** 800 B 载荷场景 Host 复现。

## 5. [P1] Storage 错误路径不能保证 OTA 请求完成，取消也可能无限等待

位置：[app_rtos.c:1883](../firmware/app/rtos/src/app_rtos.c#L1883)、[app_rtos.c:1934](../firmware/app/rtos/src/app_rtos.c#L1934)、[app_rtos.c:1447](../firmware/app/rtos/src/app_rtos.c#L1447)。

存在两条缺少回执的路径：

- Storage 挂载失败时，任务延迟 1 s 后直接重试，不消费 OTA 请求。`process_ota_storage_request()` 虽然有“未挂载则返回错误”的分支，但在这里不会被调用。
- `xQueueSelectFromSet()` 已取出一个成员就绪事件后，如果 Flash 唤醒失败，任务直接 `continue`，没有接收对应成员中的请求，也没有保留该就绪事件。请求仍在成员队列，Queue Set 中对应的通知已经消失。后续取新事件会留下一个没有事件对应的队列尾项。

**触发与影响：** Flash 不可用时执行 `ota start`，或 Flash 休眠后唤醒失败一次，OTA 请求可能永远得不到完成通知。`wait_for_ota_io()` 是无限循环；即使收到 cancel，它也只记录取消标志，仍等原 I/O 完成后才返回。OTA/Storage 均继续更新心跳，Supervisor 不会自动解决这个挂起。

**建议：** 无论挂载状态如何，都应对 OTA 请求给出有界错误回执；已选出的 Queue Set 成员必须完成接收，或把选中状态保留至重试。增加整体请求期限、取消确认及明确请求所有权，不能直接在超时后返回并遗留指向 OTA 栈对象的请求指针。

**证据等级：** RTOS 调用与错误路径静态确认；现有 Host 构建不执行 `app_rtos.c`。

## 6. [P2] 收到 `CLOSED` 后，剩余 TCP 缓存不能继续读取

位置：[esp8266.c:373](../firmware/app/devices/src/esp8266.c#L373)。

接收函数看到 `CLOSED` 就立即把 `tcp_connected` 清零，然后仍可返回当前缓冲中的一部分数据。但下次调用最前面的连接检查直接拒绝，未考虑 `tcp_length` 仍然非零。

**触发与影响：** 输入 512 B `+IPD` payload 后紧跟 `CLOSED`，应用使用 256 B 输出缓冲。第一次有效读取 256 B 后，第二次返回 `ERR_DEVICE_NOT_READY`，剩余 256 B 滞留缓存。HTTP 请求明确使用 `Connection: close`，服务器发送完成即关闭是正常流程，此错误会截断下载尾部。

**建议：** 分开记录“对端已关闭”与“缓存已读完”；优先排空缓存，再报告 EOF/关闭；显式重连时清理上一连接的解析状态。

**证据等级：** Host 复现。

## 7. [P2] 被拒绝的配置改动会随下一条请求重新生效

位置：[config_subsystem.c:143](../firmware/app/subsystems/src/config_subsystem.c#L143)、[config_subsystem.c:201](../firmware/app/subsystems/src/config_subsystem.c#L201)。

`prepare()` 从累计的 `staged` 快照生成请求。`reject()` 仅在全部待处理请求都结束后才重置 staged，没有使依赖被拒绝请求的后续快照失效。

**复现：** 初始高限 20,000，依次准备 A（高限改为 21,000）、B（滞回改为 300）。拒绝 A，提交 B，结果 active 高限仍变成 21,000。B 的完整快照已经包含 A，被拒绝的字段因此重新进入运行配置与持久化。

**影响：** 连续 CLI/UI 改参时，保存失败或活动告警期间被拒绝的阈值改动，可能在后续请求成功后悄然生效，与“失败回滚旧配置”的事务预期不符。

**建议：** 简化为一次只允许一个未完成配置事务；或队列保存 patch 与基准 revision，按已提交 active 重新计算，拒绝时处理所有依赖请求。

**证据等级：** Host 复现。

## 8. [P2] 告警恢复样本计数不是连续计数

位置：[alarm_subsystem.c:187](../firmware/app/subsystems/src/alarm_subsystem.c#L187)。

在 `ALARM_STATE_RECOVER_PENDING` 中，只有再次达到告警条件时才清零恢复计数；位于告警阈值与恢复阈值之间的样本既不增加也不清零。源码按累计次数恢复，与文档要求的连续恢复样本不一致。

**复现：** 高限 20,000、滞回 200、恢复要求 3 个样本。先输入 21,000 触发告警，再输入 `19,700 → 19,700 → 19,900 → 19,700`。19,900 不满足恢复条件，本应重新计数；实际最后一个样本使告警清除、Relay 去激励。即使没有连续 3 个恢复样本也发生了恢复。

**建议：** 每次不满足恢复条件都重置连续计数，仍保留告警活动状态；同时明确质量异常是否中断阈值确认/恢复计数，并增加交错数据质量场景。

**证据等级：** Host 复现。

## 验证记录

复用现有 CMake preset/build cache 完成增量构建与测试，编译器为 Arm GNU Toolchain 14.3.1、Host MinGW GCC 16.1.0。

| 检查 | 结果 |
|---|---|
| Host 构建 | 通过 |
| CTest | 11/11 通过 |
| ARM Debug | Bootloader/App A/App B 目标及产物门禁通过 |
| ARM Release | Bootloader/App A/App B 目标及产物门禁通过 |
| Debug App A/B Flash | 各 228,176 B / 262,144 B（87.04%） |
| Release App A/B Flash | 201,244 / 201,252 B（约 76.77%） |
| App 静态 RAM | 102,648 B / 131,072 B（78.31%） |
| 独立缺陷复现 | 5/5 重现 |
| 实板、长时运行与掉电实验 | 未执行 |

独立程序：[build/review/repro.c](../build/review/repro.c)。它用于观察当前缺陷，不属于原有 CTest；程序退出码 0 表示五个缺陷均成功重现，并不表示固件功能正确。

实测输出：

```text
IPD_800: status=ERR_NO_MEMORY delivered=0 expected=800 at_length=768
IPD_CLOSED: status=ERR_DEVICE_NOT_READY delivered=256 expected=512 buffered=256
CONFIG_REJECT: high=21000 expected=20000 hysteresis=300
ALARM_CONSECUTIVE: active=0 expected=1 relay=0
NETWORK_JOIN_15S: after=12000ms refreshes=0 stale_mask=0x20
Reproduced 5/5 defects. This probe expects failures in the current implementation.
```

在项目根目录使用 GCC 可重新编译：

```powershell
gcc -std=c99 -Wall -Wextra -Werror `
  -I firmware/common/include -I firmware/app/core/include `
  -I firmware/app/devices/include -I firmware/app/domain/include `
  -I firmware/app/subsystems/include -I firmware/app/protocols/include `
  -I firmware/app/ui/include `
  build/review/repro.c `
  firmware/app/devices/src/esp8266.c `
  firmware/app/devices/src/relay.c `
  firmware/app/devices/src/watchdog_device.c `
  firmware/app/subsystems/src/config_subsystem.c `
  firmware/app/subsystems/src/alarm_subsystem.c `
  firmware/app/subsystems/src/supervisor_subsystem.c `
  firmware/common/src/error_code.c -o build/review/repro.exe
if ($LASTEXITCODE -ne 0) { throw '复现程序编译失败' }
./build/review/repro.exe
```

## 覆盖范围与后续验证

审阅覆盖 README/设计与验收文档、手写 App 设备/协议/子系统、RTOS 任务与队列、F407 BSP、Bootloader/Metadata/镜像安装、链接分区、Python 打包工具、Host 测试、CMake/CI 和调试配置。第三方源码主要核对其接口契约与集成配置，没有对全部 LVGL、FreeRTOS、CMSIS/HAL 代码逐行审计。

工程的分层、静态 RTOS 资源、inactive-slot 安装与 descriptor-last 提交、配置双副本及第三方许可证记录具备维护基础。README/SECURITY 明确说明明文网络、未签名 OTA 和 Board Unverified，这些是已公开的实现边界，没有把它们重复计作新缺陷。

当前 Host CMake 不执行 RTOS 任务与多数 BSP；ARM 门禁主要验证地址、边界、符号和源码配置，并不执行 CAN/UART/Queue Set 故障场景。CI 也仅运行 Host profile。因此下一步应优先增加以下集成验证：

1. 修复 CAN 中断解除与 USART3 持续接收后，注入连续 CAN 帧和连续串口流，验证不会阻止调度或丢字节。
2. AP/Broker 不可用超过 15 s，确认任务保持进展、采集继续、重连退避工作且不误触发 IWDG。
3. 使用大 `+IPD`、任意分片、尾部 `CLOSED` 和 Flash 唤醒失败进行完整 HTTP→OTA→Storage 验证。
4. 为配置拒绝依赖关系、告警滞回中的计数中断加入永久回归测试。
5. 另补首次烧录/初始化流程：当前产物主要为 raw App，Bootloader 验证还需要 descriptor 与 Metadata；应提供工厂初始化工具或明确步骤，再执行首次启动、trial、boot-ok、掉电及回滚实板验收。

Debug Flash 余量约 33 KB；新增修复应继续检查链接尺寸。RAM 数字是静态链接占用，任务栈已在其中，但实际任务栈峰值与 LVGL 内存池峰值仍需长时实测。

回滚逻辑复核中特别确认：正常回滚由 `boot_meta_mark_rollback()` 写回 `BOOT_STATE_NORMAL`，不会因为旧固件收到 `BOOT_STATE_ROLLBACK` 而形成先前怀疑的重复复位；该猜测已排除，没有列入发现项。
