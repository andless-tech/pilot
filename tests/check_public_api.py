#!/usr/bin/env python3
"""Ensure the consumer interface and exported symbols hide the wire protocol."""
import json
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
headers = '\n'.join(p.read_text() for p in (root / 'include/pilot').glob('*.h'))
public_files = list((root / 'include').rglob('*.h')) + [root / 'examples/monitor/main.c']
for path in public_files:
    text = path.read_text()
    for token in ('tech.andless', 'PILOT_METHOD_', 'PILOT_SIGNAL_', 'pilot_reply', 'pilot_value',
                  'pilot_service', 'pilot_method', 'pilot_call(', 'pilot_introspect', 'session_bus', 'dbus/'):
        assert token not in text, f'{path}: private detail {token}'
    for service in json.loads((root / 'protocol/interfaces.json').read_text())['services']:
        for member in service['methods'] + service['signals']:
            assert member['name'] not in text, f'{path}: wire member {member["name"]}'
expected = set(re.findall(r'PILOT_API\s+[^;]+?\b(pilot_\w+)\(', headers))
output = subprocess.check_output([sys.argv[2], '--dyn-syms', '--wide', sys.argv[1]], text=True)
actual = set()
for line in output.splitlines():
    parts = line.split()
    if len(parts) >= 8 and parts[4] == 'GLOBAL' and parts[6] != 'UND':
        symbol = parts[7].split('@')[0]
        if symbol != 'PILOT_0.2':
            actual.add(symbol)
assert expected == actual, f'Missing={expected-actual}; leaked={actual-expected}'
print(f'PASS: {len(expected)} public symbols; no generic transport API or protocol names in public headers/example')
