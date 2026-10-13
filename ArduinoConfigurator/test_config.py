# -*- coding: utf-8 -*-
"""설정기(arduino_config.py) 시험 - 포트는 열지 않는다(가짜 serial 모듈을 먼저 끼운다 · 창은 화면 없이 만든다).

보는 것(18·19차):
  · 응답 해석: 담당자 키에 제어 문자(0x03 포함)가 든 실물 크기 응답도 읽는다 · 한글(cp949) 이름이 깨지지 않는다
  · 수신: 버퍼 머리에 잘린 프레임·날 STX·잡음이 끼어도 그 뒤 응답이 닿는다 · `PSOk` 에 `Z` 로 답하지 않는다
  · 내보내는 바이트: STX{json}ETX(펌웨어 시뮬 t_ii21 ④b 가 같은 글자를 받는다)
  · 보내는 시각: [설정 저장] 은 늘 누르는 순간의 PC 시각 · 사람이 고친 시각은 [날짜/시간 전송] 한 번에만 · 휠로는 안 바뀐다
  · [설정 저장] 은 설정을 읽은 뒤에만 켜진다 · 해제 뒤에 배달된 응답은 버린다 · 연결하면 설정을 자동으로 읽는다
  · 창이 뜰 때의 자동 연결은 한 번만 · 기기 번호 칸은 0~999
실행: py ArduinoConfigurator\\test_config.py   (끝줄 `==> pass=N fail=M` · 실패가 있으면 종료코드 1)
"""
import os
import sys
import types
from datetime import datetime

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


# 어떤 경로로도 실물 포트가 열리지 않게 - 가짜 serial 모듈(포트 목록은 비어 있고, 열려고 하면 예외)
class _NoSerial:
    def __init__(self, *a, **k):
        raise OSError('test: no real port')


_serial = types.ModuleType('serial')
_serial.Serial = _NoSerial
_serial.EIGHTBITS, _serial.PARITY_NONE, _serial.STOPBITS_ONE = 8, 'N', 1
_tools = types.ModuleType('serial.tools')
_lp = types.ModuleType('serial.tools.list_ports')
_lp.comports = lambda: []
_serial.tools = _tools
_tools.list_ports = _lp
sys.modules['serial'] = _serial
sys.modules['serial.tools'] = _tools
sys.modules['serial.tools.list_ports'] = _lp

import arduino_config as ac   # noqa: E402
from PyQt6.QtCore import QDateTime, QPoint, QPointF, Qt   # noqa: E402
from PyQt6.QtGui import QShowEvent, QWheelEvent   # noqa: E402
from PyQt6.QtTest import QTest   # noqa: E402
from PyQt6.QtWidgets import QApplication   # noqa: E402

PASS = FAIL = 0


def check(cond, label):
    global PASS, FAIL
    if cond:
        PASS += 1
        print('PASS', label)
    else:
        FAIL += 1
        print('FAIL', label)


KOREAN = '홍길동'.encode('cp949')
BODY = (b'{"device_type":"D","device_number":120,"alarm_sound":true,"washing_time":4,"df_time":18,'
        b'"manager_key":"ND01\x03\x01456","manager_name":"' + KOREAN + b'","df_cnt":12,"df_max_cnt":30,"df_sim_delay":3,'
        b'"df_sim_slot":2,"df_clear_cnt":2,"df_clear_date_time":"2026-10-01 09:00:00","patient_check":true,'
        b'"device_date_time":"2026-10-11 14:03:22"}')
FRAME = bytes([ac.STX]) + BODY + bytes([ac.ETX])
FMT = 'yyyy-MM-dd HH:mm:ss'


class _OpenSer:
    """열려 있는 가짜 포트 - 쓴 바이트를 모은다."""
    is_open = True

    def __init__(self):
        self.written = bytearray()

    def write(self, data):
        self.written += data
        return len(data)

    def close(self):
        pass


def near_now(text, slack=3.0):
    try:
        return abs((datetime.strptime(text, '%Y-%m-%d %H:%M:%S') - datetime.now()).total_seconds()) <= slack
    except (TypeError, ValueError):
        return False


def drained(stream):
    """새 수신기에 바이트를 넣고 (JSON 건수, 줄 목록, 남은 버퍼 길이, 써 보낸 바이트)."""
    w = ac.SerialWorker()
    w._ser = _OpenSer()
    seen, lines = [], []
    w.json_received.connect(seen.append)
    w.line_received.connect(lines.append)
    w._buf.extend(stream)
    w._drain()
    return seen, lines, len(w._buf), bytes(w._ser.written)


