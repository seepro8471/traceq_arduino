# -*- coding: utf-8 -*-
"""기기 설정 조회 — 담당자 등록 여부까지 사람이 읽기 좋게 출력.

사용: py tools\\show_config.py COM7
"""
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

raw = b""
for _ in range(3):
    s.write(STX + b'{"cmd":"cfg_get_config"}' + ETX)
    s.flush()
    buf = b""
    t0 = time.time()
    while time.time() - t0 < 4.0 and ETX not in buf:
        buf += s.read(256)
    if ETX in buf:
        start = buf.index(STX) + 1
        raw = buf[start:buf.index(ETX, start)]
        break
    time.sleep(1.0)
s.close()

assert raw, "설정 조회 실패"
try:
    text = raw.decode("utf-8")
except UnicodeDecodeError:
    text = raw.decode("cp949", errors="replace")
cfg = json.loads(text)

TYPE_NAME = {"G": "게이트웨이", "W": "세척", "D": "소독", "S": "서버"}
print("─" * 46)
print(f"  기기 타입   : {cfg['device_type']} ({TYPE_NAME.get(cfg['device_type'], '?')})")
print(f"  기기 번호   : {cfg['device_number']}")
print(f"  알람음      : {'켬' if cfg['alarm_sound'] else '끔'}")
print(f"  세척 알람   : {cfg['washing_time']}분")
print(f"  소독 알람   : {cfg['df_time']}분")
print(f"  환자 체크   : {'켬' if cfg['patient_check'] else '끔'}")
print("─" * 46)
mk, mn = cfg.get("manager_key", ""), cfg.get("manager_name", "")
if mk or mn:
    print(f"  담당자      : 등록됨  (키='{mk}', 이름='{mn}')")
else:
    print("  담당자      : ★미등록 — 세척/소독 시작이 거부됩니다")
print("─" * 46)
print(f"  소독 횟수   : {cfg['df_cnt']} / 최대 {cfg['df_max_cnt']}")
print(f"  액교환일    : {cfg['df_clear_date_time']}  (교체 {cfg['df_clear_cnt']}회)")
print(f"  동시소독    : 슬롯 {cfg['df_sim_slot']}, 지연 {cfg['df_sim_delay']}분")
print("─" * 46)
