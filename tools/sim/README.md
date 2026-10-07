# 펌웨어 시뮬레이터 행위 시험 (2026-09-22 신설)

제품 코드(`src/main.cpp` + `lib/TraceQ_Arduino/src` 전부)를 **실물 헤더·avr-g++ LTO** 로 그대로 빌드해
avr-gdb 내장 AVR 시뮬레이터에서 `setup()`/`loop()`/`serialEvent()` 를 돌린다. 하드웨어에 닿는 함수만 가짜다.

- `fakes/fake_mfrc522.cpp` — MFRC522 멤버 함수를 카드 모델로 정의(ISO14443-3 상태: IDLE/READY/ACTIVE/AUTH/HALT,
  nested 인증 규칙, 인증·쓰기 실패·필드 이탈·리더 쪽 읽기 오류·HaltA 유실 주입).
- `fakes/fake_hw.cpp` — DS3231(시계 조작), LCD(출력 기록), EEPROM(RAM), Serial(입출력 기록), 부저(펄스 길이), 버튼 스크립트,
  `util_soft_reset` 가로채기(시험으로 복귀).
- 사용 (Git Bash): `bash tools/sim/runall.sh "D:/traceq_arduino 2.0" <출력 폴더>`
  — 등록 시험(`runall.sh` 의 `T`·`T2` — 09-28 기준 74종) 요약 · 끝에 `tests/t_*.cpp` 중 미등록을 `!! 미등록:` 으로 찍는다.
  한 개만: `build.sh <루트> <출력> tests/t_x.cpp` 후 `run.sh <출력>/t_x.elf`.
  `build.sh` 는 요청된 시험의 옛 elf 를 먼저 지운다(빌드 실패면 elf 없음) · `run.sh` 종료코드 0 은 done() 도달·요약 있음·pass>0·fail=0 일 때뿐.
- 필요: PlatformIO 로 한 번 빌드해 둔 `.pio\libdeps\megaatmega2560`(ArduinoJson·RTClib·BusIO·LCD 헤더).

주의 (겪은 것)
- gdb 시뮬레이터는 `.data` 초기값을 RAM 에 넣지 못한다 → `run.sh` 가 `main` 진입 때 ELF 의 .data 를 복원한다(그 전엔 전역이 0).
- LTO 를 끄면 메뉴 재귀가 꼬리호출로 바뀌어 재현되지 않는다 → 제품과 같이 LTO(`LTO=0` 으로 끌 수 있음).
- gdb 명령 안의 경로는 `cygpath -m` 으로 넘긴다(`/c/...` 면 파일을 못 열고 멈춘다).
- 음성대조는 결과 줄(`==>`)이 있는지부터 본다 — 없으면 "잡힘/놓침"이 아니라 실행 실패다. TIMEOUT 이면 `g_log` 도 없다.
- **재부팅은 `hard_reset()`**(`common.h`) — `setup()` 만 다시 부르면 RAM 전역이 살아남아 "재시작 뒤" 결함을 못 본다(3차 감사에서 실제로 놓침).
- 시뮬레이터 RAM 은 0x200~0x7FFF(약 31.5KB) — `SimCard`(≈1.2KB)를 많이 정적으로 두면 **스택과 겹쳐 시험이 조용히 틀린 결과**를 낸다
  (09-23: 카드 16장 → `.bss` 34KB → 뒤쪽 시험 4개가 거짓 빨강, 멈추지도 않았다). `build.sh` 가 `.data+.bss > 28000B`(`RAM_LIMIT`) 면 빌드를 실패시킨다.
  **카드는 2~4장만 두고 돌려쓸 것** — `make_tag` 이 카드를 완전히 초기화하므로 재사용이 안전하다(동시에 살아 있어야 하는 쌍만 따로 둔다).
- 카드를 뗀 뒤 1루프만 돌리면 디바운스(3회 무응답) 때문에 같은 태그가 다시 처리되지 않는다 — `touch()` 의 기본 4루프를 지킬 것.
- 음성대조 변이는 HEAD 가 아니라 **저장소 사본**(`git archive` 또는 복사)에 적용한다 — 작업 트리를 건드리면 회귀와 섞인다.

