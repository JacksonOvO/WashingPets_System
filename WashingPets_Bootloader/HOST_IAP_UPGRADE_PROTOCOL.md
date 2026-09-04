# STM32 IAP 上位机升级协议说明

本文档描述 `StartHostIapFlasher` 上位机工具与 STM32 设备之间的升级交互逻辑。上位机同事可按本文实现同等升级功能。

## 1. 串口参数

| 参数 | 值 |
|---|---|
| 串口 | 实际设备枚举出的 COM 口 |
| 波特率 | `115200` |
| 数据位 | `8` |
| 校验 | `None` |
| 停止位 | `1` |
| 硬件流控 | 无 |

建议读写超时：

| 阶段 | 建议超时 |
|---|---:|
| Query/普通命令 | `2s ~ 5s` |
| 擦除/写入/CRC 校验阶段 | `30s` |
| APP 进入 Bootloader 后等待重启 | `1.2s` 左右后重新打开串口或重新查询 |

## 2. Flash 布局

当前单片机 Flash 布局：

| 区域 | 地址范围 | Sector | 大小 | 说明 |
|---|---:|---:|---:|---|
| Bootloader | `0x08000000 - 0x08003FFF` | Sector 0 | 16 KB | Bootloader/IAP 程序 |
| APP | `0x08004000 - 0x080BFFFF` | Sector 1-9 | 752 KB | 业务 APP，升级时会擦除 |
| 参数区 | `0x080C0000 - 0x080DFFFF` | Sector 10 | 128 KB | 掉电保存参数，升级时不擦除 |
| 元数据区 | `0x080E0000 - 0x080FFFFF` | Sector 11 | 128 KB | Bootloader 保存 APP 版本、大小、CRC、状态 |

关键常量：

| 名称 | 值 |
|---|---:|
| `TARGET_ID` | 由芯片 96 位 UID 计算 CRC32 得到 |
| `HW_VERSION` | `0x0001` |
| `APP_START` | `0x08004000` |
| `APP_CAPACITY` | `770048` 字节，`0x000BC000` |
| 主机推荐 chunk | `128` 字节 |
| Bootloader 最大 chunk | QueryInfo 返回，目前为 `256` 字节 |

实际发送数据块大小建议：

```text
chunk_size = min(128, device_max_chunk)
```

## 3. 基础帧格式

所有命令帧和响应帧格式一致：

```text
55 AA | CMD | SEQ | LEN_L LEN_H | PAYLOAD | CRC16_L CRC16_H
```

字段说明：

| 字段 | 长度 | 说明 |
|---|---:|---|
| `55 AA` | 2 | 帧头 |
| `CMD` | 1 | 命令或响应码 |
| `SEQ` | 1 | 序号，`uint8`，从 0 开始递增，溢出回 0 |
| `LEN` | 2 | payload 长度，小端 `uint16` |
| `PAYLOAD` | N | 命令数据 |
| `CRC16` | 2 | 小端 `uint16` |

所有多字节整数均为小端序。

CRC16 算法：

| 参数 | 值 |
|---|---|
| 算法 | CRC16-CCITT-FALSE |
| 初值 | `0xFFFF` |
| 多项式 | `0x1021` |
| 输入范围 | `CMD + SEQ + LEN + PAYLOAD` |
| 不包含 | `55 AA` 帧头和 CRC16 本身 |
| 输出 | 小端写入帧尾 |

空 payload 的 QueryInfo 示例：

```text
TX: 55 AA 10 00 00 00 67 9F
```

解释：

```text
CMD=0x10, SEQ=0, LEN=0, CRC16=0x9F67，小端发送为 67 9F
```

## 4. 命令与响应码

### 命令码

| 命令 | 值 | 发送对象 | 说明 |
|---|---:|---|---|
| `QUERY_INFO` | `0x10` | APP 或 Bootloader | 查询设备信息 |
| `ENTER_UPGRADE` | `0x11` | APP | 通知 APP 复位进入 Bootloader |
| `UPGRADE_START` | `0x20` | Bootloader | 开始升级，发送固件信息 |
| `UPGRADE_DATA` | `0x21` | Bootloader | 发送一包 APP 数据 |
| `UPGRADE_END` | `0x22` | Bootloader | 结束升级，触发整包 CRC 校验 |
| `UPGRADE_ABORT` | `0x23` | Bootloader | 异常时中止升级 |
| `RUN_APP` | `0x25` | Bootloader | 请求跳转 APP，可选 |

