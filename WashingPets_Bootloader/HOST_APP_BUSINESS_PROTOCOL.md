# STM32 APP 与上位机业务交互协议说明

## 1. 【公共说明，无 CMD】文档目的

本文档在现有在线升级协议基础上，定义 APP 正常运行期间与上位机的业务交互协议，覆盖：

1. 设备状态和自动流程实时上报。
2. 自动运行过程中设备与上位机的交互确认。
3. 工艺参数读取、修改、校验和掉电保存。
4. 自动、手动、空闲、暂停、故障等运行状态同步。

在线升级协议继续使用 `HOST_IAP_UPGRADE_PROTOCOL.md`，本协议不修改现有 IAP 命令。

## 2. 【公共说明，无 CMD】当前工程对应关系

| 业务功能 | 当前 APP 模块 |
|---|---|
| USART2 上位机通信任务 | `task_host232.c` |
| 二进制帧解析入口 | `app_iap.c` |
| 自动流程调度 | `auto_process.c` |
| 通用步骤执行器 | `app_flow.c` |
| 上位机流程命令标志 | `auto_device.c` |
| 手动/自动输出模式 | `task_output.c` |
| 工艺参数和 Flash 保存 | `app_param.c` |
| 参数区 | Sector 10，`0x080C0000-0x080DFFFF` |

当前 `task_host232.c` 的 ASCII 百分比输入属于临时调试功能。正式业务协议接入后，建议统一使用本文定义的二进制帧。

## 3. 【公共说明，无 CMD】串口及基础帧

串口参数：

```text
USART2
115200 baud
8 data bits
No parity
1 stop bit
No flow control
```

帧格式沿用 IAP：

```text
55 AA | CMD | SEQ | LEN_L LEN_H | PAYLOAD | CRC16_L CRC16_H
```

| 字段 | 长度 | 说明 |
|---|---:|---|
| 帧头 | 2 | 固定 `55 AA` |
| CMD | 1 | 命令或响应码 |
| SEQ | 1 | 请求序号或主动上报序号 |
| LEN | 2 | Payload 长度，小端 `uint16` |
| PAYLOAD | N | 数据内容 |
| CRC16 | 2 | CRC16-CCITT-FALSE，小端 |

所有 `uint16/int16/uint32/int32/uint64` 均采用小端序。

CRC16 参数：

```text
初值：0xFFFF
多项式：0x1021
输入：CMD + SEQ + LEN_L + LEN_H + PAYLOAD
不包含：55 AA 和 CRC16 本身
```

固件文件 CRC32 和参数 CRC32仍使用：

```text
初值：0xFFFFFFFF
多项式：0xEDB88320
结果异或：0xFFFFFFFF
```

## 4. 【CMD 总览】命令空间

### 4.1 【CMD=0x10~0x25、0x90、0xF0、0xF1】已有 IAP 命令

| 范围 | 用途 |
|---|---|
| `0x10-0x25` | Query、进入升级、升级 Start/Data/End 等 |
| `0x90` | IAP 设备信息响应 |
| `0xF0` | 通用 ACK |
| `0xF1` | 通用 NACK |

### 4.2 【CMD=0x30~0x43】新增业务请求命令

| CMD | 名称 | 方向 | 说明 |
|---:|---|---|---|
| `0x30` | `QUERY_DEVICE_STATUS` | 主机 -> 设备 | 连接或重连时查询当前模式和流程状态 |
| `0x31` | `SET_WORK_MODE` | 主机 -> 设备 | 切换自动/手动模式 |
| `0x32` | `PROCESS_CONTROL` | 主机 -> 设备 | 启动、暂停、继续、停止、复位故障 |
| `0x33` | `PROCESS_INTERACTION` | 主机 -> 设备 | 回复流程中的开门、关门、继续等请求 |
| `0x34` | `MANUAL_OUTPUT_CONTROL` | 主机 -> 设备 | 手动模式下控制指定输出，可选 |
| `0x40` | `GET_PARAM_BLOCK` | 主机 -> 设备 | 读取全局参数或指定配方 |
| `0x41` | `SET_PARAM_BLOCK` | 主机 -> 设备 | 修改全局参数或指定配方 |
| `0x42` | `SAVE_PARAMS` | 主机 -> 设备 | 立即保存参数到 Sector 10 |
| `0x43` | `RESTORE_DEFAULT_PARAMS` | 主机 -> 设备 | 恢复默认参数，建议仅维护权限可用 |

### 4.3 【CMD=0xA1~0xA5、0xF0、0xF1】新增业务响应和主动上报

| CMD | 名称 | 方向 | 说明 |
|---:|---|---|---|
| `0xA0` | 保留 | - | 当前协议不使用，避免与 A1/A4/A5 重复 |
| `0xA1` | `DEVICE_STATUS` | 设备 -> 主机 | 模式或流程状态变化时主动上报；也用于状态查询响应 |
| `0xA2` | `PARAM_BLOCK` | 设备 -> 主机 | 参数读取响应 |
| `0xA3` | `INTERACTION_REQUEST` | 设备 -> 主机 | 流程主动请求上位机确认或操作 |
| `0xA4` | `FAULT_EVENT` | 设备 -> 主机 | 仅上报故障发生、更新和解除事件 |
| `0xA5` | `TELEMETRY` | 设备 -> 主机 | 仅周期上报温度、湿度、氧气、噪声等传感器测量值 |
| `0xF0` | `ACK` | 设备 -> 主机 | 命令执行成功 |
| `0xF1` | `NACK` | 设备 -> 主机 | 命令拒绝或执行失败 |

业务上报按职责拆分：A1 回答“设备正在做什么”，A4 回答“设备发生了什么故障”，A5 回答“传感器当前测量值是多少”。三类 Payload 不重复携带彼此的数据。

