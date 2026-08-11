# -*- coding: utf-8 -*-
"""G1 본체번호 규칙 실기 확인 (2.2.6).

  · G1 이 0 이 아니면  → 그 번호를 태그에 기록 (LCD 우하단 G:NN 이 그 값)
  · G1 이 0000 이면     → 기기 자체 번호를 사용

사용: py tools\\test_gate_number.py COM4 0002
      py tools\\test_gate_number.py COM4 0000
"""
import datetime
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM4"
GATE = sys.argv[2] if len(sys.argv) > 2 else "0002"

s = serial.Serial()
s.port = PORT
s.baudrate = 115200
s.timeout = 0.2
s.dsrdtr = False
s.open()
s.dtr = False
time.sleep(0.1)
s.dtr = True
time.sleep(2.0)
s.reset_input_buffer()

now = datetime.datetime.now()
dow = now.isoweekday() % 7
cmd = (f"G1{GATE};"
       f"G2{now.year};{now.month};{now.day};{dow};{now.hour};{now.minute};{now.second};"
       f"G3TEST01;홍길동;G4위내시경검사;;;G51970;01;01;M;")
s.write(cmd.encode("cp949", errors="replace"))
s.flush()
print(f"[전송] G1={GATE}")
print(cmd)
print()
print("→ 리더기 LCD 우하단의 'G:NN' 을 확인하세요.")
print(f"   G1=0000 이면 기기 번호, 그 외에는 {GATE} 가 보여야 합니다.")
print("→ 이어서 태그를 대면 그 번호가 태그에 기록됩니다.")
print()

buf = b""
t0 = time.time()
while time.time() - t0 < 90.0:
    chunk = s.read(256)
    if chunk:
        buf += chunk
        print("[수신]", repr(chunk), flush=True)
    if b"Sm!" in buf or b"Not Patient Info" in buf or b"No Complete" in buf:
        break

s.close()
print("done")
