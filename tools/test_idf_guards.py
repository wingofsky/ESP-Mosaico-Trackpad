#!/usr/bin/env python3
"""Integration check against the installed SDK, using an intentionally invalid port."""
import os
from pathlib import Path
import subprocess
from esptool.util import get_bytes

ROOT = Path(__file__).resolve().parents[1]
firmware = (ROOT / 'build/mosaico_trackpad.bin').read_bytes()
if get_bytes(firmware)[0] != firmware:
    raise SystemExit('FAIL: esptool image API is incompatible')
print('PASS: built image accepted by the installed esptool input API', flush=True)
ACTIONS = ('flash', 'app-flash', 'bootloader-flash', 'partition-table-flash',
           'erase-flash', 'erase-otadata', 'merge-bin')
env = dict(os.environ, ESPPORT='/dev/DOES_NOT_EXIST')
for tool in ('idf.py', 'cmake'):
    for target in ACTIONS:
        args = ['idf.py', '-p', env['ESPPORT'], target] if tool == 'idf.py' else [
            'cmake', '--build', 'build', '--target', target]
        result = subprocess.run(args, cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
        message = 'Generic flash/erase/export is disabled' if tool == 'idf.py' else 'Generic flashing is disabled.'
        if result.returncode == 0 or message not in result.stdout + result.stderr:
            raise SystemExit(f'FAIL: {tool} {target}\n{result.stdout}\n{result.stderr}')
        print(f'PASS: {tool} {target} blocked', flush=True)
