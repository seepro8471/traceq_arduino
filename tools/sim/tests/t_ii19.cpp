// 17회차 VD-1 — 서버 덤프의 이탈 위치(removeAfterOps)를 전수로 훑어 `Ok!` 뒤 구간의 소리·재접촉을 잠근다.
//  커밋(Process 0) 전 이탈 = Write Error + 재접촉이 같은 덤프를 다시 낸다(PC 가 중복으로 거른다 · 종전 그대로).
//  커밋 뒤 소거 중 이탈 = **완료음**(PC 는 저장·태그는 완료 — 종전엔 Write Error 가 나서 규칙대로 다시 대면 Not W and D 거부음이 났다 ·
//  17차 판정 (나)) · 남은 칸(검사일시·검사항목·환자)은 다음 세척 시작이 지운다(안전망 실측).
#include "common.h"

static SimCard sc;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', 'H'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'H', 'O', 'N', 'G'};

static void done_scope(SimCard &t, uint8_t uid, int no)
{
    make_tag(t, uid, SCOPE_TYPE_TAG, no, "SC", "SER");
    set_process(t, Process{1, 1, 1, 1, 1, false, 0, 2});
    set_record(t, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    set_record(t, SECTOR3_WASHING_END, 1, rel_date(9, 4, 0));
    set_record(t, SECTOR5_DISINFECTION_START, 2, rel_date(9, 5, 0));
    set_record(t, SECTOR6_DISINFECTION_END, 2, rel_date(9, 23, 0));
    set_record(t, SECTOR1_GATEWAY, 7, rel_date(8, 50, 0));
    memcpy(t.data[SECTOR2_PATIENT_KEY], "PT01", 4);
    memcpy(t.data[SECTOR15_EXAMINATION_SUBJECT], "EGD", 3);
}
static bool nz(const SimCard &c, uint8_t b) { for (uint8_t i = 0; i < 16; ++i) if (c.data[b][i]) return true; return false; }
static bool leftover(const SimCard &c)
{
    return nz(c, SECTOR1_GATEWAY) || nz(c, SECTOR15_EXAMINATION_SUBJECT) || nz(c, SECTOR2_PATIENT_KEY);
}

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('S');
    serial_inject("Z", 1);
    GUARDED(serialEvent());
    run_loops(1);

    // 기준: 온전한 덤프의 카드 동작 수(접촉 예산은 t_ops 가 잠근다 — 여기선 훑기 상한으로만)
    done_scope(sc, 0x31, 31);
    logs_clear();
    serial_inject("Z", 1);
    touch(sc);
    const uint16_t total = sc.opCount;
    tlog("  기준 온전 덤프 ops=%u Ok!=%d 완료음150=%u\n", total, serial_has("Ok!"), (unsigned)buzz_count(150));
    CHECK(serial_has("Ok!") && buzz_count(150) == 1 && !leftover(sc), "양성대조: 온전한 덤프는 Ok! + 완료음 + 소거");

    uint8_t nA = 0, nB = 0, nC = 0, nD = 0;
    uint8_t bWerr = 0, bRedump = 0, cWerr = 0, cDone = 0, cNotWD = 0, cLeft = 0;
    for (int16_t n = 1; n <= (int16_t)total + 1; ++n)
    {
        sim_advance_ms(60UL * 1000);
        done_scope(sc, (uint8_t)(0x40 + (n & 0x3F)), 40);
        sc.removeAfterOps = n;
        logs_clear(); buzz_clear();
        serial_inject("Z", 1);
        touch(sc);
        sc.removeAfterOps = 0;
        const bool ok = serial_has("Ok!");
        const Process p = get_process(sc);
        const bool committed = (p.WashingStatus == 0 && p.DisinfectionCount == 0);
        const bool werr = lcd_has("Write Error");
        const uint8_t doneBuzz = buzz_count(150);
        char cat;
        if (!ok)             { cat = 'A'; ++nA; }
        else if (!committed) { cat = 'B'; ++nB; if (werr) ++bWerr; }
        else if (n <= (int16_t)total) { cat = 'C'; ++nC; if (werr) ++cWerr; if (doneBuzz == 1) ++cDone; }
        else                 { cat = 'D'; ++nD; }
        if (cat == 'B' || cat == 'C')
        {
            sim_advance_ms(3000);
            logs_clear();
            serial_inject("Z", 1);
            touch(sc);                                             // 규칙대로 다시 댄다
            const bool notwd = serial_has("Not W and D");
            const bool ok2 = serial_has("Ok!");
            if (cat == 'B' && ok2) ++bRedump;
            if (cat == 'C') { if (notwd) ++cNotWD; if (leftover(sc)) ++cLeft; }
            tlog("  n=%d %c WErr=%d 완료음=%u | 재접촉 NotWD=%d Ok!=%d 남은칸=%d\n", n, cat, werr, doneBuzz, notwd, ok2, leftover(sc));
        }
    }
    tlog("  합계 total=%u A(Ok!없음)=%u B(Ok!+커밋전)=%u C(Ok!+커밋뒤)=%u D=%u | B: WErr=%u 재덤프=%u | C: WErr=%u 완료음=%u 재접촉NotWD=%u 남은칸=%u\n",
         total, nA, nB, nC, nD, bWerr, bRedump, cWerr, cDone, cNotWD, cLeft);
    CHECK(nB > 0 && nC > 0, "전제: Ok! 뒤에 커밋 전 이탈과 커밋 뒤 이탈이 둘 다 있다");
    CHECK(bWerr == nB && bRedump == nB,
          "① 커밋 전 이탈은 Write Error 이고 재접촉이 같은 덤프를 다시 낸다(PC 가 중복으로 거른다 · 종전 그대로)");
    // 커밋 **쓰기 직후 확인 읽기**에서 끊긴 한 자리(n=51)는 커밋이 닿았어도 리더가 알 수 없어 Write Error 가 남는다(재읽기도 실패 ·
    //  소독 시작의 "커밋 뒤 확인 절단 1자리" 와 같은 부류 · 17차 판정) — 그 한 자리를 빼고 전부 완료음이어야 한다.
    CHECK(cWerr <= 1 && cDone == nC - cWerr,
          "②(17차) 커밋 뒤 소거 중 이탈은 Write Error 가 아니라 완료음(커밋 확인 절단 1자리만 예외) — PC 는 저장했고 태그는 완료");
    CHECK(cNotWD == nC, "②b 사실: 그래도 다시 대면 Not W and D(완료된 스코프 · 거부가 맞다)");

    // 안전망: 남은 칸이 있는 태그를 세척기에 대면 다음 세척 시작이 지운다(잔재 판정 · 선행 소거)
    {
        sim_advance_ms(60UL * 1000);
        done_scope(sc, 0x7E, 126);
        sc.removeAfterOps = (int16_t)(total - 8);                  // 커밋 뒤 · 소거 도중
        logs_clear(); buzz_clear(); serial_inject("Z", 1); touch(sc); sc.removeAfterOps = 0;
        const bool pre = get_process(sc).WashingStatus == 0 && leftover(sc) && !lcd_has("Write Error");
        power_restore(); deviceOption.SetType('W'); hard_reset(false, 2); managerOption.SetData(mk, mn);
        recordOption.SetPatientCheck(false);
        rtc_set(rel_date(11, 0, 0));
        logs_clear(); buzz_clear(); touch(sc);
        const Process pw = get_process(sc);
        tlog("  안전망: 전제=%d → 세척 시작 WS=%u RW=%u 남은칸=%d 성공음=%u\n", pre, pw.WashingStatus, pw.Rewrite, leftover(sc), (unsigned)buzz_count(50));
        CHECK(pre, "안전망 전제: 커밋 뒤 소거 도중 이탈 — 완료 처리됐고 칸이 남았다");
        CHECK(pw.WashingStatus == 1 && !leftover(sc) && buzz_count(50) == 1,
              "안전망: 그 태그의 다음 세척 시작이 남은 칸(검사일시·검사항목·환자)을 지운다");
    }
    done();
    for (;;) {}
}
