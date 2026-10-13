#!/bin/bash
# runallsp.sh <루트> <출력폴더> [추가시험.cpp ...]  — 19종(+추가)을 스택 계측으로 돌린다.
ROOT="$1"; OUTD="$2"
# runall.sh 와 같은 가드(19차): 빈 인자 · 공백 · 표식(.simout) 없는 비어 있지 않은 폴더는 지우지 않는다
[ -n "$ROOT" ] && [ -n "$OUTD" ] || { echo "사용: runallsp.sh <펌웨어 루트> <출력 폴더(공백 없는 경로)> [추가시험.cpp ...]"; exit 2; }
case "$OUTD" in *" "*) echo "!! 출력 폴더에 공백 — gdb 가 elf 를 못 연다: $OUTD"; exit 2 ;; esac
if [ -e "$OUTD" ] && [ ! -f "$OUTD/.simout" ] && [ -n "$(ls -A "$OUTD" 2>/dev/null)" ]; then
    echo "!! 출력 폴더가 비어 있지 않고 표식(.simout)이 없다 — 지우지 않는다(새 폴더 이름을 줄 것): $OUTD"; exit 2
fi
shift 2
HERE="$(cd "$(dirname "$0")" && pwd)"
T=(t_smoke t_disinfect t_rfid t_server t_ui t_issue t_clear t_firstboot t_gateway t_notify t_a4 t_lcd t_menu t_a5 t_a6 t_a7 t_a8 t_a9 t_a10)
SRC=()
for t in "${T[@]}"; do SRC+=("$HERE/tests/$t.cpp"); done
for x in "$@"; do SRC+=("$x"); T+=("$(basename "$x" .cpp)"); done
rm -rf "$OUTD"; mkdir -p "$OUTD" && : > "$OUTD/.simout"
bout=$(bash "$HERE/build.sh" "$ROOT" "$OUTD" "${SRC[@]}" 2>&1); brc=$?
echo "$bout" | grep -v "^warning\|lto-wrapper" | grep -E "★|error:|RAM" | head -8
[ $brc -ne 0 ] && echo "!! build rc=$brc"
for t in "${T[@]}"; do
    [ -f "$OUTD/$t.elf" ] || { echo "$t: BUILD FAIL"; continue; }
    res=$(bash "$HERE/runsp.sh" "$OUTD/$t.elf" 400)
    echo "$t | $(echo "$res" | tr '\n' ' ')"
done
