#!/usr/bin/env python3
import argparse
import struct
import zlib


APP_ADDRESS = 0x08004000
METADATA_ADDRESS = 0x080E0000
APP_CAPACITY = METADATA_ADDRESS - APP_ADDRESS
METADATA_MAGIC = 0x424C4D44
STATUS_VALID = 0x3FFFFFFF
TARGET_ID = 0x407A0C06


def encode_version(text):
    parts = [int(part, 0) for part in text.split(".")]
    if len(parts) != 3 or not all(0 <= value <= 255 for value in parts):
        raise ValueError("version must use MAJOR.MINOR.PATCH with values 0..255")
    return (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8)


def read_hex(path):
    memory = {}
    upper = 0
    with open(path, "r", encoding="ascii") as source:
        for line_number, text in enumerate(source, 1):
            line = text.strip()
            if not line:
                continue
            if not line.startswith(":"):
                raise ValueError(f"invalid Intel HEX record at {path}:{line_number}")
            raw = bytes.fromhex(line[1:])
            if (sum(raw) & 0xFF) != 0:
                raise ValueError(f"invalid Intel HEX checksum at {path}:{line_number}")
            count = raw[0]
            address = (raw[1] << 8) | raw[2]
            record_type = raw[3]
            data = raw[4 : 4 + count]
            if record_type == 0:
                for offset, value in enumerate(data):
                    memory[upper + address + offset] = value
            elif record_type == 4:
                upper = int.from_bytes(data, "big") << 16
            elif record_type == 1:
                break
    return memory


def make_record(address, record_type, data=b""):
    body = bytes((len(data), address >> 8, address & 0xFF, record_type)) + data
    return ":" + (body + bytes(((-sum(body)) & 0xFF,))).hex().upper()


def write_hex(path, memory):
    addresses = sorted(memory)
    with open(path, "w", encoding="ascii", newline="\n") as output:
        current_upper = None
        index = 0
        while index < len(addresses):
            start = addresses[index]
            upper = start >> 16
            if upper != current_upper:
                output.write(make_record(0, 4, upper.to_bytes(2, "big")) + "\n")
                current_upper = upper
            data = bytearray((memory[start],))
            index += 1
            while (index < len(addresses) and
                   addresses[index] == start + len(data) and
                   (addresses[index] >> 16) == upper and len(data) < 16):
                data.append(memory[addresses[index]])
                index += 1
            output.write(make_record(start & 0xFFFF, 0, bytes(data)) + "\n")
        output.write(make_record(0, 1) + "\n")


def merge(memory, additions):
    for address, value in additions.items():
        if address in memory and memory[address] != value:
            raise ValueError(f"conflicting data at 0x{address:08X}")
        memory[address] = value


def main():
    parser = argparse.ArgumentParser(description="Create STM32F407 Factory_All.hex")
    parser.add_argument("bootloader_hex")
    parser.add_argument("app_hex", help="App.hex linked at 0x08004000")
    parser.add_argument("app_bin", help="matching App.bin used for CRC metadata")
    parser.add_argument("output_hex")
    parser.add_argument("--version", default="1.0.0")
    args = parser.parse_args()

    with open(args.app_bin, "rb") as source:
        image = source.read()
    if len(image) < 8 or len(image) > APP_CAPACITY:
        raise ValueError(f"App.bin size must be between 8 and {APP_CAPACITY} bytes")

    app_memory = read_hex(args.app_hex)
    if (not app_memory or min(app_memory) < APP_ADDRESS or
            max(app_memory) >= METADATA_ADDRESS):
        raise ValueError("App.hex is not linked inside the configured App region")
    for offset, value in enumerate(image):
        if app_memory.get(APP_ADDRESS + offset, 0xFF) != value:
            raise ValueError("App.hex and App.bin do not contain the same App image")

    memory = read_hex(args.bootloader_hex)
    merge(memory, app_memory)
    image_crc = zlib.crc32(image) & 0xFFFFFFFF
    metadata_head = struct.pack(
        "<IIIII", METADATA_MAGIC, TARGET_ID, encode_version(args.version),
        len(image), image_crc
    )
    metadata = metadata_head + struct.pack(
        "<II", zlib.crc32(metadata_head) & 0xFFFFFFFF, STATUS_VALID
    )
    merge(memory, {METADATA_ADDRESS + i: value for i, value in enumerate(metadata)})
    write_hex(args.output_hex, memory)
    print(f"Created {args.output_hex}: {len(image)} bytes, CRC32=0x{image_crc:08X}")


if __name__ == "__main__":
    main()
