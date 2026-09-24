#!/bin/bash
# 사용: run.sh <elf> [제한초]  — gdb 내장 AVR 시뮬레이터로 돌려 g_log 와 합계를 출력. 실패 있으면 종료코드 1.
# gdb 시뮬레이터는 .data 초기값을 RAM 에 넣지 못한다 → main 진입 때 ELF 의 .data 를 직접 복원.
ELF="$1"; LIMIT="${2:-600}"
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
echo "$out" | grep -q "==> pass=[0-9]* fail=0" && ! echo "$out" | grep -q "^FAIL"
