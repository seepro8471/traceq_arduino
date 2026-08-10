# 기기 타입을 G(게이트웨이)로 복귀 — 전체 필드 포함 cfg_set_config (부분 전송 시 0 덮임 방지)
import json
import sys
import time
import serial

STX, ETX = b"\x02", b"\x03"
s = serial.Serial()
s.port = sys.argv[1] if len(sys.argv) > 1 else "COM4"
s.baudrate = 115200
s.timeout = 0.2
s.dsrdtr = False
s.open()
s.dtr = False
time.sleep(0.1)
s.dtr = True
time.sleep(2.0)
s.reset_input_buffer()


def get_config():
    s.write(STX + b'{"cmd":"cfg_get_config"}' + ETX)
    s.flush()
    buf = b""
    t0 = time.time()
    while time.time() - t0 < 4.0 and ETX not in buf:
        buf += s.read(256)
    start = buf.index(STX) + 1
    return json.loads(buf[start:buf.index(ETX, start)].decode("utf-8"))


cfg = get_config()
print("before:", cfg["device_type"])
payload = {"cmd": "cfg_set_config", "device_type": "G",
           "device_number": cfg["device_number"], "alarm_sound": cfg["alarm_sound"],
           "washing_time": cfg["washing_time"], "df_time": cfg["df_time"],
           "patient_check": cfg["patient_check"], "df_max_cnt": cfg["df_max_cnt"],
           "df_sim_delay": cfg["df_sim_delay"], "df_sim_slot": cfg["df_sim_slot"],
           "df_clear_cnt": cfg["df_clear_cnt"]}
s.write(STX + json.dumps(payload, separators=(",", ":")).encode("ascii") + ETX)
s.flush()
time.sleep(9.0)   # readBytes 1s + Notify 2s + 소프트 리셋 + 부팅
s.reset_input_buffer()
print("after:", get_config()["device_type"])
s.close()
