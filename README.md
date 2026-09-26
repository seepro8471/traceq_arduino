# TraceQ Arduino 2.2

`traceq_arduino 1` (v1.4.1)을 분석해 **RFID 안전성/쓰기 속도**와 **시리얼 입력 안전성**을
재설계한 2.0을 거쳐, **1.4.1의 전 기능·전 프로토콜을 보존하는 완성판**(2.1)으로 마무리하고,
**서버 Latest 갈래를 삭제**(2.2)한 버전. 핵심 로직은 1.4.1과 1:1 호환이며,
태그/EEPROM 바이너리 레이아웃도 동일하다.

## 2.2.0 — 서버 Latest 갈래 삭제 (사용자 확정)

1.4.1의 서버(S) 모드는 Version(Latest/Old) 설정으로 두 갈래였으나, Latest 갈래는
데이터 전송 없이 태그만 초기화하는 미완의 경로였고 현장 등록·완료 흐름은 전부
Old(레거시 블록 덤프)였다. 2.2.0 은 Latest 를 통째로 삭제했다:

- 서버 업로드 = **레거시 블록 덤프 단일 경로** (PSOk/Z 핸드셰이크 → 섹터 덤프 → `Ok!`)
- 부팅 PSOk = 서버 타입이면 **항상** 송신 (구분 조건 삭제)
- LCD 의 Version(Latest/Old) 메뉴 삭제, LCD 상태 표시는 `connected` 단일 표기
- EEPROM 주소 0(LatestCompat 플래그)은 미사용 예약 — 기존 기기 호환을 위해 재사용 금지
- JSON 설정 명령(cfg_*)은 Latest 와 무관한 기능이므로 그대로 유지

## 2.1에서 완성된 것 (2.0의 미완 항목)

| 항목 | 2.0 상태 | 2.1 |
|------|---------|-----|
| 시리얼 수신 | STX..ETX 프레임만 수신(`ReadFrame`) — 현장 PC(SeePro/TraceQ_Python/구 TraceQ Desktop)는 `Z`/`G1..G5`/C·M·S를 **raw로** 보내므로 전부 폐기됨 | 1.0과 동일한 raw 수신(`readBytes`) 복원. 511바이트 상한으로 NUL 종료 보장(1.0의 512B 만재 OOB 수정). JSON은 버퍼 안의 `{..}`만 추출하므로 STX 유무와 무관하게 함께 동작 |
| legacy 서버 프로토콜 | `LegacySerialEvent`가 발명 프로토콜("TraceQ"→"AuthOk") stub | 1.0 `SerialProcessor_legacy.cpp` **전체 이식**: 'Z' 인증, C/M/S 태그 발급, PSOk/Z 핸드셰이크, 섹터 5,6[,7,8],14 + `B; C; S; G; W;` 블록 덤프 + `Ok!` |
| 소독기 RTC 자동 복구 | `get_adjuest_start_time`에서 `rtc.SetDateTime` 소실 + 근거 없는 +3분 가산 | 1.0의 RTC 자동 복구 복원: 세척 시작시각이 미래면 RTC 리셋으로 판단, **"세척 종료시각 + 1분"**으로 RTC 복구 (+1분 = 이동 시간 반영, 2026-08-09 사용자 확정 — 1.0은 종료시각 그대로였음) |
| 태그 발급 키 전환 | type_id 0은 항상 실패(공장 태그를 TraceQ 키로 인증 시도), 2는 반쪽 동작 | `InstallTraceQKeys`/`RestoreFactoryKeys` 신설 — 1.0 `SetAccessMethod`/`InitAccessMethod`와 동일 절차로 공장↔TraceQ 왕복 완성 |
| 원격 타입 변경 재시작 | `if (reset) reset()` — reset이 항상 nullptr라 재시작 안 됨 | `util_soft_reset()`(명시적 `jmp 0`) — 1.0의 nullptr 점프와 동일 동작을 정의된 방법으로 |

## 2.1에서 수정한 1.4.1 승계 버그

- sprintf 스택 오버런 5곳 (메뉴 제목 버퍼: Number/Max Count/Delay/Range/Alarm Time) + 기기정보 버퍼(`%c:%02d`, 3자리 번호)
- **소독기(D)에서 매니저 태그를 `washingProcessor`에 저장하던 인스턴스 불일치** — M-Check=Yes 소독기가 매니저 태그를 대도 시작이 항상 거부되던 버그. `disinfectionProcessor`로 정정
- `Screen`/`Menu`의 `<=` 경계 검사(OOB 쓰기 허용) → `<`
- `ManagerOption` 키 세터(현 `SetData`)에 14바이트 `Tag.ID`를 넘겨 2바이트 OOB read → 16바이트 0패딩 버퍼 경유
- LCD `clear_line`이 19칸만 지워 마지막 열 잔상 → 20칸
- `Write`/`WriteBlocks`가 쓰기 실패를 `VerifyMismatch`로 오보고 → 실패 원인 구분(`WriteFailed`/`VerifyMismatch`)
- GatewayProcess 검사항목 기록 전 섹터15 선-소거 복원 (중간 실패 시 이전 검사항목 잔존 방지 — 1.0과 동일)

## 2.0에서 그대로 유지된 개선

