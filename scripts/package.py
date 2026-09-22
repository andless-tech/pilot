#!/usr/bin/env python3
"""Package a binary consumer SDK, not private source/protocol implementation."""
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
target = sys.argv[1] if len(sys.argv) > 1 else 'arm'
strip = sys.argv[2] if len(sys.argv) > 2 else 'strip'
if target not in ('arm', 'host'):
    raise SystemExit('Expected arm or host')
version = re.search(r'#define PILOT_VERSION "([^"]+)"', (ROOT / 'include/pilot/pilot.h').read_text()).group(1)
dist = ROOT / 'dist'
dist.mkdir(exist_ok=True)
archive = dist / f'pilot-{version}-{target}-sdk.tar.gz'
with tempfile.TemporaryDirectory(prefix='pilot-package-') as tmp:
    stage = Path(tmp) / 'pilot'
    stage.mkdir()
    for filename in ('README.md', 'THIRD_PARTY.md', 'pilot.mk'):
        shutil.copyfile(ROOT / filename, stage / filename)
    shutil.copytree(ROOT / 'include', stage / 'include')
    for filename in ('API.md', 'USAGE.md', 'BUILD.md', 'TEST_REPORT.md', 'LCD.md', 'DEPLOY.md'):
        (stage / 'docs').mkdir(exist_ok=True)
        shutil.copyfile(ROOT / 'docs' / filename, stage / 'docs' / filename)
    (stage / 'scripts').mkdir()
    shutil.copyfile(ROOT / 'scripts/pilot-hid.py', stage / 'scripts/pilot-hid.py')
    example = stage / 'examples/monitor'
    example.mkdir(parents=True)
    for filename in ('main.c', 'Makefile'):
        shutil.copyfile(ROOT / 'examples/monitor' / filename, example / filename)
    lcd_example = stage / 'examples/lcd'
    lcd_example.mkdir(parents=True)
    for filename in ('main.c', 'Makefile'):
        shutil.copyfile(ROOT / 'examples/lcd' / filename, lcd_example / filename)
    libs = stage / 'lib' / target
    libs.mkdir(parents=True)
    for filename in ('libpilot.a', 'libpilot.so.1'):
        shutil.copyfile(ROOT / 'build' / target / filename, libs / filename)
        subprocess.run([strip, '--strip-debug' if filename.endswith('.a') else '--strip-unneeded',
                        str(libs / filename)], check=True)
    (libs / 'libpilot.so').symlink_to('libpilot.so.1')
    binaries = stage / 'bin' / target
    binaries.mkdir(parents=True)
    for filename in ('pilot-monitor', 'pilot-monitor-shared', 'pilot-lcd'):
        shutil.copyfile(ROOT / 'build' / target / filename, binaries / filename)
        (binaries / filename).chmod(0o755)
        subprocess.run([strip, '--strip-unneeded', str(binaries / filename)], check=True)
    if target == 'arm':
        shutil.copytree(ROOT / 'toolchain', stage / 'toolchain')
        shutil.copytree(ROOT / 'vendor/dbus/lib', stage / 'vendor/dbus/lib')
    with tarfile.open(archive, 'w:gz') as output:
        output.add(stage, arcname='pilot')
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
archive.with_suffix(archive.suffix + '.sha256').write_text(f'{digest}  {archive.name}\n')
print(f'{archive}: {archive.stat().st_size} bytes, sha256={digest}')
