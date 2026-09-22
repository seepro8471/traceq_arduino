# 펌웨어 시뮬레이터 행위 시험 (2026-09-22 신설)

제품 코드(`src/main.cpp` + `lib/TraceQ_Arduino/src` 전부)를 **실물 헤더·avr-g++ LTO** 로 그대로 빌드해
avr-gdb 내장 AVR 시뮬레이터에서 `setup()`/`loop()`/`serialEvent()` 를 돌린다. 하드웨어에 닿는 함수만 가짜다.

- `fakes/fake_mfrc522.cpp` — MFRC522 멤버 함수를 카드 모델로 정의(ISO14443-3 상태: IDLE/READY/ACTIVE/AUTH/HALT,
  nested 인증 규칙, 인증·쓰기 실패·필드 이탈·리더 쪽 읽기 오류·HaltA 유실 주입).
- `fakes/fake_hw.cpp` — DS3231(시계 조작), LCD(출력 기록), EEPROM(RAM), Serial(입출력 기록), 부저(펄스 길이), 버튼 스크립트,
  `util_soft_reset` 가로채기(시험으로 복귀).
- 사용 (Git Bash): `bash tools/sim/runall.sh "D:/traceq_arduino 2.0" <출력 폴더>`
  — 시험 6종 요약. 한 개만: `build.sh <루트> <출력> tests/t_x.cpp` 후 `run.sh <출력>/t_x.elf`.
- 필요: PlatformIO 로 한 번 빌드해 둔 `.pio\libdeps\megaatmega2560`(ArduinoJson·RTClib·BusIO·LCD 헤더).

주의 (겪은 것)
- gdb 시뮬레이터는 `.data` 초기값을 RAM 에 넣지 못한다 → `run.sh` 가 `main` 진입 때 ELF 의 .data 를 복원한다.
- LTO 를 끄면 메뉴 재귀가 꼬리호출로 바뀌어 재현되지 않는다 → 제품과 같이 LTO(`LTO=0` 으로 끌 수 있음).
- gdb 명령 안의 경로는 `cygpath -m` 으로 넘긴다(`/c/...` 면 파일을 못 열고 멈춘다).
- 음성대조는 결과 줄(`==>`)이 있는지부터 본다 — 없으면 "잡힘/놓침"이 아니라 실행 실패다.