- `RfidController`: 섹터 캐시 인증(같은 섹터 인증 1회), 모든 경로 `StopCrypto1` 보장,
  트레일러/제조사 블록 보호, 쓰기 read-back 검증 + 3회 재시도, millis 폴링 소프트 리셋, 1K/4K 허용
- `SerialProcessor`: JSON `cmd` nullptr 가드(1.0은 crash), `device_type` 부재 가드
- `GatewayProcessor`: 검사항목 1개뿐일 때 subject1이 subject3 블록에 중복 기록되던 1.0 버그 수정 유지
- `AvrString`: dst 크기 명시(`str_substring_safe`), nullptr/길이 검사, 비숫자 거부
- NVM 옵션: 입력 클램프 (음수→0, 타입 화이트리스트). 주소·기본값·크기는 1.0과 동일

## 1.4.1과의 와이어 계약 (변경 금지)

- 115200 8N1, DTR 리셋 후 2초 내 부팅 완료 (PC가 포트를 열 때마다 기기 리부팅 전제)
- PC→기기: raw cp949 통짜 단일 write — `Z`, `G1{...};G2y;m;d;dow;h;n;s;G3key;name;G4s1;s2;s3;G5[...]`
  (G1 직후·G5 직후 내용은 있어도/없어도 동작 — SeePro/TraceQ_Python 두 변형 모두 수용), `C/M/S` 태그 발급,
  STX`{json}`ETX (cfg_new_tag/cfg_get_config/cfg_set_config/cfg_set_date_time)
- 기기→PC: `PSOk`(→'Z' 550ms 대기), 게이트웨이 스캔 `S;` → `0302{32hex};` → `0704{32hex};` → `Sm!`,
  `Not Patient Info`, legacy 서버 덤프(`B; C; S; G; W;` + 37자 블록 라인 + `Ok!`), STX`{json}`ETX
- `PSOk`/`Sm!`/`Ok!`/`Not Patient Info`는 PC가 **부분 문자열**로 판정하므로 다른 출력에 섞으면 안 됨
- MIFARE 블록 맵·구조체 레이아웃·EEPROM 주소 맵·KEY_B(90 25 84 71 84 72)·COMPANY_CODE(4088153715) 불변

## 디렉터리 구조

```
traceq_arduino 2.0/
├── platformio.ini
├── README.md
├── src/main.cpp
├── examples/RfidWriteBenchmark/main.cpp
└── lib/TraceQ_Arduino/src/
    ├── TraceQ_Arduino.hpp               우산 헤더
    └── TraceQ_Arduino/
        ├── version.hpp                  2.1.0
        ├── BaseProcessor.{hpp,cpp}      Processor 부모
        ├── RecordProcessor.{hpp,cpp}    세척/소독 공통
        ├── WashingProcessor.{hpp,cpp}
        ├── DisinfectionProcessor.{hpp,cpp}
        ├── GatewayProcessor.{hpp,cpp}
        ├── SerialProcessor.{hpp,cpp}    JSON + legacy 프로토콜 전체
        ├── SimpleScanner.{hpp,cpp}      READER_MODE 전용
        ├── avr/{AvrString,AvrUtil}.{hpp,cpp}
        ├── data/...                     블록맵·구조체 (1.0 레이아웃 동일)
        ├── data/nvm/                    EEPROM 옵션 5종 (1.0 주소 호환)
        ├── module/rtc/  module/lcd/     1.0 기반 (경계 수정만 반영)
        ├── ui/                          1.0 기반 (버퍼·리셋 수정만 반영)
        └── rfid/RfidController.{hpp,cpp}  2.0 신규 + 키 전환 API
```

## 빌드

```
cd "D:\traceq_arduino 2.0"
pio run
pio run -t upload
```

`MFRC522`는 1.0과 동일하게 [miguelbalboa/rfid 1.2.0](https://github.com/miguelbalboa/rfid/releases/tag/1.2.0)
태그 고정. **주의**: 1.2.0에는 `MFRC522_SPICLOCK` 매크로가 없어(2.0의 8 MHz 플래그는 무효였음)
SPI는 라이브러리 기본 속도로 동작한다. 실속도 상향은 라이브러리 수정 + 실기 검증이 필요한 별도 작업.

## 시리얼

```
baud_rate : 115200
databits  : 8
stopbits  : 1
parity    : None
수신      : raw(프레이밍 없음, 통짜 단일 write 전제) + STX(0x02)..ETX(0x03) JSON 혼용
송신 라인 : CRLF (Serial.println)
```

## 남은 실기 검증 (하드웨어 필요)

1. SeePro 연동 전주기: 검사시작(G1~G5 수신 비프) → 태그 스캔(`S;`→`0302`→`0704`→`Sm!`) → `.UID` 기록
2. TraceQ_Python 연동: `Z` 인증, `T`(무시됨 — 1.4.1과 동일), `S/M` 태그 발급, JSON cfg 왕복
3. 구 TraceQ Desktop(레거시 서버 모드): 스코프 태그 → PSOk/Z → 블록 덤프 → `Ok!` → 태그 초기화
4. cfg_new_tag type_id 0/2: 공장 태그 ↔ TraceQ 태그 왕복 (RfidWriteBenchmark 아님 — 실태그 소모 주의)
5. 소독기 RTC 리셋 시나리오: 세척 태그(미래 시각) 접촉 → RTC 복구 확인
