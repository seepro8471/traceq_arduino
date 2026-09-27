#!/bin/bash
# runallsp.sh <루트> <출력폴더> [추가시험.cpp ...]  — 19종(+추가)을 스택 계측으로 돌린다.
ROOT="$1"; OUTD="$2"; shift 2
HERE="$(cd "$(dirname "$0")" && pwd)"
T=(t_smoke t_disinfect t_rfid t_server t_ui t_issue t_clear t_firstboot t_gateway t_notify t_a4 t_lcd t_menu t_a5 t_a6 t_a7 t_a8 t_a9 t_a10)
SRC=()
for t in "${T[@]}"; do SRC+=("$HERE/tests/$t.cpp"); done
for x in "$@"; do SRC+=("$x"); T+=("$(basename "$x" .cpp)"); done
rm -rf "$OUTD"
bout=$(bash "$HERE/build.sh" "$ROOT" "$OUTD" "${SRC[@]}" 2>&1); brc=$?
echo "$bout" | grep -v "^warning\|lto-wrapper" | grep -E "★|error:|RAM" | head -8
[ $brc -ne 0 ] && echo "!! build rc=$brc"
for t in "${T[@]}"; do
    [ -f "$OUTD/$t.elf" ] || { echo "$t: BUILD FAIL"; continue; }
    res=$(bash "$HERE/runsp.sh" "$OUTD/$t.elf" 400)
    echo "$t | $(echo "$res" | tr '\n' ' ')"
done
