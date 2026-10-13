# -*- coding: utf-8 -*-
"""TraceQ 아두이노 펌웨어 업로더 — 더블클릭 한 번으로 플래시.

동작:
  1. USB 에 연결된 아두이노(정품 Mega 2560 / CH340·CH341 클론) 자동 감지
  2. avrdude(내장)로 firmware.hex 업로드
  3. 완료 안내 (새 빌드 첫 부팅 시 리더기가 설정 유지/초기화를 묻는다)
  4. 리더기 시계(RTC)를 이 PC 의 현재 시각으로 맞추고 되읽어 확인 (사장님 10-11 · sync_clock)

firmware.hex 교체: exe 와 같은 폴더에 firmware.hex 를 두면 내장본 대신
그 파일을 올린다 (새 펌웨어 배포 시 exe 재빌드 불필요).

빌드(개발 PC): uploader\\build_uploader.bat 참조.
"""
import json
import os
import subprocess
import sys
import time
from datetime import datetime

STX, ETX = 0x02, 0x03
# 펌웨어의 출시 기준일(version.hpp TRACEQ_RELEASE_*) — 리더기는 시계가 이보다 앞서면 '미동기' 로 본다.
# PC 날짜가 이보다 앞이면 PC 시계가 틀린 것이므로 리더기에 보내지 않는다(test_clock_sync 가 version.hpp 와 대조).
CLOCK_FLOOR = (2026, 9, 26)
# 되읽은 리더기 시계와 PC 시각의 허용 차(초) — 게이트웨이는 수신 대기 탓에 약 1초 늦게 맞는다.
CLOCK_TOLERANCE_S = 5


def _safe_stdout() -> None:
    """출력을 파일·파이프로 돌리면(cp949) 못 쓰는 글자 하나에 죽는다 — 성공한 업로드가 [오류] 로 끝났다. 못 쓰는 글자는 '?' 로."""
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(errors='replace')
        except Exception:  # noqa: BLE001
            pass


def resource_dir() -> str:
    """PyInstaller onefile 내장 리소스 경로 (개발 실행 시 스크립트 폴더)."""
    return getattr(sys, '_MEIPASS', os.path.dirname(os.path.abspath(__file__)))


def fw_version() -> str:
    """내장 펌웨어 버전 — 빌드 시 version.hpp 에서 자동 생성된 version.txt.

    (하드코딩 상수는 재빌드 때 갱신을 잊는 사고가 실제로 있었다 — 자동 주입.)
    """
    try:
        with open(os.path.join(resource_dir(), 'fw_version.txt'),
                  encoding='ascii') as f:
            return f.read().strip()
    except OSError:
        return '?'


def exe_dir() -> str:
    if getattr(sys, 'frozen', False):
        return os.path.dirname(os.path.abspath(sys.executable))
    return os.path.dirname(os.path.abspath(__file__))


def find_arduino_ports():
    from serial.tools import list_ports
    hits = []
    for p in list_ports.comports():
        desc = (p.description or '').lower()
        mfr = (p.manufacturer or '').lower()
        if ('arduino' in desc or 'arduino' in mfr
                or 'ch340' in desc or 'ch341' in desc):
            hits.append(p)
    return hits


def pick_port() -> str:
    ports = find_arduino_ports()
    if not ports:
        print()
        print('[오류] 아두이노를 찾지 못했습니다.')
        print('  - USB 케이블 연결을 확인하십시오.')
        print('  - 현재 인식된 포트:')
        from serial.tools import list_ports
        for p in list_ports.comports():
            print(f'      {p.device}  {p.description}')
        return ''
    if len(ports) == 1:
        print(f'아두이노 감지: {ports[0].device}  ({ports[0].description})')
        return ports[0].device
    print('아두이노가 여러 대 감지되었습니다:')
    for i, p in enumerate(ports, start=1):
        print(f'  {i}. {p.device}  ({p.description})')
    while True:
        sel = input('업로드할 번호를 입력하세요: ').strip()
        if sel.isdigit() and 1 <= int(sel) <= len(ports):
            return ports[int(sel) - 1].device


def pre_reset(port: str) -> None:
    """avrdude 전에 DTR 토글로 보드를 한 번 리셋해 부트로더 진입 확률을 높인다.

    일부 보드/케이블에서 avrdude 자체의 자동 리셋이 불발되어
    stk500v2 sync timeout 이 나는 것을 보완 (현장 실사례).
    """
    try:
        import serial
        s = serial.Serial()
        s.port = port
        s.baudrate = 115200
        s.dsrdtr = False
        s.open()
        s.dtr = False
        time.sleep(0.1)
        s.dtr = True
        s.close()
        time.sleep(0.3)   # 부트로더 진입 직후 avrdude 가 잡도록 짧게만 대기
    except Exception:
        pass              # 리셋 실패해도 avrdude 자체 리셋에 맡기고 진행