## 5. 【公共说明，适用于全部 CMD】SEQ 和通信规则

### 5.1 【全部主机请求 CMD】主机请求规则

1. 主机请求的 `SEQ` 从 0 开始递增，溢出后回到 0。
2. 设备响应必须回显请求的 `SEQ`。
3. 主机通过 `CMD + SEQ` 匹配请求和响应。
4. 同一请求超时重发时，应保持原 `SEQ` 和原 Payload 不变。

### 5.2 【CMD=0xA1/0xA3/0xA4/0xA5】设备主动上报规则

1. 设备主动上报使用独立的发送序号，从 0 开始递增。
2. `DEVICE_STATUS` 在模式、运行状态、阶段或步骤变化时立即发送，可每 1000 ms 补发一次作为在线心跳。
3. `TELEMETRY` 按固定周期发送，建议周期为 500~1000 ms。
4. 状态和遥测帧不要求主机逐帧 ACK。
5. `INTERACTION_REQUEST` 和严重 `FAULT_EVENT` 建议主机回复 ACK；设备超时可重复上报相同 `request_id/event_id`。
6. 主机必须按 `request_id/event_id` 去重，不能仅按 SEQ 去重。

### 5.3 【全部 CMD】推荐超时

| 操作 | 超时 |
|---|---:|
| 状态查询/模式切换/流程命令 | 2 秒 |
| 参数读取/修改 | 3 秒 |
| 参数 Flash 保存 | 10 秒 |
| 流程交互等待 | 设备持续等待匹配的 CMD=0x33；停止、故障或模式切换时取消等待 |

## 6. 【CMD=0xF0/0xF1】通用 ACK/NACK

### 6.1 【CMD=0xF0】ACK 成功响应

Payload 固定 6 字节：

| 偏移 | 长度 | 类型 | 说明 |
|---:|---:|---|---|
| 0 | 1 | `uint8` | 被确认的请求 CMD |
| 1 | 1 | `uint8` | 状态，成功为 0 |
| 2 | 4 | `uint32` | detail，按具体命令解释 |

业务命令中 `detail` 建议解释：

| 命令 | detail |
|---|---|
| `SET_WORK_MODE` | 生效后的模式 |
| `PROCESS_CONTROL` | 生效后的运行状态 |
| `PROCESS_INTERACTION` | 已接受的 `request_id` |
| `SET_PARAM_BLOCK` | 新参数 revision |
| `SAVE_PARAMS` | 已保存的 revision |

ACK 收发实例：主机设置自动模式，设备执行成功。

```text
主机发送 CMD=0x31，SEQ=1：
55 AA 31 01 02 00 02 00 67 45
55 AA 31 02 02 00 01 00 E6 FE

设备回复 CMD=0xF0，SEQ=1：
55 AA F0 01 06 00 31 00 02 00 00 00 20 36
```

中文说明：ACK Payload 中 `31 00` 表示 CMD `0x31` 成功，`02 00 00 00` 表示实际模式为自动模式 2。

### 6.2 【CMD=0xF1】NACK 失败响应

Payload 固定 8 字节，兼容现有 IAP：

| 偏移 | 长度 | 类型 | 说明 |
|---:|---:|---|---|
| 0 | 1 | `uint8` | 被拒绝的请求 CMD |
| 1 | 1 | `uint8` | 错误码 |
| 2 | 1 | `uint8` | 设备期望的 SEQ，业务协议可填当前请求 SEQ |
| 3 | 1 | `uint8` | 保留，填 0 |
| 4 | 4 | `uint32` | detail，例如参数 ID、request_id 或 fault_code |

新增业务错误码：

| 值 | 名称 | 说明 |
|---:|---|---|
| `0x11` | `INVALID_MODE` | 当前模式不允许该操作 |
| `0x12` | `INVALID_STATE` | 当前运行状态不允许该操作 |
| `0x13` | `INVALID_PARAM_GROUP` | 参数组不存在 |
| `0x14` | `INVALID_PARAM_VALUE` | 参数值越界或组合非法 |
| `0x15` | `PARAM_VERSION_MISMATCH` | 上位机参数结构版本不匹配 |
| `0x16` | `PARAM_REVISION_CONFLICT` | 参数已被其他请求修改，应重新读取 |
| `0x17` | `SAVE_FAILED` | Flash 擦写或回读失败 |
| `0x18` | `REQUEST_ID_MISMATCH` | 回复的交互请求 ID 已失效 |
| `0x19` | `INTERLOCK_REJECTED` | 安全互锁拒绝操作 |
| `0x1A` | `FAULT_ACTIVE` | 故障未解除，禁止启动或切换模式 |

NACK 回复实例：设备拒绝模式切换。

```text
设备回复 CMD=0xF1，SEQ=9：
55 AA F1 09 08 00 31 11 09 00 02 00 00 00 8E B2
```

中文说明：被拒绝的是 CMD `0x31`，错误码 `0x11=INVALID_MODE`，detail 为 2。

## 7. 【CMD=0x30/0x31/0x32/0xA1】设备模式和运行状态

### 7.1 【CMD=0x31 设置，CMD=0xA1 上报】工作模式 `work_mode`

| 值 | 名称 | 说明 |
|---:|---|---|
| 0 | `IDLE` | 空闲/待机，不接受危险手动输出 |
| 1 | `MANUAL` | 手动模式，上位机可发送允许的手动输出命令 |
| 2 | `AUTO` | 自动模式；从 MANUAL 成功切换到 AUTO 后，设备立即开始完整自动流程 |
| 3 | `SERVICE` | 维护/单阶段测试模式 |
| 4 | `FAULT` | 故障锁定模式 |
| 5 | `BOOTLOADER` | 仅用于上位机显示，APP 正常业务帧不会处于此值 |

