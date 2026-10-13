#!/bin/bash
# 사용: run.sh <elf> [제한초]  — gdb 내장 AVR 시뮬레이터로 돌려 g_log 와 합계를 출력.
# 종료코드 0 은 "done() 에 닿았고 요약이 있고 pass>0 · fail=0 · FAIL 줄 없음" 일 때뿐(TIMEOUT·요약 없음·CHECK 0개는 1).
# 요약은 done() 의 끝(sim_done_stop)에서 찍는다 — done() 안의 관문 넷(전원 차단이 켜진 채 끝남 · 포화된 로그에 물음 · 입력 잘림 · loop 끊김)이 반영된 값이다.
# gdb 시뮬레이터는 .data 초기값을 RAM 에 넣지 못한다 → main 진입 때 ELF 의 .data 를 직접 복원.
# main 이 한 번 더 들어오면(리셋 벡터로 튐) 앞 판의 FAIL 이 지워지므로 거기서 끝낸다 — 요약이 없어 빨강이다(19차).
#  중단점은 함수 **주소**(`*main`)에 — `break main` 은 프롤로그 뒤라, 첫 호출이 인라인되고 바로 루프가 오면 루프 머리에 잡혀 헛 재진입이 났다.
ELF="$1"; LIMIT="${2:-600}"
[ -f "$ELF" ] || { echo "!! elf 없음: $ELF"; exit 1; }
# 경로에 공백이 있으면 gdb 명령 문자열(`file …`)이 갈라져 빈 프로그램이 멈춘다 — 전량이 TIMEOUT 으로 보였다(17차 · 출력 폴더는 공백 없는 경로로).
case "$ELF" in *" "*) echo "!! elf 경로에 공백 — gdb 가 못 연다(출력 폴더를 공백 없는 경로로): $ELF"; exit 1 ;; esac
TC="/c/Users/alu5/.platformio/packages/toolchain-atmelavr/bin"
VMA=$("$TC/avr-objdump" -h "$ELF" | awk '$2==".data"{print $4}')
VMA=$(printf '0x%x' $((16#$VMA)))
"$TC/avr-objcopy" -O binary -j .data "$ELF" "$ELF.data.bin"
# gdb 명령 안의 경로는 Git Bash 가 바꿔 주지 않는다 — /c/... 면 파일을 못 열고 빈 프로그램이 멈춘다.
EW=$(cygpath -m "$ELF")
# 명령은 스크립트 파일로 준다(18차): 시험이 g_log 를 다 채우면 log_flush 에서 멈춰 지금까지를 찍고 계속 간다 —
# 종전엔 4096B 를 넘는 시험의 뒤쪽 실측 줄이 안 보였다. 표식 줄(@@SIMLOG@@) 뒤가 시험의 출력이다.
GS="$ELF.gdb"
cat > "$GS" <<EOF
file $EW
target sim
load
break *main
run
restore $EW.data.bin binary $VMA
delete
break *main
commands
silent
printf "\n!! main re-entered\n"
quit
end
break log_flush
commands
silent
printf "%s", g_log
continue
end
break sim_done_stop
commands
silent
printf "%s", g_log
printf "\n--- FAIL list (kept even when the log is long) ---\n%s", g_failLog
if g_satLog > 0
printf "!! tlog line cut x%d (line longer than the log room)\n", g_satLog
end
printf "==> pass=%d fail=%d\n", g_pass, g_fail
end
echo @@SIMLOG@@\n
continue
EOF
out=$(timeout "$LIMIT" "$TC/avr-gdb.exe" -batch -x "$(cygpath -m "$GS")" 2>&1)
rc=$?
[ $rc -eq 124 ] && echo "!! TIMEOUT ${LIMIT}s"
body=$(echo "$out" | sed -n '/^@@SIMLOG@@/,$p' | tail -n +2)
echo "$body"
# 요약은 done() 에 닿은 뒤의 출력에서만 읽는다(runall 이 보는 것과 같게) · g_failLog 가 차면 `==>` 가 줄 머리에 안 온다.
[ $rc -eq 124 ] && exit 1
sum=$(echo "$body" | grep -o '==> pass=[0-9]* fail=[0-9]*' | tail -n 1)
[ -z "$sum" ] && exit 1
np=${sum#*pass=}; np=${np%% *}; nf=${sum##*fail=}
[ "$nf" -eq 0 ] && [ "$np" -gt 0 ] && ! echo "$body" | grep -q "^FAIL"
