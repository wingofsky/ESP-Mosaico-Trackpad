#!/usr/bin/env python3
"""Observe cursor position while explicitly testing the attached USB firmware."""
import ctypes
import json
import threading
import time
import serial
from serial.tools import list_ports

class Point(ctypes.Structure):
    _fields_=[('x',ctypes.c_double),('y',ctypes.c_double)]
cg=ctypes.CDLL('/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics')
cf=ctypes.CDLL('/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation')
cg.CGEventCreate.argtypes=[ctypes.c_void_p];cg.CGEventCreate.restype=ctypes.c_void_p
cg.CGEventGetLocation.argtypes=[ctypes.c_void_p];cg.CGEventGetLocation.restype=Point
cf.CFRelease.argtypes=[ctypes.c_void_p]
def position():
    e=cg.CGEventCreate(None)
    if not e: raise RuntimeError('Cannot read cursor position')
    p=cg.CGEventGetLocation(e);cf.CFRelease(e)
    return [p.x,p.y]
ports=[p.device for p in list_ports.comports() if p.vid==0x303A and p.pid==0x4005 and p.product=="Mosaico Trackpad"]
if len(ports)!=1: raise SystemExit('Expected exactly one Mosaico Trackpad')
s=serial.Serial();s.port=ports[0];s.baudrate=115200;s.timeout=.1;s.dtr=False;s.rts=False;s.open()
samples=[]
start=time.monotonic()
def sample():
    while time.monotonic()-start<2:
        samples.append([round(time.monotonic()-start,4),*position()]);time.sleep(.004)
t=threading.Thread(target=sample);t.start()
time.sleep(.4);s.write(b'hidtest\n');s.flush()
output=[]
while time.monotonic()-start<2:
    line=s.readline()
    if line: output.append(line.decode(errors='replace').strip())
t.join();s.close()
result={'serial':output,'cursor_changed':len(set((a[1],a[2]) for a in samples))>1,
        'samples':samples}
print(json.dumps(result,indent=2))
if not result['cursor_changed'] or not any('HIDTEST PASS' in x for x in output): raise SystemExit(1)
