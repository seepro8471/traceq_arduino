# -*- coding: utf-8 -*-
"""업로더의 리더기 시계 맞춤(sync_clock) 시험 - 실물 없이 리더기 흉내(FakeReader)로. 포트는 열지 않는다.

FakeReader 는 펌웨어 실측에 맞춘다(18차 - 종전 가짜는 응답 88바이트·바쁨 1.2초·수신 대기 기준이 달라 변이 19종이 초록이었다):
  · ready_at 전(부팅·Keep settings)에는 명령을 읽지 않는다 - 바이트는 수신 버퍼에 쌓인다(drop_until 전은 부트로더가 삼킨다)
  · 한가해져 읽기 시작한 때와 마지막 도착 중 늦은 쪽부터 read_timeout(세척기 0.15초 · 게이트웨이 1초) 동안 더 안 오면
    버퍼를 한 번에 읽고, 그 안의 첫 JSON 만 처리한다(둘째는 버린다)
  · cfg_get_config -> STX{json}ETX 응답: 실물과 같은 15칸 약 300바이트 · 담당자 이름은 cp949 한글(0x80 이상) · 키에 날 0x03 ·
    device_date_time = 리더기의 지금 시계(2.2.36 부터 - clock_key=False 면 옛 판)
  · cfg_set_date_time -> 시계 설정(시리얼 응답 없음) + 안내음 동안 2.08초 바쁨(켬 1초 + 끔 1초)
실행: py uploader\test_clock_sync.py   (끝줄 `==> pass=N fail=M` · 실패가 있으면 종료코드 1)
배치(build_uploader.bat)가 exe 를 만들기 전에 돌린다 - 출력 글자는 cp949 로 쓸 수 있는 것만([18] 이 지킨다).
"""
import ast
import contextlib
import io
import json
import os
import re
import sys
import tempfile
import types
from datetime import datetime, timedelta

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


# 어떤 경로로도 실물 포트가 열리지 않게 - 시험 내내 가짜 serial 모듈을 끼워 둔다.
class _FakeSerialPort:
    plan = []            # open() 마다 하나씩 꺼낸다: None = 성공 · 예외 = 그 예외
    made = []

    def __init__(self):
        self.port = None
        self.baudrate = None
        self.timeout = None
        self.write_timeout = None
        self.dtr = True
        self.rts = True
        self.opened = False
        self.at_open = None
        _FakeSerialPort.made.append(self)

    def open(self):
        self.at_open = (self.port, self.baudrate, self.timeout, self.write_timeout, self.dtr, self.rts)
        step = _FakeSerialPort.plan.pop(0) if _FakeSerialPort.plan else None
        if step is not None:
            raise step
        self.opened = True


_fake_serial = types.ModuleType('serial')
_fake_serial.Serial = _FakeSerialPort
sys.modules['serial'] = _fake_serial

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


KOREAN_NAME = '홍길동'.encode('cp949')       # C8 AB B1 E6 B5 BF - 응답 본문에 날것으로 실린다


