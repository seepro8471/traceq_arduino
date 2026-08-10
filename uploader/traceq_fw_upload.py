# -*- coding: utf-8 -*-
"""TraceQ 아두이노 펌웨어 업로더 — 더블클릭 한 번으로 플래시.

동작:
  1. USB 에 연결된 아두이노(정품 Mega 2560 / CH340·CH341 클론) 자동 감지
  2. avrdude(내장)로 firmware.hex 업로드
  3. 완료 안내 (새 빌드 첫 부팅 시 EEPROM 완전 초기화 — 타입/번호 재설정 필요)

firmware.hex 교체: exe 와 같은 폴더에 firmware.hex 를 두면 내장본 대신
그 파일을 올린다 (새 펌웨어 배포 시 exe 재빌드 불필요).

빌드(개발 PC): uploader\\build_uploader.bat 참조.
"""
import os
import subprocess
import sys
import time


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


def main() -> int:
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
            print(f'재시도 {attempt}/3 — 보드를 리셋하고 다시 시도합니다...')
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
    print('※ 새 펌웨어의 첫 부팅에서 설정값이 전부 초기화됩니다 (완전 초기화).')
    print('   리더기 메뉴(또는 세척관리 프로그램)에서 기기 타입과 번호를')
    print('   다시 설정하십시오. (기본값: 타입 W, 번호 1)')
    return 0


if __name__ == '__main__':
    try:
        code = main()
    except Exception as e:  # noqa: BLE001 — 현장 사용자에게 원문 노출
        print(f'[오류] 예기치 못한 문제: {e}')
        code = 1
    print()
    input('엔터를 누르면 창이 닫힙니다...')
    sys.exit(code)
