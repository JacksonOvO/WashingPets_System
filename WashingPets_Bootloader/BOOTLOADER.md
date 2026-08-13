# STM32F407ZG USART2 Bootloader

本工程按 `HostCommProtocol.md` 的帧格式实现升级，但只保留最小可用流程，避免
Bootloader 代码过大。

## 串口与灯

- USART2：PA2/TX、PA3/RX、115200 8N1
- PF6：Bootloader 状态灯，低电平亮，高电平灭
- 上电进入 Bootloader 后灯亮；跳转 App 前灯灭

## Flash 分区

| 区域 | 地址 | F407 扇区 |
|---|---:|---:|
| Bootloader | `0x08000000-0x08003FFF` | Sector 0 |
| App | `0x08004000-0x080BFFFF` | Sectors 1-9 |
| Parameters | `0x080C0000-0x080DFFFF` | Sector 10 |
| Metadata | `0x080E0000-0x080FFFFF` | Sector 11 |

App 必须链接到 `0x08004000`，并设置：

```c
SCB->VTOR = 0x08004000U;
```

## 帧格式

使用 HostCommProtocol 的 CRC16 帧：

```text
55 AA | CMD | SEQ | LEN_L LEN_H | PAYLOAD | CRC16_L CRC16_H
```

CRC16 为 CCITT-FALSE，覆盖 `CMD + SEQ + LEN + PAYLOAD`。

## 当前保留的命令

| 命令 | 值 | 说明 |
|---|---:|---|
| QUERY_INFO | `0x10` | 查询 Bootloader 信息 |
| UPGRADE_START | `0x20` | 校验固件信息并擦除 App 区 |
| UPGRADE_DATA | `0x21` | 写入一包 App.bin 数据 |
| UPGRADE_END | `0x22` | 校验完整 App.bin CRC32 并标记有效 |
| UPGRADE_ABORT | `0x23` | 取消升级 |
| RUN_APP | `0x25` | 跳转有效 App |

响应：

```text
ACK  0xF0 Payload: request_cmd, status, next_offset_u32
NACK 0xF1 Payload: request_cmd, error, expected_seq, reserved, next_offset_u32
```

未实现 `BOOT_READY`、`QUERY_PROGRESS`、`APP_READY`、`APP_START_CONFIRM`，上位机可用
`QUERY_INFO` 兜底确认 Bootloader 是否在线。

## 升级流程

1. App 收到上位机 `ENTER_UPGRADE` 后写 `RTC->BKP0R = 0xA55A5AA5` 并复位。
2. Bootloader 读取并清除升级标志，停留升级模式。
3. 上位机发送 `QUERY_INFO`。
4. 上位机发送 `UPGRADE_START`：
   - `target_id`
   - `hw_min`
   - `hw_max`
   - `fw_version`
   - `fw_size`
   - `fw_crc32`
   - `chunk_size`
5. 上位机循环发送 `UPGRADE_DATA`：
   - `offset`
   - `data_len`
   - `data`
6. 上位机发送 `UPGRADE_END`，Bootloader 校验 CRC32 并写有效标志。
7. 上位机发送 `RUN_APP`，Bootloader 熄灯并跳转 App。

## 工具使用

```powershell
py -m pip install -r tools\requirements.txt
py tools\bootloader_upload.py COM5 path\to\App.bin --version 1.0.0
```

现场在线升级只发送 `App.bin`。`App.hex` 和 `Factory_All.hex` 仅用于 JTAG/SWD 或量产烧录。