class FakeReader:
    def __init__(self, clk, ready_at=0.0, read_timeout=0.15, drop_until=0.0, dead=False, clock_key=True, drop_sets=0,
                 dtype=b'W'):
        self.clk, self.ready_at, self.read_timeout = clk, ready_at, read_timeout
        self.drop_until, self.dead, self.clock_key, self.drop_sets = drop_until, dead, clock_key, drop_sets
        self.dtype = dtype
        self.rx = bytearray()
        self.last_rx = 0.0
        self.read_start = None
        self.busy_until = 0.0
        self.tx = bytearray(b'Firmware Version: 0x92 = v2.0\r\nPSOk\r\n')   # 부팅 때 나오는 줄(STX 없음)
        self.rtc_offset = timedelta(hours=-5, minutes=-7)    # 리더기 시계는 PC 보다 5시간 7분 늦게 가 있다
        self.rtc_text = None
        self.rtc_set_at = None
        self.gets = 0
        self.sets = 0
        self.set_frames = 0
        self.writes = 0
        self.write_fail_at = 0       # n번째 write 에서 예외(0 = 없음)
        self.resp_at = []
        self.closed = False

    def reader_now(self):
        return self.clk.now() + self.rtc_offset

    def reply(self):
        body = (b'{"device_type":"' + self.dtype + b'","device_number":3,"alarm_sound":true,"washing_time":4,"df_time":18,'
                b'"manager_key":"ND01\x03456","manager_name":"' + KOREAN_NAME + b'","df_cnt":12,"df_max_cnt":30,'
                b'"df_sim_delay":3,"df_sim_slot":1,"df_clear_cnt":2,"df_clear_date_time":"2026-10-01 09:00:00",'
                b'"patient_check":true')
        if self.clock_key:
            body += b',"device_date_time":"' + self.reader_now().strftime('%Y-%m-%d %H:%M:%S').encode('ascii') + b'"'
        return bytes([up.STX]) + body + b'}' + bytes([up.ETX])

    # -- 리더기 쪽 --
    def _tick(self):
        t = self.clk.t
        if self.dead or not self.rx or t < self.ready_at or t < self.busy_until:
            self.read_start = None
            return
        if self.read_start is None:
            self.read_start = t
        if t - max(self.read_start, self.last_rx) < self.read_timeout:
            return
        self.read_start = None
        buf, self.rx = bytes(self.rx), bytearray()
        a = buf.find(b'{')
        b = buf.find(b'}', a + 1) if a >= 0 else -1
        if a < 0 or b < 0:
            return
        try:
            doc = json.loads(buf[a:b + 1].decode('latin-1'))
        except ValueError:
            return
        if doc.get('cmd') == 'cfg_get_config':
            self.gets += 1
            self.tx += self.reply()
            self.resp_at.append(t)
        elif doc.get('cmd') == 'cfg_set_date_time':
            self.set_frames += 1
            text = doc.get('device_date_time') or ''
            self.busy_until = t + 2.08         # 안내음 켬 1초 + 끔 1초(실패음이어도 비슷하게 바쁘다)
            if self.set_frames <= self.drop_sets:
                return                         # 이 프레임은 반영되지 않았다(시험용 - 버려진 시각 명령)
            # 펌웨어 DefaultRtc::FromString 의 마스크 "dddd-dd-dd dd:dd:dd" - 어긋나면 시계를 안 바꾼다(실패음)
            if not re.fullmatch(r'\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}', text):
                return
            self.sets += 1
            self.rtc_text = text
            self.rtc_set_at = t
            self.rtc_offset = datetime.strptime(text, '%Y-%m-%d %H:%M:%S') - self.clk.now()

    # -- pyserial 쪽 --
    def write(self, data):
        self.writes += 1
        if self.write_fail_at and self.writes == self.write_fail_at:
            raise OSError('Write timeout')
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
    with contextlib.redirect_stdout(io.StringIO()) as out:
        res = up.sync_clock('COMx', opener=lambda p: reader, now=clk.now, sleep=sleep, clock=clk.mono)
    return res, out.getvalue()


def clock_ok(r):
    """리더기 시계가 PC 시각과 허용 차 안인가."""
    return abs(r.rtc_offset.total_seconds()) <= up.CLOCK_TOLERANCE_S


def printed_strings(path):
    """그 소스의 문서 문자열을 뺀 문자열 상수 전부(화면에 찍힐 수 있는 것)."""
    tree = ast.parse(open(path, encoding='utf-8').read())
    doc = set()
    for node in ast.walk(tree):
        if isinstance(node, (ast.Module, ast.FunctionDef, ast.ClassDef)):
            b = node.body
            if b and isinstance(b[0], ast.Expr) and isinstance(b[0].value, ast.Constant) and isinstance(b[0].value.value, str):
                doc.add(id(b[0].value))
    return [n.value for n in ast.walk(tree) if isinstance(n, ast.Constant) and isinstance(n.value, str) and id(n) not in doc]


