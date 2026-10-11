# -*- coding: utf-8 -*-
"""version.hpp 에서 TRACEQ_VERSION_STRING 을 추출해 fw_version.txt 생성.

build_uploader.bat 이 호출 - 업로더 표기 버전을 펌웨어와 자동 동기화해
'재빌드 때 상수 갱신을 잊는' 사고를 원천 차단한다.
같은 폴더의 firmware.hex(배치가 방금 복사한 것) 안에 그 버전 문자열이 들어 있는지도 본다(18차) -
펌웨어를 다시 빌드하지 않고 배치만 돌리면 옛 hex 가 새 버전 이름의 exe 에 담겼다. 없으면 빌드를 멈춘다.
"""
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).parent


def hex_image(path) -> bytes:
    """Intel HEX -> 주소 순 바이트(빈 곳은 0xFF)."""
    img, base = {}, 0
    for line in open(path, encoding='ascii'):
        line = line.strip()
        if not line.startswith(':'):
            continue
        b = bytes.fromhex(line[1:])
        n, addr, typ, data = b[0], (b[1] << 8) | b[2], b[3], b[4:4 + b[0]]
        if typ == 0:
            for i, v in enumerate(data):
                img[base + addr + i] = v
        elif typ == 4:
            base = ((data[0] << 8) | data[1]) << 16
        elif typ == 2:
            base = ((data[0] << 8) | data[1]) << 4
    if not img:
        return b''
    lo, hi = min(img), max(img)
    return bytes(img.get(a, 0xFF) for a in range(lo, hi + 1))


def main(hpp=None, hexfile=None, out=None) -> int:
    hpp = pathlib.Path(hpp) if hpp else HERE.parent / 'lib' / 'TraceQ_Arduino' / 'src' / 'TraceQ_Arduino' / 'version.hpp'
    hexfile = pathlib.Path(hexfile) if hexfile else HERE / 'firmware.hex'
    out = pathlib.Path(out) if out else HERE / 'fw_version.txt'
    m = re.search(r'TRACEQ_VERSION_STRING\s+"([^"]+)"', hpp.read_text(encoding='utf-8'))
    if not m:
        print('[FAIL] version.hpp 에서 TRACEQ_VERSION_STRING 을 찾지 못했습니다')
        return 1
    ver = m.group(1)
    if not hexfile.is_file():
        print('[FAIL] firmware.hex 가 없습니다 - 펌웨어를 먼저 빌드하십시오(py -m platformio run)')
        return 1
    if ver.encode('ascii') not in hex_image(hexfile):
        print('[FAIL] firmware.hex 안에 버전 문자열 %s 이 없습니다 - 옛 빌드입니다. 펌웨어를 다시 빌드하십시오(py -m platformio run)' % ver)
        return 1
    out.write_text(ver, encoding='ascii')
    print('fw_version:', ver, '(firmware.hex 와 일치)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
