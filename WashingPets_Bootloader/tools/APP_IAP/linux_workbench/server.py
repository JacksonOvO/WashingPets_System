#!/usr/bin/env python3
import csv
import glob
import struct
import threading
import time
from pathlib import Path

import serial

ROOT = Path(__file__).resolve().parent
APP_IAP_DIR = ROOT.parent
DEFAULT_BIN = APP_IAP_DIR / "APP.bin"

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
    "OUT1 排气扇", "OUT2 制氧机", "OUT3 空气清新剂", "OUT4 手动清洁电磁阀", "OUT5 增压泵",
    "OUT6 热水器", "OUT7 自来水阀", "OUT8 蠕动泵", "OUT9 淋浴阀上", "OUT10 淋浴阀中",
    "OUT11 淋浴阀下", "OUT12 淋浴阀后", "OUT13 沐浴液阀", "OUT14 芬香液阀", "OUT15 柔顺液阀",
    "OUT16 除螨液阀", "OUT17 护毛素阀", "OUT18 消毒液阀", "OUT19 紫外线灯", "OUT20 自清洁顶部阀",
    "OUT21 自清洁前侧上阀", "OUT22 自清洁前侧下阀", "OUT23 自清洁后端阀", "OUT24 旁通阀",
    "OUT25 臭氧发生器", "OUT26 舱内照明", "OUT27 正门灯带", "OUT28 LOGO发光板", "OUT29 正门加热",
    "OUT30 下门电动推杆", "OUT31 上门闭门器", "OUT32 上门电磁锁", "OUT33 下门电磁锁",
    "OUT34 左电插锁", "OUT35 右电插锁", "OUT36 后电插锁1", "OUT37 后电插锁2",
    "OUT38 下门灯板", "OUT39 备用输出", "OUT40 主板电源控制",
]