当前 `TaskOutput_SetAutoMode(1/0)` 只有自动/手动二值。实现本协议时建议新增统一 `AppRuntimeState`，再由它驱动 `TaskOutput_SetAutoMode()`。

### 7.2 【CMD=0x32 控制，CMD=0xA1 上报】运行状态 `run_state`

| 值 | 名称 | 说明 |
|---:|---|---|
| 0 | `STOPPED` | 未运行 |
| 1 | `STARTING` | 正在启动流程 |
| 2 | `RUNNING` | 正常执行 |
| 3 | `WAIT_HOST` | 等待上位机交互 |
| 4 | `PAUSED` | 已暂停 |
| 5 | `STOPPING` | 正在安全停机 |
| 6 | `FINISHED` | 本次流程完成 |
| 7 | `FAULTED` | 流程失败或安全故障 |

### 7.3 【CMD=0x32 控制，CMD=0xA1 上报】过程类型 `process_type`

| 值 | 名称 |
|---:|---|
| 0 | 无流程 |
| 1 | 完整自动流程 |
| 2 | 单阶段测试 |
| 3 | 自清洗流程 |
| 4 | 手动操作 |

## 8. 【CMD=0x30/0xA1】查询和主动上报设备状态

### 8.1 【CMD=0x30】`QUERY_DEVICE_STATUS` 主机查询状态

主机 Payload：空。

设备响应：立即发送 `DEVICE_STATUS 0xA1`，SEQ 回显请求 SEQ。

发送实例：

```text
主机发送 CMD=0x30，SEQ=0，Payload 为空：
55 AA 30 00 00 00 29 A8
```

中文说明：设备收到后必须回复同一个 SEQ 的 CMD `0xA1`。平时不需要上位机持续轮询，A1 由设备主动发送。若当前存在故障，设备紧接着主动发送一帧 A4，便于上位机重连后恢复故障显示。

### 8.2 【CMD=0xA1】`DEVICE_STATUS` 状态响应/主动上报

设备应在以下情况发送：

1. 收到状态查询时立即响应。
2. 工作模式、运行状态、阶段或步骤发生变化时立即主动上报。

Payload 固定 12 字节，只传递运行任务需要的核心状态：

| 偏移 | 长度 | 类型 | 说明 |
|---:|---:|---|---|
| 0 | 1 | `uint8` | `work_mode`：设备工作模式，空闲/手动/自动/维护/故障 |
| 1 | 1 | `uint8` | `run_state`：当前流程状态，停止/启动/运行/等待/暂停/完成/故障 |
| 2 | 1 | `uint8` | `process_type`：当前过程类型，完整自动/单阶段测试/自清洗/手动 |
| 3 | 1 | `uint8` | `stage_id`：当前自动流程阶段编号，例如 3 表示冲洗 |
| 4 | 2 | `uint16` | `step_id`：当前阶段内部的步骤编号 |
| 6 | 1 | `uint8` | `step_state`：步骤状态，进入/运行/等待/完成/失败 |
| 7 | 1 | `uint8` | `recipe_id`：当前流程使用的配方编号，0~3；无配方时填 `0xFF` |
| 8 | 2 | `uint16` | `progress_x10`：总进度，0~1000，450 表示 45.0% |
| 10 | 1 | `uint8` | `result`：流程或步骤结果，0=无错误，其他值为结束原因或错误码 |
| 11 | 1 | `uint8` | `reserved`：保留字段，当前固定填 0 |

回复实例：

```text
设备回复或主动发送 CMD=0xA1，SEQ=0，LEN=12：
55 AA A1 00 0C 00 02 02 01 03 08 00 01 00 C2 01 00 00 5B 4C
```

中文说明：设备处于自动模式，正在运行完整自动流程，使用配方 0，当前处于冲洗阶段步骤 8，步骤正在运行，总进度 45.0%，当前结果为正常。

## 9. 【CMD=0x31/0x32】模式和流程控制

### 9.1 【CMD=0x31】`SET_WORK_MODE` 设置工作模式

Payload：

| 偏移 | 长度 | 类型 | 字段名及中文说明 |
|---:|---:|---|---|
| 0 | 1 | `uint8` | `target_work_mode`：要求设备切换到的目标工作模式；0=空闲，1=手动，2=自动，3=维护，4=故障模式 |
| 1 | 1 | `uint8` | `reserved`：保留字段，当前固定填 0 |

规则：

1. 自动流程运行中禁止直接切手动，应先安全停止。
2. 急停或严重故障存在时禁止进入 AUTO。
3. 进入 MANUAL 时危险输出先回安全状态。
4. 设备上电后的默认模式为 MANUAL，不会自行启动自动流程。
5. 从 MANUAL 成功切换到 AUTO 时，设备调用 `AutoProcess_RequestFull()`，从入舱相关阶段开始执行。
6. 此时尚未选择配方；入舱完成后由设备通过 CMD=0xA3 请求上位机下发配方。
7. 成功返回 ACK，detail 为实际生效模式。

收发实例：

```text
主机发送 CMD=0x31，设置 work_mode=2 自动模式：
55 AA 31 01 02 00 02 00 67 45

设备回复 ACK：
55 AA F0 01 06 00 31 00 02 00 00 00 20 36
```

中文说明：设备完成模式切换，ACK detail=2。

随后设备调用 `AutoProcess_RequestFull()` 并主动发送 A1。该命令只负责进入自动模式和启动前半段流程，不携带配方编号。

### 9.2 【CMD=0x32】`PROCESS_CONTROL` 启停/暂停/继续流程

Payload 固定 4 字节，用于自动流程开始后的暂停、继续、停止、单阶段测试、自清洗和故障复位：

