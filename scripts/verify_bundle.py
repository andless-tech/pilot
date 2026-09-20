#!/usr/bin/env python3
"""Rebuild our generated bundle with a fresh, private toolchain cache."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

archive = Path(sys.argv[1]).resolve()
checksum = archive.with_suffix(archive.suffix + ".sha256").read_text().split()[0]
if hashlib.sha256(archive.read_bytes()).hexdigest() != checksum:
    raise SystemExit("Bundle checksum mismatch")
with tempfile.TemporaryDirectory(prefix="pilot-bundle-") as temp:
    base = Path(temp)
    with tarfile.open(archive, "r:gz") as bundle:
        for item in bundle.getmembers():
            dest = (base / item.name).resolve()
            if not dest.is_relative_to(base):
                raise SystemExit("Unsafe bundle member")
            if item.issym() and not (dest.parent / item.linkname).resolve().is_relative_to(base):
                raise SystemExit("Unsafe bundle link")
            if item.islnk() or item.isdev() or item.isfifo():
                raise SystemExit("Unsupported bundle member")
        bundle.extractall(base)
    root = base / "pilot"
    env = {**os.environ, "PILOT_TOOLCHAIN_CACHE": str(base / "compiler")}
    subprocess.run(["sh", "toolchain/setup.sh"], cwd=root, env=env, check=True)
    subprocess.run(["make", "-B", "-j2", "all", "check-generated"], cwd=root, env=env, check=True)
    subprocess.run(["make", "-C", "examples/monitor"], cwd=root, env=env, check=True)
    print("PASS: standalone bundle rebuilt with its own toolchain outside the source tree")
