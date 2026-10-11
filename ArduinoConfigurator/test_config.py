# -*- coding: utf-8 -*-
"""설정기(arduino_config.py) 시험 - 포트는 열지 않는다(가짜 serial 모듈을 먼저 끼운다 · 창은 화면 없이 만든다).

보는 것(18차):
  · 응답 해석: 담당자 키에 제어 문자(0x03 포함)가 든 실물 크기 응답도 읽는다 · 한글(cp949) 이름이 깨지지 않는다
  · 보내는 시각: 칸을 사람이 고치지 않았으면 누르는 순간의 PC 시각(칸이 묵어 있어도) · 고쳤으면 그 값
  · [설정 저장] 은 설정을 읽은 뒤에만 켜진다(읽기 전 저장이 폼의 처음 값으로 리더기를 덮지 않게)
  · 창이 뜰 때의 자동 연결은 한 번만 · 기기 번호 칸은 0~999
실행: py ArduinoConfigurator\\test_config.py   (끝줄 `==> pass=N fail=M` · 실패가 있으면 종료코드 1)
"""
import os
import sys
import types
from datetime import datetime, timedelta

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
from PyQt6.QtCore import QDateTime   # noqa: E402
from PyQt6.QtGui import QShowEvent   # noqa: E402
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


class _OpenSer:
    is_open = True

    def close(self):
        pass


def near_now(text, slack=3.0):
    try:
        return abs((datetime.strptime(text, '%Y-%m-%d %H:%M:%S') - datetime.now()).total_seconds()) <= slack
    except (TypeError, ValueError):
        return False


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
    stream = b'Firmware Version: 0x92 = v2.0\r\n' + bytes([ac.STX]) + BODY + bytes([ac.ETX])
    w._buf.extend(stream[:150])
    w._drain()
    mid = len(seen)
    w._buf.extend(stream[150:])
    w._drain()
    check(mid == 0 and len(seen) == 1 and seen[0].get('device_type') == 'D' and len(w._buf) == 0 and
          lines == ['Firmware Version: 0x92 = v2.0'],
          '[02] 나뉘어 온 응답을 이어 한 번만 내보낸다(본문 안의 0x03 을 넘어 · 버퍼가 남지 않는다 · 부팅 줄은 줄로)')

    # 창 - 화면에 띄우지 않는다(자동 연결이 돌지 않게). 보내는 것은 가로채 모은다.
    win = ac.ArduinoConfigurator()
    sent = []
    win._worker.send_json = lambda payload: sent.append(payload) or True

    # [03] 기기 번호 칸 0~999(사장님 10-11)
    check(win.num_spin.minimum() == 0 and win.num_spin.maximum() == 999, '[03] 기기 번호 칸은 0~999')

    # [04] [설정 저장] 은 설정을 읽은 뒤에만
    win._worker._ser = _OpenSer()
    win._on_connected('COM77')
    before = win.btn_write.isEnabled()
    read_on = win.btn_read.isEnabled() and win.btn_send_dt.isEnabled()
    win._on_json(got)
    after = win.btn_write.isEnabled()
    check(not before and read_on and after, '[04] 연결만으로는 [설정 저장] 이 꺼져 있고(읽기·시각 전송은 켜짐) 설정을 읽으면 켜진다')
    check(win.type_combo.currentData() == 'D' and win.num_spin.value() == 120 and win.df_slot_spin.value() == 2 and
          win.mgr_name_edit.text() == '홍길동', '[04] 읽은 값이 폼에 들어간다(타입 D · 번호 120 · 슬롯 2 · 한글 이름)')
    win._on_disconnected()
    off = win.btn_write.isEnabled()
    win._on_connected('COM77')
    check(not off and not win.btn_write.isEnabled(), '[04] 연결을 끊었다 다시 이으면 다시 읽기 전까지 [설정 저장] 은 꺼져 있다')
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

    # [06] 사람이 칸을 고치면 그 값을 보내고 자동 갱신이 덮지 않는다 · [PC 시간으로] 가 되돌린다
    win.dt_edit.setDateTime(QDateTime.fromString('2030-01-02 03:04:05', 'yyyy-MM-dd HH:mm:ss'))
    win._tick_clock()
    sent.clear()
    win._do_send_datetime()
    win._do_write_config()
    check(win._dt_manual and [p.get('device_date_time') for p in sent] == ['2030-01-02 03:04:05'] * 2,
          '[06] 사람이 고친 시각은 그대로 보낸다(자동 갱신이 덮지 않는다)')
    win._set_dt_now()
    sent.clear()
    win._do_send_datetime()
    check(not win._dt_manual and near_now(sent[0].get('device_date_time')), '[06] [PC 시간으로] 뒤에는 다시 지금의 PC 시각')

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

    win._clock_timer.stop()
    win.close()
    print(f'==> pass={PASS} fail={FAIL}')
    return 1 if FAIL else 0


if __name__ == '__main__':
    sys.exit(main())