| 偏移 | 长度 | 类型 | 说明 |
|---:|---:|---|---|
| 0 | 1 | `uint8` | `action`：流程控制动作，暂停/继续/停止/单阶段测试/自清洗/故障复位 |
| 1 | 1 | `uint8` | `stage_id`：目标阶段编号，仅单阶段测试使用 |
| 2 | 2 | `uint16` | `reserved`：保留字段，当前固定填 0 |

action：

| 值 | 名称 | 对应 APP 接口 |
|---:|---|---|
| 1 | 保留，不使用 | 完整流程由 CMD=0x31 切换到 AUTO 时自动启动 |
| 2 | 暂停 | `AutoDevice_SetPaused(1)` |
| 3 | 继续 | `AutoDevice_SetPaused(0)` |
| 4 | 安全停止 | `AutoDevice_SetStopRequested(1)` |
| 5 | 单阶段测试 | `AutoProcess_RequestStageTest(stage_id)` |
| 6 | 启动自清洗 | `TaskClean_Request()` |
| 7 | 复位故障 | 待新增统一故障复位接口 |

收发实例：暂停当前自动流程。

```text
主机发送 CMD=0x32，action=2，stage_id=0：
55 AA 32 02 04 00 02 00 00 00 6E 73

设备回复 ACK，detail=4 表示已经进入 PAUSED：
55 AA F0 02 06 00 32 00 04 00 00 00 1D F2
```

中文说明：启动完整自动流程不再使用 CMD=0x32，也不在启动时选择配方。

## 10. 【CMD=0xA1】自动流程阶段和主动上报时机

### 10.1 【CMD=0xA1 字段定义】阶段编号

| stage_id | 名称 | 当前步骤数 |
|---:|---|---:|
| 0 | 入舱环境准备 `ENTRY_ENV` | 14 |
| 1 | 入舱 `ENTRY` | 20 |
| 2 | 洗前准备 `WASH_READY` | 18 |
| 3 | 冲洗 `SPRAY` | 16 |
| 4 | 沐浴 `BATH` | 12 |
| 5 | 清洗 `CLEAN` | 12 |
| 6 | 吹干 `DRY` | 11 |
| 7 | 出舱准备 `EXIT_PREPARE` | 4 |
| 8 | 出舱 `EXIT` | 12 |
| `0x80` | 自清洗 `SELF_CLEAN` | 25 |

`step_id` 使用各 `auto_stage_*.c` 中 `AUTO_STEP()` 的数字 ID。上位机显示文字建议按 `stage_id + step_id` 建立本地字典，不在每帧中传中文字符串。

### 10.2 【CMD=0xA1】`DEVICE_STATUS` 主动上报时机

发送时机：

1. 工作模式发生变化。
2. 流程开始、暂停、继续、结束或失败。
3. `stage_id` 或 `step_id` 发生变化。
4. 进入或退出等待上位机状态。

Payload 统一使用 8.2 节定义的 12 字节 `DEVICE_STATUS`，不再额外定义流程状态帧。

step_state：

| 值 | 说明 |
|---:|---|
| 0 | 进入步骤 |
| 1 | 步骤运行中 |
| 2 | 等待传感器/条件 |
| 3 | 等待上位机 |
| 4 | 步骤完成 |
| 5 | 步骤失败 |
| 6 | 流程完成 |

建议在 `AppFlow_Process()` 首次进入 ACTION 时调用统一上报钩子，而不是发送调试 ASCII 文本。主动发送实例与 8.2 节相同，仅 SEQ 使用设备主动发送序号。

## 11. 【CMD=0xA3/0x33】自动流程中的上位机交互

### 11.1 【CMD=0xA3 请求，CMD=0x33 回复】交互类型

| interaction_type | 名称 | 当前 AutoCommand |
|---:|---|---|
| 1 | 打开上舱门 | `AUTO_COMMAND_OPEN_UPPER_DOOR` |
| 2 | 打开下舱门 | `AUTO_COMMAND_OPEN_LOWER_DOOR` |
| 3 | 关闭上舱门 | `AUTO_COMMAND_CLOSE_UPPER_DOOR` |
| 4 | 关闭下舱门 | `AUTO_COMMAND_CLOSE_LOWER_DOOR` |
| 5 | 继续流程 | `AUTO_COMMAND_CONTINUE` |
| 6 | 选择本次运行配方 | 入舱完成后设备请求上位机返回 `recipe_id` |
| 7 | 取消本次操作 | 预留 |

### 11.2 【CMD=0xA3】`INTERACTION_REQUEST` 设备请求上位机操作

当流程进入等待上位机步骤时，设备主动发送固定 8 字节 Payload：

| 偏移 | 长度 | 类型 | 说明 |
|---:|---:|---|---|
| 0 | 4 | `uint32` | `request_id`：本次上位机交互请求编号，每次请求递增 |
| 4 | 1 | `uint8` | `interaction_type`：要求上位机执行的操作，例如开上门或继续 |
| 5 | 1 | `uint8` | `stage_id`：发起请求时所在的流程阶段 |
| 6 | 2 | `uint16` | `step_id`：发起请求时所在的步骤 |

设备应每 1000 ms 重发未完成的交互请求，直到收到匹配回复，或因停止、故障、模式切换而取消当前流程。

主动请求实例：

```text
设备发送 CMD=0xA3，request_id=1，请求打开上舱门：
55 AA A3 0B 08 00 01 00 00 00 01 01 01 00 3E E0
```

中文说明：设备正处于入舱阶段 1、步骤 1，等待上位机确认打开上舱门。

### 11.3 【CMD=0x33】`PROCESS_INTERACTION` 主机回复交互请求

主机回复：

