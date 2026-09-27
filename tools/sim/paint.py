# -*- coding: utf-8 -*-
# make: 4바이트 무늬 파일 생성 / scan: 덤프에서 무늬가 깨진 **가장 낮은** 주소 찾기
import sys
PAT = bytes((0xA5, 0x5A, 0xC3, 0x3C))

if sys.argv[1] == 'make':
    n = int(sys.argv[3])
    open(sys.argv[2], 'wb').write((PAT * (n // 4 + 1))[:n])
    sys.exit(0)

buf = open(sys.argv[2], 'rb').read()
base = int(sys.argv[3])
first = None          # 무늬가 깨진 최저 주소
run8 = None           # 8바이트 연속으로 깨진 최저 주소(오탐 배제)
bad = 0
for i, b in enumerate(buf):
    if b != PAT[i & 3]:
        if first is None: first = i
        bad += 1
        if bad >= 8 and run8 is None: run8 = i - 7
    else:
        bad = 0
if first is None:
    print('SCAN: 무늬가 하나도 깨지지 않았다 — 칠한 구간에 스택이 닿지 않음 (paint 하한 %#x)' % base)
else:
    print('SCAN low_broken=%#x (=%u)  low_run8=%s  paint=[%#x,%#x)' %
          (base + first, base + first,
           ('%#x' % (base + run8)) if run8 is not None else 'none',
           base, base + len(buf)))
