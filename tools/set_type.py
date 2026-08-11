# -*- coding: utf-8 -*-
"""기기 타입 변경 — 전체 필드 포함 cfg_set_config (부분 전송 시 0 덮임 방지).

사용: py tools\\set_type.py COM7 W      (G/W/D/S)
"""
import json
import sys
import time

import serial

STX, ETX = b"\x02", b"\x03"
PORT = sys.argv[1] if len(sys.argv) > 1 else "COM4"
WANT = (sys.argv[2] if len(sys.argv) > 2 else "G").upper()
assert WANT in ("G", "W", "D", "S"), "타입은 G/W/D/S 중 하나"

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


def get_config(tries=3):
    for _ in range(tries):
        s.write(STX + b'{"cmd":"cfg_get_config"}' + ETX)
        s.flush()
        buf = b""
        t0 = time.time()
        while time.time() - t0 < 4.0 and ETX not in buf:
            buf += s.read(256)
        if ETX in buf:
            start = buf.index(STX) + 1
            raw = buf[start:buf.index(ETX, start)]
            # 담당자 이름은 태그에서 읽은 cp949 바이트 그대로 실려 온다 —
            # UTF-8 로만 디코드하면 한글 이름이 등록된 순간 깨진다.
            try:
                text = raw.decode("utf-8")
            except UnicodeDecodeError:
                text = raw.decode("cp949", errors="replace")
            return json.loads(text)
        time.sleep(1.0)
    return None


cfg = get_config()
assert cfg is not None, "설정 조회 실패"
print("before:", cfg["device_type"], "| 소독횟수:", cfg["df_cnt"],
      "| 액교환일:", cfg["df_clear_date_time"])

if cfg["device_type"] != WANT:
    payload = {"cmd": "cfg_set_config", "device_type": WANT,
               "device_number": cfg["device_number"], "alarm_sound": cfg["alarm_sound"],
               "washing_time": cfg["washing_time"], "df_time": cfg["df_time"],
               "patient_check": cfg["patient_check"], "df_max_cnt": cfg["df_max_cnt"],
               "df_sim_delay": cfg["df_sim_delay"], "df_sim_slot": cfg["df_sim_slot"],
               "df_clear_cnt": cfg["df_clear_cnt"]}
    s.write(STX + json.dumps(payload, separators=(",", ":")).encode("ascii") + ETX)
    s.flush()
    time.sleep(9.0)          # readBytes + Notify 2s + 소프트리셋 + 부팅
    s.reset_input_buffer()
    cfg = get_config()
    assert cfg is not None, "전환 후 설정 조회 실패"

print("after :", cfg["device_type"], "| 소독횟수:", cfg["df_cnt"],
      "| 액교환일:", cfg["df_clear_date_time"])
s.close()