| 偏移 | 长度 | 类型 | 说明 |
|---:|---:|---|---|
| 0 | 4 | `uint32` | `request_id`：被回复的请求编号，必须与设备当前请求一致 |
| 4 | 1 | `uint8` | `interaction_type`：被确认的交互类型，必须与设备请求一致 |
| 5 | 1 | `uint8` | `result`：上位机处理结果，1=确认执行，0=拒绝或取消 |
| 6 | 2 | `uint16` | `value`：交互附加值；选择配方时填写 `recipe_id`（0~3），其他交互填 0 |

设备校验 `request_id + interaction_type + 当前 stage/step`。门和继续类交互调用对应 `AutoDevice_SetCommand()`；选择配方交互校验 `value` 后设置本次运行配方并继续流程。

重要：不能只设置一个永久命令标志。使用 `request_id` 可以防止延迟到达的旧“开门”命令被下一轮流程误用。

正式联调时必须设置：

```c
#define AUTO_DEVICE_HOST_COMMAND_BYPASS 0U
```

收发实例：

```text
主机发送 CMD=0x33，确认 request_id=1、interaction_type=1：
55 AA 33 03 08 00 01 00 00 00 01 01 00 00 70 EC

设备回复 ACK，detail=request_id=1：
55 AA F0 03 06 00 33 00 01 00 00 00 DB E0
```

中文说明：设备校验 request_id 后调用 `AutoDevice_SetCommand(AUTO_COMMAND_OPEN_UPPER_DOOR)`，流程进入上门解锁步骤。

选择配方收发实例：入舱完成后选择配方 2。

```text
设备主动请求配方，CMD=0xA3，SEQ=12，request_id=2，interaction_type=6：
55 AA A3 0C 08 00 02 00 00 00 06 02 01 00 8B 09

主机回复 CMD=0x33，SEQ=4，result=1，value=2：
55 AA 33 04 08 00 02 00 00 00 06 01 02 00 F7 3A

设备接受配方后回复 ACK，detail=request_id=2：
55 AA F0 04 06 00 33 00 02 00 00 00 AC CA
```

中文说明：设备在入舱完成后暂停后续工艺并请求配方。上位机选择配方 2，设备读取参数区中配方 2 的数据，然后继续执行洗前准备、冲洗、沐浴、清洗和吹干等后续流程。配方选择只对本次运行生效，不写入 Flash；只有修改配方参数时才执行 CMD=0x41/0x42 保存。

## 12. 【CMD=0x40/0x41/0x42/0x43/0xA2】参数协议

### 12.1 【参数类 CMD 公共规则】设计原则

1. 不直接发送 `AppParam_t` 的裸内存，避免编译器 padding、字节对齐和结构升级问题。
2. 使用固定参数块和明确偏移。
3. 参数修改先进入 RAM，校验通过后 ACK。
4. 主机显式发送 `SAVE_PARAMS` 后写入 Sector 10。
5. 使用 revision 防止多个界面或请求相互覆盖。

### 12.2 【CMD=0x40/0x41/0xA2 字段定义】参数组

| group | 内容 | index |
|---:|---|---|
| 0 | 全局参数 | 固定 0 |
| 1 | 工艺配方 | `0~3` |

配方 ID：

| index | 配方 |
|---:|---|
| 0 | 短毛小型 |
| 1 | 短毛大型 |
| 2 | 长毛小型 |
| 3 | 长毛大型 |

### 12.3 【CMD=0x40】`GET_PARAM_BLOCK` 读取参数

Payload：

| 偏移 | 长度 | 类型 | 字段名及中文说明 |
|---:|---:|---|---|
| 0 | 1 | `uint8` | `group`：要读取的参数组；0=全局参数，1=工艺配方参数 |
| 1 | 1 | `uint8` | `index`：参数组内的索引；读取全局参数时固定填 0，读取配方时填配方编号 0~3 |

设备响应 `PARAM_BLOCK 0xA2`。

发送实例：读取全局参数 group=0、index=0。

```text
主机发送 CMD=0x40，SEQ=5：
55 AA 40 05 02 00 00 00 3F AA
```

中文说明：Payload `00 00` 分别表示全局参数组和索引 0。

### 12.4 【CMD=0xA2】`PARAM_BLOCK` 参数读取响应

Payload 头：

| 偏移 | 长度 | 类型 | 说明 |
|---:|---:|---|---|
| 0 | 1 | `uint8` | `group`：参数组编号，0=全局参数，1=工艺配方 |
| 1 | 1 | `uint8` | `index`：参数组内索引；全局参数固定 0，配方为 0~3 |
| 2 | 2 | `uint16` | `protocol_version`：参数数据结构版本，当前为 1；上位机应检查版本后再解析参数数据 |
| 4 | 4 | `uint32` | `revision`：当前参数修订号，用于防止旧页面覆盖新参数 |
| 8 | 2 | `uint16` | `data_length`：后续参数数据区的字节数 |
| 10 | N | `uint8[]` | `data`：参数数据区；具体字段布局由 `group` 决定，见后面的全局参数表和配方参数表 |

回复实例：

```text
设备回复 CMD=0xA2，SEQ=5，revision=2，data_length=16：
55 AA A2 05 1A 00 00 00 01 00 02 00 00 00 10 00 32 01
05 00 E8 03 00 00 60 EA 00 00 00 00 00 00 0D DC
```

中文说明：默认百分比 50%，Modbus 站号 1，PWM 频率 5，自动周期 1000ms，自清洗周期 60000ms；末尾 4 字节为保留字段。

### 12.5 【CMD=0xA2/0x41，group=0】全局参数块字段

