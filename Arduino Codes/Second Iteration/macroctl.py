#!/usr/bin/env python3
import sys, serial, time, json, pathlib

PORT = sys.argv[2] if len(sys.argv)>2 else '/dev/ttyACM0'  # adjust on Windows (COMx)
BAUD = 115200

def get():
    with serial.Serial(PORT, BAUD, timeout=2) as s:
        time.sleep(0.5)
        s.write(b'GET\n')
        out = s.read(65535).decode(errors='ignore')
        print(out.strip())

def put(path):
    data = pathlib.Path(path).read_bytes()
    with serial.Serial(PORT, BAUD, timeout=2) as s:
        time.sleep(0.5)
        s.write(f'PUT {len(data)}\n'.encode())
        time.sleep(0.05)
        s.write(data)
        print(s.read(128).decode(errors='ignore').strip())

if __name__=="__main__":
    if len(sys.argv)<2:
        print("Usage: macroctl.py [get|put <file>] [port]")
        sys.exit(1)
    if sys.argv[1]=="get": get()
    elif sys.argv[1]=="put": put(sys.argv[3])