### 响应码

| 响应 | 值 | 说明 |
|---|---:|---|
| `DEVICE_INFO` | `0x90` | QueryInfo 响应 |
| `ACK` | `0xF0` | 命令成功 |
| `NACK` | `0xF1` | 命令失败 |

### 错误码

| 错误 | 值 | 说明 |
|---|---:|---|
| `IAP_STATUS_OK` | `0x00` | 成功 |
| `IAP_ERR_BAD_CMD` | `0x01` | 不支持的命令 |
| `IAP_ERR_BAD_LENGTH` | `0x02` | payload 长度错误 |
| `IAP_ERR_BAD_CRC16` | `0x03` | 帧 CRC16 错误 |
| `IAP_ERR_BAD_STATE` | `0x04` | 状态错误，例如未 START 就 DATA |
| `IAP_ERR_BUSY` | `0x05` | 忙 |
| `IAP_ERR_SEQ` | `0x06` | 序号错误 |
| `IAP_ERR_TARGET_MISMATCH` | `0x08` | target 或硬件版本不匹配 |
| `IAP_ERR_SIZE` | `0x0A` | 固件大小或 chunk 大小错误 |
| `IAP_ERR_OFFSET` | `0x0B` | 数据 offset 错误 |
| `IAP_ERR_FLASH` | `0x0C` | Flash 擦写错误 |
| `IAP_ERR_FW_CRC32` | `0x0D` | 固件整包 CRC32 错误 |
| `IAP_ERR_APP_INVALID` | `0x0E` | APP 无效，不能跳转 |

## 5. 响应 Payload

### 5.1 ACK

响应帧：

```text
CMD = 0xF0
LEN = 6
```

Payload：

| 偏移 | 长度 | 类型 | 说明 |
|---:|---:|---|---|
| 0 | 1 | `uint8` | 被确认的请求命令 |
| 1 | 1 | `uint8` | 状态，成功时为 `0x00` |
| 2 | 4 | `uint32_le` | `next_offset`，下一次应写入的 APP 偏移 |

例如 DATA 写入后：

```text
RX: 55 AA F0 85 06 00 21 00 F4 41 00 00 B0 9D
```

含义：

```text
响应 DATA(0x21) 成功，next_offset=0x000041F4=16884
```

### 5.2 NACK

响应帧：

```text
CMD = 0xF1
LEN = 8
```

Payload：

| 偏移 | 长度 | 类型 | 说明 |
|---:|---:|---|---|
| 0 | 1 | `uint8` | 出错的请求命令 |
| 1 | 1 | `uint8` | 错误码 |
| 2 | 1 | `uint8` | 设备期望的 sequence |
| 3 | 1 | `uint8` | 保留，当前为 0 |
| 4 | 4 | `uint32_le` | `next_offset` |

上位机收到 NACK 后应停止本次升级。若已经进入升级中，建议发送 `UPGRADE_ABORT`。

### 5.3 DEVICE_INFO

响应帧：

```text
CMD = 0x90
LEN = 28
```

Payload：

| 偏移 | 长度 | 类型 | 说明 |
|---:|---:|---|---|
| 0 | 1 | `uint8` | `run_mode`，`0=APP`，`1=Bootloader` |
| 1 | 1 | `uint8` | 协议主版本，目前 `1` |
| 2 | 1 | `uint8` | 协议次版本，目前 `1` |
| 3 | 1 | `uint8` | APP 是否有效，`0/1` |
| 4 | 4 | `uint32_le` | `target_id`，由设备运行时上报，升级开始时原样带回 |
| 8 | 2 | `uint16_le` | 硬件版本，应为 `0x0001` |
| 10 | 4 | `uint32_le` | Bootloader 版本 |
| 14 | 4 | `uint32_le` | APP 版本 |
| 18 | 4 | `uint32_le` | APP 起始地址，应为 `0x08004000` |
| 22 | 4 | `uint32_le` | APP 最大容量，目前 `770048` |
| 26 | 2 | `uint16_le` | 设备最大单包数据长度，目前 `256` |

