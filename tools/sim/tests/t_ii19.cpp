// 서버 덤프의 이탈 위치(removeAfterOps)를 전수로 훑어 `Ok!` 뒤 구간의 소리·재접촉·남은 칸의 뒷일을 잠근다(17·18회차).
//  커밋(Process 0) 전 이탈 = Write Error + 재접촉이 같은 덤프를 다시 낸다(PC 가 중복으로 거른다 · 종전 그대로).
//  커밋 뒤 소거 중 **이탈** = 완료음(17차 판정 — PC 는 저장·태그는 완료 · 종전엔 Write Error 가 나서 다시 대면 Not W and D 가 반복됐다).
//  남은 칸(블록 5·8·9·60·61·62)은 다음 접촉이 지운다: 세척기가 먼저면 잔재 판정·선행 소거, 게이트웨이가 먼저면 새 검사가 덮는다.
//  커밋 뒤 소거 실패인데 **카드는 살아 있는** 것(상한 블록)은 Write Error(18차 — 그 칸은 어느 접촉도 못 지운다).
#include "common.h"

static SimCard sc;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', 'H'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'H', 'O', 'N', 'G'};

// 지난 주기를 다 마친 스코프 — 검사·환자 칸 여섯에 OLD 표지
static void done_scope(SimCard &t, uint8_t uid, int no)
{
    make_tag(t, uid, SCOPE_TYPE_TAG, no, "SC", "SER");
    set_process(t, Process{1, 1, 1, 1, 1, false, 0, 2});
    set_record(t, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    set_record(t, SECTOR3_WASHING_END, 1, rel_date(9, 4, 0));
    set_record(t, SECTOR5_DISINFECTION_START, 2, rel_date(9, 5, 0));
    set_record(t, SECTOR6_DISINFECTION_END, 2, rel_date(9, 23, 0));
    set_record(t, SECTOR1_GATEWAY, 7, rel_date(8, 50, 0));
    memcpy(t.data[SECTOR2_PATIENT_KEY], "OLDKEY", 6);
    memcpy(t.data[SECTOR2_PATIENT_NAME], "OLDNAME", 7);
    memcpy(t.data[SECTOR15_EXAMINATION_SUBJECT], "OLDSUBJ1", 8);
    memcpy(t.data[SECTOR15_EXAMINATION_SUBJECT2], "OLDSUBJ2", 8);
    memcpy(t.data[SECTOR15_EXAMINATION_SUBJECT3], "OLDSUBJ3", 8);
}
static bool nz(const SimCard &c, uint8_t b) { for (uint8_t i = 0; i < 16; ++i) if (c.data[b][i]) return true; return false; }
// 남은 칸: 비트0 블록5 · 1 블록8 · 2 블록9 · 3 블록60 · 4 블록61 · 5 블록62 (여섯 전부 — 17차는 셋만 봤다)
static uint8_t rmask(const SimCard &c)
{
    return (uint8_t)((nz(c, SECTOR1_GATEWAY) ? 1 : 0) | (nz(c, SECTOR2_PATIENT_KEY) ? 2 : 0) | (nz(c, SECTOR2_PATIENT_NAME) ? 4 : 0) |
                     (nz(c, SECTOR15_EXAMINATION_SUBJECT) ? 8 : 0) | (nz(c, SECTOR15_EXAMINATION_SUBJECT2) ? 16 : 0) |
                     (nz(c, SECTOR15_EXAMINATION_SUBJECT3) ? 32 : 0));
}
// 여섯 칸 어디든 지난 주기의 값(OLD… · 옛 본체번호 7)이 남았나
static uint8_t oldmask(const SimCard &c)
{
    uint8_t m = 0;
    int gn = 0; memcpy(&gn, c.data[SECTOR1_GATEWAY], 2);
    if (gn == 7) m |= 1;
    if (memcmp(c.data[SECTOR2_PATIENT_KEY], "OLD", 3) == 0) m |= 2;
    if (memcmp(c.data[SECTOR2_PATIENT_NAME], "OLD", 3) == 0) m |= 4;
    if (memcmp(c.data[SECTOR15_EXAMINATION_SUBJECT], "OLD", 3) == 0) m |= 8;
    if (memcmp(c.data[SECTOR15_EXAMINATION_SUBJECT2], "OLD", 3) == 0) m |= 16;
    if (memcmp(c.data[SECTOR15_EXAMINATION_SUBJECT3], "OLD", 3) == 0) m |= 32;
    return m;
}
static void to_type(char t)
{
    power_restore();
    deviceOption.SetType(t);
    hard_reset(false, 2);
    recordOption.SetPatientCheck(false);
    if (t == 'W' || t == 'D') managerOption.SetData(mk, mn);
    if (t == 'S') { serial_inject("Z", 1); GUARDED(serialEvent()); run_loops(1); }
}
static void touch_server(SimCard &c)
{
    logs_clear(); buzz_clear();
    serial_inject("Z", 1);
    touch(c);
}
// 게이트웨이에 새 환자 전문(본체번호 2 · NEWKEY/NEWNAME · 검사항목 둘 — 셋째 칸은 빈다)
static void gw_packet()
{
    char pk[120];
    snprintf(pk, sizeof(pk), "G10002;G2%u;%u;%u;0;11;30;0;G3NEWKEY;NEWNAME;G4NEWSUBJ1;NEWSUBJ2;;G5",
             (unsigned)TRACEQ_RELEASE_YEAR, (unsigned)TRACEQ_RELEASE_MONTH, (unsigned)TRACEQ_RELEASE_DAY);
    serial_inject(pk, strlen(pk));
    GUARDED(serialEvent());
    run_loops(1);
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
    touch_server(sc);
    const uint16_t total = sc.opCount;
    tlog("  기준 온전 덤프 ops=%u Ok!=%d 완료음150=%u 남은칸=%02X\n", total, serial_has("Ok!"), (unsigned)buzz_count(150), rmask(sc));
    CHECK(serial_has("Ok!") && buzz_count(150) == 1 && rmask(sc) == 0, "양성대조: 온전한 덤프는 Ok! + 완료음 + 여섯 칸 소거");

    // ── 이탈 전수 ──
    uint8_t nA = 0, nB = 0, nC = 0, nD = 0;
    uint8_t bWerr = 0, bRedump = 0, cWerr = 0, cDone = 0, cNotWD = 0, cLeft = 0;
    int16_t firstC = 0, lastC = 0;
    for (int16_t n = 1; n <= (int16_t)total + 1; ++n)
    {
        sim_advance_ms(60UL * 1000);
        done_scope(sc, (uint8_t)(0x40 + (n & 0x3F)), 40);
        sc.removeAfterOps = n;
        touch_server(sc);
        sc.removeAfterOps = 0;
        const bool ok = serial_has("Ok!");
        const Process p = get_process(sc);
        const bool committed = (p.WashingStatus == 0 && p.DisinfectionCount == 0);
        const bool werr = lcd_has("Write Error");
        const uint8_t doneBuzz = buzz_count(150);
        const uint8_t left = rmask(sc);
        char cat;
        if (!ok)             { cat = 'A'; ++nA; }
        else if (!committed) { cat = 'B'; ++nB; if (werr) ++bWerr; }
        else if (n <= (int16_t)total) { cat = 'C'; ++nC; if (werr) ++cWerr; if (doneBuzz == 1) ++cDone; if (left) ++cLeft; if (!firstC) firstC = n; lastC = n; }
        else                 { cat = 'D'; ++nD; }
        if (cat == 'B' || cat == 'C')
        {
            sim_advance_ms(3000);
            touch_server(sc);                                      // 규칙대로 다시 댄다
            const bool notwd = serial_has("Not W and D");
            const bool ok2 = serial_has("Ok!");
            if (cat == 'B' && ok2) ++bRedump;
            if (cat == 'C' && notwd) ++cNotWD;
            tlog("  n=%d %c WErr=%d 완료음=%u 남은칸=%02X | 재접촉 NotWD=%d Ok!=%d\n", n, cat, werr, doneBuzz, left, notwd, ok2);
        }
    }
    tlog("  합계 total=%u A=%u B(커밋전)=%u C(커밋뒤 n=%d..%d)=%u D=%u | B: WErr=%u 재덤프=%u | C: WErr=%u 완료음=%u NotWD=%u 칸남음=%u\n",
         total, nA, nB, firstC, lastC, nC, nD, bWerr, bRedump, cWerr, cDone, cNotWD, cLeft);
    CHECK(nB > 0 && nC > 0 && cLeft > 0, "전제: Ok! 뒤에 커밋 전 이탈과 커밋 뒤 이탈(칸이 남는 것 포함)이 둘 다 있다");
    CHECK(bWerr == nB && bRedump == nB,
          "① 커밋 전 이탈은 Write Error 이고 재접촉이 덤프를 다시 보낸다(Ok! · PC 가 중복으로 거른다)");
    // 커밋 **쓰기 직후 확인 읽기**에서 끊긴 한 자리는 커밋이 닿았어도 리더가 알 수 없어 Write Error 가 남는다(재읽기도 실패 · 17차 판정).
    CHECK(cWerr <= 1 && cDone == nC - cWerr,
          "②(17차) 커밋 뒤 소거 중 **이탈**은 Write Error 가 아니라 완료음(커밋 확인 절단 1자리만 예외) — PC 는 저장했고 태그는 완료");
    CHECK(cNotWD == nC, "②b 사실: 그래도 다시 대면 Not W and D(완료된 스코프 · 거부가 맞다)");

    // ── 안전망: 커밋 뒤 절단점 **전부**에서, 남은 칸이 다음 접촉으로 사라진다 — 세척기 직행 / 게이트웨이(새 환자) 먼저 ──
    uint8_t washLeft = 0, gwOld = 0, pts = 0;
    for (int16_t n = firstC; n <= lastC; ++n)
    {
        for (uint8_t path = 0; path < 2; ++path)
        {
            to_type('S');
            sim_advance_ms(60UL * 1000);
            done_scope(sc, (uint8_t)(0x80 + (n & 0x3F)), 80);
            sc.removeAfterOps = n;
            touch_server(sc);
            sc.removeAfterOps = 0;
            const uint8_t left = rmask(sc);
            if (path == 0)
            {
                to_type('W');
                rtc_set(rel_date(11, 0, 0));
                logs_clear(); buzz_clear(); touch(sc);                 // 세척 시작
                if (get_process(sc).WashingStatus != 1 || rmask(sc) != 0) { ++washLeft; tlog("  !세척기 n=%d 전=%02X 뒤=%02X\n", n, left, rmask(sc)); }
            }
            else
            {
                to_type('G');
                rtc_set(rel_date(11, 0, 0));
                gw_packet();
                logs_clear(); buzz_clear(); touch(sc);                 // 새 환자 기록
                if (get_process(sc).Status != 1 || oldmask(sc) != 0) { ++gwOld; tlog("  !게이트웨이 n=%d 전=%02X 옛값=%02X\n", n, left, oldmask(sc)); }
            }
        }
        ++pts;
    }
    tlog("  안전망: 절단점 %u × 두 길 — 세척기 뒤 칸 남음=%u · 게이트웨이 뒤 옛 값=%u\n", pts, washLeft, gwOld);
    CHECK(pts == nC && washLeft == 0, "③ 커밋 뒤 절단점 전부: 세척기에 먼저 가면 세척 시작이 여섯 칸을 다 지운다(잔재 판정·선행 소거)");
    CHECK(gwOld == 0, "③b 커밋 뒤 절단점 전부: 게이트웨이에 먼저 가면 새 검사가 여섯 칸을 덮는다(옛 환자·검사 0 — 셋째 검사항목 칸까지)");

    // ── ④(18차) 상한 블록: 카드는 살아 있는데 그 블록만 영영 안 써진다 → Write Error(완료음 아님) — 접촉이 끊긴 것과 가른다 ──
    {
        static const uint8_t bad[] = {SECTOR1_GATEWAY, SECTOR15_EXAMINATION_SUBJECT2};
        uint8_t werrN = 0, doneN = 0;
        for (uint8_t i = 0; i < 2; ++i)
        {
            to_type('S');
            sim_advance_ms(60UL * 1000);
            done_scope(sc, (uint8_t)(0x70 + i), 70 + i);
            sc.nackBlock = bad[i];
            touch_server(sc);
            const Process p = get_process(sc);
            tlog("  ④ 상한 블록 %u: Ok!=%d WErr=%d 완료음=%u 실패음100=%u 커밋=%d 남은칸=%02X\n", bad[i], serial_has("Ok!"), lcd_has("Write Error"),
                 (unsigned)buzz_count(150), (unsigned)buzz_count(100), p.WashingStatus == 0, rmask(sc));
            if (serial_has("Ok!") && lcd_has("Write Error") && buzz_count(100) == 4 && p.WashingStatus == 0) ++werrN;
            if (buzz_count(150) != 0) ++doneN;
            sc.nackBlock = -1;
        }
        CHECK(werrN == 2 && doneN == 0,
              "④(18차) 커밋 뒤 소거가 실패했는데 카드가 살아 있으면(상한 블록) Write Error 100×4 — 완료음으로 넘기지 않는다");
    }
    done();
    for (;;) {}
}
