#!/usr/bin/env python3
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("pilot_hid", Path(__file__).resolve().parents[1] / "scripts/pilot-hid.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class Device:
    def __init__(self, reject=False):
        self.fragments = {}
        self.reply = []
        self.reject = reject

    def write(self, frame):
        self.assert_frame(frame)
        raw = frame[1:]
        index, count, size = module.struct.unpack(">HHH", raw[22:28])
        self.fragments[index] = raw[28:28 + size]
        if len(self.fragments) == count:
            request = json.loads(b"".join(self.fragments[i] for i in range(count)))
            response = {"type": "response", "request_id": request["request_id"],
                        "cmd": request["cmd"], "returns": {"ok": not self.reject, "log_hex": "ab" * 1024}}
            self.reply = list(reversed(module.encode_frames(raw[6:22], json.dumps(response).encode())))
            self.fragments = {}
        return len(frame)

    def assert_frame(self, frame):
        assert len(frame) == 257 and frame[0] == 0

    def read(self, length, timeout):
        # Linux may omit the leading report id.
        return self.reply.pop(0)[1:] if self.reply else b""


class HidCliTests(unittest.TestCase):
    def test_fragmented_bidirectional(self):
        reply = module.Client(Device()).call("pilot_logs", {"padding": "a" * 65000})
        self.assertEqual(len(bytes.fromhex(reply["log_hex"])), 1024)

    def test_device_error(self):
        with self.assertRaises(RuntimeError):
            module.Client(Device(reject=True)).call("pilot_status")

    def test_lengths(self):
        for length in (1, 227, 228, 65536):
            frames = module.encode_frames(b"x" * 16, b"y" * length)
            self.assertTrue(all(len(frame) == 257 for frame in frames))
        for data in (b"", b"a" * 65537):
            with self.assertRaises(ValueError):
                module.encode_frames(b"x" * 16, data)

    def test_upload_rejects_oversize_before_io(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "app"
            path.write_bytes(b"x" * (module.MAX_SIZE + 1))
            with self.assertRaises(ValueError):
                module.Client(None).upload(path)

    def test_upload_ack_and_fixed_path(self):
        calls = []
        payload = b"\x7fELF\x01\x01\x01" + b"x" * 50000

        class Fake(module.Client):
            def call(self, command, args=None, retries=3):
                calls.append((command, args))
                if command == "pilot_upload_begin":
                    return {"chunk_size": module.CHUNK_SIZE, "next_offset": 0}
                if command == "pilot_upload_chunk":
                    chunk = module.base64.b64decode(args["data"])
                    self_test.assertEqual(f"{module.zlib.crc32(chunk):08x}", args["crc32"])
                    return {"next_offset": args["offset"] + len(chunk)}
                if command == "pilot_upload_commit":
                    return {"path": "/opt/pilot/app", "size": len(payload),
                            "sha256": module.hashlib.sha256(payload).hexdigest()}
                raise AssertionError(command)

        self_test = self
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "any-local-name"
            path.write_bytes(payload)
            Fake(None).upload(path)
        self.assertEqual(calls[0][1]["name"], "app")
        self.assertEqual(calls[-1][0], "pilot_upload_commit")
        self.assertNotIn("pilot_restart", [c[0] for c in calls])


if __name__ == "__main__":
    unittest.main()