def _frame(obj) -> bytes:
    """설정기(ArduinoConfigurator)와 같은 꼴 — STX{json}ETX."""
    return bytes([STX]) + json.dumps(obj, separators=(',', ':')).encode('ascii') + bytes([ETX])


def _parse_frame(buf):
    """버퍼에서 STX{json}ETX 한 건을 dict 로. 아직 없으면 None.

    담당자 키·이름은 태그의 날 바이트라 본문에 0x03(ETX)이 들 수 있다 — ETX 후보를 차례로 시험한다(설정기와 같은 방식).
    부팅 때 나오는 줄(`Firmware Version: ...` · `PSOk`)은 STX 가 없어 건너뛴다. 앞에 날 0x02 가 끼어 있어도(잡음·본문 바이트)
    그 뒤의 STX 후보를 차례로 본다.
    """
    stx, etx = bytes([STX]), bytes([ETX])
    start = buf.find(stx)
    while start >= 0:
        pos = start
        while True:
            pos = buf.find(etx, pos + 1)
            if pos < 0:
                break
            try:
                obj = json.loads(bytes(buf[start + 1:pos]).decode('latin-1'), strict=False)
            except ValueError:
                continue
            if isinstance(obj, dict):
                return obj
        start = buf.find(stx, start + 1)
    return None


def _wait_config(ser, wait_s, clock=time.monotonic):
    """설정 읽기 응답을 wait_s 초까지 기다린다. 받으면 dict · 못 받으면 None."""
    buf = bytearray()
    deadline = clock() + wait_s
    while clock() < deadline:
        chunk = ser.read(256)
        if chunk:
            buf += chunk
            got = _parse_frame(buf)
            if got is not None:
                return got
    return None


def _open_port(port, tries=6, sleep=time.sleep):
    """리더기 포트를 연다. avrdude 가 포트를 막 닫은 직후라 드라이버가 잠깐 거부할 수 있다 — 0.5초 간격으로 다시 연다."""
    import serial
    last = None
    for i in range(tries):
        s = serial.Serial()
        s.port = port
        s.baudrate = 115200
        s.timeout = 0.2
        s.write_timeout = 2
        s.dtr = False          # DTR 을 세우면 보드가 다시 리셋된다 — 내린 채로 연다
        s.rts = False
        try:
            s.open()
            return s
        except Exception as e:  # noqa: BLE001
            last = e
            if i + 1 < tries:
                sleep(0.5)
    raise last


def _reader_clock(info):
    """설정 읽기 응답의 device_date_time(펌웨어 2.2.36 부터) → datetime. 칸이 없거나 꼴이 다르면 None."""
    text = info.get('device_date_time') if isinstance(info, dict) else None
    if not isinstance(text, str):
        return None
    try:
        return datetime.strptime(text, '%Y-%m-%d %H:%M:%S')
    except ValueError:
        return None


