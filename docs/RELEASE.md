# 출하 절차 (펌웨어) — 빠뜨린 것을 절차로 막는다

2026-09-27 12회차에 **`probe`·`integrity` env 빌드를 여러 회차 동안 빠뜨린 것**을 발견해 만들었다.
"기억해서 하기" 로는 매번 다른 것이 빠진다(같은 세션에서 자리표 `{WEND}` 출하·종 수 오기·수치 오기가 있었다).

## 순서 (이 순서를 바꾸지 말 것)

1. **회귀 전량** — `bash tools/sim/runall.sh "D:/traceq_arduino 2.0" <출력폴더>`
   - 판정은 `==> pass=N fail=M` **만**. `pass=0 fail=0`·TIMEOUT 은 **초록이 아니다**.
   - 합계를 **손으로 더하지 말고** 출력에서 계산한다(이 프로젝트에서 손계산이 네 번 틀렸다):
     `awk 'match($0,/pass=[0-9]+/){p+=substr($0,RSTART+5,RLENGTH-5)} match($0,/fail=[0-9]+/){f+=substr($0,RSTART+5,RLENGTH-5)} /==>/{n++} END{printf "종=%d pass=%d fail=%d\n",n,p,f}' <로그>`
2. **음성대조(변이)** — 이번 회차에 고친 자리를 **하나씩 되돌려** 빨강이 되는지. 무해 변이 하나는 초록이어야 한다(과적합 점검).
   - **판정을 시험에 박았으면 "고치면 나빠지는 쪽" 도 되돌려 본다**(11차 P2-2: 상태를 직접 만드는 시험은 그 상태를 **만드는 순서**를 못 잠근다).
3. **실빌드 세 env** — `py -m platformio run` (제품) · `py -m platformio run -e probe -e integrity` (진단 도구)
   - 제품 env 는 `[static-ram] .data+.bss ≤ 5000B` 관문을 통과해야 한다(`tools/check_static_ram.py`).
   - `probe`·`integrity` 는 `examples/` 를 빌드하지만 `lib/` 를 공유한다 — 깨지면 현장 진단을 못 한다.
4. **기록** — `version.hpp` 항목 + `docs/AUDIT_*.md`.
   - 자리표(`{...}`)가 남았는지 **grep 으로** 확인한다(9차에 `{WEND}` 가 출하됐다).
   - 종 수·pass 수는 1번의 계산 결과를 **그대로** 옮긴다.
5. **커밋 · push** — `git push origin main`. 커밋 메시지에 P1/P2/P3 와 **되돌림 변이 결과**를 적는다.
6. **업로더 exe** — `uploader/build_uploader.bat`
7. **출하물 대조(필수)** — exe 안의 hex 가 **이번 빌드의 hex** 인지 sha 로 확인한다. 파일명만 보면 안 된다
   (배치가 hex 복사에 실패해도 이름은 새 버전이 된다):
   - `.pio/build/megaatmega2560/firmware.hex` · `uploader/firmware.hex` · **exe 내장 PYZ/CArchive 의 firmware.hex** 세 sha 가 같아야 한다.
   - exe 내장 `fw_version.txt` 도 새 버전인지 본다.
8. **업로더 버전 표기 커밋** — `uploader/fw_version.txt` (배치가 자동 갱신한다).

## 짝 출하 (양쪽을 같이 내보내야 하는 변경)

와이어로 나가는 **문자열·줄 집합·종결자**를 바꾸면 세척관리(`D:\TraceQ_Python`)도 같이 나가야 한다.
- `RejectDebug`·`Serial.println` 은 **PC 로 줄이 나간다** → 짝 출하.
- `Reject`·`CustomWarning` 은 **LCD·부저만** → 펌웨어 단독 출하 가능.
- 예: v2.2.24 의 "읽기 실패면 `Ok!` 보류" 는 세척관리 v2.0.39(PSOk 마다 누적 비우기)와 **짝**이다.

## 하지 말 것

- 회귀가 **도는 중에** 제품 파일을 고치지 말 것(그 판의 결과가 무엇의 결과인지 알 수 없게 된다).
- 음성대조와 회귀를 **동시에** 돌리지 말 것(CPU 경합으로 헛 TIMEOUT).
- 손으로 센 숫자를 기록에 적지 말 것.
- "빨강이 났다" 를 잠금의 증거로 쓰기 전에 **그 빨강이 CHECK 실패인지 크래시인지** 가릴 것(10차에 사본이 문법조차 깨진 채 빨강이었다).

## 시험을 쓸 때 (12차까지 내 판정식 오류 여섯 번 — 전부 이 목록에 들어간다)

- **블록을 읽기 전에 그 블록의 레이아웃을 확인한다.** 같은 태그 안에 셋이 섞여 있다:
  - **레코드**(`DisinfectionRecord`/`WashingRecord`) = `int16 기기번호` + `LocalDateTime` → `get_ldt()` · `dev_of()`
  - **DETAIL**(`DisinfectionDetail`) = `LocalDateTime` + `int16 그룹번호` → `lcd_of()` · `group_of()`
  - **Process** 9바이트 → `get_process()`
  레코드를 DETAIL 로 읽으면 `26:00` 같은 값이 나온다(12차에 실제로 그랬다).
- **시작/종료를 종료 블록으로 가르지 못한다** — 소독·세척 시작은 **자동 종료를 미리 채운다**. 시작 시각의 초로 가른다.
- **시각 표본은 `rel_date()` 로 유도한다**(출시일보다 앞서면 `IsUnsynced()` 가 켜져 갈래가 통째로 달라진다).
- **소리를 근거로 쓴 판정은 소리를 센다**(`buzz_count`). 화면 글자는 `Reject` 가 1.44초 뒤 지운다 — 문자열만 보면
  `Reject`→`Notify`(성공음) 변이가 통과한다(12차 GG1 P2-1).
- **상태를 직접 만드는 시험은 "그 상태를 만드는 순서" 를 못 잠근다** — 판정에 "고치면 나빠진다" 가 있으면 그 변형을
  실제로 만들어 빨강을 봐야 한다(11차 P2-2).
- **`grep -c` 는 부분 문자열에 속는다**(`_ROOT` 가 `_PRODROOT` 를 센다) — 단어 경계로.