版本号编码：

```text
version = (major << 24) | (minor << 16) | (patch << 8)
```

例如 `1.0.0` 编码为：

```text
0x01000000
```

小端发送时为：

```text
00 00 00 01
```

## 6. CRC32

固件整包 CRC32 使用常见以太网/ZIP CRC32：

| 参数 | 值 |
|---|---|
| 初值 | `0xFFFFFFFF` |
| 多项式 | `0xEDB88320`，反射算法 |
| 输出异或 | `0xFFFFFFFF` |
| 输入 | 整个 APP bin 文件所有字节 |

该 CRC32 会在 `UPGRADE_START` 和 `UPGRADE_END` 中发送。Bootloader 写完 APP 后会重新计算 Flash 中 APP 内容的 CRC32，必须和主机发送值一致。

## 7. 完整升级流程

### 7.1 总流程

1. 打开串口。
2. 发送 `QUERY_INFO(seq=0)`。
3. 如果返回 `run_mode=0`，说明当前在 APP：
   - 发送 `ENTER_UPGRADE(seq=1, payload="IAP1")`。
   - APP 收到后写 RTC 标志并软复位。
   - 上位机等待约 `1200 ms`。
   - 关闭并重新打开串口，或清空串口缓冲。
   - 重新从 `seq=0` 发送 `QUERY_INFO`。
4. 如果返回 `run_mode=1`，说明当前在 Bootloader：
   - 检查 `target_id` 与升级前 `QUERY_INFO` 读到的设备 ID 一致。
   - 检查 APP bin 大小 `<= app_capacity`。
   - 计算 `image_crc32`。
   - 选择 `chunk_size = min(128, max_chunk)`。
5. 发送 `UPGRADE_START`。
6. 按 offset 从 0 开始循环发送 `UPGRADE_DATA`。
7. 发送 `UPGRADE_END`。
8. 收到 `UPGRADE_END` 的 ACK 后升级成功，Bootloader 会跳转 APP。
9. 任一步失败，建议发送 `UPGRADE_ABORT`，然后提示用户失败原因。

### 7.2 序号规则

每次重新开始一轮连接/升级，从 `seq=0` 开始。

正常递增示例：

| 步骤 | 命令 | seq |
|---|---|---:|
| 1 | `QUERY_INFO` | 0 |
| 2 | `ENTER_UPGRADE`，如果当前在 APP | 1 |
| 3 | 设备复位后重新 `QUERY_INFO` | 0 |
| 4 | `UPGRADE_START` | 1 |
| 5 | 第一包 `UPGRADE_DATA` | 2 |
| 6 | 第二包 `UPGRADE_DATA` | 3 |
| ... | ... | ... |
| N | `UPGRADE_END` | 最后一包 DATA 的 seq + 1 |

Bootloader 要求 sequence 与内部期望值一致。收到 ACK 后，上位机再递增 seq。

如果设备复位进入 Bootloader，sequence 重新从 0 开始。

## 8. 各命令 Payload

### 8.1 QUERY_INFO `0x10`

发送对象：APP 或 Bootloader。

Payload：

```text
空，LEN=0
```

示例：

```text
TX: 55 AA 10 00 00 00 67 9F
```

期望响应：

```text
DEVICE_INFO(0x90)
```

### 8.2 ENTER_UPGRADE `0x11`

发送对象：APP。

Payload：

| 偏移 | 长度 | 内容 |
|---:|---:|---|
| 0 | 4 | ASCII 字符串 `IAP1`，即 `49 41 50 31` |

示例，`seq=1`：

```text
TX: 55 AA 11 01 04 00 49 41 50 31 22 6A
```

期望行为：

