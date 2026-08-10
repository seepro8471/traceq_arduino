# TraceQ Arduino 2.1 게이트웨이 전주기 스모크 — COM4
# 1) SeePro 형식 G1~G5 환자정보를 통짜 단일 write (수신 시 500ms 비프 기대)
# 2) 60초간 태그 스캔 회신 대기 — 기대: S; / 0302... / 0704... / Sm!
import sys
import time
import datetime
import serial

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

now = datetime.datetime.now()
dow = now.isoweekday() % 7  # 0=일 … 6=토
cmd = (
    f"G11;G2{now.year};{now.month};{now.day};{dow};{now.hour};{now.minute};{now.second};"
    f"G3TEST01;홍길동;G4위내시경검사;;;G51970;01;01;M;"
)
s.write(cmd.encode("cp949", errors="replace"))
s.flush()
print("[sent]", cmd)

buf = b""
t0 = time.time()
while time.time() - t0 < 120.0:
    chunk = s.read(256)
    if chunk:
        buf += chunk
        print("[recv]", repr(chunk), flush=True)
    if b"Sm!" in buf or b"Not Patient Info" in buf:
        break

print("[total]", repr(buf))
s.close()
print("done")
