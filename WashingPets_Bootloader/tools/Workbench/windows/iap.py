"""Shared Windows IAP client for PawClean (STM32F407) and Store (STM32F103).

Only the selected profile's reported flash layout is accepted. A raw .bin has
no model identifier, so the caller must also show a human confirmation.
"""

from __future__ import annotations

import struct
import time
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

HEAD = b"\x55\xaa"
QUERY_INFO, ENTER_UPGRADE, APP_CONFIRM = 0x10, 0x11, 0x12
START, DATA, END = 0x20, 0x21, 0x22
INFO, APP_READY, ACK, NACK = 0x90, 0x93, 0xF0, 0xF1


@dataclass(frozen=True)
class Profile:
    name: str
    app_start: int
    capacity: int
    ram_end: int
    boot_major: int


PROFILES = {
    "PawClean": Profile("PawClean", 0x08004000, 0x000BC000, 0x20020000, 2),
    "Store": Profile("Store", 0x08004000, 0x00003C00, 0x20002800, 1),
}


class IapError(RuntimeError):
    def __init__(self, message: str, *, code: int | None = None,
                 expected_sequence: int | None = None,
                 next_offset: int | None = None):
        super().__init__(message)
        self.code = code
        self.expected_sequence = expected_sequence
        self.next_offset = next_offset


def crc16(data: bytes) -> int:
    value = 0xFFFF
    for byte in data:
        value ^= byte << 8
        for _ in range(8):
            value = ((value << 1) ^ (0x1021 if value & 0x8000 else 0)) & 0xFFFF
    return value


def packet(command: int, sequence: int, payload: bytes = b"") -> bytes:
    body = struct.pack("<BBH", command, sequence & 0xFF, len(payload)) + payload
    return HEAD + body + struct.pack("<H", crc16(body))


def parse_version(text: str) -> int:
    try:
        parts = [int(part) for part in text.strip().split(".")]
    except ValueError as exc:
        raise IapError("版本号应为 1.2.3 格式") from exc
    if len(parts) != 3 or any(not 0 <= part <= 255 for part in parts):
        raise IapError("版本号应为 1.2.3 格式，各段范围 0..255")
    return (parts[0] << 16) | (parts[1] << 8) | parts[2]


def version_text(value: int) -> str:
    if value == 0xFFFFFFFF:
        return "--"
    return f"{(value >> 16) & 255}.{(value >> 8) & 255}.{value & 255}"


def parse_info(payload: bytes) -> dict:
    if len(payload) != 28:
        raise IapError(f"设备信息长度错误：{len(payload)}")
    return {
        "mode": payload[0], "protocol": f"{payload[1]}.{payload[2]}",
        "app_valid": bool(payload[3]),
        "target_id": struct.unpack_from("<I", payload, 4)[0],
        "hardware": struct.unpack_from("<H", payload, 8)[0],
        "boot_version": struct.unpack_from("<I", payload, 10)[0],
        "app_version": struct.unpack_from("<I", payload, 14)[0],
        "app_start": struct.unpack_from("<I", payload, 18)[0],
        "capacity": struct.unpack_from("<I", payload, 22)[0],
        "max_chunk": struct.unpack_from("<H", payload, 26)[0],
    }


def validate_device(profile: Profile, info: dict) -> None:
    if info["app_start"] != profile.app_start or info["capacity"] != profile.capacity:
        raise IapError(
            f"设备与 {profile.name} 不匹配：APP 起始地址 0x{info['app_start']:08X}，"
            f"容量 {info['capacity']} 字节")
    if (info["boot_version"] >> 16) & 255 != profile.boot_major:
        raise IapError(f"Bootloader 版本与 {profile.name} 不匹配："
                       f"{version_text(info['boot_version'])}")
    if info["max_chunk"] < 1 or info["max_chunk"] > 4096:
        raise IapError("设备上报的分包大小无效")


def inspect_image(path: str | Path, profile: Profile) -> tuple[bytes, int]:
    image = Path(path).read_bytes()
    if len(image) < 8 or len(image) > profile.capacity:
        raise IapError(f"固件大小 {len(image)} 字节，不在 {profile.name} APP 容量内")
    stack, reset = struct.unpack_from("<II", image)
    if not 0x20000000 <= stack <= profile.ram_end or stack & 3:
        raise IapError("固件向量表的初始栈地址无效")
    if not reset & 1 or not profile.app_start <= (reset & ~1) < profile.app_start + len(image):
        raise IapError("固件向量表的复位入口无效")
    return image, zlib.crc32(image) & 0xFFFFFFFF


