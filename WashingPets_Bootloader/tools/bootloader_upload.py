#!/usr/bin/env python3
import argparse
import struct
import sys
import time
import zlib

import serial


HEAD = b"\x55\xAA"
RSP_DEVICE_INFO = 0x90
RSP_ACK = 0xF0
RSP_NACK = 0xF1

CMD_QUERY_INFO = 0x10
CMD_UPGRADE_START = 0x20
CMD_UPGRADE_DATA = 0x21
CMD_UPGRADE_END = 0x22
CMD_UPGRADE_ABORT = 0x23
CMD_RUN_APP = 0x25

TARGET_ID = 0x407A0C06
HW_VERSION = 0x0001
APP_ADDRESS = 0x08004000
CHUNK_SIZE = 128

ERROR_NAMES = {
    0x01: "bad command",
    0x02: "bad length",
    0x03: "bad frame CRC16",
    0x04: "bad state",
    0x05: "busy",
    0x06: "sequence error",
    0x08: "target mismatch",
    0x0A: "image size error",
    0x0B: "offset error",
    0x0C: "flash error",
    0x0D: "firmware CRC32 error",
    0x0E: "application invalid",
}


def crc16_ccitt_false(data):
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def encode_version(text):
    parts = text.split(".")
    if len(parts) != 3:
        raise ValueError("version must use MAJOR.MINOR.PATCH")
    major, minor, patch = (int(part, 0) for part in parts)
    if not all(0 <= value <= 255 for value in (major, minor, patch)):
        raise ValueError("each version component must be in range 0..255")
    return (major << 24) | (minor << 16) | (patch << 8)


def format_version(value):
    if value == 0xFFFFFFFF:
        return "none"
    return f"{value >> 24}.{(value >> 16) & 0xFF}.{(value >> 8) & 0xFF}"


def make_packet(command_id, sequence, payload=b""):
    header = struct.pack("<BBH", command_id, sequence, len(payload))
    crc = crc16_ccitt_false(header + payload)
    return HEAD + header + payload + struct.pack("<H", crc)


def read_exact(port, size):
    data = bytearray()
    while len(data) < size:
        chunk = port.read(size - len(data))
        if not chunk:
            raise TimeoutError("bootloader response timeout")
        data.extend(chunk)
    return bytes(data)


def read_frame(port):
    previous = b""
    while True:
        byte = read_exact(port, 1)
        if previous == HEAD[:1] and byte == HEAD[1:]:
            break
        previous = byte

    header = read_exact(port, 4)
    command_id, sequence, length = struct.unpack("<BBH", header)
    payload = read_exact(port, length)
    expected_crc = struct.unpack("<H", read_exact(port, 2))[0]
    actual_crc = crc16_ccitt_false(header + payload)
    if actual_crc != expected_crc:
        raise RuntimeError("response frame CRC16 mismatch")
    return command_id, sequence, payload


class BootloaderClient:
    def __init__(self, port, retries=3):
        self.port = port
        self.sequence = 0
        self.retries = retries

    def send(self, command_id, payload=b"", expected_response=RSP_ACK):
        frame = make_packet(command_id, self.sequence, payload)
        last_error = None
        for _ in range(self.retries):
            self.port.write(frame)
            self.port.flush()
            try:
                response, sequence, response_payload = read_frame(self.port)
                if sequence != self.sequence:
                    raise RuntimeError("unexpected response sequence")
                if response == RSP_NACK:
                    if len(response_payload) < 8:
                        raise RuntimeError("short NACK response")
                    request, error, expected_seq, _, next_offset = struct.unpack(
                        "<BBBBI", response_payload[:8]
                    )
                    reason = ERROR_NAMES.get(error, f"error 0x{error:02X}")
                    raise RuntimeError(
                        f"{reason}, request=0x{request:02X}, "
                        f"expected_seq={expected_seq}, next_offset={next_offset}"
                    )
                if response != expected_response:
                    raise RuntimeError(f"unexpected response 0x{response:02X}")
                self.sequence = (self.sequence + 1) & 0xFF
                return response_payload
            except TimeoutError as error:
                last_error = error
                self.port.reset_input_buffer()
        raise last_error or TimeoutError("bootloader did not respond")

    def ack_command(self, command_id, payload=b""):
        response = self.send(command_id, payload, RSP_ACK)
        if len(response) < 6:
            raise RuntimeError("short ACK response")
        request, status, next_offset = struct.unpack("<BBI", response[:6])
        if request != command_id or status != 0:
            raise RuntimeError("ACK content mismatch")
        return next_offset

    def query_info(self):
        response = self.send(CMD_QUERY_INFO, b"", RSP_DEVICE_INFO)
        if len(response) != 28:
            raise RuntimeError("invalid device info response")
        return struct.unpack("<BBBBIHI IIIH".replace(" ", ""), response)


