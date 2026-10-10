"""Protocol tests runnable without Qt or physical serial hardware."""

import struct
import tempfile
import unittest
from pathlib import Path

from .iap import (ACK, APP_CONFIRM, APP_READY, DATA, END, INFO, NACK, QUERY_INFO,
                  START, IapClient, IapError, PROFILES, crc16, inspect_image,
                  packet, parse_info, parse_version, validate_device)


def make_info(profile, *, mode=1, target=0x12345678):
    return struct.pack("<BBBBIHIIIIH", mode, 1, 0, 1, target, 1,
                       profile.boot_major << 16, 0x00010000,
                       profile.app_start, profile.capacity, 128)


class Device:
    def __init__(self, profile, image):
        self.profile = profile
        self.image = image
        self.crc = None
        self.offset = 0
        self.sequence = 0
        self.mode = 1
        self.buffer = bytearray()
        self.queued = bytearray()
        self.started = False
        self.drop_data_ack = False
        self.last = None
        self.port = "COM7"
        self.baudrate = 115200

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def close(self):
        pass

    def open(self):
        pass

    def flush(self):
        pass

    def read(self, count):
        data = bytes(self.queued[:count])
        del self.queued[:count]
        return data

    def respond(self, command, sequence, payload):
        self.queued += packet(command, sequence, payload)

    def write(self, raw):
        assert raw[:2] == b"\x55\xaa"
        command, sequence, size = struct.unpack_from("<BBH", raw, 2)
        payload = raw[6:6 + size]
        assert crc16(raw[2:-2]) == struct.unpack_from("<H", raw, len(raw) - 2)[0]
        if command == QUERY_INFO and sequence == ((self.sequence - 1) & 255):
            self.respond(INFO, sequence, make_info(self.profile, mode=self.mode))
            return len(raw)
        if self.last == (command, sequence, payload):
            self.respond(ACK, sequence, struct.pack("<BBI", command, 0, self.offset))
            return len(raw)
        if sequence != self.sequence:
            self.respond(NACK, sequence, struct.pack("<BBBBI", command, 6,
                                                   self.sequence, 0, self.offset))
            return len(raw)
        if command == QUERY_INFO:
            self.respond(INFO, sequence, make_info(self.profile, mode=self.mode))
        elif command == START:
            target, hw_min, hw_max, version, size, crc, chunk = struct.unpack("<IHHIIIH", payload)
            assert target == 0x12345678 and hw_min <= 1 <= hw_max
            assert size == len(self.image) and chunk <= 128
            self.crc = crc
            self.offset = 0
            self.started = True
            self.respond(ACK, sequence, struct.pack("<BBI", command, 0, 0))
        elif command == DATA:
            offset, size = struct.unpack_from("<IH", payload)
            assert offset == self.offset and payload[6:] == self.image[offset:offset + size]
            self.offset += size
            self.respond(ACK, sequence, struct.pack("<BBI", command, 0, self.offset))
        elif command == END:
            size, crc = struct.unpack("<II", payload)
            assert size == self.offset == len(self.image) and crc == self.crc
            self.respond(ACK, sequence, struct.pack("<BBI", command, 0, self.offset))
            ready = bytearray(22)
            struct.pack_into("<IHIII", ready, 4, 0x12345678, 1,
                             self.version, size, crc)
            self.queued += packet(APP_READY, 0, bytes(ready))
            self.mode = 0
        elif command == APP_CONFIRM:
            self.respond(ACK, sequence, struct.pack("<BBI", command, 0, self.offset))
        else:
            raise AssertionError(f"Unexpected command {command}")
        self.last = command, sequence, payload
        self.sequence = (self.sequence + 1) & 255
        if command == DATA and self.drop_data_ack:
            self.queued.clear()
            self.drop_data_ack = False
        return len(raw)


class IapTests(unittest.TestCase):
    def setUp(self):
        self.profile = PROFILES["Store"]
        self.image = struct.pack("<II", 0x20002000, 0x08004009) + bytes(range(248))

    def test_info_profiles_and_vectors(self):
        info = parse_info(make_info(self.profile))
        validate_device(self.profile, info)
        with self.assertRaises(IapError):
            validate_device(PROFILES["PawClean"], info)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "app.bin"
            path.write_bytes(self.image)
            data, crc = inspect_image(path, self.profile)
            self.assertEqual(data, self.image)
            self.assertIsInstance(crc, int)
            path.write_bytes(struct.pack("<II", 0x10000000, 0x08004009) + self.image[8:])
            with self.assertRaises(IapError):
                inspect_image(path, self.profile)

    def test_version_and_crc(self):
        self.assertEqual(parse_version("2.3.19"), 0x00020313)
        self.assertEqual(crc16(b"123456789"), 0x29B1)
        with self.assertRaises(IapError):
            parse_version("1.256.0")

    def test_full_upgrade(self):
        device = Device(self.profile, self.image)
        device.version = parse_version("1.2.0")
        progress = []
        client = IapClient(progress=lambda percent, _: progress.append(percent),
                           serial_factory=lambda *_: device)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "app.bin"
            path.write_bytes(self.image)
            client.upgrade("COM7", 115200, self.profile, path, "1.2.0", 0x12345678)
        self.assertTrue(device.started)
        self.assertEqual(device.offset, len(self.image))
        self.assertEqual(progress[-1], 100)

    def test_existing_session_restarts_from_zero(self):
        device = Device(self.profile, self.image)
        device.version = parse_version("1.2.0")
        device.sequence = 4
        device.offset = 128
        client = IapClient(serial_factory=lambda *_: device)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "app.bin"
            path.write_bytes(self.image)
            client.upgrade("COM7", 115200, self.profile, path, "1.2.0", 0x12345678)
        self.assertEqual(device.offset, len(self.image))

    def test_wrong_device_profile_rejected_before_erase(self):
        device = Device(self.profile, self.image)
        client = IapClient(serial_factory=lambda *_: device)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "app.bin"
            path.write_bytes(self.image)
            with self.assertRaises(IapError):
                client.upgrade("COM7", 115200, PROFILES["PawClean"],
                               path, "1.2.0", 0x12345678)
        self.assertFalse(device.started)

    def test_lost_data_ack_is_retried_with_same_sequence(self):
        device = Device(self.profile, self.image)
        device.started = True
        device.drop_data_ack = True
        client = IapClient(serial_factory=lambda *_: device)
        payload = struct.pack("<IH", 0, 16) + self.image[:16]
        reply = client.invoke(device, DATA, 0, payload, ACK, timeout=.01)
        self.assertEqual(struct.unpack_from("<I", reply, 2)[0], 16)
        self.assertEqual(device.offset, 16)

    def test_sequence_nack_is_parsed(self):
        device = Device(self.profile, self.image)
        device.sequence = 3
        client = IapClient(serial_factory=lambda *_: device)
        with self.assertRaises(IapError) as raised:
            client.query(device, 0)
        self.assertEqual(raised.exception.code, 6)
        self.assertEqual(raised.exception.expected_sequence, 3)

    def test_target_id_rejected_before_erase(self):
        device = Device(self.profile, self.image)
        client = IapClient(serial_factory=lambda *_: device)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "app.bin"
            path.write_bytes(self.image)
            with self.assertRaises(IapError):
                client.upgrade("COM7", 115200, self.profile, path, "1.2.0", 1)
        self.assertFalse(device.started)


if __name__ == "__main__":
    unittest.main()
