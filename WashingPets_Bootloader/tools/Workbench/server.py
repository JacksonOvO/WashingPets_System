#!/usr/bin/env python3
import csv
import glob
import struct
import subprocess
import threading
import time
from pathlib import Path

import serial

ROOT = Path(__file__).resolve().parent
DEFAULT_BIN = ROOT / "APP.bin"

# 业务命令与 IAP 命令共用同一套错误码（0x01~0x10），固件 app_iap.c 的各 NACK
# 点只发送这些 IAP 错误码。描述里补充了业务场景语义，便于调试：
#   0x04 BAD_STATE  —— 模式/运行状态不允许（模式切换、流程控制、手动输出被
#                      自动模式或急停拒绝）
#   0x05 BUSY       —— 设备忙/互锁拒绝/流程占用
#   0x07 SEQ_CONFLICT —— 参数 revision 冲突
#   0x0C FLASH      —— Flash 擦写失败（保存参数失败）
#   0x10 CONFIG     —— 参数版本不匹配 / output_id 越界 / action 不支持 /
#                      交互 request_id 失效
IAP_ERROR_NAMES = {
    0x01: "BAD_CMD 不支持的命令",
    0x02: "BAD_LENGTH payload长度错误",
    0x03: "BAD_CRC16 帧CRC错误",
    0x04: "BAD_STATE 状态错误（当前模式/状态不允许该操作）",
    0x05: "BUSY 忙（设备占用中/安全互锁拒绝）",
    0x06: "SEQ 序号错误",
    0x07: "SEQ_CONFLICT 序号冲突（参数revision冲突）",
    0x08: "TARGET_MISMATCH 目标设备不匹配",
    0x0A: "SIZE 固件大小或分包大小错误",
    0x0B: "OFFSET 写入偏移错误",
    0x0C: "FLASH Flash擦写失败（保存参数失败）",
    0x0D: "FW_CRC32 固件CRC错误",
    0x0E: "APP_INVALID APP无效",
    0x0F: "TIMEOUT 设备超时",
    0x10: "CONFIG 配置错误（参数版本/ID失效/参数越界）",
}

# 协议 §6.2 曾定义独立业务错误码 0x11~0x1A，但固件从未发送，统一走上面的
# IAP 错误码，故此处不再维护。若未来固件实现精确业务错误码再恢复。
ERROR_NAMES = IAP_ERROR_NAMES

CMD_QUERY_STATUS = 0x30
CMD_SET_MODE = 0x31
CMD_PROCESS = 0x32
CMD_INTERACTION = 0x33
CMD_MANUAL_OUTPUT = 0x34
CMD_QUERY_IO = 0x35
CMD_MANUAL_RS485 = 0x36
CMD_SET_ACTUATOR_MODE = 0x37
CMD_BOOSTER_CONTROL = 0x38
CMD_GET_PARAM = 0x40
CMD_SET_PARAM = 0x41
CMD_SAVE_PARAM = 0x42
CMD_RESTORE_PARAM = 0x43

INPUT_NAMES = [
    "IN1 消毒液低液位", "IN2 沐浴液低液位", "IN3 芬香液低液位", "IN4 柔顺液低液位",
    "IN5 除螨液低液位", "IN6 护毛素低液位", "IN7 蓄水箱下液位", "IN8 蓄水箱中液位",
    "IN9 蓄水箱上液位", "IN10 急停开关", "IN11 排水管液位", "IN12 上门光幕",
    "IN13 下门光幕", "IN14 上门关闭到位", "IN15 下门关闭到位", "IN16 左电插锁到位",
    "IN17 右电插锁到位", "IN18 后电插锁1到位", "IN19 后电插锁2到位",
]

OUTPUT_NAMES = [
    "OUT1 排气扇", "OUT2 制氧机", "OUT3 空气清新剂", "OUT4 手动清洁电磁阀", "OUT5 排风紫外线灯",
    "OUT6 热水器", "OUT7 自来水阀", "OUT8 蠕动泵", "OUT9 淋浴阀上", "OUT10 淋浴阀中",
    "OUT11 淋浴阀下", "OUT12 淋浴阀后", "OUT13 沐浴液阀", "OUT14 芬香液阀", "OUT15 柔顺液阀",
    "OUT16 除螨液阀", "OUT17 护毛素阀", "OUT18 消毒液阀", "OUT19 紫外线灯", "OUT20 自清洁顶部阀",
    "OUT21 自清洁前侧上阀", "OUT22 自清洁前侧下阀", "OUT23 自清洁后端阀", "OUT24 旁通阀",
    "OUT25 臭氧发生器", "OUT26 舱内照明", "OUT27 正门灯带", "OUT28 LOGO发光板", "OUT29 正门加热",
    "OUT30 下门电动推杆", "OUT31 上门闭门器", "OUT32 上门电磁锁", "OUT33 下门电磁锁",
    "OUT34 左电插锁", "OUT35 右电插锁", "OUT36 后电插锁1", "OUT37 后电插锁2",
    "OUT38 下门灯板", "OUT39 热水器阀", "OUT40 主板电源控制",
]

STAGES = ["0 入舱准备", "1 入舱", "2 洗前准备", "3 冲洗", "4 沐浴", "5 清洗", "6 吹干", "7 出舱准备", "8 出舱"]
RS485_STATIONS = ["1 增压泵压力/启停", "2 比例阀角度", "10 左风机", "11 右风机", "12 底部风机", "13 顶部排风扇", "14 左加热器", "15 右加热器", "16 底部加热器"]

SELF_TEST_ITEMS = [
    "左风机", "右风机", "底部风机", "左加热管", "右加热管", "底部加热管",
    "增压泵", "比例阀+进水", "淋浴阀上", "淋浴阀中", "淋浴阀下", "淋浴阀后",
    "自清洁顶阀", "自清洁前上阀", "自清洁前下阀", "自清洁后阀", "旁通阀", "热水链路",
]

