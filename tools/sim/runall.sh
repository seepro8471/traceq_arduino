#!/bin/bash
# 사용: runall.sh <펌웨어 루트> <출력 폴더>  — 시험 6종 빌드·실행, 요약 한 줄씩.
ROOT="$1"; OUTD="$2"
HERE="$(cd "$(dirname "$0")" && pwd)"
T=(t_smoke t_disinfect t_rfid t_server t_ui t_issue t_clear t_firstboot t_gateway t_notify t_a4 t_lcd t_menu t_a5 t_a6)
SRC=()
for t in "${T[@]}"; do SRC+=("$HERE/tests/$t.cpp"); done
rm -rf "$OUTD"
# build.sh 의 RAM 관문(★줄)·종료코드를 버리지 않는다 — 넘치면 여기서 끝낸다.
bout=$(bash "$HERE/build.sh" "$ROOT" "$OUTD" "${SRC[@]}" 2>&1); brc=$?
echo "$bout" | grep -v "^warning\|lto-wrapper" | grep -E "★|error:|RAM" | head -5
[ $brc -ne 0 ] && { echo "!! build.sh 실패(rc=$brc) — 관문 위반이면 elf 가 지워진다"; }
for t in "${T[@]}"; do
    [ -f "$OUTD/$t.elf" ] || { echo "$t: ==> pass=0 fail=1 (빌드 실패)  1 FAIL"; continue; }
    res=$(bash "$HERE/run.sh" "$OUTD/$t.elf" 180)
    sum=$(echo "$res" | grep '==>')
    nf=$(echo "$res" | grep -c '^FAIL')
    # ★거짓 초록 방지: 멈춤(TIMEOUT)·요약 없음·pass=0 은 전부 빨강으로 센다.
    if echo "$res" | grep -q "!! TIMEOUT" || [ -z "$sum" ]; then
        sum="==> pass=0 fail=1 (멈춤/요약 없음 — 무한 루프 의심)"; nf=$((nf+1))
    elif echo "$sum" | grep -q "pass=0 fail=0"; then
        sum="$sum (CHECK 0개 — 시험이 아무것도 안 봄)"; nf=$((nf+1))
    fi
    echo "$t: $sum $nf FAIL"
    echo "$res" | grep '^FAIL' | sed 's/^/    /'
done
