#!/bin/bash
# 사용: runall.sh <펌웨어 루트> <출력 폴더>  — 시험 6종 빌드·실행, 요약 한 줄씩.
ROOT="$1"; OUTD="$2"
HERE="$(cd "$(dirname "$0")" && pwd)"
T=(t_smoke t_disinfect t_rfid t_server t_ui t_issue t_clear t_firstboot t_gateway)
SRC=()
for t in "${T[@]}"; do SRC+=("$HERE/tests/$t.cpp"); done
rm -rf "$OUTD"
bash "$HERE/build.sh" "$ROOT" "$OUTD" "${SRC[@]}" 2>&1 | grep -v "^warning\|lto-wrapper" | tail -5
for t in "${T[@]}"; do
    [ -f "$OUTD/$t.elf" ] || { echo "$t: 빌드 실패"; continue; }
    res=$(bash "$HERE/run.sh" "$OUTD/$t.elf" 180)
    echo "$t: $(echo "$res" | grep '==>' ) $(echo "$res" | grep -c '^FAIL') FAIL"
    echo "$res" | grep '^FAIL' | sed 's/^/    /'
done