# 协议 13.3：A4 primary_fault_code 具体故障码
FAULT_CODES = {
    0x0000: "无具体故障",
    0x0001: "急停触发",
    0x0002: "流程步骤超时(兼容值)",
    0x0101: "流程步骤跳转失败",
    0x0102: "普通输出命令失败或流程条件不允许",
    0x0103: "流程被中止或失败兜底",
    0x0201: "上舱门电磁锁输出失败",
    0x0202: "下舱门电磁锁输出失败",
    0x0203: "上舱门锁定检查失败",
    0x0204: "下舱门锁定检查失败",
    0x0205: "舱门未关闭到位",
    0x0301: "水箱温度传感器数据无效",
    0x0302: "管路出水温度传感器数据无效",
    0x0303: "管路出水温度未达到配方范围",
    0x0304: "水箱温度未达到配方范围",
    0x0305: "自清洁等待水箱高液位超时",
    0x0306: "水箱低液位保护",
    0x0308: "进水口温度传感器数据无效",
    0x0309: "混合水温传感器数据无效",
    0x0401: "舱内温湿度数据无效",
    0x0402: "舱内湿度数据无效",
    0x0403: "舱外温度数据无效",
    0x0404: "氧气数据无效",
    0x0405: "臭氧数据无效",
    0x0501: "沐浴液低液位",
    0x0502: "消毒液低液位",
    0x0503: "增压泵启动失败",
    0x0504: "增压泵压力设置失败",
    0x0505: "喷淋阀输出失败",
    0x0506: "比例阀输出失败",
    0x0601: "风机输出失败",
    0x0602: "加热棒输出失败",
    0x0603: "烘干阶段舱内温度数据无效",
    0x0604: "烘干温控超限",
    0x0701: "自清洁条件不允许",
    0x0702: "自清洁流量异常",
    0x0703: "自清洁消毒条件不允许",
    0x0801: "左风机485通信失败",
    0x0802: "右风机485通信失败",
    0x0803: "底部风机485通信失败",
    0x0804: "顶部排风扇485通信失败",
    0x0805: "左加热棒485通信失败",
    0x0806: "右加热棒485通信失败",
    0x0807: "底部加热棒485通信失败",
    0x0808: "增压泵485通信失败",
    0x0809: "比例调节阀485通信失败",
    0x0901: "自检安全条件不允许",
    0x0902: "自检压力超过安全阈值",
    0x0903: "自检舱温超过安全阈值",
    0x0904: "自检液位组合异常",
}

# 协议 13.2：A4 事件类型与等级
FAULT_EVENT_TYPES = {1: "故障发生", 2: "故障更新", 3: "故障解除"}
FAULT_SEVERITIES = {1: "提示", 2: "警告", 3: "停机", 4: "急停"}


def empty_fault_state():
    return {
        "event_id": 0,
        "event_type": 0,
        "severity": 0,
        "primary_fault_code": 0,
        "active_code": 0,
        "event_type_text": "--",
        "severity_text": "--",
        "fault_code_text": "0x0000",
        "fault_name": "无故障",
    }


RECIPE_PAYLOAD_LENGTH = 84
RECIPE_COUNT = 26
RECIPE_FIELDS = [
    {"key": "tank", "label": "水箱目标温度 (℃)", "offset": 0, "kind": "i16x10", "min": -40, "max": 100},
    {"key": "tankTol", "label": "水箱水温容差 (±℃)", "offset": 2, "kind": "u16x10", "min": 0, "max": 100},
    {"key": "pipeTol", "label": "管路水温容差 (±℃)", "offset": 4, "kind": "u16x10", "min": 0, "max": 100},
    {"key": "sprayLevel", "label": "冲洗水压 (kg)", "offset": 6, "kind": "u8x10", "min": 0, "max": 10},
    {"key": "bathLevel", "label": "沐浴水压 (kg)", "offset": 7, "kind": "u8x10", "min": 0, "max": 10},
    {"key": "cleanLevel", "label": "清洗水压 (kg)", "offset": 8, "kind": "u8x10", "min": 0, "max": 10},
    {"key": "dryFanPct", "label": "烘干风机功率 (%)", "offset": 9, "kind": "u8", "min": 0, "max": 100},
    {"key": "dryCabinTemp", "label": "烘干舱内温度 (℃)", "offset": 10, "kind": "i16x10", "min": -40, "max": 100},
    {"key": "spraySec", "label": "冲洗总时间 (秒)", "offset": 12, "kind": "ms", "min": 0, "max": 86400},
    {"key": "sprayOnSec", "label": "冲洗出水时间 (秒)", "offset": 16, "kind": "ms", "min": 0, "max": 86400},
    {"key": "sprayPauseSec", "label": "冲洗暂停时间 (秒)", "offset": 20, "kind": "ms", "min": 0, "max": 86400},
    {"key": "bathSec", "label": "沐浴总时间 (秒)", "offset": 24, "kind": "ms", "min": 0, "max": 86400},
    {"key": "bathOnSec", "label": "沐浴出水时间 (秒)", "offset": 28, "kind": "ms", "min": 0, "max": 86400},
    {"key": "bathPauseSec", "label": "沐浴暂停时间 (秒)", "offset": 32, "kind": "ms", "min": 0, "max": 86400},
    {"key": "soakSec", "label": "洗剂浸润时间 (秒)", "offset": 36, "kind": "ms", "min": 0, "max": 86400},
    {"key": "cleanSec", "label": "清洗总时间 (秒)", "offset": 40, "kind": "ms", "min": 0, "max": 86400},
    {"key": "drySec", "label": "烘干时间 (秒)", "offset": 44, "kind": "ms", "min": 0, "max": 86400},
    {"key": "selfTopSec", "label": "自清洁顶部 (秒)", "offset": 48, "kind": "ms", "min": 0, "max": 86400},
    {"key": "selfLeftUpperSec", "label": "自清洁左侧上层 (秒)", "offset": 52, "kind": "ms", "min": 0, "max": 86400},
    {"key": "selfLeftMiddleSec", "label": "自清洁左侧夹层 (秒)", "offset": 56, "kind": "ms", "min": 0, "max": 86400},
    {"key": "selfRearSec", "label": "自清洁后端 (秒)", "offset": 60, "kind": "ms", "min": 0, "max": 86400},
    {"key": "selfDrySec", "label": "自清洁烘干 (秒)", "offset": 64, "kind": "ms", "min": 0, "max": 86400},
    {"key": "selfLiquidPressure", "label": "自清洁药液压力 (kg)", "offset": 68, "kind": "u8x10", "min": 0, "max": 10},
    {"key": "selfRinsePressure", "label": "自清洁冲洗压力 (kg)", "offset": 69, "kind": "u8x10", "min": 0, "max": 10},
    {"key": "selfLiquidSpraySec", "label": "自清洁清洁液喷洒 (秒)", "offset": 72, "kind": "ms", "min": 0, "max": 86400},
    {"key": "selfDisinfectSpraySec", "label": "自清洁消毒液喷洒 (秒)", "offset": 76, "kind": "ms", "min": 0, "max": 86400},
    {"key": "selfSoakSec", "label": "自清洁药液静置 (秒)", "offset": 80, "kind": "ms", "min": 0, "max": 86400},
]