def wheel(widget):
    ev = QWheelEvent(QPointF(5, 5), QPointF(5, 5), QPoint(0, 0), QPoint(0, 120), Qt.MouseButton.NoButton,
                     Qt.KeyboardModifier.NoModifier, Qt.ScrollPhase.NoScrollPhase, False)
    QApplication.sendEvent(widget, ev)


def main():
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(errors='replace')
        except Exception:  # noqa: BLE001
            pass

    # [01] 응답 해석(순수 함수)
    got = ac.decode_reply(BODY) or {}       # 못 읽으면 빈 것으로 — 아래 판정들이 죽지 않고 FAIL 을 낸다
    check(got.get('device_number') == 120 and got.get('manager_key') == 'ND01\x03\x01456',
          '[01] 담당자 키에 제어 문자(0x03·0x01)가 든 응답도 읽는다(strict=False)')
    check(ac.decode_reply(BODY[:40]) is None and ac.decode_reply(b'[1,2]') is None and ac.decode_reply(b'') is None,
          '[01] 덜 온 본문·객체가 아닌 JSON·빈 것은 None')
    check(ac.reader_text(got.get('manager_name', '')) == '홍길동' and ac.reader_text('KIMJH\x00\x00') == 'KIMJH' and ac.reader_text('') == '',
          '[01] 한글 담당자 이름(cp949)이 깨지지 않는다 · 영문·빈 글자는 그대로')
    now = datetime(2026, 10, 11, 14, 0, 30)
    check(ac.clock_to_send(False, '2026-10-11 14:00:02', now) == '2026-10-11 14:00:30' and
          ac.clock_to_send(True, '2030-01-02 03:04:05', now) == '2030-01-02 03:04:05',
          '[01] 보낼 시각: 칸을 안 고쳤으면 지금의 PC 시각 · 고쳤으면 그 값')
    check('7초' in ac.clock_gap_text('2026-10-11 14:00:23', now) and ac.clock_gap_text('YYYY-MM-DD hh:mm:ss', now) == '',
          '[01] 되읽은 리더기 시계와 PC 의 차이 글자(꼴이 다르면 빈 글자)')

    app = QApplication.instance() or QApplication(sys.argv)

    # [02] 수신 조각 -> 프레임(제어 문자 든 본문 · 두 조각으로 나뉘어 옴 · 앞뒤 줄)
    w = ac.SerialWorker()
    seen, lines = [], []
    w.json_received.connect(seen.append)
    w.line_received.connect(lines.append)
    stream = b'Firmware Version: 0x92 = v2.0\r\n' + FRAME
    w._buf.extend(stream[:150])
    w._drain()
    mid = len(seen)
    w._buf.extend(stream[150:])
    w._drain()
    check(mid == 0 and len(seen) == 1 and seen[0].get('device_type') == 'D' and len(w._buf) == 0 and
          lines == ['Firmware Version: 0x92 = v2.0'],
          '[02] 나뉘어 온 응답을 이어 한 번만 내보낸다(본문 안의 0x03 을 넘어 · 버퍼가 남지 않는다 · 부팅 줄은 줄로)')

    # [02b] 버퍼 머리가 막혀도 그 뒤 응답이 닿는다(19차 - 종전엔 한 번 막히면 응답이 전부 폼에 안 닿았다)
    cut = FRAME[:60]                                             # 응답이 나오다 리더기가 재부팅 - 잘린 프레임
    a = drained(cut + b'Firmware Version: 0x92 = v2.0\r\n' + FRAME)
    b = drained(bytes([ac.STX]) + FRAME)                         # 날 STX 한 바이트
    c = drained(b'\xff' + FRAME)                                 # 줄바꿈 없는 잡음 한 바이트
    d = drained(cut + FRAME)                                     # 잘린 프레임 바로 뒤에 온전한 프레임(줄 없음)
    check(all(len(x[0]) == 1 and x[0][0].get('device_number') == 120 and x[2] == 0 for x in (a, b, c, d)),
          '[02b] 머리에 잘린 프레임·날 STX·잡음이 끼어도 그 뒤 응답 1건이 닿고 버퍼가 남지 않는다')
    check(len(a[1]) == 1 and a[1][0].endswith('Firmware Version: 0x92 = v2.0'),
          '[02b] 잘린 프레임 뒤의 부팅 줄은 버리지 않는다(잘린 잔해가 앞에 붙은 한 줄로 보인다)')
    g = drained(b'\xff' + FRAME + b'PSOk\r\n')                   # 잡음 + 응답 + 줄 - 줄바꿈이 있어도 응답을 줄로 삼키지 않는다
    h = drained(FRAME + FRAME)                                   # 응답 둘이 한 조각에
    i = drained(cut + cut + FRAME)                               # 잘린 프레임 둘 연속(뒤쪽 STX 후보를 전부 본다)
    check(len(g[0]) == 1 and g[1] == ['PSOk'] and g[2] == 0, '[02b] 잡음·응답·줄이 한 조각에 와도 응답 1건 + 줄 1건')
    check(len(h[0]) == 2 and h[2] == 0, '[02b] 한 조각에 든 응답 둘을 둘 다 내보낸다')
    check(len(i[0]) == 1 and i[2] == 0, '[02b] 잘린 프레임이 둘 연속이어도 그 뒤 응답이 닿는다')
    e = drained(FRAME[:200])                                     # 아직 덜 온 응답 - 기다린다(버리지 않는다)
    check(len(e[0]) == 0 and e[2] == 200, '[02b] 덜 온 응답은 버리지 않고 기다린다')
    f = drained(cut + b'Firmware Version: 0x92 = v2.0\r\nPSOk\r\n')   # 잘린 프레임 뒤에 줄만 - 다음 응답을 기다리지 않고 줄을 보인다
    check(len(f[1]) == 2 and f[1][1] == 'PSOk' and f[2] == 0, '[02b] 잘린 프레임 뒤의 줄은 다음 응답이 없어도 곧바로 줄로 보인다')

    # [02c] `PSOk` 에 `Z` 로 답하지 않는다(19차 판정 - 서버 리더기가 설정기를 PC 프로그램으로 알면 완료 태그의 기록이 지워진다)
    p = drained(b'PSOk\r\n')
    check(p[1] == ['PSOk'] and p[3] == b'', '[02c] PSOk 줄은 로그로만 - 아무것도 써 보내지 않는다')

    # [02d] 내보내는 바이트(19차 - 그동안 시험이 send_json 을 통째로 갈아 끼워 아무도 안 봤다 · 펌웨어 시뮬 t_ii21 ④b 와 같은 글자)
    w2 = ac.SerialWorker()
    w2._ser = _OpenSer()
    ok1 = w2.send_json({'cmd': 'cfg_get_config'})
    ok2 = w2.send_json({'cmd': 'cfg_set_date_time', 'device_date_time': '2026-10-13 15:04:05'})
    check(ok1 and ok2 and bytes(w2._ser.written) ==
          b'\x02{"cmd": "cfg_get_config"}\x03\x02{"cmd": "cfg_set_date_time", "device_date_time": "2026-10-13 15:04:05"}\x03',
          '[02d] send_json 은 STX{json}ETX 를 쓴다(구분자 `": "`·`", "`)')
    w2._ser = None
    check(w2.send_json({'cmd': 'cfg_get_config'}) is False, '[02d] 포트가 없으면 보내지 않고 False')

    # 창 - 화면에 띄우지 않는다(자동 연결이 돌지 않게). 보내는 것은 가로채 모은다.
    win = ac.ArduinoConfigurator()
    sent, logs = [], []
    win._worker.send_json = lambda payload: sent.append(payload) or True
    win._log = logs.append

    # [03] 기기 번호 칸 0~999(사장님 10-11)
    check(win.num_spin.minimum() == 0 and win.num_spin.maximum() == 999, '[03] 기기 번호 칸은 0~999')

    # [04] [설정 저장] 은 설정을 읽은 뒤에만
    win._worker._ser = _OpenSer()
    win._on_connected('COM77')
    before = win.btn_write.isEnabled()
    read_on = win.btn_read.isEnabled() and win.btn_send_dt.isEnabled()
    win._on_json({'df_cnt': 3})                # 설정 응답이 아닌 것(타입 칸 없음)으로는 안 켜진다
    still_off = win.btn_write.isEnabled()
    win._on_json(got)
    after = win.btn_write.isEnabled()
    check(not before and read_on and not still_off and after,
          '[04] 연결만으로는 [설정 저장] 이 꺼져 있고(읽기·시각 전송은 켜짐) 설정(타입 칸이 있는 응답)을 읽으면 켜진다')
    check(win.type_combo.currentData() == 'D' and win.num_spin.value() == 120 and win.df_slot_spin.value() == 2 and
          win.mgr_name_edit.text() == '홍길동', '[04] 읽은 값이 폼에 들어간다(타입 D · 번호 120 · 슬롯 2 · 한글 이름)')
    win._on_disconnected()
    off = win.btn_write.isEnabled()
    win._on_connected('COM77')
    check(not off and not win.btn_write.isEnabled(), '[04] 연결을 끊었다 다시 이으면 다시 읽기 전까지 [설정 저장] 은 꺼져 있다')
    # 해제 뒤에 배달된 응답(수신 스레드가 줄 세워 둔 것)은 버린다 - 다음 연결이 읽기 전에 저장을 켜지 않는다(19차)
    win._on_disconnected()
    win._worker._ser = None
    win.num_spin.setValue(5)
    win._on_json(got)
    kept = win.num_spin.value() == 5
    win._worker._ser = _OpenSer()
    win._on_connected('COM78')
    check(kept and not win.btn_write.isEnabled(), '[04] 해제 뒤에 배달된 응답은 폼도 [설정 저장] 도 건드리지 않는다')
    win._do_reset_defaults()
    check(win.btn_write.isEnabled() and win.num_spin.value() == 1, '[04] [초기값으로 설정] 은 일부러 채운 것이라 저장을 켠다')

    # [05] 보내는 시각 - 칸이 묵어 있어도 누르는 순간의 PC 시각
    stale = QDateTime.currentDateTime().addSecs(-25)
    win._dt_auto = True
    win.dt_edit.setDateTime(stale)             # 프로그램이 채운 값(자동 갱신 사이의 묵은 표시)
    win._dt_auto = False
    sent.clear()
    win._do_send_datetime()
    win._do_write_config()
    check(len(sent) == 2 and sent[0].get('cmd') == 'cfg_set_date_time' and near_now(sent[0].get('device_date_time')) and
          sent[1].get('cmd') == 'cfg_set_config' and near_now(sent[1].get('device_date_time')),
          '[05] [날짜/시간 전송]·[설정 저장] 둘 다 누르는 순간의 PC 시각을 보낸다(칸이 25초 묵어 있어도)')

    # [06] 시각 칸(19차): 휠로는 안 바뀐다 · 사람이 고친 시각은 [날짜/시간 전송] 한 번에만 · [설정 저장] 은 늘 PC 시각
    win._set_dt_now()
    shown = win.dt_edit.dateTime().toString(FMT)
    wheel(win.dt_edit)
    check(not win._dt_manual and win.dt_edit.dateTime().toString(FMT) == shown,
          '[06] 시각 칸 위의 휠은 값을 바꾸지 않는다(종전엔 연도가 +1 된 채 굳어 저장·전송마다 그 시각이 나갔다)')
    spins = [win.num_spin, win.washing_spin, win.df_spin, win.df_max_spin, win.df_delay_spin, win.df_slot_spin, win.df_clear_spin]
    combos = [win.type_combo, win.port_combo]
    win.type_combo.setCurrentIndex(1)
    for sp in spins:
        sp.setValue(sp.minimum() + 1)
    before_vals = [sp.value() for sp in spins] + [cb.currentIndex() for cb in combos]
    for wdg in spins + combos:
        wheel(wdg)
    check([sp.value() for sp in spins] + [cb.currentIndex() for cb in combos] == before_vals,
          '[06] 타입 콤보·숫자 칸도 휠로 바뀌지 않는다(시각 칸의 형제 - 바뀐 채 [설정 저장] 으로 나갔다)')
    check(all(wdg.focusPolicy() == Qt.FocusPolicy.StrongFocus for wdg in spins + combos + [win.dt_edit]),
          '[06] 그 칸들은 휠로 초점도 받지 않는다(시각 칸이 초점을 받으면 자동 갱신이 멈춘다)')
    edited = QDateTime.fromString('2030-01-02 03:04:05', FMT)
    win.dt_edit.setDateTime(edited)            # 사람이 고침
    win._tick_clock()
    marked = win._dt_manual and win.dt_edit.styleSheet() != '' and win.dt_edit.dateTime().toString(FMT) == '2030-01-02 03:04:05'
    sent.clear()
    logs.clear()
    win._do_write_config()
    still = win._dt_manual
    log_cfg = list(logs)
    logs.clear()
    win._do_send_datetime()
    log_manual = list(logs)
    back = not win._dt_manual and win.dt_edit.styleSheet() == '' and near_now(win.dt_edit.dateTime().toString(FMT))
    logs.clear()
    win._do_send_datetime()
    log_auto = list(logs)
    check(marked, '[06] 사람이 고친 시각은 자동 갱신이 덮지 않고 칸에 표시가 난다')
    check(len(sent) == 3 and near_now(sent[0].get('device_date_time')) and still,
          '[06] [설정 저장] 은 고친 칸과 무관하게 누르는 순간의 PC 시각을 싣는다(고친 값은 그대로 남는다)')
    check(len(sent) == 3 and sent[1].get('device_date_time') == '2030-01-02 03:04:05' and back and near_now(sent[2].get('device_date_time')),
          '[06] 고친 시각은 [날짜/시간 전송] 한 번에만 쓰이고 칸은 PC 시각으로 돌아간다(다음 전송은 PC 시각)')
    check(len(sent) == 3 and len(log_cfg) == 1 and ('시각=' + str(sent[0].get('device_date_time'))) in log_cfg[0] and
          len(log_manual) == 1 and '2030-01-02 03:04:05' in log_manual[0] and '직접 고친' in log_manual[0] and
          len(log_auto) == 1 and '직접 고친' not in log_auto[0],
          '[06] 로그가 실제로 보낸 시각을 적고, 고친 시각을 보냈을 때만 그렇다고 적는다')
    win.dt_edit.setDateTime(edited)
    win._worker.send_json = lambda payload: False
    win._do_send_datetime()
    held = win._dt_manual
    win._worker.send_json = lambda payload: sent.append(payload) or True
    win.btn_pc_time.click()
    check(held and not win._dt_manual and near_now(win.dt_edit.dateTime().toString(FMT)),
          '[06] 전송이 실패하면 고친 시각이 남고 · [PC 시간으로] 버튼이 자동 갱신으로 되돌린다')
    win._dt_auto = True
    win.dt_edit.setDateTime(stale)
    win._dt_auto = False
    win.dt_edit.hasFocus = lambda: True        # 사람이 칸에 들어가 있는 동안
    win._tick_clock()
    focused = win.dt_edit.dateTime().toString(FMT) == stale.toString(FMT)
    del win.dt_edit.hasFocus
    win._tick_clock()
    check(focused and near_now(win.dt_edit.dateTime().toString(FMT)),
          '[06] 칸에 초점이 있는 동안에는 자동 갱신이 덮지 않는다(나오면 다시 따라간다)')
    win._dt_auto = True
    win.dt_edit.setDateTime(stale)
    win._dt_auto = False
    cal = win.dt_edit.calendarWidget()
    cal.isVisible = lambda: True               # 달력 팝업을 열어 둔 동안
    win._tick_clock()
    popup = win.dt_edit.dateTime().toString(FMT) == stale.toString(FMT)
    del cal.isVisible
    win._tick_clock()
    check(cal is not None and popup and near_now(win.dt_edit.dateTime().toString(FMT)),
          '[06] 달력 팝업이 열려 있는 동안에는 자동 갱신이 덮지 않는다(팝업의 달이 매초 이번 달로 돌아갔다)')

    # [07] 설정 저장 프레임의 칸(리더기가 읽는 이름 그대로)
    sent.clear()
    win.num_spin.setValue(999)
    win._do_write_config()
    keys = {'cmd', 'device_type', 'device_number', 'alarm_sound', 'washing_time', 'df_time', 'df_max_cnt', 'df_sim_delay',
            'df_sim_slot', 'df_clear_cnt', 'patient_check', 'device_date_time'}
    check(set(sent[0].keys()) == keys and sent[0]['device_number'] == 999, '[07] 설정 저장 프레임의 칸 이름 12개 · 번호 999 가 그대로 나간다')

    # [08] 창이 뜰 때의 자동 연결은 한 번만(창을 내렸다 올려도 다시 돌지 않는다)
    hits = []
    win._auto_connect_once = lambda: hits.append(1)
    win.showEvent(QShowEvent())
    win.showEvent(QShowEvent())
    QTest.qWait(700)
    check(len(hits) == 1, '[08] 자동 연결은 한 번만')

    # [09] 연결하면 설정을 자동으로 읽는다 - 18차 뒤로는 이것이 [설정 저장] 을 켜는 정상 길이다
    QTest.qWait(700)                           # 앞 판정들이 걸어 둔 자동 읽기를 먼저 흘려보낸다
    sent.clear()
    win._on_connected('COM77')
    QTest.qWait(700)
    check([p.get('cmd') for p in sent] == ['cfg_get_config'], '[09] 연결 0.5초 뒤 설정 읽기를 한 번 보낸다')

    win._clock_timer.stop()
    win.close()
    print(f'==> pass={PASS} fail={FAIL}')
    return 1 if FAIL else 0


if __name__ == '__main__':
    sys.exit(main())
