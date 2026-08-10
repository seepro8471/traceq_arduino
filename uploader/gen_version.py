# -*- coding: utf-8 -*-
"""version.hpp 에서 TRACEQ_VERSION_STRING 을 추출해 fw_version.txt 생성.

build_uploader.bat 이 호출 — 업로더 표기 버전을 펌웨어와 자동 동기화해
'재빌드 때 상수 갱신을 잊는' 사고를 원천 차단한다.
"""
import pathlib
import re

hpp = pathlib.Path(__file__).parent.parent / 'lib' / 'TraceQ_Arduino' / 'src' / 'TraceQ_Arduino' / 'version.hpp'
text = hpp.read_text(encoding='utf-8')
m = re.search(r'TRACEQ_VERSION_STRING\s+"([^"]+)"', text)
if not m:
    raise SystemExit('version.hpp 에서 TRACEQ_VERSION_STRING 을 찾지 못했습니다')
out = pathlib.Path(__file__).parent / 'fw_version.txt'
out.write_text(m.group(1), encoding='ascii')
print('fw_version:', m.group(1))
