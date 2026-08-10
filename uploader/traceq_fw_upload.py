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

FW_VERSION = '2.2.3'


def resource_dir() -> str:
    """PyInstaller onefile 내장 리소스 경로 (개발 실행 시 스크립트 폴더)."""
    return getattr(sys, '_MEIPASS', os.path.dirname(os.path.abspath(__file__)))


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


def main() -> int:
    print('=' * 58)
    print(f' TraceQ 아두이노 펌웨어 업로더  (기본 내장: v{FW_VERSION})')
    print('=' * 58)

    # 펌웨어 선택 — exe 옆의 firmware.hex 우선, 없으면 내장본.
    external = os.path.join(exe_dir(), 'firmware.hex')
    if os.path.isfile(external):
        hex_path = external
        print(f'펌웨어: 외부 firmware.hex 사용 ({external})')
    else:
        hex_path = os.path.join(resource_dir(), 'firmware.hex')
        print(f'펌웨어: 내장본 v{FW_VERSION} 사용')
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

    print()
    print(f'{port} 에 업로드를 시작합니다... (보드를 뽑지 마십시오)')
    print('-' * 58)
    cmd = [avrdude, '-C', conf, '-p', 'atmega2560', '-c', 'wiring',
           '-P', port, '-b', '115200', '-D',
           '-U', f'flash:w:{hex_path}:i']
    ret = subprocess.call(cmd)
    print('-' * 58)
    if ret != 0:
        print('[실패] 업로드에 실패했습니다.')
        print('  - 다른 프로그램(세척관리/SeePro)이 포트를 잡고 있으면 종료 후 재시도')
        print('  - USB 케이블/포트를 바꿔 재시도')
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
