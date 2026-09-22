# SeePro 새 send_patient_info 가 포트에 쓰는 바이트를 C 문자열 상수로 뽑는다(날짜부만 고정값으로).
import sys
sys.path.insert(0, r'D:\SeePro')
sys.path.insert(0, r'D:\SeePro\tests')
import _cfgsafe  # noqa: F401
from traceq.traceq_rfid import TraceQRfidReader


class S:
    is_open = True
    out = b''

    def write(self, b):
        S.out += bytes(b)
        return len(b)


class A:
    def is_alive(self):
        return True


r = TraceQRfidReader('COM99')
r._serial = S()
r._thread = A()
r.send_patient_info('ABCDEFGHIJ1234567890', '가나다라마바사아자차', '상부위장관내시경검사', gate='0000')
raw = S.out
i, j = raw.find(b'G2'), raw.find(b'G3')
raw = raw[:i] + b'G22026;9;23;3;11;0;0;' + raw[j:]
esc = ''.join(chr(92) + 'x%02X' % c for c in raw)
out = r'D:\traceq_arduino 2.0\tools\sim\tests\seepro_packet.h'
with open(out, 'w', encoding='utf-8', newline='\n') as f:
    f.write('// SeePro send_patient_info(15바이트 맞춤) 실제 출력 — 키 20자·이름 한글 10자·검사명 한글 10자, 날짜부만 고정.\n')
    f.write('// 다시 뽑기: py tools/sim/gen_seepro_packet.py (SeePro 저장소 D:/SeePro 필요)\n')
    f.write('static const char kSeeProPacket[] = "' + esc + '";\n')
print(len(raw), raw)