def crc16(data):
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def crc32(data):
    crc = 0xFFFFFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if crc & 1 else crc >> 1
    return crc ^ 0xFFFFFFFF


def build_packet(command, sequence, payload=b""):
    header = b"\x55\xAA" + bytes([command, sequence]) + struct.pack("<H", len(payload))
    body = header[2:] + payload
    return header + payload + struct.pack("<H", crc16(body))


def hex_bytes(data):
    return " ".join(f"{b:02X}" for b in data)


def u16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]


def i16(data, offset):
    return struct.unpack_from("<h", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def put_u16(buf, offset, value):
    struct.pack_into("<H", buf, offset, int(value) & 0xFFFF)


def put_i16(buf, offset, value):
    struct.pack_into("<h", buf, offset, int(value))


def put_u32(buf, offset, value):
    struct.pack_into("<I", buf, offset, int(value) & 0xFFFFFFFF)


class IapNack(RuntimeError):
    def __init__(self, payload):
        self.payload = bytes(payload)
        self.request = self.payload[0] if len(self.payload) > 0 else 0
        self.error = self.payload[1] if len(self.payload) > 1 else 0
        self.expected_sequence = self.payload[2] if len(self.payload) > 2 else 0
        self.next_offset = u32(self.payload, 4) if len(self.payload) >= 8 else 0
        name = IAP_ERROR_NAMES.get(self.error, f"未知错误0x{self.error:02X}")
        # BAD_STATE 的 detail 通常是故障码，直接给出中文便于定位。
        fault_hint = ""
        if self.error == 0x04 and self.next_offset in FAULT_CODES:
            fault_hint = f", 故障码=0x{self.next_offset:04X} {FAULT_CODES[self.next_offset]}"
        super().__init__(
            f"设备返回NACK: request=0x{self.request:02X}, err=0x{self.error:02X} {name}, "
            f"expected_seq={self.expected_sequence}, next_offset={self.next_offset}{fault_hint}"
        )

    def is_sequence_error(self):
        return self.error == 0x06


def parse_version(text):
    parts = [int(x) for x in text.split(".")]
    if len(parts) != 3 or any(x < 0 or x > 255 for x in parts):
        raise ValueError("版本格式应为 1.0.0，范围 0..255")
    return (parts[0] << 16) | (parts[1] << 8) | parts[2]


def format_version(value):
    return f"{(value >> 16) & 0xFF}.{(value >> 8) & 0xFF}.{value & 0xFF}"


class WorkbenchState:
    def __init__(self):
        self.lock = threading.RLock()
        self.serial = None
        self.reader = None
        self.running = False
        self.sequence = 0
        self.logs = []
        # 故障报警历史：每次收到 A4 事件追加一条，会话内只增不减，供界面完整展示。
        self.fault_alarms = []
        self.status = {"connected": False, "port": "", "metrics": {}, "telemetry": {}, "telemetry_ui": {}, "power": {}, "power_ui": {}, "process_time": {}, "flags": "--", "io": {"inputs": [], "outputs": []}, "io_bools": {"inputs": [], "outputs": []}, "ioSummary": "未收到 IO 状态", "paramRevision": 0, "actuator_mode": 1, "self_test": None, "last_nack": None}
        self.status["fault"] = empty_fault_state()
        self.parameters = {}
        self.pending_interaction = None
        self.samples = []
        self.test_running = False
        self.test_started_at = 0
        self.last_sample_at = 0
        self.sample_interval = 30.0
        self.last_poll = 0
        self.mode_event = threading.Event()
        self._mode_result = None  # CMD_SET_MODE 应答结果: None=未决, (True, None) / (False, 拒绝原因)

    def add_log(self, text):
        line = time.strftime("[%H:%M:%S] ") + text
        with self.lock:
            self.logs.append(line)
            self.logs = self.logs[-900:]
        print(line, flush=True)

    def snapshot(self, include_logs=True, include_samples=True, sample_limit=2000):
        with self.lock:
            return {
                "connected": self.status["connected"],
                "port": self.status["port"],
                "metrics": dict(self.status["metrics"]),
                "telemetry": dict(self.status["telemetry"]),
                "telemetry_ui": dict(self.status.get("telemetry_ui", {})),
                "power": dict(self.status["power"]),
                "power_ui": dict(self.status.get("power_ui", {})),
                "flags": self.status["flags"],
                "actuator_mode": self.status["actuator_mode"],
                "self_test": self.status.get("self_test"),
                "process_time": dict(self.status["process_time"]),
                "last_nack": self.status.get("last_nack"),
                "io": {"inputs": list(self.status["io"]["inputs"]), "outputs": list(self.status["io"]["outputs"])},
                "io_bools": {"inputs": list(self.status.get("io_bools", {}).get("inputs", [])),
                             "outputs": list(self.status.get("io_bools", {}).get("outputs", []))},
                "ioSummary": self.status.get("ioSummary", "未收到 IO 状态"),
                "parameters": dict(self.parameters),
                "paramRevision": self.status["paramRevision"],
                "pendingInteraction": self.pending_interaction,
                "fault": dict(self.status["fault"]),
                "faultAlarms": list(self.fault_alarms[-200:]),
                "logs": list(self.logs[-220:]) if include_logs else [],
                "samples": list(self.samples[-sample_limit:]) if include_samples else [],
                "testRunning": self.test_running,
            }

    def next_seq(self):
        with self.lock:
            seq = self.sequence
            self.sequence = (self.sequence + 1) & 0xFF
            return seq

    def open(self, port):
        self.close()
        ser = serial.Serial(port, 115200, timeout=0.1, write_timeout=1)
        with self.lock:
            self.serial = ser
            self.running = True
            self.sequence = 0
            self.status["connected"] = True
            self.status["port"] = port
        self.reader = threading.Thread(target=self.reader_loop, daemon=True)
        self.reader.start()
        self.add_log(f"APP connected: {port}")
        self.send(CMD_QUERY_STATUS)
        self.send(CMD_QUERY_IO)

    def close(self):
        with self.lock:
            self.running = False
            ser = self.serial
            self.serial = None
            self.status["connected"] = False
            # 设备端执行器模式默认 RS485，断开后勾选框复位为该值。
            self.status["actuator_mode"] = 1
        if ser:
            try:
                ser.close()
            except Exception:
                pass

    def send(self, command, payload=b"", sequence=None):
        with self.lock:
            ser = self.serial
        if not ser or not ser.is_open:
            raise RuntimeError("APP串口未连接")
        seq = self.next_seq() if sequence is None else sequence
        packet = build_packet(command, seq, payload)
        ser.write(packet)
        self.add_log("TX: " + hex_bytes(packet))

    def reader_loop(self):
        buffer = bytearray()
        while True:
            with self.lock:
                active = self.running
                ser = self.serial
            if not active or not ser:
                return
            try:
                chunk = ser.read(ser.in_waiting or 1)
                if chunk:
                    buffer.extend(chunk)
                while True:
                    while len(buffer) >= 2 and (buffer[0] != 0x55 or buffer[1] != 0xAA):
                        del buffer[0]
                    if len(buffer) < 6:
                        break
                    length = buffer[4] | (buffer[5] << 8)
                    if length > 4096:
                        del buffer[0]
                        continue
                    frame_len = 8 + length
                    if len(buffer) < frame_len:
                        break
                    raw = bytes(buffer[:frame_len])
                    del buffer[:frame_len]
                    actual = crc16(raw[2:6 + length])
                    expected = u16(raw, frame_len - 2)
                    self.add_log("RX: " + hex_bytes(raw))
                    if actual != expected:
                        self.add_log(f"RX CRC错误: actual=0x{actual:04X}, expected=0x{expected:04X}")
                        continue
                    self.process_frame(raw[2], raw[3], raw[6:6 + length])
            except Exception as exc:
                self.add_log("APP RX ERROR: " + str(exc))
                time.sleep(0.2)

    def process_frame(self, command, sequence, payload):
        if command == 0xA1:
            self.update_status(payload)
        elif command == 0xA2:
            self.update_parameters(payload)
        elif command == 0xA3:
            self.update_interaction(payload)
        elif command == 0xA4:
            self.update_fault(payload)
        elif command == 0xA5:
            self.update_telemetry(payload)
            ack = bytearray(6)
            ack[0] = 0xA5
            try:
                self.send(0xF0, bytes(ack), sequence=sequence)
            except Exception as exc:
                self.add_log("APP ACK ERROR: " + str(exc))
        elif command == 0xA6:
            self.update_io(payload)
        elif command == 0xA7:
            self.update_self_test(payload, sequence)
        elif command == 0xF0:
            if len(payload) >= 6:
                request = payload[0]
                detail = u32(payload, 2)
                with self.lock:
                    self.status["last_nack"] = None
                self.add_log(f"ACK request=0x{request:02X}, detail={detail}")
                if request in (CMD_SET_PARAM, CMD_RESTORE_PARAM):
                    with self.lock:
                        self.status["paramRevision"] = detail
                if request == CMD_SET_MODE:
                    with self.lock:
                        self._mode_result = (True, None)
                    self.mode_event.set()
                if request == CMD_SET_ACTUATOR_MODE:
                    # detail = 设备当前执行器模式：1=RS485(默认)，0=GPIO(泵仍485)
                    with self.lock:
                        self.status["actuator_mode"] = detail
                if request == CMD_RESTORE_PARAM:
                    self.request_param(0)
                if request == CMD_PROCESS and detail == 7:
                    self.clear_fault("故障复位ACK")
                if request == CMD_INTERACTION:
                    # 交互回复被设备接受后，等待提示立即清除。
                    with self.lock:
                        self.pending_interaction = None
        elif command == 0xF1:
            if len(payload) >= 2:
                request = payload[0]
                error = payload[1]
                detail = u32(payload, 4) if len(payload) >= 8 else 0
                name = ERROR_NAMES.get(error, f"未知错误0x{error:02X}")
                detail_name = FAULT_CODES.get(detail) if error == 0x04 else None
                suffix = f" ({detail_name})" if detail_name else ""
                with self.lock:
                    self.status["last_nack"] = {
                        "request_cmd": request,
                        "error": error,
                        "detail": detail,
                        "text": f"命令被拒：0x{request:02X} {name}",
                    }
                self.add_log(f"NACK request=0x{request:02X}, err=0x{error:02X} {name}, detail={detail}{suffix}")
                if request == CMD_SET_MODE:
                    with self.lock:
                        self._mode_result = (False, f"err=0x{error:02X} {name}, detail={detail}{suffix}")
                    self.mode_event.set()
            else:
                self.add_log("NACK: " + hex_bytes(payload))

    def update_status(self, p):
        if len(p) < 12:
            return
        modes = ["空闲", "手动", "自动", "单阶段测试", "故障"]
        states = ["空闲", "启动", "运行", "等待", "暂停", "停止中", "完成", "故障"]
        processes = ["无流程", "自动流程", "单流程测试", "自清洗流程", "手动操作"]
        stages = ["入舱准备", "入舱", "洗前准备", "冲洗", "沐浴", "清洗", "吹干", "出舱准备", "出舱"]
        step_states = ["空闲", "运行", "等待", "暂停", "完成", "错误"]
        process = map_name(processes, p[2])
        is_self_clean = p[2] == 3
        stage = "自清洁" if is_self_clean else ("无" if p[2] == 0 else map_name(stages, p[3]))
        metrics = {
            "模式": map_name(modes, p[0]),
            "设备状态": map_name(states, p[1]),
            "运行类型": process,
            "当前流程": stage,
            "步骤": str(u16(p, 4)),
            "步骤状态": map_name(step_states, p[6]),
            "配方": f"配方{p[7] + 1}",
            "进度": f"{u16(p, 8) / 10.0:.1f}%",
            # UI expects English keys with numeric values
            "work_mode": p[0],
            "device_state": p[1],
            "process_type": p[2],
            "stage": p[3] if not is_self_clean else -1,
            "step": u16(p, 4),
            "step_state": p[6],
            "recipe": p[7],
            "progress": u16(p, 8),
        }
        with self.lock:
            self.status["metrics"] = metrics
            self.status["work_mode"] = p[0]
            # 与设备端逻辑对齐：流程不再运行（模式退出自动/单阶段测试，
            # 或运行类型已无流程）时，交互请求自动失效，清掉等待提示。
            interaction_stale = (p[0] not in (2, 3)) or (p[2] == 0)
            had_pending = self.pending_interaction is not None
            if interaction_stale and had_pending:
                self.pending_interaction = None
        if interaction_stale and had_pending:
            self.add_log("流程已结束，交互请求自动失效")

    def update_telemetry(self, p):
        if len(p) < 24:
            return
        flags = u16(p, 22)

        def valid(bit):
            return bool(flags & bit)

        telemetry_values = {
            "舱外温度": (i16(p, 0) / 10.0, "℃", valid(0x0001)),
            "舱外湿度": (u16(p, 2) / 10.0, "%", valid(0x0002)),
            "舱内氧气": (u16(p, 4) / 10.0, "%", valid(0x0004)),
            "进水口水温": (i16(p, 8) / 10.0, "℃", valid(0x0010)),
            "水箱水温": (i16(p, 10) / 10.0, "℃", valid(0x0020)),
            "出水口水温": (i16(p, 12) / 10.0, "℃", valid(0x0040)),
            "备用温度": (i16(p, 14) / 10.0, "℃", valid(0x0080)),
            "舱内温度": (i16(p, 16) / 10.0, "℃", valid(0x0100)),
            "舱内湿度": (u16(p, 18) / 10.0, "%", valid(0x0200)),
            "舱内臭氧": (u16(p, 20) / 100.0, "ppm", valid(0x0400)),
        }
        telemetry = {k: (None if not ok else round(value, 2), unit) for k, (value, unit, ok) in telemetry_values.items()}
        # UI-friendly telemetry with English keys and plain float values
        telemetry_ui = {}
        key_map = [
            ("舱外温度", "cabin_ext_temp"), ("舱外湿度", "cabin_ext_humi"),
            ("舱内氧气", "cabin_oxygen"), ("进水口水温", "inlet_water_temp"),
            ("水箱水温", "tank_water_temp"), ("出水口水温", "outlet_water_temp"),
            ("备用温度", "reserve_temp"), ("舱内温度", "cabin_temp"),
            ("舱内湿度", "cabin_humidity"), ("舱内臭氧", "cabin_ozone"),
        ]
        for cn_key, en_key in key_map:
            val, _ = telemetry[cn_key]
            telemetry_ui[en_key] = val
        process_time = {}
        if len(p) >= 41:
            total_s = u32(p, 33) / 1000.0
            stage_s = u32(p, 37) / 1000.0
            process_time = {
                "total_remaining_s": total_s,
                "stage_remaining_s": stage_s,
                "total_remaining_ms": int(total_s * 1000),
                "stage_remaining_ms": int(stage_s * 1000),
            }
        power = {}
        power_ui = {}
        if len(p) >= 30:
            power.update({
                "左风机": [p[24], "%"], "右风机": [p[25], "%"], "底部风机": [p[26], "%"],
                "左加热棒": [p[27], "%"], "右加热棒": [p[28], "%"], "底部加热棒": [p[29], "%"],
            })
            power_ui.update({
                "left_fan_pct": float(p[24]), "right_fan_pct": float(p[25]),
                "bottom_fan_pct": float(p[26]),
                "left_heater_pct": float(p[27]), "right_heater_pct": float(p[28]),
                "bottom_heater_pct": float(p[29]),
            })
            if len(p) >= 31:
                power["比例阀角度"] = [p[30], "°"]
                power_ui["valve_angle"] = float(p[30])
            if len(p) >= 32:
                power["顶部排风扇"] = [p[31], "%"]
                power_ui["top_exhaust_pct"] = float(p[31])
            if len(p) >= 33:
                power["增压泵压力"] = [p[32] / 10.0, "kg"]
                power_ui["booster_pressure"] = p[32] / 10.0
        sample = {
            "time": time.strftime("%H:%M:%S"),
            "seconds": time.time() - self.test_started_at if self.test_started_at else 0,
            "cabinTemp": telemetry["舱内温度"][0],
            "cabinHumidity": telemetry["舱内湿度"][0],
            "cabinOxygen": telemetry["舱内氧气"][0],
            "cabinOzone": telemetry["舱内臭氧"][0],
            "fan": max([p[24], p[25], p[26]]) if len(p) >= 30 else None,
            "heater": max([p[27], p[28], p[29]]) if len(p) >= 30 else None,
        }
        now = time.time()
        with self.lock:
            self.status["telemetry"] = telemetry
            self.status["telemetry_ui"] = telemetry_ui
            self.status["power"] = power
            self.status["power_ui"] = power_ui
            self.status["process_time"] = process_time
            self.status["flags"] = f"0x{flags:04X}"
            if self.test_running and now - self.last_sample_at >= self.sample_interval:
                self.last_sample_at = now
                self.samples.append(sample)

    def update_io(self, p):
        if len(p) < 2:
            return
        ins, outs = p[0], p[1]
        in_bytes = (ins + 7) // 8
        out_bytes = (outs + 7) // 8
        if len(p) < 2 + in_bytes + out_bytes:
            self.add_log(f"A6 IO长度不足: len={len(p)}, input_count={ins}, output_count={outs}")
            return
        input_bits = int.from_bytes(p[2:2 + in_bytes], "little")
        base = 2 + in_bytes
        output_bits = int.from_bytes(p[base:base + out_bytes], "little")
        inputs = []
        outputs = []
        for i, name in enumerate(INPUT_NAMES[:ins]):
            inputs.append({"name": name, "state": bool(input_bits & (1 << i))})
        for i, name in enumerate(OUTPUT_NAMES[:outs]):
            outputs.append({"id": i + 1, "name": name, "state": bool(output_bits & (1 << i))})
        summary = f"IO: 输入{ins} 输出{outs} · IN=0x{input_bits:0{max(1, in_bytes * 2)}X} OUT=0x{output_bits:0{max(1, out_bytes * 2)}X}"
        # Simple boolean arrays for UI
        input_bools = [bool(input_bits & (1 << i)) for i in range(ins)]
        output_bools = [bool(output_bits & (1 << i)) for i in range(outs)]
        with self.lock:
            self.status["io"] = {"inputs": inputs, "outputs": outputs}
            self.status["io_bools"] = {"inputs": input_bools, "outputs": output_bools}
            self.status["ioSummary"] = summary

    # 协议 19.2：0xA7 SELF_TEST_RESULT，14字节
    SELF_TEST_ITEMS = [
        "左风机", "右风机", "底部风机", "左加热管", "右加热管", "底部加热管",
        "增压泵", "比例阀+进水", "淋浴阀上", "淋浴阀中", "淋浴阀下", "淋浴阀后",
        "自清洁顶阀", "自清洁前上阀", "自清洁前下阀", "自清洁后阀", "旁通阀", "热水链路",
    ]

    def update_self_test(self, p, sequence):
        if len(p) < 14:
            self.add_log(f"A7自检结果长度不足: len={len(p)}")
            return
        result_id = u32(p, 0)
        mode = p[4]
        state = p[5]
        tested = u32(p, 6)
        passed = u32(p, 10)
        items = []
        for i, name in enumerate(self.SELF_TEST_ITEMS):
            if tested & (1 << i):
                items.append({"name": name, "passed": bool(passed & (1 << i))})
        mode_name = "深度" if mode == 1 else "快速"
        state_name = "安全中止" if state == 1 else "执行完成"
        fail = sum(1 for it in items if not it["passed"])
        with self.lock:
            self.status["self_test"] = {
                "result_id": result_id,
                "mode": mode,
                "mode_name": mode_name,
                "state": state,
                "state_name": state_name,
                "tested": tested,
                "passed": passed,
                "items": items,
                "summary": (f"自检结果：{mode_name}·{state_name} result_id={result_id}，"
                            f"通过{len(items) - fail}/{len(items)}"
                            + (f"，失败项：{'、'.join(it['name'] for it in items if not it['passed'])}" if fail else "")),
            }
        # 按协议 ACK: A7 00 result_id(uint32小端)，相同SEQ，否则设备每1s重发。
        ack = bytearray(6)
        ack[0] = 0xA7
        put_u32(ack, 2, result_id)
        try:
            self.send(0xF0, bytes(ack), sequence=sequence)
        except Exception as exc:
            self.add_log("A7 ACK ERROR: " + str(exc))
        self.add_log(f"自检结果: {mode_name}·{state_name} result_id={result_id}, "
                     f"通过{len(items) - fail}/{len(items)}" + (f"，失败项: {'、'.join(it['name'] for it in items if not it['passed'])}" if fail else ""))

    def update_parameters(self, p):
        if len(p) < 10:
            self.add_log(f"参数响应长度不足: len={len(p)}")
            return
        group = p[0]
        version = u16(p, 2)
        data_len = u16(p, 8)
        if len(p) < 10 + RECIPE_PAYLOAD_LENGTH or group != 1 or version != 1 or data_len != RECIPE_PAYLOAD_LENGTH:
            self.add_log(
                f"参数响应不匹配: group={group}, version={version}, data_len={data_len}, "
                f"expected={RECIPE_PAYLOAD_LENGTH}, frame_len={len(p)}"
            )
            return
        values = {}
        for field in RECIPE_FIELDS:
            offset = 10 + field["offset"]
            kind = field["kind"]
            if kind == "i16x10":
                value = i16(p, offset) / 10.0
            elif kind == "u16x10":
                value = u16(p, offset) / 10.0
            elif kind == "u8x10":
                value = p[offset] / 10.0
            elif kind == "u8":
                value = p[offset]
            else:
                value = u32(p, offset) / 1000.0
            values[field["key"]] = value
        with self.lock:
            self.status["paramRevision"] = u32(p, 4)
            self.parameters = values

    def update_interaction(self, p):
        if len(p) < 8:
            return
        pending = {"id": u32(p, 0), "type": p[4], "stage": p[5], "step": u16(p, 6), "text": describe_interaction(p[5], p[4])}
        with self.lock:
            self.pending_interaction = pending
        self.add_log(f"流程交互请求: {pending['text']}, request={pending['id']}, step={pending['step']}")

    def clear_fault(self, reason=""):
        with self.lock:
            self.status["fault"] = empty_fault_state()
            now = time.strftime("%H:%M:%S")
            for alarm in self.fault_alarms:
                if alarm.get("status") in ("active", "updated"):
                    alarm["status"] = "resolved"
                    alarm.setdefault("resolved_time", now)
            # Do NOT append a fault_code=0 entry — it shows as "0x0000 无具体故障" in UI
            self.fault_alarms = self.fault_alarms[-500:]
        self.add_log("故障显示已清除" + ((": " + reason) if reason else ""))

    def update_fault(self, p):
        if len(p) < 8:
            self.add_log(f"A4长度不足: len={len(p)}")
            return
        event_id = u32(p, 0)
        event_type = p[4]
        severity = p[5]
        fault_code = u16(p, 6)
        if event_type == 3 or fault_code == 0:
            fault = empty_fault_state()
            fault["event_id"] = event_id
            fault["event_type"] = event_type
            fault["severity"] = severity
            fault["event_type_text"] = FAULT_EVENT_TYPES.get(event_type, f"未知({event_type})")
            fault["severity_text"] = FAULT_SEVERITIES.get(severity, f"未知({severity})")
            fault["active_code"] = 0
            with self.lock:
                self.status["fault"] = fault
                # fault_code==0 means "no fault" — do NOT append to history
                if event_type == 3 and fault_code == 0:
                    self.add_log(f"故障解除: event_id={event_id}")
                else:
                    self.add_log(f"故障事件: {fault['event_type_text']} primary=0x{fault_code:04X}")
            return
        fault_name = FAULT_CODES.get(fault_code, f"未知故障码")
        fault = {
            "event_id": event_id,
            "event_type": event_type,
            "severity": severity,
            "primary_fault_code": fault_code,
            "active_code": fault_code,
            "event_type_text": FAULT_EVENT_TYPES.get(event_type, f"未知({event_type})"),
            "severity_text": FAULT_SEVERITIES.get(severity, f"未知({severity})"),
            "fault_code_text": f"0x{fault_code:04X}",
            "fault_name": fault_name,
        }
        with self.lock:
            self.status["fault"] = fault
            self.fault_alarms.append({
                "time": time.strftime("%H:%M:%S"),
                "event_id": event_id,
                "event_type": event_type,
                "severity": severity,
                "fault_code": fault_code,
                "fault_name": fault_name,
                "status": "active" if event_type == 1 else "updated",
            })
            self.fault_alarms = self.fault_alarms[-500:]
        self.add_log(f"故障事件: {fault['event_type_text']} 等级={fault['severity_text']} "
                     f"故障码={fault['fault_code_text']}({fault['fault_name']})")

    def poll_once(self):
        self.send(CMD_QUERY_STATUS)
        self.send(CMD_QUERY_IO)

    def request_param(self, recipe_index):
        self.send(CMD_GET_PARAM, bytes([1, recipe_index & 0xFF]))

    def set_mode_wait(self, mode, timeout=2.0):
        with self.lock:
            self.mode_event.clear()
            self._mode_result = None
        self.send(CMD_SET_MODE, u16_payload(mode))
        if not self.mode_event.wait(timeout):
            raise RuntimeError("切换模式超时，设备未应答")
        with self.lock:
            result = self._mode_result
        if result is None:
            raise RuntimeError("切换模式应答异常")
        ok, reason = result
        if not ok:
            raise RuntimeError(f"切换模式被拒绝：{reason}")

    def build_param_payload(self, recipe_index, revision, values):
        p = bytearray(10 + RECIPE_PAYLOAD_LENGTH)
        p[0] = 1
        p[1] = recipe_index & 0xFF
        put_u16(p, 2, 1)
        put_u32(p, 4, revision)
        put_u16(p, 8, RECIPE_PAYLOAD_LENGTH)
        for field in RECIPE_FIELDS:
            raw = float(values.get(field["key"], 0))
            raw = max(field["min"], min(field["max"], raw))
            offset = 10 + field["offset"]
            kind = field["kind"]
            if kind == "i16x10":
                put_i16(p, offset, round(raw * 10))
            elif kind == "u16x10":
                put_u16(p, offset, round(raw * 10))
            elif kind == "u8x10":
                p[offset] = round(raw * 10) & 0xFF
            elif kind == "u8":
                p[offset] = round(raw) & 0xFF
            else:
                put_u32(p, offset, round(raw * 1000))
        return bytes(p)


STATE = WorkbenchState()


def map_name(values, index):
    return values[index] if 0 <= index < len(values) else str(index)


def describe_interaction(stage, kind):
    stage_name = "入舱" if stage == 1 else ("出舱" if stage == 8 else f"阶段{stage}")
    type_name = {1: "打开上舱门", 2: "打开下舱门", 3: "关闭下舱门", 4: "关闭上舱门", 5: "继续", 6: "配方确认", 7: "自动关门"}.get(kind, f"交互{kind}")
    return stage_name + type_name


def list_ports():
    ports = sorted(set(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyS*") + glob.glob("/dev/serial/by-id/*")))
    return ports


def process_payload(action, param=0):
    return struct.pack("<HH", action, param)


def u16_payload(value):
    return struct.pack("<H", value)


def interaction_payload(request_id, kind, result, value):
    return struct.pack("<IBBH", request_id, kind, result, value)


def read_exact(ser, count):
    data = bytearray()
    while len(data) < count:
        chunk = ser.read(count - len(data))
        if not chunk:
            raise TimeoutError("串口读取超时")
        data.extend(chunk)
    return bytes(data)


def read_frame(ser):
    previous = 0
    while True:
        current = read_exact(ser, 1)[0]
        if previous == 0x55 and current == 0xAA:
            break
        previous = current
    header = read_exact(ser, 4)
    length = u16(header, 2)
    payload = read_exact(ser, length)
    crc_bytes = read_exact(ser, 2)
    raw = b"\x55\xAA" + header + payload + crc_bytes
    actual = crc16(header + payload)
    expected = u16(crc_bytes, 0)
    if actual != expected:
        raise ValueError("IAP响应CRC错误")
    STATE.add_log("RX: " + hex_bytes(raw))
    return header[0], header[1], payload


def invoke(ser, command, sequence, payload, expected, timeout=None):
    packet = build_packet(command, sequence, payload)
    old_timeout = ser.timeout
    if timeout is not None:
        ser.timeout = timeout
    ser.write(packet)
    STATE.add_log("TX: " + hex_bytes(packet))
    try:
        while True:
            cmd, seq, data = read_frame(ser)
            if seq != sequence:
                STATE.add_log(f"忽略非当前序号帧: CMD=0x{cmd:02X}, SEQ={seq}, 当前SEQ={sequence}")
                continue
            if cmd == 0xF1:
                nack = IapNack(data)
                STATE.add_log(str(nack))
                raise nack
            if cmd != expected:
                STATE.add_log(f"忽略非期望响应帧: CMD=0x{cmd:02X}, 期望=0x{expected:02X}")
                continue
            return data
    finally:
        ser.timeout = old_timeout


def invoke_ack(ser, command, sequence, payload, timeout=None):
    data = invoke(ser, command, sequence, payload, 0xF0, timeout=timeout)
    if len(data) < 6 or data[0] != command or data[1] != 0:
        raise ValueError("ACK内容错误")
    return u32(data, 2)


def query_iap(port, timeout=2.5):
    # 查询前先释放已连接的 APP 串口，避免双开同一端口导致读写串扰。
    with STATE.lock:
        connected_port = STATE.status["port"] if STATE.status["connected"] else None
    if connected_port:
        STATE.add_log(f"查询IAP前关闭APP连接，释放串口: {connected_port}")
        STATE.close()
        time.sleep(0.3)
    with serial.Serial(port, 115200, timeout=timeout, write_timeout=timeout) as ser:
        data = invoke(ser, 0x10, 0, b"", 0x90)
    if len(data) != 28:
        raise ValueError("设备信息长度错误")
    return {
        "runMode": data[0], "protocol": f"{data[1]}.{data[2]}", "appValid": data[3],
        "targetId": f"0x{u32(data, 4):08X}", "hardwareVersion": u16(data, 8),
        "bootVersion": format_version(u32(data, 10)), "appVersion": format_version(u32(data, 14)),
        "appStart": f"0x{u32(data, 18):08X}", "appCapacity": u32(data, 22), "maxChunk": u16(data, 26),
    }


def build_upgrade_start_payload(target_id, version, image_size, image_crc, chunk):
    start = bytearray(22)
    put_u32(start, 0, target_id)
    put_u16(start, 4, 1)
    put_u16(start, 6, 1)
    put_u32(start, 8, version)
    put_u32(start, 12, image_size)
    put_u32(start, 16, image_crc)
    put_u16(start, 20, chunk)
    return bytes(start)


def upgrade_iap(port, file_path, version_text):
    with STATE.lock:
        connected_port = STATE.status["port"] if STATE.status["connected"] else None
    if connected_port:
        STATE.add_log(f"升级前关闭APP连接，释放串口: {connected_port}")
        STATE.close()
        time.sleep(0.3)
    image = Path(file_path).read_bytes()
    if len(image) < 8:
        raise ValueError("App.bin文件过小")
    version = parse_version(version_text)
    image_crc = crc32(image)
    STATE.add_log(f"FILE: {file_path}, size={len(image)}, crc32=0x{image_crc:08X}")
    with serial.Serial(port, 115200, timeout=30, write_timeout=30) as ser:
        seq = 0
        resume_offset = None
        try:
            info = invoke(ser, 0x10, seq, b"", 0x90, timeout=5)
            seq = (seq + 1) & 0xFF
        except IapNack as nack:
            if not nack.is_sequence_error() or nack.expected_sequence == 0:
                raise
            info_seq = (nack.expected_sequence - 1) & 0xFF
            STATE.add_log(
                f"检测到Bootloader已有未完成升级，按设备期望序号恢复: "
                f"expected_seq={nack.expected_sequence}, next_offset={nack.next_offset}"
            )
            info = invoke(ser, 0x10, info_seq, b"", 0x90, timeout=5)
            seq = nack.expected_sequence
            if nack.expected_sequence >= 2:
                resume_offset = nack.next_offset
        run_mode = info[0]
        if run_mode == 0:
            try:
                ser.timeout = 2
                invoke_ack(ser, 0x11, seq, b"IAP1")
            except Exception:
                STATE.add_log("设备正在重启到Bootloader...")
            seq = 0
            ser.close()
            time.sleep(1.2)
            ser.open()
            info = invoke(ser, 0x10, seq, b"", 0x90, timeout=5)
            seq = (seq + 1) & 0xFF
            resume_offset = None
        if info[0] != 1:
            raise RuntimeError("设备未进入Bootloader")
        target_id = u32(info, 4)
        capacity = u32(info, 22)
        max_chunk = u16(info, 26)
        STATE.add_log(f"TARGET: 使用设备上报ID 0x{target_id:08X}")
        if len(image) > capacity:
            raise RuntimeError("固件超过APP容量")
        chunk = min(128, max_chunk)
        if resume_offset is None:
            start = build_upgrade_start_payload(target_id, version, len(image), image_crc, chunk)
            STATE.add_log("START: 正在擦除APP区，可能需要1-2分钟，请等待")
            invoke_ack(ser, 0x20, seq, start, timeout=180)
            seq = (seq + 1) & 0xFF
            resume_offset = 0
        else:
            if resume_offset > len(image):
                raise RuntimeError(f"设备续传偏移超过固件大小: {resume_offset}")
            STATE.add_log(f"继续未完成升级: seq={seq}, offset={resume_offset}")
        for offset in range(resume_offset, len(image), chunk):
            part = image[offset:offset + chunk]
            payload = bytearray(6 + len(part))
            put_u32(payload, 0, offset)
            put_u16(payload, 4, len(part))
            payload[6:] = part
            next_offset = invoke_ack(ser, 0x21, seq, bytes(payload))
            seq = (seq + 1) & 0xFF
            if next_offset != offset + len(part):
                raise ValueError("升级偏移错误")
            STATE.add_log(f"写入中 {int(next_offset * 100 / len(image))}%")
        end = bytearray(8)
        put_u32(end, 0, len(image))
        put_u32(end, 4, image_crc)
        invoke_ack(ser, 0x22, seq, bytes(end))
        STATE.add_log("升级完成，等待APP启动")


def desktop_dir():
    try:
        out = subprocess.run(["xdg-user-dir", "DESKTOP"], capture_output=True, text=True, timeout=2).stdout.strip()
        if out:
            return Path(out)
    except Exception:
        pass
    return Path.home() / "Desktop"


def save_samples():
    with STATE.lock:
        samples = list(STATE.samples)
    if not samples:
        STATE.add_log("TEST STOP: 没有收到遥测点")
        return None
    out_dir = desktop_dir() / "测试曲线"
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / (time.strftime("linux_workbench_%Y%m%d_%H%M%S") + ".csv")
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=["time", "seconds", "cabinTemp", "cabinHumidity", "cabinOxygen", "cabinOzone", "fan", "heater"])
        writer.writeheader()
        writer.writerows(samples)
    STATE.add_log("TEST SAVED: " + str(path))
    return path