| 数据偏移 | 长度 | 类型 | 当前字段 |
|---:|---:|---|---|
| 0 | 1 | `uint8` | `default_percent`：设备上电后的默认可控硅输出百分比，范围 0~100% |
| 1 | 1 | `uint8` | `thyristor_station`：可控硅控制器的 Modbus 站号，范围 1~247 |
| 2 | 2 | `uint16` | `pwm_frequency`：预留的 PWM 频率参数；当前代码尚未实际使用 |
| 4 | 4 | `uint32` | `auto_period_ms`：自动流程调度周期配置，单位 ms；当前属于预留参数 |
| 8 | 4 | `uint32` | `clean_period_ms`：计划执行自清洗的周期，单位 ms；当前默认 60000ms |
| 12 | 4 | `uint8[4]` | `reserved`：保留字段，当前固定填 0；本次运行配方不属于掉电保存参数 |

### 12.6 【CMD=0xA2/0x41，group=1】配方参数块字段

| 数据偏移 | 长度 | 类型 | 字段 |
|---:|---:|---|---|
| 0 | 2 | `int16` | `tank_target_temp_x10`：水箱目标温度，单位 0.1℃；400 表示 40.0℃ |
| 2 | 2 | `int16` | `pipe_target_temp_x10`：喷淋管路目标出水温度，单位 0.1℃；380 表示 38.0℃ |
| 4 | 2 | `uint16` | `temp_tolerance_x10`：允许的温度上下偏差，单位 0.1℃；20 表示 ±2.0℃ |
| 6 | 1 | `uint8` | `spray_valve_percent`：冲洗阶段 485 流量控制阀目标开度，范围 0~100% |
| 7 | 1 | `uint8` | `bath_valve_percent`：沐浴阶段 485 流量控制阀目标开度，范围 0~100% |
| 8 | 1 | `uint8` | `clean_valve_percent`：清洗阶段 485 流量控制阀目标开度，范围 0~100% |
| 9 | 1 | `uint8` | `dry_fan_percent`：吹干阶段三台风机的目标功率，范围 0~100% |
| 10 | 1 | `uint8` | `dry_heater_percent`：吹干阶段三路加热管的目标功率，范围 0~100% |
| 11 | 1 | `uint8` | `reserved`：保留字段，当前固定填 0 |
| 12 | 4 | `uint32` | `spray_time_ms`：冲洗阶段持续时间，单位 ms；60000 表示 60 秒 |
| 16 | 4 | `uint32` | `bath_time_ms`：沐浴阶段持续时间，单位 ms；从投液完成后开始计时 |
| 20 | 4 | `uint32` | `clean_time_ms`：清水清洗阶段持续时间，单位 ms |
| 24 | 4 | `uint32` | `liquid_feed_time_ms`：蠕动泵投放沐浴液的持续时间，单位 ms |
| 28 | 4 | `uint32` | `pipe_flush_time_ms`：关闭投液后使用清水冲洗药液管路的时间，单位 ms |
| 32 | 4 | `uint32` | `dry_time_ms`：热风吹干阶段持续时间，单位 ms |
| 36 | 4 | `uint32` | `fan_prestart_time_ms`：开启加热管前，风机预先运行并建立稳定风量的时间，单位 ms |
| 40 | 4 | `uint32` | `heater_cooldown_time_ms`：关闭加热管后继续吹冷风带走余热的时间，单位 ms |
| 44 | 4 | `uint32` | `fill_timeout_ms`：水箱补水允许的最长时间，超时进入故障，单位 ms |
| 48 | 4 | `uint32` | `heat_timeout_ms`：水箱加热达到目标温度允许的最长时间，单位 ms |
| 52 | 4 | `uint32` | `cool_timeout_ms`：水箱置换降温达到目标温度允许的最长时间，单位 ms |
| 56 | 4 | `uint32` | `pipe_timeout_ms`：排出管路冷水并达到目标管温允许的最长时间，单位 ms |

### 12.7 【CMD=0x41】`SET_PARAM_BLOCK` 修改参数

Payload 与 `PARAM_BLOCK` 一致。`revision` 填主机读取到的旧 revision。

设备处理：

1. 检查参数协议版本、group、index、data_length。
2. 检查 revision 是否仍一致。
3. 校验每个值的上下限和组合关系。
4. 自动流程运行期间原则上禁止修改当前活动配方；也可接受修改但规定下次流程生效。
5. 更新 RAM 参数。
6. revision 加 1。
7. 返回 ACK，detail 为新 revision。

收发实例：主机基于 revision=2 修改全局参数。

```text
主机发送 CMD=0x41，SEQ=6：
55 AA 41 06 1A 00 00 00 01 00 02 00 00 00 10 00 32 01
05 00 E8 03 00 00 60 EA 00 00 00 00 00 00 60 5D

设备回复 ACK，新 revision=3：
55 AA F0 06 06 00 41 00 03 00 00 00 A3 B4
```

中文说明：参数已更新到 RAM，但还需要 CMD `0x42` 才能确认立即写入 Flash。

### 12.8 【CMD=0x42】`SAVE_PARAMS` 保存参数到 Flash

Payload：

| 偏移 | 长度 | 类型 | 说明 |
|---:|---:|---|---|
| 0 | 4 | `uint32` | `revision`：要求写入 Flash 的参数修订号，必须等于最近一次 CMD=0x41 修改成功后设备 ACK 返回的新修订号 |

设备调用 `AppParam_SaveNow()`。Flash 写入和回读成功后返回 ACK；失败返回 `SAVE_FAILED`。

收发实例：保存 revision=3。

```text
主机发送 CMD=0x42，SEQ=7：
55 AA 42 07 04 00 03 00 00 00 71 CC

设备擦写 Sector 10 成功后回复 ACK：
55 AA F0 07 06 00 42 00 03 00 00 00 60 91
```

中文说明：只有收到此 ACK，上位机才显示“参数已保存”；如果返回 `SAVE_FAILED`，参数可能只在 RAM 中有效。

建议上位机操作顺序：