def sync_clock(port, opener=_open_port, now=datetime.now, sleep=time.sleep, clock=time.monotonic) -> str:
    """리더기 시계(RTC)를 이 PC 의 현재 시각으로 맞춘다 — 업로드 직후 한 번(사장님 10-11).

    리더기는 새 판 첫 부팅 때 'Keep settings?' 를 최대 10초 띄우고 그동안 명령을 읽지 않는다(바이트는 리더기 수신
    버퍼에 쌓인다). 그래서 먼저 설정 읽기(cfg_get_config)를 보내 **응답이 올 때**(= 부팅이 끝났을 때) 그 순간의 PC
    시각을 보낸다 — 미리 보내면 기다린 만큼 늦은 시각이 들어간다. 시각 명령에는 시리얼 응답이 없으므로(리더기는 긴 삐
    1회 + 화면 `updated`) 뒤이은 설정 읽기 응답의 `device_date_time`(리더기의 지금 시계 · 펌웨어 2.2.36 부터)을 PC 시각과
    견줘 확인한다 — 다르면 한 번 더 보낸다. 그 칸이 없는 옛 펌웨어면 보냈다는 것까지만 알린다('sent').
    리더기는 한 번에 읽힌 버퍼의 둘째 JSON 을 버리므로 명령 사이를 수신 대기(게이트웨이 1초)보다 길게 띄운다.

    돌려주는 값: 'ok'(되읽어 확인) · 'sent'(보냈으나 되읽기 없는 판) · 'mismatch'(되읽은 시계가 다름) ·
                 'bad_clock'(되읽은 값이 날짜 꼴이 아님 - 시계 부품) · 'pc_clock'(PC 날짜가 틀려 안 보냄) · 'port' · 'no_reply' · 'unconfirmed'.
    """
    t = now()
    if (t.year, t.month, t.day) < CLOCK_FLOOR:
        print(f'[주의] 이 PC 의 날짜({t:%Y-%m-%d})가 틀려 보여 리더기 시계는 맞추지 않았습니다.')
        print('       PC 날짜를 바로잡은 뒤 설정기나 세척관리로 맞추십시오.')
        return 'pc_clock'
    try:
        ser = opener(port)
    except Exception as e:  # noqa: BLE001 — 업로드는 이미 끝났다. 시계만 못 맞춘 것
        print(f'[주의] 리더기 시계를 맞추지 못했습니다(포트를 열 수 없음: {e}).')
        return 'port'
    try:
        # 포트를 열 때 보드가 다시 리셋돼도 부트로더가 끝난 뒤에 보낸다(부트로더가 받은 바이트는 사라진다).
        sleep(2.0)
        ser.reset_input_buffer()
        info = None
        for wait_s in (25.0, 15.0):   # 첫 판: Keep settings 10초 + 화면 초기화 · 둘째 판: 첫 바이트가 사라졌을 때
            ser.write(_frame({'cmd': 'cfg_get_config'}))
            info = _wait_config(ser, wait_s, clock)
            if info is not None:
                break
        if info is None:
            print('[주의] 리더기가 응답하지 않아 시계는 맞추지 못했습니다.')
            print('       리더기가 켜진 뒤 설정기나 세척관리로 맞추십시오.')
            return 'no_reply'
        print(f"리더기 응답: 타입 {info.get('device_type', '?')} · 번호 {info.get('device_number', '?')}")
        # 준비 응답 뒤 쉬고 비운다 — 위에서 다시 보낸 설정 읽기가 아직 리더기 수신 버퍼에 있으면, 곧바로 보낸 시각 프레임이
        #  그것과 한 버퍼가 되어 버려진다(둘째 JSON). 그 요청의 응답도 여기서 비운다.
        sleep(2.0)
        ser.reset_input_buffer()
        seen, diff = None, 0.0
        for attempt in (1, 2):
            text = now().strftime('%Y-%m-%d %H:%M:%S')
            ser.write(_frame({'cmd': 'cfg_set_date_time', 'device_date_time': text}))
            sleep(2.5)                # 리더기가 이 명령을 따로 읽게 한다(수신 대기 최대 1초 + 안내음 2초)
            ser.reset_input_buffer()
            ser.write(_frame({'cmd': 'cfg_get_config'}))
            back = _wait_config(ser, 8.0, clock)
            if back is None:
                print(f'[주의] 시각({text})을 보냈지만 리더기의 다음 응답을 받지 못했습니다.')
                print('       리더기 화면의 시각을 확인하십시오.')
                return 'unconfirmed'
            seen = _reader_clock(back)
            if seen is None:
                if 'device_date_time' in back and attempt == 1:
                    print('  되읽은 리더기 시계 값이 날짜 꼴이 아닙니다 - 한 번 더 보냅니다...')
                    continue
                print(f'리더기에 이 PC 시각({text})을 보냈습니다.')
                if 'device_date_time' in back:
                    # 두 번 다 날짜 꼴이 아니다 = 옛 펌웨어가 아니라 리더기의 시계 부품(RTC)이 이상한 값을 낸다(19차)
                    print(f"  [주의] 리더기가 돌려준 시계 값({back.get('device_date_time')!r})이 날짜 꼴이 아닙니다.")
                    print('         리더기의 시계 부품(RTC)을 점검하십시오 · 화면의 시각을 확인하십시오')
                    return 'bad_clock'
                print('  (이 펌웨어는 시계를 되읽어 주지 않습니다 - 긴 삐 소리 1회와 화면 `updated` · 화면의 시각을 확인하십시오)')
                return 'sent'
            diff = abs((seen - now()).total_seconds())
            if diff <= CLOCK_TOLERANCE_S:
                print(f'리더기 시계를 이 PC 시각으로 맞췄습니다 - 되읽은 리더기 시계 {seen:%Y-%m-%d %H:%M:%S} (PC 와 {diff:.0f}초 차이).')
                return 'ok'
            if attempt == 1:
                print(f'  되읽은 리더기 시계({seen:%Y-%m-%d %H:%M:%S})가 아직 다릅니다 - 한 번 더 보냅니다...')
        print(f'[주의] 리더기 시계({seen:%Y-%m-%d %H:%M:%S})가 이 PC 와 {diff:.0f}초 다릅니다.')
        print('       설정기나 세척관리로 다시 맞추십시오.')
        return 'mismatch'
    except Exception as e:  # noqa: BLE001
        print(f'[주의] 리더기 시계를 맞추는 중 통신이 끊겼습니다({e}) - 설정기나 세척관리로 맞추십시오.')
        return 'port'
    finally:
        try:
            ser.close()
        except Exception:  # noqa: BLE001
            pass