STAGES = ["0 入舱准备", "1 入舱", "2 洗前准备", "3 冲洗", "4 沐浴", "5 清洗", "6 吹干", "7 出舱准备", "8 出舱"]
RS485_STATIONS = ["1 增压泵压力/启停", "2 比例阀角度", "10 左风机", "11 右风机", "12 底部风机", "13 顶部排风扇", "14 左加热器", "15 右加热器", "16 底部加热器"]
RECIPE_PAYLOAD_LENGTH = 68
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
        self.status = {"connected": False, "port": "", "metrics": {}, "telemetry": {}, "power": {}, "flags": "--", "io": {"inputs": [], "outputs": []}, "paramRevision": 0}
        self.parameters = {}
        self.pending_interaction = None
        self.samples = []
        self.test_running = False
        self.test_started_at = 0
        self.last_sample_at = 0
        self.sample_interval = 30.0
        self.last_poll = 0

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
                "power": dict(self.status["power"]),
                "flags": self.status["flags"],
                "io": {"inputs": list(self.status["io"]["inputs"]), "outputs": list(self.status["io"]["outputs"])},
                "parameters": dict(self.parameters),
                "paramRevision": self.status["paramRevision"],
                "pendingInteraction": self.pending_interaction,
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
        elif command == 0xF0:
            if len(payload) >= 6:
                request = payload[0]
                detail = u32(payload, 2)
                self.add_log(f"ACK request=0x{request:02X}, detail={detail}")
                if request in (CMD_SET_PARAM, CMD_RESTORE_PARAM):
                    with self.lock:
                        self.status["paramRevision"] = detail
                if request == CMD_RESTORE_PARAM:
                    self.request_param(0)
        elif command == 0xF1:
            self.add_log("NACK: " + hex_bytes(payload))

    def update_status(self, p):
        if len(p) < 12:
            return
        modes = ["空闲", "手动", "自动"]
        states = ["空闲", "启动", "运行", "等待", "暂停", "停止中", "完成", "故障"]
        processes = ["无", "完整自动", "单流程", "自清洁", "手动"]
        stages = ["入舱准备", "入舱", "洗前准备", "冲洗", "沐浴", "清洗", "吹干", "出舱准备", "出舱"]
        step_states = ["空闲", "运行", "等待", "暂停", "完成", "错误"]
        process = map_name(processes, p[2])
        stage = "自清洁" if p[2] == 3 else ("无" if p[2] == 0 else map_name(stages, p[3]))
        metrics = {
            "模式": map_name(modes, p[0]),
            "设备状态": map_name(states, p[1]),
            "运行类型": process,
            "当前流程": stage,
            "步骤": str(u16(p, 4)),
            "步骤状态": map_name(step_states, p[6]),
            "配方": f"配方{p[7] + 1}",
            "进度": f"{u16(p, 8) / 10.0:.1f}%",
        }
        with self.lock:
            self.status["metrics"] = metrics

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
        power = {}
        if len(p) >= 30:
            power.update({
                "左风机": [p[24], "%"], "右风机": [p[25], "%"], "底部风机": [p[26], "%"],
                "左加热棒": [p[27], "%"], "右加热棒": [p[28], "%"], "底部加热棒": [p[29], "%"],
            })
            if len(p) >= 31:
                power["比例阀角度"] = [p[30], "°"]
            if len(p) >= 32:
                power["顶部排风扇"] = [p[31], "%"]
            if len(p) >= 33:
                power["增压泵压力"] = [p[32] / 10.0, "kg"]
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
            self.status["power"] = power
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
            return
        inputs = []
        outputs = []
        for i, name in enumerate(INPUT_NAMES[:ins]):
            inputs.append({"name": name, "state": bool(p[2 + i // 8] & (1 << (i % 8)))})
        base = 2 + in_bytes
        for i, name in enumerate(OUTPUT_NAMES[:outs]):
            outputs.append({"id": i + 1, "name": name, "state": bool(p[base + i // 8] & (1 << (i % 8)))})
        with self.lock:
            self.status["io"] = {"inputs": inputs, "outputs": outputs}

    def update_parameters(self, p):
        if len(p) < 10 + RECIPE_PAYLOAD_LENGTH or p[0] != 1 or u16(p, 2) != 1 or u16(p, 8) != RECIPE_PAYLOAD_LENGTH:
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

    def poll_once(self):
        self.send(CMD_QUERY_STATUS)
        self.send(CMD_QUERY_IO)

    def request_param(self, recipe_index):
        self.send(CMD_GET_PARAM, bytes([1, recipe_index & 0xFF]))

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


def invoke(ser, command, sequence, payload, expected):
    packet = build_packet(command, sequence, payload)
    ser.write(packet)
    STATE.add_log("TX: " + hex_bytes(packet))
    while True:
        cmd, seq, data = read_frame(ser)
        if seq != sequence:
            STATE.add_log(f"忽略非当前序号帧: CMD=0x{cmd:02X}, SEQ={seq}, 当前SEQ={sequence}")
            continue
        if cmd == 0xF1:
            raise RuntimeError("设备返回NACK: " + hex_bytes(data))
        if cmd != expected:
            STATE.add_log(f"忽略非期望响应帧: CMD=0x{cmd:02X}, 期望=0x{expected:02X}")
            continue
        return data


def invoke_ack(ser, command, sequence, payload):
    data = invoke(ser, command, sequence, payload, 0xF0)
    if len(data) < 6 or data[0] != command or data[1] != 0:
        raise ValueError("ACK内容错误")
    return u32(data, 2)


def query_iap(port, timeout=2.5):
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


def upgrade_iap(port, file_path, version_text):
    image = Path(file_path).read_bytes()
    if len(image) < 8:
        raise ValueError("App.bin文件过小")
    version = parse_version(version_text)
    image_crc = crc32(image)
    STATE.add_log(f"FILE: {file_path}, size={len(image)}, crc32=0x{image_crc:08X}")
    with serial.Serial(port, 115200, timeout=30, write_timeout=30) as ser:
        seq = 0
        info = invoke(ser, 0x10, seq, b"", 0x90)
        seq = (seq + 1) & 0xFF
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
            info = invoke(ser, 0x10, seq, b"", 0x90)
            seq = (seq + 1) & 0xFF
        if info[0] != 1:
            raise RuntimeError("设备未进入Bootloader")
        target_id = u32(info, 4)
        capacity = u32(info, 22)
        max_chunk = u16(info, 26)
        if target_id != 0x407A0C06:
            raise RuntimeError("目标设备不匹配")
        if len(image) > capacity:
            raise RuntimeError("固件超过APP容量")
        chunk = min(128, max_chunk)
        start = bytearray(22)
        put_u32(start, 0, 0x407A0C06)
        put_u16(start, 4, 1)
        put_u16(start, 6, 1)
        put_u32(start, 8, version)
        put_u32(start, 12, len(image))
        put_u32(start, 16, image_crc)
        put_u16(start, 20, chunk)
        invoke_ack(ser, 0x20, seq, bytes(start))
        seq = (seq + 1) & 0xFF
        for offset in range(0, len(image), chunk):
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


def save_samples():
    with STATE.lock:
        samples = list(STATE.samples)
    if not samples:
        STATE.add_log("TEST STOP: 没有收到遥测点")
        return
    out_dir = Path.home() / "Desktop" / "测试曲线"
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / (time.strftime("linux_workbench_%Y%m%d_%H%M%S") + ".csv")
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=["time", "seconds", "cabinTemp", "cabinHumidity", "cabinOxygen", "cabinOzone", "fan", "heater"])
        writer.writeheader()
        writer.writerows(samples)
    STATE.add_log("TEST SAVED: " + str(path))
