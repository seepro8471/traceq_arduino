# -*- coding: utf-8 -*-
"""업로더의 리더기 시계 맞춤(sync_clock) 시험 — 실물 없이 리더기 흉내(FakeReader)로.

FakeReader 는 펌웨어의 수신 규칙을 흉내 낸다(lib/TraceQ_Arduino SerialProcessor · main.cpp serialEvent):
  · ready_at 전(부팅·Keep settings)에는 명령을 읽지 않는다 — 바이트는 수신 버퍼에 쌓인다(drop_until 전은 부트로더가 삼킨다)
  · 바이트가 read_timeout 동안 더 안 오면 버퍼를 **한 번에** 읽고, 그 안의 **첫 JSON 만** 처리한다(둘째는 버린다)
  · cfg_get_config → STX{json}ETX 응답 · cfg_set_date_time → 시계 설정(시리얼 응답 없음) + 안내음 1초 동안 바쁨
실행: py uploader\\test_clock_sync.py   (끝줄 `==> pass=N fail=M` · 실패가 있으면 종료코드 1)
"""
import json
import os
import re
import sys
from datetime import datetime, timedelta

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import traceq_fw_upload as up   # noqa: E402

PASS = FAIL = 0


def check(cond, label):
    global PASS, FAIL
    if cond:
        PASS += 1
        print('PASS', label)
    else:
        FAIL += 1
        print('FAIL', label)


class Clock:
    def __init__(self):
        self.t = 0.0
        self.base = datetime(2026, 10, 11, 14, 0, 0)

    def mono(self):
        return self.t

    def now(self):
        return self.base + timedelta(seconds=self.t)


class FakeReader:
    def __init__(self, clk, ready_at=0.0, read_timeout=0.15, drop_until=0.0, dead=False):
        self.clk, self.ready_at, self.read_timeout = clk, ready_at, read_timeout
        self.drop_until, self.dead = drop_until, dead
        self.rx = bytearray()
        self.last_rx = 0.0
        self.busy_until = 0.0
        self.tx = bytearray(b'Firmware Version: 0x92 = v2.0\r\nPSOk\r\n')   # 부팅 때 나오는 줄(STX 없음)
        self.rtc_text = None
        self.rtc_set_at = None
        self.gets = 0
        self.sets = 0
        self.closed = False

    # ── 리더기 쪽 ──
    def _tick(self):
        t = self.clk.t
        if self.dead or not self.rx or t < self.ready_at or t < self.busy_until:
            return
        if t - self.last_rx < self.read_timeout:
            return
        buf, self.rx = bytes(self.rx), bytearray()
        a = buf.find(b'{')
        b = buf.find(b'}', a + 1) if a >= 0 else -1
        if a < 0 or b < 0:
            return
        try:
            doc = json.loads(buf[a:b + 1].decode('ascii'))
        except ValueError:
            return
        if doc.get('cmd') == 'cfg_get_config':
            self.gets += 1
            # 담당자 키에 날 0x03(ETX) — 펌웨어는 제어 문자를 그대로 낸다
            body = b'{"device_type":"W","device_number":3,"manager_key":"AB\x03CD","patient_check":false}'
            self.tx += bytes([up.STX]) + body + bytes([up.ETX])
        elif doc.get('cmd') == 'cfg_set_date_time':
            text = doc.get('device_date_time') or ''
            self.busy_until = t + 1.2          # 안내음 1000ms(실패음이어도 비슷하게 바쁘다)
            # 펌웨어 DefaultRtc::FromString 의 마스크 "dddd-dd-dd dd:dd:dd" — 어긋나면 시계를 안 바꾼다(실패음)
            if not re.fullmatch(r'\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}', text):
                return
            self.sets += 1
            self.rtc_text = text
            self.rtc_set_at = t

    # ── pyserial 쪽 ──
    def write(self, data):
        if self.clk.t >= self.drop_until:      # 그 전은 부트로더가 삼킨다
            self.rx += data
            self.last_rx = self.clk.t
        return len(data)

    def read(self, n):
        self._tick()
        if self.tx:
            out, self.tx = bytes(self.tx[:n]), self.tx[n:]
            return out
        self.clk.t += 0.2                      # 시리얼 timeout
        self._tick()
        return b''

    def reset_input_buffer(self):
        self.tx = bytearray()

    def close(self):
        self.closed = True


def run(reader, clk):
    def sleep(d):
        end = clk.t + d
        while clk.t < end:
            clk.t = min(end, clk.t + 0.05)
            reader._tick()
    return up.sync_clock('COMx', opener=lambda p: reader, now=clk.now, sleep=sleep, clock=clk.mono)


