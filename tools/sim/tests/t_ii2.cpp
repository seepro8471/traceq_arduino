// 14회차 II-G 형제 셋 — 앞 회차 봉합이 **한 자리만** 고치고 같은 일을 하는 다른 자리를 안 고친 것.
//  ① 3행 번호 비우기(12차 print_tag_number)의 형제 = main.cpp 회사 블록 읽기 실패
//  ② "판정 근거 블록5 는 마지막" (13차 잔재 소거)의 형제 = 서버 덤프 소거 · 레거시 Status2 소거
//  ③ 시각 동기 실패음(13차 레거시 T)의 형제 = JSON cfg_set_date_time
#include "common.h"

static SimCard mgr, a, b, sc;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', '1'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'K', 'I', 'M'};

static DateTime yday(uint8_t h, uint8_t m) { return DateTime(rel_date(h, m, 0).unixtime() - 86400UL); }
static bool blk_is(const SimCard &c, uint8_t blk, const char *s) { return memcmp(c.data[blk], s, strlen(s)) == 0; }

static void as_server()
{
    deviceOption.SetType('S');
    hard_reset(false, 2);
    serial_inject("Z", 1);
    GUARDED(serialEvent());
    run_loops(1);
}
static void as_washer()
{
    deviceOption.SetType('W');
    hard_reset(false, 2);
    managerOption.SetData(mk, mn);
}
// 어제 한 주기(검사 → 세척 → 소독)가 끝난 태그 — 서버 덤프 대기
static void done_cycle(SimCard &c, uint8_t uid, int no)
{
    char id[10], ser[10];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(c, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(c, Process{1, 1, 1, 1, 3, false, 0, 2});
    set_record(c, SECTOR1_GATEWAY, 7, yday(8, 0));
    set_record(c, SECTOR2_WASHING_START, 1, yday(9, 0));
    set_record(c, SECTOR3_WASHING_END, 1, yday(9, 4));
    set_record(c, SECTOR5_DISINFECTION_START, 3, yday(9, 10));
    set_record(c, SECTOR6_DISINFECTION_END, 3, yday(9, 28));
    put_block(c, SECTOR15_EXAMINATION_SUBJECT, "OLDSUBJ1", 8);
    put_block(c, SECTOR15_EXAMINATION_SUBJECT2, "OLDSUBJ2", 8);
    put_block(c, SECTOR15_EXAMINATION_SUBJECT3, "OLDSUBJ3", 8);
    put_block(c, SECTOR2_PATIENT_KEY, "PKEY0001", 8);
    put_block(c, SECTOR2_PATIENT_NAME, "PNAME001", 8);
}
static bool any_old(const SimCard &c)
{
    return blk_is(c, SECTOR15_EXAMINATION_SUBJECT, "OLDSUBJ") || blk_is(c, SECTOR15_EXAMINATION_SUBJECT2, "OLDSUBJ") ||
           blk_is(c, SECTOR15_EXAMINATION_SUBJECT3, "OLDSUBJ");
}

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');
    managerOption.SetData(mk, mn);
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");

    // ── ① 3행 번호 비우기의 형제 ──
    {
        touch(mgr);
        make_tag(a, 0x41, SCOPE_TYPE_TAG, 41, "SC0041", "S0041");
        touch(a);
        const bool shownA = memcmp(lcd_row(3), "00041", 5) == 0;
        make_tag(b, 0x42, SCOPE_TYPE_TAG, 77, "SC0077", "S0077");
        b.readErrBlock = SECTOR0_TAG; b.readErrTimes = 10; b.readErrSkip = 0;
        logs_clear();
        touch(b);
        const bool clearedTag = memcmp(lcd_row(3), "00041", 5) != 0;
        touch(a);                                            // 앞 건 번호를 다시 띄워 둔다
        make_tag(b, 0x43, SCOPE_TYPE_TAG, 78, "SC0078", "S0078");
        b.readErrBlock = SECTOR0_COMPANY; b.readErrTimes = 10; b.readErrSkip = 0;
        logs_clear();
        touch(b);
        const bool clearedCo = memcmp(lcd_row(3), "00041", 5) != 0;
        tlog("  1 앞건표시=%u · 번호블록실패뒤지움=%u · 회사블록실패뒤지움=%u 3행=[%.20s]\n",
             (unsigned)shownA, (unsigned)clearedTag, (unsigned)clearedCo, lcd_row(3));
        CHECK(shownA, "1 전제: 앞 건 번호(00041)가 3행에 떠 있다");
        CHECK(clearedTag, "1a 번호 블록 읽기 실패는 3행의 앞 건 번호를 지운다(12차 봉합)");
        CHECK(clearedCo, "1b 회사 블록 읽기 실패도 3행의 앞 건 번호를 지운다(형제 · 14차 II-G P3-3)");
    }

    // ── ② 덤프 소거 순서 — 커밋 뒤 이탈 지점 전수: "블록5=0 · 섹터15 잔존" 창이 있으면 안 된다 ──
    //    (블록5 가 먼저 지워지면 다음 세척 시작의 잔재 판정이 Year==0 을 보고 건너뛰어 옛 검사항목이 영구화된다)
    {
        uint8_t nCommitted = 0, nInClear = 0, nBad = 0, nCarried = 0, firstBad = 0;
        for (uint8_t n = 40; n <= 110; ++n)
        {
            as_server();
            done_cycle(sc, (uint8_t)(0x80 + (n & 0x3F)), 200 + n);
            sc.removeAfterOps = (int16_t)n;
            rtc_set(yday(18, 0));
            logs_clear();
            serial_inject("Z", 1);
            touch(sc, 2, 4);                                   // 덤프 — n 동작 뒤 접촉이 끊긴다
            sc.removeAfterOps = 0;
            const Process p = get_process(sc);
            const bool committed = p.WashingStatus == 0 && p.DisinfectionCount == 0 && p.Rewrite == 0;
            if (!committed) continue;
            ++nCommitted;
            const bool gwGone = get_ldt(sc, SECTOR1_GATEWAY).Date.Year == 0;
            const bool oldLeft = any_old(sc);
            if (oldLeft || !gwGone) ++nInClear;                // 소거 도중(또는 소거 전)에 끊긴 지점
            if (gwGone && oldLeft) { ++nBad; if (!firstBad) firstBad = n; }
            // 다음 주기(게이트웨이 안 거침): 세척 시작이 남은 잔재를 마저 지워야 한다(사장님 09-28)
            as_washer();
            touch(mgr);
            rtc_set(rel_date(10, 0, 0));
            touch(sc, 2, 4);
            if (any_old(sc) && get_process(sc).Rewrite == 1) ++nCarried;
        }
        tlog("  2 덤프 커밋 %u · 소거 도중 이탈 %u · 블록5=0·섹터15잔존 창 %u(첫 N=%u) · 다음 세척 뒤 잔존 %u\n",
             (unsigned)nCommitted, (unsigned)nInClear, (unsigned)nBad, (unsigned)firstBad, (unsigned)nCarried);
        CHECK(nCommitted > 0 && nInClear > 0, "2 양성대조: 덤프 커밋 뒤 소거 도중에 끊기는 이탈 지점이 실제로 있다");
        CHECK(nBad == 0, "2a 덤프 소거는 블록5(검사일시)를 마지막에 지운다 — 블록5=0·섹터15 잔존 창이 없다(형제 · P3-1)");
        CHECK(nCarried == 0, "2b 소거 도중 끊긴 태그도 다음 세척 시작(게이트웨이 안 거침)이 옛 검사항목을 마저 지운다");
    }

    // ── ③ JSON 시각 동기 — 무효면 실패음(레거시 T 와 같은 규칙) · 유효면 안내음 ──
    {
        as_server();
        rtc_set(rel_date(10, 0, 0));
        const uint8_t minBefore = rtc.GetCurrentDateTime().minute();
        logs_clear(); buzz_clear();
        static const char bad[] = "{\"cmd\":\"cfg_set_date_time\",\"device_date_time\":\"2026-13-45T25:99:99\"}";
        serial_inject(bad, sizeof(bad) - 1);
        GUARDED(serialEvent());
        const uint8_t bad100 = buzz_count(100), bad1000 = buzz_count(1000);
        const bool badKept = rtc.GetCurrentDateTime().minute() == minBefore;
        const bool badMsg = lcd_has("Invalid DateTime");
        logs_clear(); buzz_clear();
        static const char good[] = "{\"cmd\":\"cfg_set_date_time\",\"device_date_time\":\"2026-09-27T11:22:33\"}";
        serial_inject(good, sizeof(good) - 1);
        GUARDED(serialEvent());
        const uint8_t good1000 = buzz_count(1000), good100 = buzz_count(100);
        const bool goodSet = rtc.GetCurrentDateTime().minute() == 22;
        tlog("  3 무효: 100x%u 1000x%u 유지=%u 문구=%u · 유효: 1000x%u 100x%u 반영=%u\n",
             (unsigned)bad100, (unsigned)bad1000, (unsigned)badKept, (unsigned)badMsg,
             (unsigned)good1000, (unsigned)good100, (unsigned)goodSet);
        CHECK(good1000 == 1 && good100 == 0 && goodSet, "3 양성대조: 유효 시각은 반영되고 안내음 1000x1");
        CHECK(bad100 == 4 && bad1000 == 0 && badKept && badMsg,
              "3a JSON 시각 동기 무효는 실패음 100x4 + Invalid DateTime — 시계는 그대로(레거시 T 의 형제 · P3-5)");
    }

    // ── ④ 게이트웨이 폴백의 쓰기 순서 — Status(0) 먼저, 블록5 뒤 (II-D P3-3 · 형제 write_patient_info 와 같이) ──
    //    끊긴 자리 전수에서 "Status=1(옛 환자) + 새 검사일시" 조합이 없어야 한다(세척기가 남의 환자로 통과시키는 조합).
    {
        uint8_t nRun = 0, nBad = 0, nBetween = 0;
        for (uint8_t n = 4; n <= 40; ++n)
        {
            deviceOption.SetType('G');
            hard_reset(false, 2);
            make_tag(sc, (uint8_t)(0x90 + (n & 0x0F)), SCOPE_TYPE_TAG, 300 + n, "SC0300", "S0300");
            set_process(sc, Process{1, 0, 0, 0, 0, false, 0, 0});          // 덤프 뒤 · 옛 환자 표지 그대로
            set_record(sc, SECTOR1_GATEWAY, 7, yday(8, 0));
            put_block(sc, SECTOR2_PATIENT_KEY, "OLDKEY99", 8);
            rtc_set(rel_date(10, 0, 0));
            sc.removeAfterOps = (int16_t)n;
            touch(sc, 2, 4);                                                // PC 전문 없음 → 폴백, n 동작 뒤 끊김
            sc.removeAfterOps = 0;
            ++nRun;
            const bool gwNew = get_ldt(sc, SECTOR1_GATEWAY).Date.Day == rel_date(10, 0, 0).day() &&
                               get_ldt(sc, SECTOR1_GATEWAY).Time.Hour == 10;
            const uint8_t st = get_process(sc).Status;
            if (st == 1 && gwNew) ++nBad;                                   // 옛 환자 표지 + 새 검사일시
            if (st == 0 && !gwNew) ++nBetween;                              // Status 는 내렸고 블록5 는 아직 옛것(두 쓰기 사이)
        }
        tlog("  4 폴백 이탈 N=4..40(%u): Status1+새검사일시 %u · 두 쓰기 사이 %u\n", (unsigned)nRun, (unsigned)nBad, (unsigned)nBetween);
        CHECK(nBetween > 0, "4 양성대조: Status 를 내린 뒤 블록5 를 쓰는 사이에 끊기는 지점이 있다(순서가 그렇다)");
        CHECK(nBad == 0, "4a 폴백은 Status 를 먼저 내린다 — 어디서 끊겨도 'Status1 + 새 검사일시' 가 남지 않는다(P3-3)");
    }

    // ── ⑤ 게이트웨이 재등록의 선행 정리 — Status 1 뿐 아니라 0 이 아닌 값(델파이 시절 3) 전부 (II-D P3-6) ──
    {
        uint8_t nRun = 0, nBad = 0, nCleared = 0;
        static const char frame[] = "G10007;G22026;9;28;3;10;00;0;G3PT0009;HONG;;G4S;;;G5;";
        for (uint8_t n = 4; n <= 40; ++n)
        {
            deviceOption.SetType('G');
            hard_reset(false, 2);
            serial_inject(frame, sizeof(frame) - 1);
            GUARDED(serialEvent());
            run_loops(1);
            make_tag(sc, (uint8_t)(0xA0 + (n & 0x0F)), SCOPE_TYPE_TAG, 400 + n, "SC0400", "S0400");
            set_process(sc, Process{3, 0, 0, 0, 0, false, 0, 0});          // 델파이 시절 '환자정보 있음'
            set_record(sc, SECTOR1_GATEWAY, 7, yday(8, 0));
            put_block(sc, SECTOR2_PATIENT_KEY, "OLDKEY03", 8);
            rtc_set(rel_date(10, 0, 0));
            sc.removeAfterOps = (int16_t)n;
            touch(sc, 2, 4);
            sc.removeAfterOps = 0;
            ++nRun;
            const bool keyOld = blk_is(sc, SECTOR2_PATIENT_KEY, "OLDKEY03");
            const bool gwNew = get_ldt(sc, SECTOR1_GATEWAY).Time.Hour == 10;
            const uint8_t st = get_process(sc).Status;
            if (st != 0 && keyOld && gwNew) ++nBad;                         // 옛 환자 + 새 검사일시 + 표지 살아 있음
            if (st == 0) ++nCleared;                                        // 선행 정리가 돌았다
        }
        tlog("  5 재등록(Status 3) 이탈 N=4..40(%u): 정리됨 %u · 옛환자+새검사+표지 %u\n", (unsigned)nRun, (unsigned)nCleared, (unsigned)nBad);
        CHECK(nCleared > 0, "5 양성대조: Status 3 태그도 재등록의 선행 정리(Status 0)가 돈다");
        CHECK(nBad == 0, "5a 어디서 끊겨도 'Status≠0 + 옛 환자 + 새 검사일시' 가 남지 않는다(P3-6)");
    }

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
