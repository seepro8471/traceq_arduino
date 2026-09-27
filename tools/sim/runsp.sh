#!/bin/bash
# runsp.sh <elf> [제한초]
# run.sh 와 같이 돌리되 (1) main 진입 때 미사용 RAM 을 4바이트 무늬로 칠하고
# (2) done 에서 남은 무늬의 최저점(=스택 최대 침투)과 g_sp* 계측값을 출력한다.
ELF="$1"; LIMIT="${2:-600}"
TC="/c/Users/alu5/.platformio/packages/toolchain-atmelavr/bin"
HERE="$(cd "$(dirname "$0")" && pwd)"
VMA=$("$TC/avr-objdump" -h "$ELF" | awk '$2==".data"{print $4}')
VMA=$(printf '0x%x' $((16#$VMA)))
"$TC/avr-objcopy" -O binary -j .data "$ELF" "$ELF.data.bin"
END=$("$TC/avr-nm" "$ELF" | awk '$3=="_end"{print $1}')
PSTART=$((16#$END))
PEND=$((0x807ff0))
PSIZE=$((PEND-PSTART))
PAINT="$ELF.paint.bin"
DUMP="$ELF.ram.bin"
py "$HERE/paint.py" make "$PAINT" "$PSIZE"
EW=$(cygpath -m "$ELF")
PW=$(cygpath -m "$PAINT")
DW=$(cygpath -m "$DUMP")
out=$(timeout "$LIMIT" "$TC/avr-gdb.exe" -batch -ex "file $EW" -ex "target sim" -ex "load" \
      -ex "break main" -ex "run" -ex "restore $EW.data.bin binary $VMA" \
      -ex "restore $PW binary $(printf '0x%x' $PSTART)" \
      -ex 'printf "SPMAIN=%u\n", $sp' \
      -ex "delete" -ex "break done" -ex "continue" \
      -ex "dump binary memory $DW $(printf '0x%x' $PSTART) $(printf '0x%x' $PEND)" \
      -ex 'printf "SPTOP=%u SPMINALL=%u SPDEPTH=%u MINSP=%u\n", g_spTopMax, g_spMinAll, g_spDepthMax, g_minSP' \
      -ex 'printf "==> pass=%d fail=%d\n", g_pass, g_fail' 2>&1)
rc=$?
[ $rc -eq 124 ] && echo "!! TIMEOUT ${LIMIT}s"
echo "$out" | grep -E "SPMAIN=|SPTOP=|==> pass="
py "$HERE/paint.py" scan "$DUMP" "$PSTART"