def connect(port, timeout_seconds):
    deadline = time.monotonic() + timeout_seconds
    normal_timeout = port.timeout
    port.timeout = 0.5
    try:
        while time.monotonic() < deadline:
            port.reset_input_buffer()
            client = BootloaderClient(port)
            try:
                info = client.query_info()
                run_mode, major, minor, app_valid, target, hw, boot_ver, app_ver, app_start, capacity, chunk = info
                if run_mode != 1 or target != TARGET_ID or app_start != APP_ADDRESS:
                    raise RuntimeError("connected device is not the expected bootloader")
                return client, info
            except (TimeoutError, RuntimeError):
                time.sleep(0.2)
    finally:
        port.timeout = normal_timeout
    raise TimeoutError("bootloader not found")


def main():
    parser = argparse.ArgumentParser(description="STM32F407ZG HostCommProtocol uploader")
    parser.add_argument("port", help="serial port, for example COM5")
    parser.add_argument("image", help="App .bin linked at 0x08004000")
    parser.add_argument("--version", default="1.0.0", help="App version, default 1.0.0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--no-go", action="store_true", help="kept for compatibility; Bootloader starts the App after END")
    args = parser.parse_args()

    version = encode_version(args.version)
    with open(args.image, "rb") as image_file:
        image = image_file.read()
    if len(image) < 8:
        raise ValueError("application image is too small")

    with serial.Serial(args.port, args.baud, timeout=30, write_timeout=30) as port:
        print("Waiting for bootloader...")
        client, info = connect(port, 15.0)
        run_mode, major, minor, app_valid, target, hw, boot_ver, app_ver, app_start, capacity, max_chunk = info
        chunk_size = min(CHUNK_SIZE, max_chunk)
        print(
            f"Protocol={major}.{minor}, Target=0x{target:08X}, HW={hw}, "
            f"Boot={format_version(boot_ver)}, App={format_version(app_ver)}, "
            f"valid={app_valid}"
        )
        if len(image) > capacity:
            raise ValueError(f"image is {len(image)} bytes, capacity is {capacity} bytes")

        image_crc = zlib.crc32(image) & 0xFFFFFFFF
        start = struct.pack(
            "<IHHIIIH",
            TARGET_ID,
            HW_VERSION,
            HW_VERSION,
            version,
            len(image),
            image_crc,
            chunk_size,
        )
        print(f"Starting {args.version}, {len(image)} bytes, CRC32=0x{image_crc:08X}...")
        client.ack_command(CMD_UPGRADE_START, start)

        try:
            for offset in range(0, len(image), chunk_size):
                chunk = image[offset : offset + chunk_size]
                payload = struct.pack("<IH", offset, len(chunk)) + chunk
                next_offset = client.ack_command(CMD_UPGRADE_DATA, payload)
                if next_offset != offset + len(chunk):
                    raise RuntimeError("bootloader returned an unexpected offset")
                percent = min(100, (next_offset * 100) // len(image))
                print(f"\rWriting: {percent:3d}%", end="", flush=True)
            print()
            end = struct.pack("<II", len(image), image_crc)
            client.ack_command(CMD_UPGRADE_END, end)
        except Exception:
            try:
                client.ack_command(CMD_UPGRADE_ABORT)
            except Exception:
                pass
            raise

        print("Upgrade verified successfully. Bootloader is starting the application...")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