def main():
    # ── ① 정상: Keep settings 로 12초 뒤에야 읽기 시작 → 응답이 온 **그 순간의** PC 시각이 들어간다 ──
    clk = Clock()
    r = FakeReader(clk, ready_at=12.0)
    res = run(r, clk)
    sent = datetime.strptime(r.rtc_text, '%Y-%m-%d %H:%M:%S') if r.rtc_text else None
    lag = (clk.base + timedelta(seconds=r.rtc_set_at) - sent).total_seconds() if sent else 99
    check(res == 'ok' and r.sets == 1 and r.gets == 2, '① 정상: 설정 읽기 → 시각 → 설정 읽기(확인) = ok')
    check(sent is not None and sent >= clk.base + timedelta(seconds=12) and 0 <= lag <= 1.5,
          '① 보낸 시각은 리더기가 준비된 뒤의 PC 시각이다(기다린 12초만큼 늦은 시각이 아니다 · 리더기 반영까지 1.5초 안)')
    check(r.closed, '① 포트를 닫는다')

    # ── ② 게이트웨이(수신 대기 1초)에서도 시각 명령과 확인 읽기가 한 버퍼로 합쳐지지 않는다 ──
    clk = Clock()
    r = FakeReader(clk, ready_at=0.0, read_timeout=1.0)
    res = run(r, clk)
    check(res == 'ok' and r.sets == 1 and r.gets == 2, '② 수신 대기 1초(게이트웨이)에서도 ok — 둘째 JSON 이 버려지지 않게 띄운다')

    # ── ③ 첫 설정 읽기를 부트로더가 삼킴(포트를 열 때 재리셋) → 다시 보내 ok ──
    clk = Clock()
    r = FakeReader(clk, ready_at=0.0, drop_until=5.0)
    res = run(r, clk)
    check(res == 'ok' and r.sets == 1, '③ 첫 요청이 사라져도 다시 보내 맞춘다')

    # ── ④ 응답 없음 → 시각을 보내지 않는다 ──
    clk = Clock()
    r = FakeReader(clk, dead=True)
    res = run(r, clk)
    check(res == 'no_reply' and r.sets == 0 and r.rtc_text is None and r.closed,
          '④ 리더기가 응답하지 않으면 시각을 보내지 않고 no_reply(포트는 닫는다)')

    # ── ⑤ PC 날짜가 출시 기준일보다 앞 → 포트도 열지 않는다 ──
    clk = Clock()
    clk.base = datetime(2026, 1, 1, 9, 0, 0)
    opened = []
    res = up.sync_clock('COMx', opener=lambda p: opened.append(p), now=clk.now, sleep=lambda d: None, clock=clk.mono)
    check(res == 'pc_clock' and not opened, '⑤ PC 날짜가 틀리면(출시 기준일 앞) 리더기에 보내지 않는다')

    # ── ⑥ 포트를 못 엶 ──
    def boom(p):
        raise OSError('access denied')
    clk = Clock()
    res = up.sync_clock('COMx', opener=boom, now=clk.now, sleep=lambda d: None, clock=clk.mono)
    check(res == 'port', '⑥ 포트를 못 열면 port(예외가 밖으로 안 나간다)')

    # ── ⑦ 시각은 보냈는데 그 뒤 응답이 없다 → unconfirmed ──
    clk = Clock()
    r = FakeReader(clk)
    orig_tick = r._tick

    def tick_then_die():
        orig_tick()
        if r.sets:
            r.dead = True
    r._tick = tick_then_die
    res = run(r, clk)
    check(res == 'unconfirmed' and r.sets == 1, '⑦ 시각을 보낸 뒤 응답이 없으면 unconfirmed')

    # ── ⑧ 와이어 바이트 — 펌웨어 시뮬 시험(t_ii21)이 받는 것과 글자까지 같다 ──
    check(up._frame({'cmd': 'cfg_get_config'}) == b'\x02{"cmd":"cfg_get_config"}\x03', '⑧ 설정 읽기 프레임 바이트')
    check(up._frame({'cmd': 'cfg_set_date_time', 'device_date_time': '2026-10-11 14:03:22'}) ==
          b'\x02{"cmd":"cfg_set_date_time","device_date_time":"2026-10-11 14:03:22"}\x03', '⑧ 시각 프레임 바이트(YYYY-MM-DD hh:mm:ss)')

    # ── ⑨ 응답 해석 — 부팅 줄 뒤 · 본문 안의 ETX · 덜 온 프레임 ──
    full = b'Firmware Version: 0x92 = v2.0\r\nPSOk\r\n\x02{"device_type":"G","manager_key":"A\x03B","device_number":7}\x03'
    got = up._parse_frame(bytearray(full))
    check(isinstance(got, dict) and got.get('device_type') == 'G' and got.get('device_number') == 7,
          '⑨ 부팅 줄을 건너뛰고 본문 안의 ETX 를 넘어 해석한다')
    check(up._parse_frame(bytearray(full[:-10])) is None and up._parse_frame(bytearray(b'PSOk\r\n')) is None,
          '⑨ 덜 온 프레임·프레임 없음은 None')

    # ── ⑩ 출시 기준일 상수가 펌웨어(version.hpp)와 같다 ──
    hpp = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lib', 'TraceQ_Arduino', 'src',
                       'TraceQ_Arduino', 'version.hpp')
    src = open(hpp, encoding='utf-8').read()
    ymd = tuple(int(re.search(r'#define\s+TRACEQ_RELEASE_%s\s+(\d+)' % k, src).group(1)) for k in ('YEAR', 'MONTH', 'DAY'))
    check(ymd == up.CLOCK_FLOOR, '⑩ CLOCK_FLOOR = version.hpp 의 TRACEQ_RELEASE_*')

    print(f'==> pass={PASS} fail={FAIL}')
    return 1 if FAIL else 0


if __name__ == '__main__':
    sys.exit(main())
