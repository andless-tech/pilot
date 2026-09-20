#!/usr/bin/env python3
"""Kill/restart only our own temporary D-Bus daemon, never the host system bus."""
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile


def line(stream):
    if not select.select([stream], [], [], 10)[0]:
        raise RuntimeError("Test process did not respond within 10 seconds")
    return stream.readline().strip()


with tempfile.TemporaryDirectory(prefix="pilot-dbus-") as tmp:
    address = "unix:path=" + str(Path(tmp) / "bus")
    bus = client = None
    def start():
        proc = subprocess.Popen(["dbus-daemon", "--session", "--nofork", "--nopidfile",
                                 "--print-address=1", "--address=" + address], stdout=subprocess.PIPE)
        assert line(proc.stdout).startswith(address.encode())
        return proc
    try:
        bus = start()
        client = subprocess.Popen([sys.argv[1]], env={**os.environ, "DBUS_SESSION_BUS_ADDRESS": address},
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        assert line(client.stdout) == b"ready"
        bus.terminate()
        bus.wait(timeout=5)
        client.stdin.write(b"\n"); client.stdin.flush()
        assert line(client.stdout) == b"disconnected"
        bus = start()
        client.stdin.write(b"\n"); client.stdin.flush()
        result = line(client.stdout)
        assert result.startswith(b"PASS:"), result
        assert client.wait(timeout=5) == 0
        print(result.decode())
    finally:
        for proc in (client, bus):
            if proc and proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=5)