```text
GET_PARAM_BLOCK
显示并编辑
SET_PARAM_BLOCK
收到新 revision
SAVE_PARAMS(new revision)
重新 GET_PARAM_BLOCK 校验显示
```

### 12.9 【CMD=0x43】`RESTORE_DEFAULT_PARAMS` 恢复默认参数

为避免误操作，Payload 固定为 ASCII `DFLT`，十六进制为 `44 46 4C 54`。

```text
主机发送 CMD=0x43，SEQ=8：
55 AA 43 08 04 00 44 46 4C 54 27 C3

设备恢复默认值后回复 ACK，新 revision=4：
55 AA F0 08 06 00 43 00 04 00 00 00 B9 1D
```

中文说明：设备恢复 RAM 默认值并生成新 revision。建议上位机随后发送 CMD `0x42` 保存到 Flash。

### 12.10 【CMD=0x41 参数校验规则】参数范围建议

以下范围应由产品最终确认，协议实现至少需要校验：

| 字段 | 建议范围 |
|---|---|
| 温度 | `0.0~80.0 摄氏度` |
| 温度容差 | `0.1~20.0 摄氏度` |
| 百分比 | `0~100` |
| Modbus 站号 | `1~247` |
| 普通流程时间 | `0~3600000 ms` |
| 超时时间 | `1000~3600000 ms` |

## 13. 【CMD=0xA4】故障协议

### 13.1 【CMD=0xA4 字段定义】故障位图

| bit | 故障 |
|---:|---|
| 0 | 急停触发 |
| 1 | 上舱门/门锁互锁异常 |
| 2 | 下舱门/门锁互锁异常 |
| 3 | 上光幕遮挡 |
| 4 | 下光幕遮挡 |
| 5 | 水箱温度传感器异常 |
| 6 | 管路温度传感器异常 |
| 7 | 485 流量控制阀通信或控制异常 |
| 8 | 水箱液位异常 |
| 9 | 沐浴液低液位 |
| 10 | 其他药液低液位 |
| 11 | 氧气异常 |
| 12 | 温度严重异常 |
| 13 | 湿度严重异常 |
| 14 | RS485 通信异常 |
| 15 | 参数 CRC/Flash 保存异常 |
| 16 | 自动流程步骤超时 |
| 17 | 输出互锁拒绝 |
| 18~31 | 预留 |

### 13.2 【CMD=0xA4】`FAULT_EVENT` 故障主动上报

Payload 固定 12 字节，只传递故障处理需要的数据：

| 偏移 | 长度 | 类型 | 说明 |
|---:|---:|---|---|
| 0 | 4 | `uint32` | `event_id`：故障事件编号，每次新故障事件递增，用于上位机去重 |
| 4 | 1 | `uint8` | `event_type`：事件类型，1=故障发生，2=故障信息更新，3=故障解除 |
| 5 | 1 | `uint8` | `severity`：故障等级，1=提示，2=警告，3=停机，4=急停 |
| 6 | 2 | `uint16` | `primary_fault_code`：本次事件的主要故障码 |
| 8 | 4 | `uint32` | `fault_bitmap`：设备当前全部有效故障的位图；为 0 表示故障已全部解除 |

严重故障发生后：

1. 立即关闭危险输出。
2. `work_mode=FAULT`，`run_state=FAULTED`。
3. 立即发送 `FAULT_EVENT`。
4. 主动发送 `DEVICE_STATUS`，其中 `work_mode=FAULT`、`run_state=FAULTED`。
5. 故障条件解除后发送 `event_type=3` 的 `FAULT_EVENT`；收到复位命令后才能离开 FAULT。

主动上报实例：

```text
设备发送 CMD=0xA4，SEQ=12，LEN=12：
55 AA A4 0C 0C 00 01 00 00 00 01 03 01 00 01 00 00 00 B4 A1
```

中文说明：event_id=1，发生三级停机故障，primary_fault_code=1，fault_bitmap bit0=1，表示急停故障当前有效。

## 14. 【CMD=0xA5】实时遥测 `TELEMETRY`

建议每 500 ms 或 1000 ms 上报一次，Payload 固定 14 字节：

| 偏移 | 长度 | 类型 | 说明 |
|---:|---:|---|---|
| 0 | 2 | `int16` | `cabin_temperature_x10`：舱内空气温度，单位 0.1℃ |
| 2 | 2 | `uint16` | `cabin_humidity_x10`：舱内相对湿度，单位 0.1%RH |
| 4 | 2 | `uint16` | `oxygen_x10`：舱内氧气浓度，单位 0.1%VOL；210 表示 21.0% |
| 6 | 2 | `uint16` | `noise_x10`：舱内噪音值，单位 0.1dB；具体单位以后按传感器协议确认 |
| 8 | 2 | `int16` | `tank_temperature_x10`：水箱温度，单位 0.1℃ |
| 10 | 2 | `int16` | `pipe_temperature_x10`：喷淋管路出水温度，单位 0.1℃ |
| 12 | 2 | `uint16` | `valid_flags`：上述 6 个传感器数据的有效位图，bit0~bit5 依次对应表中字段；1=有效 |

无效传感器值仍可发送最近值，但必须将对应 valid bit 清零。

主动上报实例：

```text
设备发送 CMD=0xA5，SEQ=13，LEN=14：
55 AA A5 0D 0E 00 FA 00 26 02 D2 00 C2 01 90 01 7C 01
3F 00 49 06
```

中文说明：舱温 25.0℃、湿度 55.0%、氧气 21.0%、噪音 45.0dB、水箱 40.0℃、管路 38.0℃，`valid_flags=0x003F` 表示 6 项传感器数据全部有效。设备没有流量计，因此不上传实际流量；485 流量控制阀按配方中的目标开度控制。

