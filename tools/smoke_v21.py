# TraceQ Arduino 2.1 스모크 테스트 — COM4 Mega 2560
# 1) DTR 리셋 후 부팅 출력 캡처 (SERVER+old 모드면 PSOk 기대, 그 외엔 무출력)
# 2) STX{"cmd":"cfg_get_config"}ETX → STX-JSON 응답 왕복 (수신 경로 + JSON 경로 검증)
# 3) raw 'Z' 전송 (SERVER면 인증·LCD 'Program Start', 와이어 응답은 없음 — 예외만 확인)
import sys
import time
import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM4"
STX = b"\x02"
ETX = b"\x03"

s = serial.Serial()
s.port = PORT
s.baudrate = 115200
s.timeout = 0.2
s.dsrdtr = False
s.open()

# DTR 토글 리셋 (SeePro/TraceQ_Python 과 동일 시퀀스)
s.dtr = False
time.sleep(0.1)
s.dtr = True

# 부팅 출력 캡처 (flush 하지 않음 — PSOk 여부 확인 목적)
boot = b""
t0 = time.time()
while time.time() - t0 < 3.0:
    chunk = s.read(256)
    if chunk:
        boot += chunk
print("[boot 3s]", repr(boot))

# JSON cfg_get_config 왕복
s.write(STX + b'{"cmd":"cfg_get_config"}' + ETX)
s.flush()
resp = b""
t0 = time.time()
while time.time() - t0 < 4.0:
    chunk = s.read(256)
    if chunk:
        resp += chunk
    if ETX in resp:
        break
print("[cfg_get_config]", repr(resp))

# raw 'Z' (와이어 응답 없음 — 전송만 확인)
s.write(b"Z")
s.flush()
time.sleep(1.5)
print("[after Z]", repr(s.read(256)))

s.close()
print("done")
