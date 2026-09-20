#!/usr/bin/env python3
"""Assemble a self-contained development bundle, without touching a board."""
import hashlib
from pathlib import Path
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]
target = sys.argv[1] if len(sys.argv) > 1 else "arm"
if target not in ("arm", "host"):
    raise SystemExit("Expected arm or host")
files = [ROOT / name for name in ("README.md", "THIRD_PARTY.md", "Makefile", ".gitignore", ".gitattributes")]
for name in ("include", "src", "protocol", "docs", "scripts", "tests", "examples", "toolchain", "vendor"):
    files.extend(p for p in (ROOT / name).rglob("*") if p.is_file()
                 and "__pycache__" not in p.parts and p.name != "pilot-monitor")
files.extend(ROOT / "build" / target / p for p in ("libpilot.a", "libpilot.so.0", "libpilot.so", "pilot-monitor"))
for p in files:
    if not p.is_file():
        raise SystemExit(f"Missing input: {p}")
required = ROOT / "toolchain/arm-rockchip830-linux-uclibcgnueabihf.tar.gz"
if target == "arm" and not required.is_file():
    raise SystemExit("Import the toolchain before packaging the standalone ARM SDK")
dist = ROOT / "dist"
dist.mkdir(exist_ok=True)
archive = dist / f"pilot-0.1.0-{target}-sdk.tar.gz"
with tarfile.open(archive, "w:gz") as out:
    for p in sorted(files):
        out.add(p, arcname="pilot/" + str(p.relative_to(ROOT)), recursive=False)
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
archive.with_suffix(archive.suffix + ".sha256").write_text(f"{digest}  {archive.name}\n")
print(f"{archive}: {archive.stat().st_size} bytes, sha256={digest}")
