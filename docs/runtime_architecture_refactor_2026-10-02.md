# RTOS 架构调整与问题修复记录

日期：2026-10-02。本文记录项目审阅后的实际代码修改；[原审阅报告](project_review_2026-10-02.md)保留修改前的证据。验证等级为 Host Verified + ARM Build Verified，尚未执行开发板验收或远端 GitHub Actions。

## 模块划分

`firmware/app/rtos/src/app_rtos.c` 收敛到约 330 行，只保留静态资源分配、任务创建、启动和公开消息提交接口。10 个任务的逻辑移至 `tasks/`，公共服务移至 `services/`。任务数量、优先级和栈预算沿用原配置。

| 模块 | 职责与访问边界 |
| --- | --- |
| `app_rtos.c` | 创建 15 个队列、Storage Queue Set、互斥锁、事件组和任务 |
| `task_contexts.c/.h` | 启动时从 `app_context_t` 绑定每个任务所需的类型化指针；任务入口不再接收整个系统上下文 |
| `tasks/supervisor_task.c` | 心跳检查、喂狗、启动确认、功耗策略及深度休眠协调 |
| `tasks/acquisition_task.c` | 采集与周期测量 |
| `tasks/data_hub_task.c` | 聚合快照、告警判断和消息分发 |
| `tasks/fieldbus_tasks.c` | Modbus 与 CAN 两个任务 |
| `tasks/network_task.c` | 网络连接推进、MQTT、OTA 网络请求与网络租约 |
| `tasks/storage_task.c` | 外部 Flash、日志、告警记录、配置持久化与 OTA 暂存 |
| `tasks/ota_task.c` | OTA 流程和启动确认 |
| `tasks/ui_task.c`、`tasks/cli_task.c` | 界面和命令交互，通过配置服务读取配置与提交修改 |
| `services/runtime_support.c` | 快照读取、心跳、功耗锁、临界区及 RTOS 诊断 |
| `services/config_service.c` | 配置互斥与入队、读取、持久化事务、告警确认 |
| `services/ota_io_service.c` | 跨任务请求的静态邮箱、缓冲区、等待和完成通知 |

`firmware/app/rtos/CMakeLists.txt` 是 RTOS 源文件清单，ARM 固件与 Host RTOS 测试共用它，避免测试和固件漏编不同模块。

`app_context_t` 仍是启动时的对象装配容器。此次收窄了任务的直接访问范围，没有将所有共享状态改成单任务所有权：Modbus/CAN 仍共享 Fieldbus 子系统；CLI 仍需要诊断对象；配置与告警共享状态继续由同一配置互斥锁保护。进一步增加模块时，应先扩展对应任务视图或服务接口，不再将整个上下文传入任务。

## 网络连接分步执行

`network_transport` 增加独立、可选的分步连接接口，保留既有同步接口的兼容路径。F407 ESP8266 绑定分步接口，Network 任务逐次推进：

```text
模块初始化 → Wi-Fi 加入 → TCP 连接 → MQTT CONNECT → 等待 CONNACK
        失败/取消 → 重置未完成 AT 会话 → 按退避策略重试
```

每次 AT 响应轮询最多等待 10 ms，命令发送有 100 ms 超时；Wi-Fi 加入和 TCP 连接各自维护绝对截止时间。MQTT CONNACK 允许分片到达。Wi-Fi 不可用时任务仍可返回调度循环，更新心跳并处理控制请求；真正停止运行的任务仍会被 Supervisor 检测。

AT 回复按行边界识别，避免把 `WIFI DISCONNECT` 当作 `CONNECT`。取消未完成命令后，下次初始化先复位模块，避免迟到回复被下一条命令消费。TCP 数据与 AT 文本分流，保留跨读取边界的 `+IPD` 前缀，连续短帧不会无限累积 AT 文本。

这是连接阶段的改造。MQTT 发布、HTTP OTA、Flash 驱动仍使用有超时限制的同步调用；UART 等待过程中按片更新网络 I/O 进度，但这不能替代完整的业务状态机。若要进一步降低 OTA 期间控制请求的响应时间，应继续拆分 HTTP 请求、头部接收、正文读取和关闭阶段。

## 跨任务请求生命周期

可移植的 `request_lifecycle` 统一 OTA Network/Storage 请求的状态、代次、截止时间、结果和取消标记：

```text
IDLE/COMPLETED → QUEUED → RUNNING → COMPLETED
                     └─ 过期或已取消 → COMPLETED（不执行外设操作）
```

调用者负责提交，工作任务负责完成。调用者等待超时不会释放工作任务仍持有的缓冲区；邮箱只能在完成后复用。URL、网络接收和 Storage 写入数据均由静态缓冲区保存，避免调用者退出后留下栈指针。代次检查阻止旧请求完成覆盖新状态，截止时间比较覆盖 tick 回绕。

Network I/O 请求截止时间为 60 秒，Storage 为 5 秒，等待以 100 ms 为片更新 OTA 心跳和轮询取消。取消是协作式的：尚未执行的请求会被拒绝，已经开始的底层 I/O 允许安全完成，不能撤销已经发生的 Flash 写入。

网络租约申请增加 5 秒截止时间，过期申请不会取得租约；申请完成后再次检查截止时间，结果入队失败也释放租约。释放请求即使迟到仍会执行，避免永久占用网络。租约仍按原有单 OTA 调用者与 FIFO 队列约束使用。

## 配置事务

`config_service` 负责 RTOS 同步，`config_transaction_service` 负责可移植事务：