def main():
    up._safe_stdout()

    # [01] 정상: Keep settings 로 12초 뒤에야 읽기 시작 -> 응답이 온 뒤의 PC 시각이 들어가고, 되읽어 확인한다
    clk = Clock()
    r = FakeReader(clk, ready_at=12.0)
    res, out = run(r, clk)
    sent = datetime.strptime(r.rtc_text, '%Y-%m-%d %H:%M:%S') if r.rtc_text else None
    lag = (clk.base + timedelta(seconds=r.rtc_set_at) - sent).total_seconds() if sent else 99
    check(res == 'ok' and r.sets == 1 and r.gets == 2 and clock_ok(r), '[01] 정상: 설정 읽기 -> 시각 -> 설정 읽기(되읽기 확인) = ok · 리더기 시계가 PC 와 같다')
    check(sent is not None and sent >= clk.base + timedelta(seconds=12) and 0 <= lag <= 1.5,
          '[01] 보낸 시각은 리더기가 준비된 뒤의 PC 시각이다(기다린 12초만큼 늦은 시각이 아니다 · 리더기 반영까지 1.5초 안)')
    check(r.closed and '되읽은 리더기 시계' in out, '[01] 포트를 닫고, 되읽은 리더기 시계를 화면에 알린다')

    # [02] 게이트웨이(수신 대기 1초 · 확인 응답이 1.6초쯤 뒤)에서도 ok - 시각 명령과 확인 읽기가 한 버퍼로 합쳐지지 않는다
    clk = Clock()
    r = FakeReader(clk, ready_at=11.2, read_timeout=1.0, dtype=b'G')
    res, _ = run(r, clk)
    check(res == 'ok' and r.sets == 1 and r.gets == 2 and clock_ok(r), '[02] 수신 대기 1초(게이트웨이)에서도 ok - 둘째 JSON 이 버려지지 않게 띄우고 확인 응답을 충분히 기다린다')

    # [03] 첫 설정 읽기를 부트로더가 삼킴(포트를 열 때 재리셋) -> 다시 보내 ok
    clk = Clock()
    r = FakeReader(clk, ready_at=0.0, drop_until=5.0)
    res, _ = run(r, clk)
    check(res == 'ok' and r.sets == 1 and clock_ok(r), '[03] 첫 요청이 사라져도 다시 보내 맞춘다')

    # [04] 응답 없음 -> 시각을 보내지 않는다
    clk = Clock()
    r = FakeReader(clk, dead=True)
    res, _ = run(r, clk)
    check(res == 'no_reply' and r.sets == 0 and r.rtc_text is None and r.closed,
          '[04] 리더기가 응답하지 않으면 시각을 보내지 않고 no_reply(포트는 닫는다)')

    # [05] PC 날짜가 출시 기준일보다 앞 -> 포트도 열지 않는다
    clk = Clock()
    clk.base = datetime(2026, 1, 1, 9, 0, 0)
    opened = []
    with contextlib.redirect_stdout(io.StringIO()):
        res = up.sync_clock('COMx', opener=lambda p: opened.append(p), now=clk.now, sleep=lambda d: None, clock=clk.mono)
    check(res == 'pc_clock' and not opened, '[05] PC 날짜가 틀리면(출시 기준일 앞) 리더기에 보내지 않는다')

    # [06] 포트를 못 엶
    def boom(p):
        raise OSError('access denied')
    clk = Clock()
    with contextlib.redirect_stdout(io.StringIO()):
        res = up.sync_clock('COMx', opener=boom, now=clk.now, sleep=lambda d: None, clock=clk.mono)
    check(res == 'port', '[06] 포트를 못 열면 port(예외가 밖으로 안 나간다)')

    # [07] 시각은 보냈는데 그 뒤 응답이 없다 -> unconfirmed
    clk = Clock()
    r = FakeReader(clk)
    orig_tick = r._tick

    def tick_then_die():
        orig_tick()
        if r.sets:
            r.dead = True
    r._tick = tick_then_die
    res, _ = run(r, clk)
    check(res == 'unconfirmed' and r.sets == 1, '[07] 시각을 보낸 뒤 응답이 없으면 unconfirmed')

    # [08] 와이어 바이트 - 펌웨어 시뮬 시험(t_ii21)이 받는 것과 글자까지 같다
    check(up._frame({'cmd': 'cfg_get_config'}) == b'\x02{"cmd":"cfg_get_config"}\x03', '[08] 설정 읽기 프레임 바이트')
    check(up._frame({'cmd': 'cfg_set_date_time', 'device_date_time': '2026-10-11 14:03:22'}) ==
          b'\x02{"cmd":"cfg_set_date_time","device_date_time":"2026-10-11 14:03:22"}\x03', '[08] 시각 프레임 바이트(YYYY-MM-DD hh:mm:ss)')

    # [09] 응답 해석 - 실물 크기(256바이트 넘음)·한글 이름(0x80 이상)·본문 안의 ETX·앞에 낀 날 STX·덜 온 프레임
    clk = Clock()
    full = b'Firmware Version: 0x92 = v2.0\r\nPSOk\r\n' + FakeReader(clk, dtype=b'G').reply()
    got = up._parse_frame(bytearray(full))
    check(len(full) > 256 + 38 and isinstance(got, dict) and got.get('device_type') == 'G' and got.get('device_number') == 3 and
          got.get('manager_name', '').encode('latin-1') == KOREAN_NAME,
          '[09] 부팅 줄을 건너뛰고 본문 안의 ETX·한글 바이트를 넘어 해석한다(응답은 256바이트를 넘는다)')
    check(up._parse_frame(bytearray(full[:-10])) is None and up._parse_frame(bytearray(b'PSOk\r\n')) is None,
          '[09] 덜 온 프레임·프레임 없음은 None')
    noisy = b'\x02garbage' + FakeReader(clk).reply()
    got = up._parse_frame(bytearray(noisy))
    check(isinstance(got, dict) and got.get('device_type') == 'W', '[09] 앞에 날 0x02 가 끼어 있어도 그 뒤 프레임을 해석한다(STX 후보를 차례로)')
    check(up._reader_clock({'device_date_time': '2026-10-11 14:03:22'}) == datetime(2026, 10, 11, 14, 3, 22) and
          up._reader_clock({'device_date_time': 'YYYY-MM-DD hh:mm:ss'}) is None and up._reader_clock({}) is None and
          up._reader_clock({'device_date_time': 5}) is None, '[09] 되읽은 시계 해석: 꼴이 맞을 때만 datetime · 없거나 다르면 None')

    # [10] 출시 기준일 상수가 펌웨어(version.hpp)와 같다
    hpp = os.path.join(HERE, '..', 'lib', 'TraceQ_Arduino', 'src', 'TraceQ_Arduino', 'version.hpp')
    src = open(hpp, encoding='utf-8').read()
    ymd = tuple(int(re.search(r'#define\s+TRACEQ_RELEASE_%s\s+(\d+)' % k, src).group(1)) for k in ('YEAR', 'MONTH', 'DAY'))
    check(ymd == up.CLOCK_FLOOR, '[10] CLOCK_FLOOR = version.hpp 의 TRACEQ_RELEASE_*')

    # [11] 재시도 경계 - 준비가 첫 대기(25초)의 끝에 걸쳐 둘째 설정 읽기가 막 나간 때에도 거짓 "맞췄습니다" 가 없다
    #      (종전: 시각 프레임이 그 둘째 요청과 한 버퍼가 되어 버려지고도 ok 였다)
    bad = []
    for timeout, dtype in ((0.15, b'W'), (1.0, b'G')):
        for k in range(2400, 2760, 4):
            clk = Clock()
            r = FakeReader(clk, ready_at=k / 100.0, read_timeout=timeout, dtype=dtype)
            res, _ = run(r, clk)
            if not (res == 'ok' and clock_ok(r)):
                bad.append((dtype, k / 100.0, res, r.sets))
    check(not bad, '[11] 준비 시각 24.0~27.6초(재시도 경계) 180판 전부 ok 이고 리더기 시계가 실제로 맞다' + (' ' + repr(bad[:3]) if bad else ''))

    # [12] 첫 시각 프레임이 반영되지 않으면(버려짐) 되읽기가 알아채고 한 번 더 보낸다
    clk = Clock()
    r = FakeReader(clk, drop_sets=1)
    res, out = run(r, clk)
    check(res == 'ok' and r.set_frames == 2 and r.sets == 1 and clock_ok(r) and '한 번 더' in out,
          '[12] 첫 시각 명령이 반영되지 않으면 되읽기로 알아채고 다시 보내 ok')

    # [13] 끝내 반영되지 않으면 "맞췄습니다" 가 아니라 mismatch
    clk = Clock()
    r = FakeReader(clk, drop_sets=9)
    res, out = run(r, clk)
    check(res == 'mismatch' and r.set_frames == 2 and not clock_ok(r) and '맞췄습니다' not in out and '[주의]' in out,
          '[13] 두 번 보내도 리더기 시계가 다르면 mismatch(거짓 성공 문구 없음)')

    # [14] 옛 펌웨어(되읽기 칸 없음) -> 보냈다는 것까지만 알린다
    clk = Clock()
    r = FakeReader(clk, clock_key=False)
    res, out = run(r, clk)
    check(res == 'sent' and r.sets == 1 and '맞췄습니다' not in out, '[14] device_date_time 이 없는 판이면 sent(되읽어 확인했다고 말하지 않는다)')

    # [15] 느린 부팅(버튼 조작 + 재리셋 · 게이트웨이 14.5초)도 준비 대기 안
    clk = Clock()
    r = FakeReader(clk, ready_at=14.5, read_timeout=1.0, dtype=b'G')
    res, _ = run(r, clk)
    check(res == 'ok' and clock_ok(r), '[15] 준비가 14.5초 걸려도 ok(준비 대기의 하한)')

    # [16] 통신 중 예외(시각 명령 쓰기에서 시간 초과) -> 밖으로 안 나가고 port · 포트는 닫는다
    clk = Clock()
    r = FakeReader(clk)
    r.write_fail_at = 2
    try:
        res, _ = run(r, clk)
    except Exception as e:  # noqa: BLE001
        res = 'EXC:' + type(e).__name__
    check(res == 'port' and r.closed and r.sets == 0, '[16] 통신 중 예외는 밖으로 안 나간다(성공한 업로드가 [오류] 로 끝나지 않게) · 포트를 닫는다')

    # [17] _open_port - DTR·RTS 를 내린 채 115200 으로 열고, 거부되면 0.5초 간격으로 다시 연다
    naps = []
    _FakeSerialPort.made.clear()
    _FakeSerialPort.plan[:] = [OSError('busy'), OSError('busy'), None]
    try:
        s = up._open_port('COM77', sleep=naps.append)
    except OSError:
        s = None
    check(s is not None and s.opened and s.at_open == ('COM77', 115200, 0.2, 2, False, False) and len(_FakeSerialPort.made) == 3 and naps == [0.5, 0.5],
          '[17] 포트 열기: 115200 · 읽기 0.2초 · 쓰기 제한 2초 · DTR/RTS 내림(열기 전에) · 두 번 거부돼도 다시 열어 성공')
    naps = []
    _FakeSerialPort.plan[:] = [OSError('denied')] * 6
    try:
        up._open_port('COM77', sleep=naps.append)
        raised = False
    except OSError:
        raised = True
    check(raised and len(naps) == 5, '[17] 끝내 못 열면 예외(6번 시도 · 사이 5번 쉼) - sync_clock 이 port 로 알린다')
    _FakeSerialPort.plan[:] = []

    # [18] 화면에 찍는 글자는 cp949 로 쓸 수 있다(배치가 출력을 파일로 돌리면 한 글자로 빌드가 멈춘다) + 못 쓰는 글자는 '?' 로
    bad = []
    for path in (os.path.join(HERE, 'traceq_fw_upload.py'), os.path.join(HERE, 'gen_version.py'), os.path.abspath(__file__)):
        for text in printed_strings(path):
            try:
                text.encode('cp949')
            except UnicodeEncodeError:
                bad.append((os.path.basename(path), text[:30]))
    check(not bad, '[18] 업로더·gen_version·이 시험의 출력 문자열은 전부 cp949 로 쓸 수 있다' + (' ' + ascii(bad[:3]) if bad else ''))
    keep = sys.stdout
    try:
        sys.stdout = io.TextIOWrapper(io.BytesIO(), encoding='cp949')
        up._safe_stdout()
        try:
            print(chr(0x2014) + ' ok')
            sys.stdout.flush()
            survived = True
        except UnicodeEncodeError:
            survived = False
    finally:
        sys.stdout = keep
    check(survived, '[18] _safe_stdout 뒤에는 cp949 에 없는 글자를 찍어도 죽지 않는다')

    # [19] main 의 순서 - 업로드가 성공해야 시계를 맞추고, 시계 맞춤이 실패해도 종료코드는 0
    calls = []
    saved = (up.pick_port, up.pre_reset, up.subprocess.call, up.sync_clock, up.os.path.isfile, up.time.sleep)
    try:
        up.pick_port = lambda: 'COM77'
        up.pre_reset = lambda port: calls.append(('reset', port))
        up.os.path.isfile = lambda p: True
        up.time.sleep = lambda d: None
        up.sync_clock = lambda port: calls.append(('clock', port)) or 'mismatch'
        up.subprocess.call = lambda cmd: calls.append(('avrdude', cmd[cmd.index('-P') + 1])) or 0
        with contextlib.redirect_stdout(io.StringIO()):
            code_ok = up.main()
        order_ok = [c[0] for c in calls]
        calls.clear()
        up.subprocess.call = lambda cmd: calls.append(('avrdude', cmd[cmd.index('-P') + 1])) or 1
        with contextlib.redirect_stdout(io.StringIO()):
            code_fail = up.main()
        order_fail = [c[0] for c in calls]
    finally:
        (up.pick_port, up.pre_reset, up.subprocess.call, up.sync_clock, up.os.path.isfile, up.time.sleep) = saved
    check(code_ok == 0 and order_ok == ['reset', 'avrdude', 'clock'],
          '[19] 업로드 성공 -> 그 포트로 시계 맞춤 1회 · 시계가 안 맞아도 종료코드 0')
    check(code_fail == 1 and order_fail == ['reset', 'avrdude'] * 3,
          '[19] 업로드 실패(3회) -> 시계 맞춤을 부르지 않는다(옛 펌웨어에 시각을 보내지 않는다) · 종료코드 = avrdude 의 것')

    # [20] gen_version - firmware.hex 안에 version.hpp 의 버전 문자열이 없으면(옛 빌드) 멈추고 fw_version.txt 를 안 쓴다
    import gen_version as gv

    def ihex(addr, data):
        """데이터 레코드 한 줄(주소 16비트)."""
        body = bytes([len(data), (addr >> 8) & 0xFF, addr & 0xFF, 0]) + data
        return ':' + (body + bytes([(-sum(body)) & 0xFF])).hex().upper() + '\n'

    with tempfile.TemporaryDirectory() as tmp:
        hpp_t = os.path.join(tmp, 'version.hpp')
        hex_t = os.path.join(tmp, 'firmware.hex')
        out_t = os.path.join(tmp, 'fw_version.txt')
        open(hpp_t, 'w', encoding='utf-8').write('#define TRACEQ_VERSION_STRING "9.8.7"\n')
        # 확장 선형 주소(04) 뒤의 레코드 + 문자열이 두 레코드에 걸친 hex
        open(hex_t, 'w', encoding='ascii').write(':020000040000FA\n' + ihex(0x0100, b'\x0c\x94 Ver 9.8') + ihex(0x010A, b'.7\x00rest') + ':00000001FF\n')
        with contextlib.redirect_stdout(io.StringIO()):
            rc_same = gv.main(hpp_t, hex_t, out_t)
        wrote = os.path.isfile(out_t) and open(out_t, encoding='ascii').read() == '9.8.7'
        os.remove(out_t)
        open(hex_t, 'w', encoding='ascii').write(':020000040000FA\n' + ihex(0x0100, b'\x0c\x94 Ver 9.8.6\x00') + ':00000001FF\n')
        with contextlib.redirect_stdout(io.StringIO()):
            rc_old = gv.main(hpp_t, hex_t, out_t)
        check(rc_same == 0 and wrote and rc_old == 1 and not os.path.isfile(out_t),
              '[20] gen_version: hex 안에 그 버전 문자열이 있으면 fw_version.txt 를 쓰고(레코드 경계에 걸쳐도), 없으면 멈추고 쓰지 않는다')

    print(f'==> pass={PASS} fail={FAIL}')
    return 1 if FAIL else 0


if __name__ == '__main__':
    sys.exit(main())
