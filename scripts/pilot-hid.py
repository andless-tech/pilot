#!/usr/bin/env python3
"""Upload the single Pilot application and inspect its bounded runtime log."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import struct
import sys
import time
import uuid
import zlib

MAX_SIZE = 1024 * 1024
CHUNK_SIZE = 24 * 1024
FRAGMENT_SIZE = 227


def encode_frames(message_id, message):
    if not 0 < len(message) <= 65536:
        raise ValueError("Invalid HID message length")
    count = (len(message) + FRAGMENT_SIZE - 1) // FRAGMENT_SIZE
    frames = []
    for index in range(count):
        chunk = message[index * FRAGMENT_SIZE:(index + 1) * FRAGMENT_SIZE]
        payload = (b"AND2\x01\x00" + message_id +
                   struct.pack(">HHH", index, count, len(chunk)) + chunk)
        frames.append(b"\x00" + payload.ljust(256, b"\x00"))
    return frames


class Client:
    def __init__(self, device):
        self.device = device

    def call(self, command, args=None, retries=3):
        request_id = str(uuid.uuid4())
        message = json.dumps({"type": "request", "cmd": command,
                              "request_id": request_id, "args": args or {}},
                             separators=(",", ":")).encode()
        # Reuse transaction metadata on retry. Chunk and commit are idempotent.
        for attempt in range(retries):
            message_id = uuid.uuid4().bytes
            for frame in encode_frames(message_id, message):
                if self.device.write(frame) != len(frame):
                    raise OSError("HID short write")
            deadline = time.monotonic() + 8
            chunks, expected = {}, None
            while time.monotonic() < deadline:
                raw = bytes(self.device.read(257, 100))
                if raw[:1] in (b"\x00", b"\x30") and raw[1:5] == b"AND2":
                    raw = raw[1:]
                if len(raw) < 28 or raw[:6] != b"AND2\x01\x00" or raw[6:22] != message_id:
                    continue
                index, count, size = struct.unpack(">HHH", raw[22:28])
                if (not 0 < count <= 289 or index >= count or size > FRAGMENT_SIZE or
                        len(raw) < 28 + size or (index < count - 1 and size != FRAGMENT_SIZE) or
                        (expected is not None and count != expected)):
                    raise ValueError("Invalid HID response framing")
                expected = count
                chunks[index] = raw[28:28 + size]
                if len(chunks) != expected:
                    continue
                body = json.loads(b"".join(chunks[i] for i in range(expected)))
                if (body.get("request_id") != request_id or body.get("cmd") != command or
                        body.get("type") != "response"):
                    raise ValueError("HID response identity mismatch")
                result = body.get("returns", {})
                if result.get("ok") is not True:
                    raise RuntimeError(f"{result.get('code', 'error')}: {result.get('error', result)}")
                return result
            if attempt + 1 == retries:
                raise TimeoutError(f"{command}: device did not respond")

    def upload(self, path):
        with path.open("rb") as file:
            data = file.read(MAX_SIZE + 1)
        if not 0 < len(data) <= MAX_SIZE:
            raise ValueError("程序大小必须为 1..1,048,576 字节")
        if data[:7] != b"\x7fELF\x01\x01\x01":
            raise ValueError("请使用 Pilot 工具链生成的 ARM32 小端 ELF 程序")
        digest = hashlib.sha256(data).hexdigest()
        transaction = uuid.uuid4().hex
        begin = self.call("pilot_upload_begin", {
            "upload_id": transaction, "name": "app", "size": len(data),
            "sha256": digest, "executable": True})
        try:
            size = begin["chunk_size"]
            offset = begin["next_offset"]
            if not isinstance(size, int) or not 0 < size <= CHUNK_SIZE or offset != 0:
                raise ValueError("Invalid device upload limits")
            while offset < len(data):
                chunk = data[offset:offset + size]
                reply = self.call("pilot_upload_chunk", {
                    "upload_id": transaction, "offset": offset,
                    "crc32": f"{zlib.crc32(chunk):08x}",
                    "data": base64.b64encode(chunk).decode("ascii")})
                if reply["next_offset"] != offset + len(chunk):
                    raise ValueError("Invalid upload acknowledgement")
                offset += len(chunk)
                print(f"\r上传 {offset}/{len(data)} 字节", end="", file=sys.stderr, flush=True)
            reply = self.call("pilot_upload_commit", {"upload_id": transaction})
            if (reply["path"] != "/opt/pilot/app" or reply["size"] != len(data) or
                    reply["sha256"].lower() != digest):
                raise ValueError("Installed file verification mismatch")
            print(file=sys.stderr)
            return reply
        except BaseException:
            try:
                self.call("pilot_upload_abort", {"upload_id": transaction}, retries=1)
            except Exception:
                pass
            raise


def main():
    parser = argparse.ArgumentParser(description="Pilot 程序上传、运行状态和日志")
    parser.add_argument("--serial", help="多设备时指定 USB 序列号")
    sub = parser.add_subparsers(dest="command", required=True)
    upload = sub.add_parser("upload", help="覆盖上传，下次启动生效")
    upload.add_argument("file", type=Path)
    upload.add_argument("--restart", action="store_true", help="上传成功后重启用户程序，不重启开发板")
    for name in ("status", "logs", "restart", "stop"):
        sub.add_parser(name)
    args = parser.parse_args()
    try:
        import hid
    except ImportError:
        parser.error("请先安装依赖：python -m pip install hidapi")
    devices = [d for d in hid.enumerate(0x2207, 0x0019)
               if not args.serial or d.get("serial_number") == args.serial]
    if len(devices) != 1:
        parser.error(f"找到 {len(devices)} 个设备；请连接一台设备或使用 --serial 指定")
    device = hid.device()
    try:
        device.open_path(devices[0]["path"])
        client = Client(device)
        if args.command == "upload":
            result = client.upload(args.file)
            print(json.dumps(result, ensure_ascii=False))
            if args.restart:
                print(json.dumps(client.call("pilot_restart", retries=1), ensure_ascii=False))
        elif args.command == "logs":
            result = client.call("pilot_logs")
            data = bytes.fromhex(result["log_hex"])
            if len(data) > 1024:
                raise ValueError("Invalid log size")
            print(data.decode("utf-8", errors="replace"), end="")
        else:
            result = client.call("pilot_" + args.command,
                                 retries=1 if args.command in ("restart", "stop") else 3)
            print(json.dumps(result, ensure_ascii=False, indent=2))
    except (OSError, RuntimeError, ValueError, KeyError) as error:
        print(f"错误：{error}", file=sys.stderr)
        return 1
    finally:
        device.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