## 15. 【CMD=0x34】手动输出控制，可选

`MANUAL_OUTPUT_CONTROL 0x34` 仅在 MANUAL 或 SERVICE 模式接受。

Payload：

| 偏移 | 长度 | 类型 | 说明 |
|---:|---:|---|---|
| 0 | 2 | `uint16` | `output_id`：要控制的硬件输出编号，对应 `AutoOutputPoint` 枚举 |
| 2 | 1 | `uint8` | `action`：输出动作，0=关闭，1=持续打开，2=按指定时间点动 |
| 3 | 1 | `uint8` | 保留 |
| 4 | 4 | `uint32` | `pulse_time_ms`：点动保持时间，单位 ms；非点动命令填 0 |

设备必须先执行门、液位、急停、温度等安全互锁。不能因处于手动模式而绕过安全保护。

收发实例：手动打开 output_id=22（当前枚举中的增压泵）。

```text
主机发送 CMD=0x34，SEQ=4：
55 AA 34 04 08 00 16 00 01 00 00 00 00 00 0E 33

设备互锁检查通过后回复 ACK，detail=22：
55 AA F0 04 06 00 34 00 16 00 00 00 BB D3
```
主机发送 CMD=0x34，SEQ=3：输出PF7
55 AA 34 03 08 00 20 00 01 00 00 00 00 00 A4 DC
```
主机发送 CMD=0x34，SEQ=3：关闭PF7
55 AA 34 03 08 00 20 00 00 00 00 00 00 00 04 99
中文说明：该命令只允许在 MANUAL/SERVICE 模式执行；互锁不满足时回复 NACK `INTERLOCK_REJECTED`。

## 16. 【多 CMD 组合流程】上位机推荐工作流程

### 16.1 【CMD=0x10/0x30/0x40】建立连接实例

```text
打开串口
-> QUERY_INFO 识别 APP/Bootloader
-> 如果 run_mode=APP，发送 QUERY_DEVICE_STATUS
-> 分别读取全局参数和 4 套配方
-> 开始接收 A1 状态、A3 交互、A4 故障和 A5 遥测上报
```

### 16.2 【CMD=0x31/0x32/0xA1/0xA3/0x33/0xA4/0xA5】启动自动流程实例

```text
SET_WORK_MODE(AUTO)
-> 设备立即调用 AutoProcess_RequestFull()，开始入舱相关流程
-> 接收设备主动上报的 DEVICE_STATUS
-> 入舱完成后收到 INTERACTION_REQUEST(type=SELECT_RECIPE)
-> 上位机选择配方并发送 PROCESS_INTERACTION(request_id, type, result=1, value=recipe_id)
-> 设备从对应参数区读取配方，继续执行后续自动流程
-> 其他等待步骤仍通过 INTERACTION_REQUEST/PROCESS_INTERACTION 交互
-> 持续显示 DEVICE_STATUS 和 TELEMETRY
-> 流程完成或故障
```

### 16.3 【CMD=0x40/0xA2/0x41/0x42】修改参数实例

```text
GET_PARAM_BLOCK
-> 用户编辑
-> 本地范围检查
-> SET_PARAM_BLOCK(old revision)
-> 设备校验并返回 new revision
-> SAVE_PARAMS(new revision)
-> 再次 GET_PARAM_BLOCK 核对
```

## 17. 【实现说明，无单独 CMD】APP 侧需要新增的实现

当前工程已有流程和参数基础，但本文业务协议还需要代码接入：

1. 扩展 `app_iap.c`，将新 CMD 分发给业务协议处理器，而不是返回 BAD_CMD。
2. 在 `task_host232.c` 增加业务帧发送队列，避免多个 RTOS 任务同时操作 USART2。
3. 新增统一 `AppRuntimeState`，管理 work_mode、run_state、stage、step、fault_bitmap、revision。
4. 给 `AppFlow` 增加步骤进入/完成/失败回调，用于主动发送 `DEVICE_STATUS`。
5. 给等待上位机的步骤增加 `request_id`，发送和重发 `INTERACTION_REQUEST`。
6. `PROCESS_INTERACTION` 校验 request_id 后调用 `AutoDevice_SetCommand()`。
7. 参数协议按字段编码/解码，不直接 memcpy C 结构体到串口。
8. 参数修改增加完整范围校验和 revision。
9. `AutoDevice_OnReport()` 接入业务上报队列。
10. 正式联调前关闭 `AUTO_DEVICE_HOST_COMMAND_BYPASS`。
11. APP 上电默认进入 MANUAL；CMD=0x31 从 MANUAL 切换到 AUTO 时调用 `AutoProcess_RequestFull()`。
12. 入舱完成后发送 `interaction_type=6` 的 A3，并等待 CMD=0x33 返回本次运行的 `recipe_id`。
13. 新增只修改 RAM 的运行时配方选择接口；不要直接使用当前会请求保存 Flash 的 `AppParam_SetActiveRecipe()`，避免每次动物类型变化都擦写参数区。
14. 从 `AppRecipe_t` 和协议中删除 `minimum_flow_x10`；将三段流量参数实现为 485 流量控制阀目标开度，不再执行流量计采集或低流量判断。

## 18. 【公共说明，适用于全部 CMD】兼容和版本管理

1. `QUERY_INFO` 中协议版本目前为 1.1。业务协议落地时建议升级为 1.2，或在业务状态中增加独立版本字段。
2. 上位机必须先检查协议版本，再启用对应页面和命令。
3. 新字段优先追加在 Payload 尾部，不改变已有字段偏移。
4. 接收端以 LEN 判断可用字段，允许旧版本忽略尾部新增字段。
5. 保留未知 CMD 时返回 `BAD_CMD`，未知参数版本返回 `PARAM_VERSION_MISMATCH`。