def main() -> int:
    _safe_stdout()
    ver = fw_version()
    print('=' * 58)
    print(f' TraceQ 아두이노 펌웨어 업로더  (기본 내장: v{ver})')
    print('=' * 58)

    # 펌웨어 선택 — exe 옆의 firmware.hex 우선, 없으면 내장본.
    external = os.path.join(exe_dir(), 'firmware.hex')
    if os.path.isfile(external):
        hex_path = external
        print(f'펌웨어: 외부 firmware.hex 사용 ({external})')
    else:
        hex_path = os.path.join(resource_dir(), 'firmware.hex')
        print(f'펌웨어: 내장본 v{ver} 사용')
    if not os.path.isfile(hex_path):
        print('[오류] firmware.hex 를 찾을 수 없습니다.')
        return 1

    avrdude = os.path.join(resource_dir(), 'avrdude', 'avrdude.exe')
    conf = os.path.join(resource_dir(), 'avrdude', 'avrdude.conf')
    if not os.path.isfile(avrdude):
        print('[오류] 내장 avrdude 를 찾을 수 없습니다.')
        return 1

    port = pick_port()
    if not port:
        return 1

    cmd = [avrdude, '-C', conf, '-p', 'atmega2560', '-c', 'wiring',
           '-P', port, '-b', '115200', '-D',
           '-U', f'flash:w:{hex_path}:i']

    ret = 1
    for attempt in range(1, 4):        # 최대 3회 자동 재시도
        print()
        if attempt > 1:
            print(f'재시도 {attempt}/3 - 보드를 리셋하고 다시 시도합니다...')
        print(f'{port} 에 업로드를 시작합니다... (보드를 뽑지 마십시오)')
        print('-' * 58)
        pre_reset(port)
        ret = subprocess.call(cmd)
        print('-' * 58)
        if ret == 0:
            break
        time.sleep(1.5)

    if ret != 0:
        print('[실패] 업로드에 실패했습니다. 아래 순서로 조치 후 다시 실행하십시오:')
        print('  1. 세척관리(TraceQ)/SeePro 프로그램이 켜져 있으면 완전히 종료')
        print('  2. USB 케이블을 뽑았다가 다시 꽂고 재실행')
        print('  3. 그래도 안 되면: 업로드 시작 직후 보드의 RESET 버튼을 한 번 누름')
        print('  4. 다른 USB 포트/케이블로 교체')
        return ret

    print('[완료] 업로드가 끝났습니다.')
    print()
    print('★ 지금 리더기 화면을 보십시오 - 설정을 그대로 둘지 묻습니다 (10초 안에 고르십시오).')
    print()
    print('      Keep settings?')
    print('    > Keep settings      <- 설정 유지')
    print('      Erase all             완전 초기화')
    print('    <>move  MENU=OK  7s')
    print()
    print('   · `<` `>` 로 고르고 MENU 를 눌러 확정합니다 (리더기 메뉴와 같은 조작).')
    print('   · 아무것도 누르지 않으면 10초 뒤 "설정 유지" 로 넘어갑니다.')
    print('   · 유지하면 기기 타입·번호·세척시간·담당자·소독 횟수·액교환일이 그대로 남습니다.')
    print('   · 완전 초기화를 고르면 기본값(타입 W, 번호 1)이 되니 다시 설정해야 합니다.')
    print()
    print('   ※ 나중에 초기화하려면: RIGHT(`>`) 을 누른 채 리더기 전원을 켜면 이 화면이 다시 뜹니다.')
    print('   ※ 공장 출하 새 보드는 묻지 않고 바로 초기화됩니다.')
    print()
    print('   [2.2.10] 설정 메뉴를 열어 두고 60초 동안 아무 버튼도 안 누르면 저장 없이 홈으로 나옵니다')
    print('            (메뉴에 머무는 동안은 태그를 읽지 않으므로).')
    print()
    print('리더기 시계를 이 PC 시각으로 맞춥니다 - 리더기 화면의 선택이 끝날 때까지 기다립니다(보통 20초 안팎 · 길면 1분)...')
    sync_clock(port)      # 실패해도 업로드는 성공이다 — 안내만 하고 0 을 돌려준다
    return 0


if __name__ == '__main__':
    _safe_stdout()
    try:
        code = main()
    except Exception as e:  # noqa: BLE001 — 현장 사용자에게 원문 노출
        print(f'[오류] 예기치 못한 문제: {e}')
        code = 1
    print()
    input('엔터를 누르면 창이 닫힙니다...')
    sys.exit(code)
