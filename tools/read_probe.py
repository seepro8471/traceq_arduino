# -*- coding: utf-8 -*-
"""RegisterProbe 출력 수집 — 지정 시간 동안 시리얼을 그대로 찍는다.

사용: py tools\\read_probe.py COM7 [초]
"""
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM4"
DURATION = float(sys.argv[2]) if len(sys.argv) > 2 else 60.0

s = serial.Serial()
s.port = PORT
s.baudrate = 115200
s.timeout = 0.2
s.dsrdtr = False
s.open()
s.dtr = False
time.sleep(0.1)
s.dtr = True          # 리셋 → setup() 출력부터 캡처

buf = b""
t0 = time.time()
while time.time() - t0 < DURATION:
    chunk = s.read(512)
    if not chunk:
        continue
    buf += chunk
    while b"\n" in buf:
        idx = buf.index(b"\n")
        line = buf[:idx].decode("cp949", errors="replace").rstrip("\r")
        buf = buf[idx + 1:]
        print(line, flush=True)

s.close()
print("--- capture end ---")