이 장치가 증명하지 못하는 가정 (실기로만 닫힌다 — 3차 G 갈래)
1. ✅**실기 확인(09-24)** "정지시키지 못한 카드는 REQA 에 한 번 걸러 응답한다" — 스코프 태그를 30초 올려 둬도 1회만 처리됐다(폴링 ~1000회).
2. ✅**실기 확인(09-24)** "인증 실패 → 카드 IDLE" — 타사 카드를 30초 올려 둬도 경고 1회였다.
3. "쓰기 NACK → 카드 IDLE" — **도구 안에서 자기모순(15회차 III-J H1) · 실기 미확인 · [10-05 사장님 판정 · 재론 금지] 이대로 둔다**
   (실사용은 실기 확인: 실패음 뒤 다시 대면 정상 · 실물로 닫을 진단은 없다 — `integrity` env 는 SPI 오류 건수만 잰다). `failWriteAt` 은 NACK 뒤 카드 IDLE(이 가정),
   `nackBlock`·KEY_A 트레일러 쓰기 NACK 은 카드가 AUTH 로 산다(반대) — 실물은 한 가지다(데이터시트 EV1 Rev 3.2 에 NAK 뒤 상태 서술 없음).
   IDLE 에 기대는 시험: `failWriteAt` 을 쓰는 t_disinfect·t_rfid·t_notify·t_hh2w·t_hh2g·t_hh2s · 산다에 기대는 시험: t_hh1a 2b·3a · t_hh1b 5a·5a2 ·
   t_ii2 ②c · t_ii18 A9 둘 (17차 V-J k1 실측 · t_gg2m ⑫g 는 `nackBlock` 이지만 두 모델 모두 같은 결과).
4. 게이트웨이 조각 시험은 `serial_queue` 의 도착 시각 하나에 기댄다(실물 USB-CDC 간격은 실측 필요).

가짜가 실물과 다르다고 아는 곳
- 제품 정상 경로에선 도달 불가: HALT 카드가 다른 명령을 받으면 IDLE 로 돌림 · 인증 실패 때 리더 Crypto1 을 스스로 끔 ·
  KEY_B 인증 가능 판정을 C2 비트 하나로(010/100/101 접근조건은 틀림) · 데이터 블록 접근조건 무시.
- 제품 경로에서 닿는 것(시험 결과가 여기에 기댈 수 있다):
  - 쓰기 NACK 뒤 카드 상태가 둘로 갈린다 — `nackBlock`·KEY_A 트레일러 NACK 은 카드가 산다(AUTH), `failWriteAt` 은 IDLE(가정 3).
  - 쓰기 찢김·데이터 ACK 유실(써졌는데 실패 보고) 주입이 없다 — 쓰기는 전부 반영 아니면 전혀 안 씀.
  - 카드 동작 시간 0(인증·읽기·쓰기가 시간을 안 쓴다) — 접촉 예산은 `t_ops` 의 동작 수로만 잠긴다.
  - LCD 20칸 넘는 글자는 버린다 — 실물 HD44780 20x4 는 0행→2행, 1행→3행으로 넘어가 잔상이 남는다.
  - 트레일러 읽기가 저장된 키 바이트를 그대로 돌려준다 — 실물은 KEY_A 를 00 으로, 읽기 불가 KEY_B 도 00 으로 준다.
  - DS3231 초 위상 고정 — `rtc_set` 순간이 늘 초의 0ms 라 더블터치 2초 창이 시뮬에선 늘 2초, 실물에선 위상 따라 1~2초.
  - **큰 시각 점프 뒤 첫 접촉 루프에서 알람이 울린다**(2.4초 막힘 · 13종에서 관측) — 실물이면 쉬는 동안 이미 울렸을 알람이다. 그래서
    "종료가 알람을 지운다" 를 잠그려면 **알람 전 시각**에 종료해야 한다(17차: t_disinfect·t_smoke 의 옛 잠금이 늘 참이었다).
  - `run_loops` 는 Arduino 코어의 `serialEventRun`(loop 마다 serialEvent) 을 흉내 내지 않는다 — 코어처럼 바꿔 32종을 돌려도 결과 동일(17차).
  - `hard_reset` 이 못 지우는 것: 제품의 유일한 함수 static `sTailOfTruncated` · `g_ms`(millis 는 계속 간다 — 부팅 직후 조건은 `g_ms` 를 직접 놓는다) ·
    수신 버퍼 `s_in`. 16차 RAM 실패 표지(`mFailScope/mFailMs`)는 지워진다(실측).
  - 로그 버퍼(LCD 1536 · 시리얼 2048 · 부저 64)가 차면 `!! … log full` 을 한 번 찍는다 — 그 뒤의 "없음" 판정은 못 믿는다(17차 · 문구는
    PROGMEM — 하네스 문자열은 .data 라 t_a4 의 RAM 관문 3B 여유를 먹는다).
