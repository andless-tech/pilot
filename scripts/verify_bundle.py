#!/usr/bin/env python3
"""Verify a consumer can link both library variants without implementation files."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

archive = Path(sys.argv[1]).resolve()
checksum = archive.with_suffix(archive.suffix + '.sha256').read_text().split()[0]
if hashlib.sha256(archive.read_bytes()).hexdigest() != checksum:
    raise SystemExit('Bundle checksum mismatch')
with tempfile.TemporaryDirectory(prefix='pilot-bundle-') as temp:
    base = Path(temp)
    with tarfile.open(archive, 'r:gz') as bundle:
        for item in bundle.getmembers():
            dest = (base / item.name).resolve()
            if not dest.is_relative_to(base):
                raise SystemExit('Unsafe bundle member')
            if item.issym() and not (dest.parent / item.linkname).resolve().is_relative_to(base):
                raise SystemExit('Unsafe bundle link')
            if item.islnk() or item.isdev() or item.isfifo():
                raise SystemExit('Unsupported bundle member')
        bundle.extractall(base)
    root = base / 'pilot'
    for name in ('src', 'protocol', 'tests', 'scripts', 'vendor/dbus/include'):
        assert not (root / name).exists(), f'Private implementation shipped: {name}'
    assert not list(root.rglob('transport.h')) and not list(root.rglob('interfaces.json'))
    env = {**os.environ, 'PILOT_TOOLCHAIN_CACHE': str(base / 'compiler')}
    subprocess.run(['sh', 'toolchain/setup.sh'], cwd=root, env=env, check=True)
    for mode in ('static', 'shared'):
        subprocess.run(['make', '-B', '-C', 'examples/monitor', f'PILOT_LINK={mode}', f'OUTPUT=pilot-monitor-{mode}'],
                       cwd=root, env=env, check=True)
        # Exercise automatic SDK root resolution outside the example directory.
        makefile = (f'PILOT_LINK := {mode}\ninclude {root}/pilot.mk\n'
                    'all:\n\t$(CC) $(PILOT_CPPFLAGS) $(PILOT_ROOT)/examples/monitor/main.c '
                    f'$(PILOT_LDLIBS) -o consumer-{mode}\n')
        subprocess.run(['make', '--no-print-directory', '-f', '-'], input=makefile,
                       text=True, cwd=base, env=env, check=True)
    reader = next((base / 'compiler').rglob('bin/*-readelf'))
    for mode in ('static', 'shared'):
        for binary in (root / 'examples/monitor' / f'pilot-monitor-{mode}', base / f'consumer-{mode}'):
            output = subprocess.check_output([str(reader), '-d', str(binary)], text=True)
            needs_pilot = 'Shared library: [libpilot.so.1]' in output
            assert needs_pilot == (mode == 'shared'), output
    print('PASS: binary SDK hides private sources; static/shared examples linked with bundled toolchain')
