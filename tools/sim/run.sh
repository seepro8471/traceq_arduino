#!/bin/bash
# 사용: run.sh <elf> [제한초]  — gdb 내장 AVR 시뮬레이터로 돌려 g_log 와 합계를 출력.
# 종료코드 0 은 "done() 에 닿았고 요약이 있고 pass>0 · fail=0 · FAIL 줄 없음" 일 때뿐(TIMEOUT·요약 없음·CHECK 0개는 1).
# gdb 시뮬레이터는 .data 초기값을 RAM 에 넣지 못한다 → main 진입 때 ELF 의 .data 를 직접 복원.
ELF="$1"; LIMIT="${2:-600}"
[ -f "$ELF" ] || { echo "!! elf 없음: $ELF"; exit 1; }
# 경로에 공백이 있으면 gdb 명령 문자열(`file …`)이 갈라져 빈 프로그램이 멈춘다 — 전량이 TIMEOUT 으로 보였다(17차 · 출력 폴더는 공백 없는 경로로).
case "$ELF" in *" "*) echo "!! elf 경로에 공백 — gdb 가 못 연다(출력 폴더를 공백 없는 경로로): $ELF"; exit 1 ;; esac
TC="/c/Users/alu5/.platformio/packages/toolchain-atmelavr/bin"
VMA=$("$TC/avr-objdump" -h "$ELF" | awk '$2==".data"{print $4}')
VMA=$(printf '0x%x' $((16#$VMA)))
"$TC/avr-objcopy" -O binary -j .data "$ELF" "$ELF.data.bin"
# gdb 명령 문자열 안의 경로는 Git Bash 가 바꿔 주지 않는다 — /c/... 면 파일을 못 열고 빈 프로그램이 멈춘다.
EW=$(cygpath -m "$ELF")
out=$(timeout "$LIMIT" "$TC/avr-gdb.exe" -batch -ex "file $EW" -ex "target sim" -ex "load" \
      -ex "break main" -ex "run" -ex "restore $EW.data.bin binary $VMA" -ex "delete" \
      -ex "break done" -ex "continue" \
      -ex 'printf "%s", g_log' \
      -ex 'printf "\n--- 실패 목록(g_log 가 잘려도 남는다) ---\n%s", g_failLog' \
      -ex 'printf "==> pass=%d fail=%d\n", g_pass, g_fail' 2>&1)
rc=$?
[ $rc -eq 124 ] && echo "!! TIMEOUT ${LIMIT}s"
echo "$out" | sed -n '/^Breakpoint 2, done/,$p' | tail -n +3
# 요약은 done() 에 닿은 뒤의 출력에서만 읽는다(runall 이 보는 것과 같게) · g_failLog 가 차면 `==>` 가 줄 머리에 안 온다.
[ $rc -eq 124 ] && exit 1
sum=$(echo "$out" | sed -n '/^Breakpoint 2, done/,$p' | grep -o '==> pass=[0-9]* fail=[0-9]*' | tail -n 1)
[ -z "$sum" ] && exit 1
np=${sum#*pass=}; np=${np%% *}; nf=${sum##*fail=}
[ "$nf" -eq 0 ] && [ "$np" -gt 0 ] && ! echo "$out" | grep -q "^FAIL"
