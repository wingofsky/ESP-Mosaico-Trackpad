#!/usr/bin/env python3
"""Send an explicit diagnostic command to this firmware, without reset lines."""
import argparse
import time
import serial
from serial.tools import list_ports

p=argparse.ArgumentParser()
p.add_argument('command',nargs='?',default='status')
p.add_argument('--port')
p.add_argument('--seconds',type=float,default=3)
a=p.parse_args()
port=a.port
if not port:
    matches=[x.device for x in list_ports.comports() if x.vid==0x303A and x.pid==0x4005 and x.product=="Mosaico Trackpad"]
    if len(matches)!=1: raise SystemExit(f'Expected one Mosaico Trackpad, found {len(matches)}')
    port=matches[0]
s=serial.Serial()
s.port=port;s.baudrate=115200;s.timeout=0.1;s.dtr=False;s.rts=False
s.open()
s.write((a.command+'\n').encode())
s.flush()
end=time.monotonic()+a.seconds
try:
    while time.monotonic()<end:
        line=s.readline()
        if line: print(line.decode(errors='replace').rstrip(),flush=True)
except serial.SerialException:
    if a.command!='bootloader': raise
finally:
    s.close()