1. APP 返回 ACK，或因立即复位导致上位机读 ACK 超时。
2. APP 写入 `RTC->BKP0R = 0xA55A5AA5`。
3. APP 软复位。
4. Bootloader 上电读取该标志后停留在升级模式。

注意：旧版本主机如果发送空 payload，会收到：

```text
NACK err=0x02 BAD_LENGTH
```

### 8.3 UPGRADE_START `0x20`

发送对象：Bootloader。

Payload 固定 22 字节：

| 偏移 | 长度 | 类型 | 说明 |
|---:|---:|---|---|
| 0 | 4 | `uint32_le` | `target_id = QUERY_INFO 返回的设备 ID` |
| 4 | 2 | `uint16_le` | `hw_min = 0x0001` |
| 6 | 2 | `uint16_le` | `hw_max = 0x0001` |
| 8 | 4 | `uint32_le` | APP 版本 |
| 12 | 4 | `uint32_le` | APP bin 字节数 |
| 16 | 4 | `uint32_le` | APP bin CRC32 |
| 20 | 2 | `uint16_le` | 本次升级使用的 chunk size |

Bootloader 检查：

| 条件 | 不满足时 |
|---|---|
| payload 长度必须等于 22 | `BAD_LENGTH` |
| target 匹配 | `TARGET_MISMATCH` |
| 当前硬件版本在 `[hw_min, hw_max]` 范围内 | `TARGET_MISMATCH` |
| size 在 `[8, APP_CAPACITY]` | `SIZE` |
| chunk_size 在 `[1, 256]` | `SIZE` |
| 元数据写入和 APP 区擦除成功 | `FLASH` |

当前 Bootloader 在 START 成功后会：

1. 擦除元数据区 Sector 11。
2. 写入升级中状态。
3. 擦除 APP 区 Sector 1-9。
4. 返回 ACK，`next_offset=0`。

### 8.4 UPGRADE_DATA `0x21`

发送对象：Bootloader。

Payload：

| 偏移 | 长度 | 类型 | 说明 |
|---:|---:|---|---|
| 0 | 4 | `uint32_le` | 本包写入偏移 offset，从 0 开始 |
| 4 | 2 | `uint16_le` | 本包数据长度 data_length |
| 6 | N | bytes | APP bin 数据 |

payload 总长度：

```text
LEN = 6 + data_length
```

Bootloader 检查：

| 条件 | 不满足时 |
|---|---|
| 已经收到 START | `BAD_STATE` |
| payload 长度至少 6 | `BAD_LENGTH` |
| `LEN == 6 + data_length` | `OFFSET` |
| `offset == written` | `OFFSET` |
| `data_length > 0` | `OFFSET` |
| `data_length <= chunk_size` | `OFFSET` |
| 不超过镜像剩余大小 | `OFFSET` |
| Flash 写入并回读成功 | `FLASH` |

成功响应：

```text
ACK next_offset = offset + data_length
```

上位机必须检查 ACK 中的 `next_offset` 是否等于期望值，否则应中止。

### 8.5 UPGRADE_END `0x22`

发送对象：Bootloader。

Payload 固定 8 字节：

| 偏移 | 长度 | 类型 | 说明 |
|---:|---:|---|---|
| 0 | 4 | `uint32_le` | APP bin 字节数 |
| 4 | 4 | `uint32_le` | APP bin CRC32 |

Bootloader 检查：

| 条件 | 不满足时 |
|---|---|
| 已经收到 START | `BAD_STATE` |
| payload 长度必须等于 8 | `BAD_LENGTH` |
| size 与 START 一致 | `FW_CRC32` |
| crc 与 START 一致 | `FW_CRC32` |
| 已写入字节数等于 image_size | `FW_CRC32` |
| Flash 中 APP 整包 CRC32 正确 | `FW_CRC32` |
| APP 向量表合法 | `FW_CRC32` |
| 元数据状态写成 VALID 成功 | `FLASH` |

成功后：

1. 返回 ACK，`next_offset=image_size`。
2. Bootloader 延时约 50 ms。
3. 跳转 APP。

示例，APP 大小为 `16884 = 0x41F4`：

