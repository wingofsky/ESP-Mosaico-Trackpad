#!/usr/bin/env python3
"""Wait for the known firmware USB identity and record its serial diagnostics."""
import time
import serial
from serial.tools import list_ports

deadline=time.monotonic()+180
port=None
while time.monotonic()<deadline:
    matches=[p.device for p in list_ports.comports() if p.vid==0x303A and p.pid==0x4005 and p.product=="Mosaico Trackpad"]
    if len(matches)==1: port=matches[0];break
    time.sleep(.2)
if not port: raise SystemExit('Mosaico Trackpad did not enumerate within 180 seconds')
print('PORT',port,flush=True)
s=serial.Serial();s.port=port;s.baudrate=115200;s.timeout=.1;s.dtr=False;s.rts=False;s.open()
deadline=time.monotonic()+20
last=0
while time.monotonic()<deadline:
    if time.monotonic()-last>=2:
        s.write(b'status\n');last=time.monotonic()
    line=s.readline()
    if line: print(line.decode(errors='replace').rstrip(),flush=True)
s.close()
