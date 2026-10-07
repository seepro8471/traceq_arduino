#!/bin/bash
# 사용: runall.sh <펌웨어 루트> <출력 폴더>  — 등록된 시험 전부 빌드·실행, 요약 한 줄씩.
ROOT="$1"; OUTD="$2"
# 인자 둘이 비면 빈 경로로 빌드해 전량 "빌드 실패" 로 보인다 · 출력 폴더에 공백이 있으면 run.sh 가 전량 TIMEOUT 으로 보인다(17차 · 둘 다 겪음)
[ -n "$ROOT" ] && [ -n "$OUTD" ] || { echo "사용: runall.sh <펌웨어 루트> <출력 폴더(공백 없는 경로)>"; exit 2; }
case "$OUTD" in *" "*) echo "!! 출력 폴더에 공백 — gdb 가 elf 를 못 연다: $OUTD"; exit 2 ;; esac
HERE="$(cd "$(dirname "$0")" && pwd)"
T=(t_smoke t_disinfect t_rfid t_server t_ui t_issue t_clear t_firstboot t_gateway t_notify t_a4 t_lcd t_menu t_a5 t_a6 t_a7 t_a8 t_a9 t_a10 t_sp t_ops t_gwmix t_dd t_dd3s t_dd3g t_dd1 t_dd1b t_dd1c t_dd1d t_ee1a t_ee1b t_ee1c t_ee1d t_ee1e t_ee2r t_ff1a t_ff1b t_ff1c t_ff2a t_ff2m t_gg2g t_gg2s t_gg2w t_gg2m t_gg1a t_gg1b t_subj t_hh1a t_hh1b t_gnum t_ii1 t_ii2 t_ii3 t_ii4 t_ii5 t_ii6 t_ii7 t_ii8 t_ii9 t_ii10 t_ii11 t_ii12 t_ii13 t_ii14 t_ii15 t_ii16 t_ii17 t_ii18 t_ii19 t_ii20)
# 소리·글자 계측 시험 — `-DHH2_DWELL` 이 필요해서 **따로** 빌드한다(그 계측을 전부에 넣으면 t_a4 가 정적 RAM 관문 28000B 를 넘는다).
T2=(t_hh2w t_hh2g t_hh2s t_hh2lock t_hh2lock2 t_hh2fix)
rm -rf "$OUTD"
TOTAL_FAIL=0

# 한 무리를 빌드하고 하나씩 돌린다. $1 = 출력 폴더 · $2 = 추가 define · 나머지 = 시험 이름
run_group() {
    local outd="$1"; shift
    local defs="$1"; shift
    local names=("$@")
    [ ${#names[@]} -eq 0 ] && return
    local src=()
    for t in "${names[@]}"; do src+=("$HERE/tests/$t.cpp"); done
    # build.sh 의 RAM 관문(★줄)·종료코드를 버리지 않는다 — 넘치면 여기서 끝낸다.
    local bout brc
    bout=$(HH2_DEFS="$defs" bash "$HERE/build.sh" "$ROOT" "$outd" "${src[@]}" 2>&1); brc=$?
    echo "$bout" | grep -v "^warning\|lto-wrapper" | grep -E "★|error:|RAM" | head -5
    [ $brc -ne 0 ] && { echo "!! build.sh 실패(rc=$brc) — 관문 위반이면 elf 가 지워진다"; }
    for t in "${names[@]}"; do
        [ -f "$outd/$t.elf" ] || { echo "$t: ==> pass=0 fail=1 (빌드 실패)  1 FAIL"; TOTAL_FAIL=$((TOTAL_FAIL+1)); continue; }   # 종료코드에도 센다(17차: 빌드 실패에 0 이 났다)
        local res sum nf
        res=$(bash "$HERE/run.sh" "$outd/$t.elf" 180)
        sum=$(echo "$res" | grep '==>')
        nf=$(echo "$res" | grep -c '^FAIL')
        # ★거짓 초록 방지: 멈춤(TIMEOUT)·요약 없음·pass=0 은 전부 빨강으로 센다.
        if echo "$res" | grep -q "!! TIMEOUT" || [ -z "$sum" ]; then
            sum="==> pass=0 fail=1 (멈춤/요약 없음 — 무한 루프 의심)"; nf=$((nf+1))
        elif echo "$sum" | grep -q "pass=0 fail=0"; then
            sum="==> pass=0 fail=1 (CHECK 0개 — 시험이 아무것도 안 봄)"; nf=$((nf+1))   # 숫자도 fail=1 로 — awk 합산에서 사라지지 않게(16차)
        fi
        TOTAL_FAIL=$((TOTAL_FAIL+nf))
        echo "$t: $sum $nf FAIL"
        echo "$res" | grep '^FAIL' | sed 's/^/    /'
    done
}

run_group "$OUTD" "" "${T[@]}"
run_group "$OUTD/dwell" "-DHH2_DWELL" "${T2[@]}"

# ★등록 수 검사 — tests/t_*.cpp 가 T·T2 에 없으면 한 번도 안 돈다(목록에서 빠져도 초록 줄만 줄어 모른다).
reg=" ${T[*]} ${T2[*]} "; nfile=0; unreg=(); nofile=()
for f in "$HERE"/tests/t_*.cpp; do
    nm=$(basename "$f" .cpp); nfile=$((nfile+1))
    case "$reg" in *" $nm "*) ;; *) unreg+=("$nm") ;; esac
done
for nm in "${T[@]}" "${T2[@]}"; do [ -f "$HERE/tests/$nm.cpp" ] || nofile+=("$nm"); done
echo "등록 $(( ${#T[@]} + ${#T2[@]} ))종 · tests/t_*.cpp ${nfile}개"
if [ ${#unreg[@]} -gt 0 ]; then echo "!! 미등록: ${unreg[*]}"; fi
if [ ${#nofile[@]} -gt 0 ]; then echo "!! 파일 없음: ${nofile[*]}"; fi
# 종료코드: 빨강·미등록·파일 없음이 하나라도 있으면 1 (16차 — 종전엔 늘 0)
if [ "${TOTAL_FAIL:-0}" -gt 0 ] || [ ${#unreg[@]} -gt 0 ] || [ ${#nofile[@]} -gt 0 ]; then exit 1; fi
exit 0