```text
TX: 55 AA 22 86 08 00 F4 41 00 00 52 9F 24 4B 1D 06
RX: 55 AA F0 86 06 00 22 00 F4 41 00 00 14 7E
```

含义：

```text
UPGRADE_END 成功，next_offset=16884，升级完成。
```

### 8.6 UPGRADE_ABORT `0x23`

发送对象：Bootloader。

Payload：

```text
空，LEN=0
```

用途：

任意升级失败、上位机异常、用户取消时发送。Bootloader 会把当前升级状态标记为失败，并返回 ACK。

### 8.7 RUN_APP `0x25`

发送对象：Bootloader。

Payload：

```text
空，LEN=0
```

如果 APP 有效，Bootloader 返回 ACK 并跳转 APP。如果 APP 无效，返回：

```text
NACK err=0x0E APP_INVALID
```

当前升级流程不必须使用该命令，因为 `UPGRADE_END` 成功后 Bootloader 会自动跳 APP。

## 9. 上位机实现伪代码

```c
open_serial(115200, 8N1);

seq = 0;
info = query_info(seq++);

if (info.run_mode == 0) {
    send_ack_command(CMD_ENTER_UPGRADE, seq++, bytes("IAP1"));
    // 如果这里超时，也可能是 APP 已复位，允许继续等待。
    close_serial();
    sleep_ms(1200);
    open_serial(115200, 8N1);

    seq = 0;
    info = query_info(seq++);
}

if (info.run_mode != 1) fail("not in bootloader");
使用 QUERY_INFO 返回的 target_id 作为 UPGRADE_START.target_id。
if (image_size > info.app_capacity) fail("image too large");

chunk = min(128, info.max_chunk);
crc32 = calc_crc32(image);
version = encode_version("1.0.0");

payload_start = pack_le(
    u32(info.target_id),
    u16(0x0001),
    u16(0x0001),
    u32(version),
    u32(image_size),
    u32(crc32),
    u16(chunk)
);
next = send_ack_command(CMD_UPGRADE_START, seq++, payload_start);
if (next != 0) fail("unexpected next_offset");

offset = 0;
while (offset < image_size) {
    count = min(chunk, image_size - offset);
    payload_data = u32_le(offset) + u16_le(count) + image[offset:offset+count];
    next = send_ack_command(CMD_UPGRADE_DATA, seq++, payload_data);
    if (next != offset + count) fail("unexpected next_offset");
    offset = next;
}

payload_end = u32_le(image_size) + u32_le(crc32);
next = send_ack_command(CMD_UPGRADE_END, seq++, payload_end);
if (next != image_size) fail("unexpected final offset");

success();
```

## 10. 接收帧建议

串口接收时建议按以下方式找帧：

1. 持续读字节，直到发现连续 `55 AA`。
2. 再读 4 字节 header：`CMD SEQ LEN_L LEN_H`。
3. 根据 LEN 读 payload。
4. 读 2 字节 CRC16。
5. 对 `CMD SEQ LEN payload` 重新计算 CRC16。
6. CRC 不一致则丢弃该帧或报错。

APP 模式下业务串口可能会输出普通业务数据。上位机应忽略 `55 AA` 前面的杂散字节。

## 11. 重要注意事项

1. APP 必须链接到 `0x08004000`。
2. APP bin 不能超过 `0x000BC000` 字节，否则会占用参数区。
3. 上位机升级时不要擦除 Sector 10；IAP Bootloader 当前只擦 Sector 1-9。
4. 如果直接用烧录器烧 Bootloader，注意不要整片擦除，否则 APP 会丢失。
5. 如果 Query 返回 `mode=1, app_valid=0`，表示当前在 Bootloader 且 APP 无效，需要重新升级 APP。
6. 如果 Query 返回 `mode=0`，表示当前在 APP，必须先发 `ENTER_UPGRADE` 且 payload 为 `IAP1`。
7. 收到任何 NACK 时，优先根据错误码提示用户；如果升级已经开始，发送 `UPGRADE_ABORT`。
8. `UPGRADE_END` 成功 ACK 后即可认为升级成功，Bootloader 会自动跳转 APP。

