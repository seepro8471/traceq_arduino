# TraceQ Arduino 2.1 전체 인수 테스트 — COM4
# 1) 현재 설정 읽기 → 2) S(서버) 타입으로 전환(소프트 리셋 검증) → 3) Z 인증
# 4) 태그 접촉 대기(50s) — latest 서버 = 태그 Process 초기화
# 5) G(게이트웨이) 타입 복귀 → 6) 환자정보 전송 → 7) 태그 접촉 대기(60s) — 스캔 회신 기대
import json
import time
import datetime
import serial

PORT = "COM4"
STX = b"\x02"
ETX = b"\x03"


def log(*a):
    print(*a, flush=True)


def read_for(s, seconds, reply_z_on_psok=False, stop_tokens=()):
    buf = b""
    t0 = time.time()
    while time.time() - t0 < seconds:
        chunk = s.read(256)
        if chunk:
            buf += chunk
            log("[recv]", repr(chunk))
            if reply_z_on_psok and b"PSOk" in buf and not getattr(read_for, "_z_sent", False):
                s.write(b"Z")
                s.flush()
                log("[sent] Z (PSOk reply)")
                read_for._z_sent = True
        for tok in stop_tokens:
            if tok in buf:
                return buf
    return buf


def get_config(s):
    s.write(STX + b'{"cmd":"cfg_get_config"}' + ETX)
    s.flush()
    buf = read_for(s, 4.0, stop_tokens=(ETX,))
    try:
        start = buf.index(STX) + 1
        end = buf.index(ETX, start)
        return json.loads(buf[start:end].decode("utf-8", errors="replace"))
    except (ValueError, json.JSONDecodeError):
        return None


def get_config_retry(s, tries=3):
    for _ in range(tries):
        c = get_config(s)
        if c:
            return c
        time.sleep(1.0)
    return None


def set_type(s, cfg, new_type):
    # 부분 전송 시 나머지 옵션이 0으로 덮이므로 반드시 전체 필드로 전송.
    payload = {
        "cmd": "cfg_set_config",
        "device_type": new_type,
        "device_number": cfg["device_number"],
        "alarm_sound": cfg["alarm_sound"],
        "washing_time": cfg["washing_time"],
        "df_time": cfg["df_time"],
        "patient_check": cfg["patient_check"],
        "df_max_cnt": cfg["df_max_cnt"],
        "df_sim_delay": cfg["df_sim_delay"],
        "df_sim_slot": cfg["df_sim_slot"],
        "df_clear_cnt": cfg["df_clear_cnt"],
    }
    s.write(STX + json.dumps(payload, separators=(",", ":")).encode("ascii") + ETX)
    s.flush()
    log("[sent] cfg_set_config type ->", new_type)


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

log("=== 1) 현재 설정 ===")
cfg = get_config(s)
log(cfg)
assert cfg is not None, "cfg_get_config 응답 없음"

log("=== 2) S 타입 전환 (소프트 리셋 기대) ===")
if cfg["device_type"] != "S":
    set_type(s, cfg, "S")
    # readBytes 1s + Notify 부저 2s + 소프트 리셋 + 부팅. S+Old면 부팅 PSOk가 온다.
    read_for(s, 9.0, stop_tokens=(b"PSOk",))
    time.sleep(1.0)
    s.reset_input_buffer()
else:
    log("(이미 S 타입 — 전환 생략)")
cfg2 = get_config_retry(s)
log("전환 후:", cfg2)
assert cfg2 and cfg2["device_type"] == "S", "S 전환 실패"

log("=== 3) Z 인증 ===")
s.write(b"Z")
s.flush()
time.sleep(1.5)
log("[after Z]", repr(s.read(512)))

log("=== 4) 태그 접촉 대기 50s — 서버(latest) = 태그 초기화 ===")
read_for._z_sent = False
buf = read_for(s, 50.0, reply_z_on_psok=True, stop_tokens=(b"Ok!",))
log("[server phase total]", repr(buf))

log("=== 5) G 타입 복귀 ===")
cfgS = get_config_retry(s) or cfg2
set_type(s, cfgS, "G")
read_for(s, 9.0)  # readBytes 1s + Notify 2s + 리셋 + 부팅 (G 부팅은 무출력)
s.reset_input_buffer()
cfg3 = get_config_retry(s)
log("복귀 후:", cfg3)
assert cfg3 and cfg3["device_type"] == "G", "G 복귀 실패"

log("=== 6) 환자정보 전송 ===")
now = datetime.datetime.now()
dow = now.isoweekday() % 7
cmd = (
    f"G11;G2{now.year};{now.month};{now.day};{dow};{now.hour};{now.minute};{now.second};"
    f"G3TEST01;홍길동;G4위내시경검사;;;G51970;01;01;M;"
)
s.write(cmd.encode("cp949", errors="replace"))
s.flush()
log("[sent]", cmd)
buf = read_for(s, 4.0, stop_tokens=(b"buzz-done",))

log("=== 7) 태그 접촉 대기 60s — 스캔 회신(Sm!) 기대 ===")
buf = read_for(s, 60.0, stop_tokens=(b"Sm!",))
log("[gateway phase total]", repr(buf))

s.close()
log("done")