1. 从已提交配置准备修改，只允许一个待提交请求。
2. 在同一把锁内完成准备及入队；队列满时立即拒绝，不留下无人处理的待提交状态。
3. Storage 收到请求后核对请求 ID 和暂存内容，拒绝旧队列项。
4. 应用告警配置、持久化并提交；持久化失败时恢复旧告警配置并拒绝事务。

UI/CLI 通过服务读取配置副本与事务状态；Storage 不再直接操作配置/告警对象。活动告警存在时继续拒绝配置修改。

持久化期间仍持有配置互斥锁，这是当前事务一致性的边界。改成完全异步提交需要另行定义告警发生时如何处理待提交配置，以及掉电后的恢复顺序；此次没有改变这些行为。

## 审阅问题的修复对应

| 原报告问题 | 已实施修改 | 验证范围 |
| --- | --- | --- |
| CAN RX pending 中断风暴 | ISR 屏蔽 pending 通知，任务排空 FIFO 后重开并复查 | ARM 编译、源码/产物检查；真实 IRQ 时序待上板 |
| 网络合法等待超过心跳窗口 | ESP/Wi-Fi/TCP/MQTT 建连分步推进，网络控制等待分片 | Host 15 秒离线与任务停滞用例 |
| USART3 没有连续接收 | Circular DMA + 静态 RX ring，TC/IDLE 重复位置去重；两份 CubeMX 配置同步 | Host 游标、回绕、重复事件、溢出；HAL/DMA 待上板 |
| 大 `+IPD` 载荷失败 | 按长度流式分离 AT 与 TCP 数据，支持任意读取分片 | Host 800/1460/4096 B 帧，二进制载荷与连续短帧 |
| Storage/OTA 错误路径无回执 | 选中的 Queue Set 成员始终消费，离线/唤醒/锁失败完成或拒绝请求；静态邮箱生命周期 | 实际 RTOS 任务源码 + Host FreeRTOS 模拟 |
| `CLOSED` 后缓存数据丢失 | 先交付已缓存 TCP 数据，再报告关闭 | Host 多次读取及尾部关闭 |
| 被拒绝配置污染后续修改 | 单待提交事务、从 active 准备、匹配请求、持久化失败回滚 | Host 子系统、事务与队列满用例 |
| 告警连续恢复计数错误 | 死区或无效样本中断计数，无效样本也中断触发计数 | Host 告警回归 |

## 验证结果

本地工具：Arm GNU GCC 14.3.1、MinGW GCC 16、CMake/Ninja、Python。以下命令均成功：

```powershell
cmake --build --preset host-debug --parallel 4
ctest --preset host-debug
cmake --build --preset debug --parallel 4
cmake --build --preset release --parallel 4
```

- Host CTest：13/13 通过。
- 新增 `runtime_services.host`：请求生命周期、超时/取消/代次/tick 回绕、配置持久化失败与旧请求拒绝。
- 新增 `rtos_runtime.host`：编译全部实际 RTOS 源文件，使用确定性的 FreeRTOS 模拟检查 Queue Set 选择/消费契约、离线和唤醒失败、Flash 锁失败、OTA 超时后邮箱所有权、网络初始化早期错误、过期租约、配置队列满及离线网络心跳。
- 离线网络运行超过 15 秒仍正常刷新模拟看门狗；停止网络任务步骤 2600 ms 后，Supervisor 子系统报告网络任务超时。
- ARM Debug/Release：Bootloader、App A/B 编译通过，向量、分区、静态 RTOS、功耗及各功能路由产物检查通过。
- `git diff --check`：无空白错误。

Host RTOS 模拟不是实际 FreeRTOS 调度器，不能验证优先级反转、真实抢占、中断竞争、DMA 缓存/总线时序或栈峰值。

最终链接资源如下，FLASH 容量按每个 App 的 256 KiB 分区计算，RAM 按 128 KiB 主 SRAM 计算：

| 镜像 | FLASH | FLASH 占比 | RAM | RAM 占比 |
| --- | ---: | ---: | ---: | ---: |
| Debug App A/B | 236960 B | 90.39% | 109160 B | 83.28% |
| Release App A | 209300 B | 79.84% | 109168 B | 83.29% |
| Release App B | 209308 B | 79.84% | 109168 B | 83.29% |

CCM 仍未使用。后续扩大队列、任务栈、LVGL 或网络缓冲区时需检查链接余量；本次没有将 DMA 缓冲区迁移到 CCM。

## CI 与开发板验收

GitHub Actions 增加 Debug/Release ARM 构建矩阵，固定 Arm GNU 14.3.rel1 Linux 工具链、校验 SHA-256、缓存工具链并上传 ELF/BIN/HEX/资源报告。归档校验值取自 [Arm 官方校验文件](https://developer.arm.com/-/media/Files/downloads/gnu/14.3.rel1/binrel/arm-gnu-toolchain-14.3.rel1-x86_64-arm-none-eabi.tar.xz.sha256asc)。本地构建已通过，尚未推送或执行远端 CI。

开发板验收优先覆盖：CAN 连续接收、USART3 DMA HT/TC/IDLE 相邻触发、Wi-Fi 长时间离线及恢复、OTA 下载中取消/断网、W25Q128 唤醒失败、配置保存掉电与活动告警竞争。执行方法沿用 [可靠性与功耗验收](reliability_power_board_validation.md)、[OTA 验收](ota_update_design_and_validation.md)及[深度功耗验收](deep_power_design_and_validation.md)。
