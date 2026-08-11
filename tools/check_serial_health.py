# -*- coding: utf-8 -*-
"""시리얼 수신 건전성 점검 — G 패킷 수신 직후에도 JSON 응답이 정상인지.
(2.2.5 에서 readBytes 타임아웃을 1s→150ms 로 낮춘 뒤 회귀 확인용)

사용: py tools\\check_serial_health.py COM7
"""
import datetime
import json
import sys
import time

import serial

STX, ETX = b"\x02", b"\x03"
PORT = sys.argv[1] if len(sys.argv) > 1 else "COM4"

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


def get_config(label):
    t0 = time.time()
    s.write(STX + b'{"cmd":"cfg_get_config"}' + ETX)
    s.flush()
    buf = b""
    while time.time() - t0 < 5.0 and ETX not in buf:
        buf += s.read(256)
    if ETX not in buf:
        print(f"[{label}] 응답 없음 (5s)")
        return None
    start = buf.index(STX) + 1
    cfg = json.loads(buf[start:buf.index(ETX, start)].decode("utf-8"))
    print(f"[{label}] 응답 {time.time()-t0:.2f}s  type={cfg['device_type']} "
          f"num={cfg['device_number']} clear={cfg['df_clear_date_time']}")
    return cfg


print("=== 1) 기본 JSON 왕복 ===")
assert get_config("cold") is not None

print("=== 2) G 패킷 송신 직후 JSON 왕복 (수신 경로가 막히지 않는지) ===")
now = datetime.datetime.now()
dow = now.isoweekday() % 7
g = (f"G11;G2{now.year};{now.month};{now.day};{dow};{now.hour};{now.minute};{now.second};"
     f"G3TEST01;홍길동;G4위내시경검사;;;G51970;01;01;M;")
s.write(g.encode("cp949", errors="replace"))
s.flush()
t0 = time.time()
time.sleep(1.0)          # 부저 500ms + 처리
s.reset_input_buffer()
assert get_config("after-G") is not None
print(f"   (G 송신 → 응답 완료까지 {time.time()-t0:.2f}s)")

print("=== 3) 연속 3회 왕복 (안정성) ===")
for i in range(3):
    assert get_config(f"loop{i+1}") is not None

s.close()
print("done — 시리얼 수신 경로 정상")