def port_names() -> list[str]:
    from serial.tools import list_ports
    return sorted({port.device for port in list_ports.comports()})


class IapClient:
    def __init__(self, log: Callable[[str], None] = lambda _: None,
                 progress: Callable[[int, str], None] = lambda *_: None,
                 serial_factory=None):
        self.log = log
        self.progress = progress
        self.serial_factory = serial_factory

    def _open(self, port: str, baudrate: int):
        if baudrate <= 0:
            raise IapError("波特率必须为正数")
        if self.serial_factory is not None:
            return self.serial_factory(port, baudrate)
        import serial
        return serial.Serial(port, baudrate, bytesize=8, parity="N", stopbits=1,
                             timeout=0.2, write_timeout=3)

    @staticmethod
    def _exact(ser, count: int, deadline: float) -> bytes:
        data = bytearray()
        while len(data) < count:
            if time.monotonic() >= deadline:
                raise TimeoutError("串口响应超时")
            part = ser.read(count - len(data))
            if part:
                data.extend(part)
        return bytes(data)

    def _read_frame(self, ser, deadline: float) -> tuple[int, int, bytes]:
        prev = 0
        while True:
            value = self._exact(ser, 1, deadline)[0]
            if prev == 0x55 and value == 0xAA:
                break
            prev = value
        header = self._exact(ser, 4, deadline)
        command, sequence, size = struct.unpack("<BBH", header)
        if size > 4096:
            raise IapError("响应帧长度异常")
        payload = self._exact(ser, size, deadline)
        check = self._exact(ser, 2, deadline)
        raw = HEAD + header + payload + check
        self.log("RX: " + raw.hex(" ").upper())
        if crc16(header + payload) != struct.unpack("<H", check)[0]:
            raise IapError("响应帧 CRC16 错误")
        return command, sequence, payload

    def invoke(self, ser, command: int, sequence: int, payload: bytes,
               expected: int, timeout: float = 3, retries: int = 2) -> bytes:
        frame = packet(command, sequence, payload)
        for attempt in range(retries + 1):
            ser.write(frame)
            ser.flush()
            self.log("TX: " + frame.hex(" ").upper())
            deadline = time.monotonic() + timeout
            try:
                while True:
                    rsp, rsp_seq, body = self._read_frame(ser, deadline)
                    if rsp_seq != sequence:
                        self.log(f"忽略其他序号帧：{rsp_seq}")
                        continue
                    if rsp == NACK:
                        if len(body) < 8:
                            raise IapError("设备返回短 NACK")
                        request, code, expected_seq, _, offset = struct.unpack_from("<BBBBI", body)
                        raise IapError(f"设备拒绝 0x{request:02X}：错误 0x{code:02X}，"
                                       f"期望序号 {expected_seq}，偏移 {offset}",
                                       code=code, expected_sequence=expected_seq,
                                       next_offset=offset)
                    if rsp == expected:
                        return body
                    self.log(f"忽略非期望响应 0x{rsp:02X}")
            except TimeoutError:
                if attempt == retries:
                    raise IapError(f"命令 0x{command:02X} 响应超时，重试已用尽")
                self.log(f"命令 0x{command:02X} 超时，同序号重发 {attempt + 1}/{retries}")
        raise AssertionError("unreachable")

    def ack(self, ser, command: int, sequence: int, payload: bytes = b"",
            timeout: float = 3) -> int:
        body = self.invoke(ser, command, sequence, payload, ACK, timeout)
        if len(body) < 6 or body[0] != command or body[1] != 0:
            raise IapError("ACK 内容错误")
        return struct.unpack_from("<I", body, 2)[0]

    def query(self, ser, sequence: int = 0) -> dict:
        return parse_info(self.invoke(ser, QUERY_INFO, sequence, b"", INFO, 4))

    def query_port(self, port: str, baudrate: int, profile: Profile) -> dict:
        with self._open(port, baudrate) as ser:
            info = self.query(ser)
            validate_device(profile, info)
            return info

    def _wait_app(self, ser, version: int, size: int, crc: int,
                  target_id: int, sequence: int) -> None:
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            try:
                command, _, payload = self._read_frame(ser, min(deadline, time.monotonic() + .5))
            except TimeoutError:
                continue
            if command == APP_READY and len(payload) == 22:
                found_target, hardware, found_version, found_size, found_crc = struct.unpack_from("<IHIII", payload, 4)
                matched = (found_target, found_version, found_size, found_crc) == (target_id, version, size, crc)
                self.ack(ser, APP_CONFIRM, sequence, struct.pack("<BBII", 0 if matched else 1, 0, version, crc))
                if not matched:
                    raise IapError("APP_READY 的设备 ID、版本、长度或 CRC32 不匹配")
                self.log(f"APP_READY: 硬件 {hardware}，版本 {version_text(found_version)}")
                return
        self.log("未收到 APP_READY，重新查询 APP 信息")
        # APP has restarted; its request sequence may begin at zero.
        info = self.query(ser, 0)
        if info["mode"] != 0 or info["target_id"] != target_id or info["app_version"] != version:
            raise IapError("升级后 APP 版本或设备 ID 校验失败")
        self.ack(ser, APP_CONFIRM, 1, struct.pack("<BBII", 0, 0, version, crc))

    def upgrade(self, port: str, baudrate: int, profile: Profile,
                path: str | Path, version_text_input: str, target_id: int,
                chunk_size: int = 128) -> None:
        image, image_crc = inspect_image(path, profile)
        version = parse_version(version_text_input)
        self.log(f"FILE: {path}；SIZE: {len(image)}；CRC32: 0x{image_crc:08X}")
        with self._open(port, baudrate) as ser:
            sequence = 0
            try:
                info = self.query(ser, sequence)
                sequence = 1
            except IapError as exc:
                if exc.code != 0x06 or not exc.expected_sequence:
                    raise
                sequence = exc.expected_sequence
                info = self.query(ser, (sequence - 1) & 255)
                self.log(f"检测到未完成会话，序号 {sequence}，偏移 {exc.next_offset}；"
                         "原始 BIN 无法核对旧会话 CRC，重新擦除后从头升级")
            validate_device(profile, info)
            if info["target_id"] != target_id:
                raise IapError(f"目标 ID 不匹配：设备 0x{info['target_id']:08X}，输入 0x{target_id:08X}")
            if len(image) > info["capacity"]:
                raise IapError("固件超过设备 APP 容量")
            if info["mode"] == 0:
                try:
                    self.ack(ser, ENTER_UPGRADE, sequence, b"IAP1", 3)
                except (IapError, TimeoutError) as exc:
                    self.log(f"设备可能正在复位：{exc}")
                ser.close()
                time.sleep(1.2)
                ser.open()
                info = self.query(ser, 0)
                validate_device(profile, info)
                sequence = 1
            if info["mode"] != 1:
                raise IapError("设备未进入 Bootloader")
            chunk = min(chunk_size, info["max_chunk"])
            if chunk < 1:
                raise IapError("分包大小无效")
            payload = struct.pack("<IHHIIIH", target_id, info["hardware"],
                                  info["hardware"], version, len(image), image_crc, chunk)
            self.progress(0, "擦除 APP 区")
            offset = self.ack(ser, START, sequence, payload, 180)
            if offset != 0:
                raise IapError("START 返回非零偏移")
            sequence = (sequence + 1) & 255
            while offset < len(image):
                part = image[offset:offset + chunk]
                body = struct.pack("<IH", offset, len(part)) + part
                next_offset = self.ack(ser, DATA, sequence, body, 30)
                if next_offset != offset + len(part):
                    raise IapError(f"DATA 偏移错误：设备 {next_offset}，预期 {offset + len(part)}")
                offset, sequence = next_offset, (sequence + 1) & 255
                self.progress(int(offset * 100 / len(image)), f"写入 {offset}/{len(image)}")
            try:
                self.ack(ser, END, sequence, struct.pack("<II", len(image), image_crc), 180)
            except IapError as exc:
                if "响应超时" not in str(exc):
                    raise
                self.log("END ACK 丢失，继续验证 APP 启动结果")
            sequence = (sequence + 1) & 255
            self.progress(100, "等待 APP 启动确认")
            self._wait_app(ser, version, len(image), image_crc, target_id, sequence)
        self.progress(100, "升级完成")
