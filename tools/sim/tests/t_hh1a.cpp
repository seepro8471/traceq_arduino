// HH1(13회차) — v2.2.29 `washing_start` 의 '지난 검사 잔재 소거' 한 자리 재추적.
//  판정은 2.2.32 부터 표지만(사장님 선택 1): 공정 전부 0 + Status 0 + 검사일시 있음 → 지운다 · 시간 비교 없음.
//  ① 커밋 전 실패 뒤 사람이 다시 대는 회복 경로에서 **이번 검사**(Status 1)가 지워지는가
//  ② 블록5 가 쓰기 불가(손상)면 그 스코프가 영구히 세척 불가가 되는가 · 덤프·재발급으로 회복되나
//  ③ 섹터15 의 한 블록만 손상이면 어떤 상태로 굳나
//  ④ 시각 경계가 없다 — 검사일시가 지난 세척 종료와 같은 초든 1초 뒤든 Status 0 이면 지운다
#include "common.h"

static SimCard mgr, sc;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', '1'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'K', 'I', 'M'};

static DateTime yday(uint8_t h, uint8_t m) { return DateTime(rel_date(h, m, 0).unixtime() - 86400UL); }

static void as_type(char t)
{
    deviceOption.SetType(t);
    hard_reset(false, 2);
    managerOption.SetData(mk, mn);
}
static void as_server()
{
    deviceOption.SetType('S');
    hard_reset(false, 2);
    serial_inject("Z", 1);
    GUARDED(serialEvent());
    run_loops(1);
}
// 레거시 발급 명령 한 번(카드는 올려 둔 채)
static void issue(SimCard &c, const char *cmd)
{
    card_place(&c);
    logs_clear();
    serial_inject(cmd, strlen(cmd));
    GUARDED(serialEvent());
    card_remove();
    run_loops(4);
}

// 지난 주기 세척(ws/we) + 검사 정보(exam·subj)를 심은 스코프
static void scope(SimCard &c, uint8_t uid, int no, uint8_t status,
                  const DateTime &ws, const DateTime &we, const DateTime &exam, const char *subj)
{
    char id[10], ser[10];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(c, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(c, Process{status, 0, 0, 0, 0, false, 0, 0});
    set_record(c, SECTOR2_WASHING_START, 1, ws);
    set_record(c, SECTOR3_WASHING_END, 1, we);
    set_record(c, SECTOR1_GATEWAY, 7, exam);
    put_block(c, SECTOR15_EXAMINATION_SUBJECT, subj, (uint8_t)strlen(subj));
    put_block(c, SECTOR2_PATIENT_KEY, "PKEY0001", 8);
    put_block(c, SECTOR2_PATIENT_NAME, "PNAME001", 8);
}
static bool subj_is(const SimCard &c, const char *s)
{
    return memcmp(c.data[SECTOR15_EXAMINATION_SUBJECT], s, strlen(s)) == 0;
}
static int tag_type(const SimCard &c)
{
    Company co{};
    memcpy(&co, c.data[SECTOR0_COMPANY], sizeof(co));
    return co.TagType;
}

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");

    // ── ① 커밋 전 실패(담당자 키 블록 쓰기 거부) → 실패음 → 사람이 8초 뒤 다시 댄다 ──
    //    첫 시도에서 블록10(세척 시작)은 이미 **이번 접촉 시각**으로 써졌다. 옛 시간 판정은 그 값을
    //    '지난 주기 세척 시작' 으로 읽을 수 있었다 — 지금은 시각을 안 보고, 이 태그는 Status 1 이라 남는다.
    {
        as_type('W');
        touch(mgr);
        rtc_set(rel_date(10, 0, 0));
        scope(sc, 0x41, 41, 1, yday(9, 0), yday(9, 4), rel_date(8, 0, 0), "TODAYSUB");
        sc.nackBlock = SECTOR3_WASHING_START_MANAGER_KEY;   // 담당자 키 블록만 쓰기 거부(카드는 산다)
        logs_clear(); buzz_clear();
        touch(sc, 2, 4);
        const bool err = lcd_has("Write Error");
        const uint8_t b100 = buzz_count(100);
        const LocalDateTime ws1 = get_ldt(sc, SECTOR2_WASHING_START);
        tlog("  1a 실패: 문구=%u 짧게100=%u 블록10=%02u:%02u RW=%u 검사항목=%u\n",
             (unsigned)err, (unsigned)b100, ws1.Time.Hour, ws1.Time.Minute,
             get_process(sc).Rewrite, (unsigned)subj_is(sc, "TODAYSUB"));
        CHECK(err && b100 == 4 && ws1.Time.Hour == 10 && get_process(sc).Rewrite == 0 &&
              subj_is(sc, "TODAYSUB"),
              "1a 전제: 커밋 전 실패 — 실패음 4회 · 블록10 엔 이번 시각이 써졌고 커밋(RW)은 안 됐다");

        sc.nackBlock = -1;
        rtc_set(rel_date(10, 0, 8));                        // 더블터치 창(2초) 밖의 재접촉
        logs_clear(); buzz_clear();
        touch(sc, 2, 4);
        const LocalDateTime gw = get_ldt(sc, SECTOR1_GATEWAY);
        tlog("  1b 재접촉: 검사항목=%u 검사일시연도=%u %02u:%02u · 성공음50=%u RW=%u\n",
             (unsigned)subj_is(sc, "TODAYSUB"), gw.Date.Year, gw.Time.Hour, gw.Time.Minute,
             (unsigned)buzz_count(50), get_process(sc).Rewrite);
        CHECK(get_process(sc).Rewrite == 1,
              "1b 전제: 재접촉의 세척 시작은 성공(커밋)했다");
        CHECK(subj_is(sc, "TODAYSUB") && gw.Date.Year != 0 && gw.Time.Hour == 8,
              "1c 커밋 전 실패 뒤 재접촉이 이번 검사의 검사일시·검사항목을 지우지 않는다");
    }

    // ── ② 블록5(게이트웨이) 쓰기 불가 + 지난 주기 잔재 → 소거 실패로 세척 시작이 중단된다 ──
    {
        as_type('W');
        touch(mgr);
        rtc_set(rel_date(10, 0, 0));
        scope(sc, 0x42, 42, 0, yday(9, 0), yday(9, 4), yday(8, 0), "OLDSUBJ9");
        sc.nackBlock = SECTOR1_GATEWAY;                     // 그 블록만 영구 쓰기 불가
        uint8_t nErr = 0;
        for (uint8_t i = 0; i < 3; ++i)
        {
            rtc_set(rel_date(10, (uint8_t)(i + 1), 0));
            logs_clear();
            touch(sc, 2, 4);
            if (lcd_has("Write Error")) ++nErr;
        }
        const Process p = get_process(sc);
        tlog("  2a 블록5 손상: 실패 %u/3 · RW=%u WS=%u 세척시작=%02u:%02u\n",
             (unsigned)nErr, p.Rewrite, p.WashingStatus,
             get_ldt(sc, SECTOR2_WASHING_START).Time.Hour,
             get_ldt(sc, SECTOR2_WASHING_START).Time.Minute);
        // ★판정식은 "Write Error 문구 없음" 이 아니라 **커밋(RW=1)** 으로 — 종류 미지정 거부도 문구가 없다
        CHECK(p.Rewrite == 1,
              "2b 블록5 가 쓰기 불가인 태그도 세척 시작은 커밋되어야 한다(영구 막힘 금지)");

        // 회복 ① 서버 덤프
        as_server();
        logs_clear();
        serial_inject("Z", 1);
        touch(sc, 2, 4);
        const bool dumped = serial_has("Ok!");
        // 회복 ② 레거시 재발급
        issue(sc, "ZS42;S0042;");
        const int tt = tag_type(sc);
        // 회복 ③ 다시 세척기
        as_type('W');
        touch(mgr);
        rtc_set(rel_date(11, 0, 0));
        logs_clear();
        touch(sc, 2, 4);
        tlog("  2c 회복: 덤프Ok=%u 재발급뒤종류=%d · 그 뒤 세척 RW=%u lcd=[%.20s]\n",
             (unsigned)dumped, tt, get_process(sc).Rewrite, lcd_row(2));
    }

    // ── ③ 섹터15 의 마지막 블록(62)만 손상 — 어떤 상태로 굳나 ──
    {
        as_type('W');
        touch(mgr);
        rtc_set(rel_date(10, 0, 0));
        scope(sc, 0x43, 43, 0, yday(9, 0), yday(9, 4), yday(8, 0), "OLDSUBJ8");
        put_block(sc, SECTOR15_EXAMINATION_SUBJECT3, "TAIL0003", 8);
        sc.nackBlock = SECTOR15_EXAMINATION_SUBJECT3;
        uint8_t nErr = 0;
        for (uint8_t i = 0; i < 2; ++i)
        {
            rtc_set(rel_date(10, (uint8_t)(i + 1), 0));
            logs_clear();
            touch(sc, 2, 4);
            if (lcd_has("Write Error")) ++nErr;
        }
        const bool gwGone = get_ldt(sc, SECTOR1_GATEWAY).Date.Year == 0;
        const bool s60Gone = sc.data[SECTOR15_EXAMINATION_SUBJECT][0] == 0;
        const bool tailLeft = memcmp(sc.data[SECTOR15_EXAMINATION_SUBJECT3], "TAIL0003", 8) == 0;
        tlog("  3 블록62 손상: 실패 %u/2 · 블록5비움=%u 블록60비움=%u 블록62잔존=%u RW=%u\n",
             (unsigned)nErr, (unsigned)gwGone, (unsigned)s60Gone, (unsigned)tailLeft,
             get_process(sc).Rewrite);
        CHECK(get_process(sc).Rewrite == 1,
              "3a 섹터15 한 블록이 쓰기 불가여도 두 번 안에 세척 시작이 커밋된다(영구 막힘 금지)");
    }

    // ── ④ 시각 경계가 없음을 **양쪽으로**: 검사일시 = 지난 세척 종료(블록14)와 같은 초(4a) · 1초 뒤(4b) ──
    //    둘 다 완료 뒤 Status 0 이라 지운다. `exam <= prev` 같은 시간 비교를 되살리면 4b 가 빨강이 된다.
    {
        as_type('W');
        touch(mgr);
        rtc_set(rel_date(10, 0, 0));
        scope(sc, 0x44, 44, 0, yday(9, 0), yday(9, 4), yday(9, 4), "EQSUBJ01");
        logs_clear();
        touch(sc, 2, 4);
        const bool gone = get_ldt(sc, SECTOR1_GATEWAY).Date.Year == 0 && !subj_is(sc, "EQSUBJ01");
        tlog("  4a 같은 초: 비워짐=%u 연도=%u 항목='%.8s' RW=%u 오류=%u\n", (unsigned)gone,
             get_ldt(sc, SECTOR1_GATEWAY).Date.Year, (const char *)sc.data[SECTOR15_EXAMINATION_SUBJECT],
             get_process(sc).Rewrite, (unsigned)lcd_has("Error"));
        CHECK(gone, "4a 완료 뒤 Status 0 + 검사일시 있음 → 지운다(표지만 · 시각은 보지 않는다)");

        as_type('W');
        touch(mgr);
        rtc_set(rel_date(10, 0, 0));
        scope(sc, 0x46, 46, 0, yday(9, 0), yday(9, 4),
              DateTime(yday(9, 4).unixtime() + 1), "GTSUBJ01");
        logs_clear();
        touch(sc, 2, 4);
        const bool kept1s = subj_is(sc, "GTSUBJ01") && get_ldt(sc, SECTOR1_GATEWAY).Date.Year != 0;
        tlog("  4b 1초 뒤: 보존=%u 항목='%.8s' RW=%u\n", (unsigned)kept1s,
             (const char *)sc.data[SECTOR15_EXAMINATION_SUBJECT], get_process(sc).Rewrite);
        // [사장님 선택 1 · 09-28] 시각은 보지 않는다 — Status 0(환자 없음)이면 지난 세척보다 뒤인 검사일시(폴백)도 지워진다.
        CHECK(!kept1s, "4b 검사일시가 지난 세척 종료보다 뒤여도 Status 0 이면 지운다 — 폴백 검사일시는 감수(표지만 판정)");
    }

    // ── ⑨ 검사일시가 **출시일보다 앞**(2026-01-01)이어도 표지(공정 0 + Status 0 + 검사 있음)면 비운다 ──
    //    판정은 날짜를 안 본다(`WashingProcessor.cpp` [사장님 선택 09-28 · 재론 금지]).
    //    ★'출시일 앞이면 제외' 같은 날짜 가드를 넣으면 이 CHECK 가 빨강이 된다.
    {
        as_type('W');
        touch(mgr);
        rtc_set(rel_date(10, 0, 0));                        // 세척기 시계는 정상
        scope(sc, 0x45, 45, 0, yday(9, 0), yday(9, 4),
              DateTime(TRACEQ_RELEASE_YEAR, 1, 1, 9, 0, 0), "UNSYNCSB");
        logs_clear();
        touch(sc, 2, 4);
        const bool wiped = !subj_is(sc, "UNSYNCSB") && get_ldt(sc, SECTOR1_GATEWAY).Date.Year == 0;
        tlog("  9 출시일 앞 검사일시: 비워짐=%u RW=%u\n", (unsigned)wiped, get_process(sc).Rewrite);
        CHECK(wiped, "9 출시일보다 앞선 검사일시(업그레이드 잔재·방전 게이트웨이)는 잔재로 보고 비운다");
    }

    // ⓪(게이트웨이 폴백 종단)은 여기서 뺐다 — 폴백 검사일시가 세척 시작에서 지워지는 것(선택 1 의 대가)은
    //  `t_ii6` F 가, 게이트웨이 폴백이 블록 5·섹터15 를 새로 쓰는 것은 `t_gg1b` G1 이 잠근다.

    // ── ⑪ 사실 기록: **덤프 전에 같은 날 두 번째 세척**을 해도 그 검사 정보는 남는다(덤프 전 · Status 1) ──
    //    (CHECK 없음 — 같은 주기 재세척 보존은 `t_ii1` B1 이 잠근다 · 실측 2차뒤보존=1)
    {
        as_type('W');
        touch(mgr);
        scope(sc, 0x47, 47, 1, yday(9, 0), yday(9, 4), rel_date(8, 0, 0), "ONEEXAM1");
        rtc_set(rel_date(10, 0, 0));
        touch(sc, 2, 4);                                    // 1차 세척 시작
        const bool kept1 = subj_is(sc, "ONEEXAM1");
        rtc_set(rel_date(10, 20, 0));
        touch(sc, 2, 4);                                    // 세척 종료
        as_type('D');
        touch(mgr);
        rtc_set(rel_date(10, 30, 0));
        touch(sc, 2, 4);                                    // 소독 시작
        rtc_set(rel_date(10, 50, 0));
        touch(sc, 2, 4);                                    // 소독 종료
        as_type('W');
        touch(mgr);
        rtc_set(rel_date(12, 0, 0));
        touch(sc, 2, 4);                                    // 덤프 없이 2차 세척 시작
        tlog("  11 덤프 전 재세척: 1차뒤보존=%u 2차뒤보존=%u RW=%u\n",
             (unsigned)kept1, (unsigned)subj_is(sc, "ONEEXAM1"), get_process(sc).Rewrite);
    }

    // ── ⑬ 지난 주기 세척 **종료**(블록14)가 **손상**(연도 0xFFFF)이어도 이번 검사를 지우지 않는다 ──
    //    표지 판정은 블록14 를 읽지 않고, 이 표본은 Status 1(환자 있음)이라 남는다(옛 `prev <= current` 상한은 걷어냈다).
    {
        as_type('W');
        touch(mgr);
        rtc_set(rel_date(10, 0, 0));
        scope(sc, 0x48, 48, 1, yday(9, 0), yday(9, 4), rel_date(8, 0, 0), "THISEX13");
        {   // 블록14 = {int 기기번호, LocalDateTime(연도 0xFFFF)}
            uint8_t buf[16]{};
            const int dev = 1;
            LocalDateTime t{LocalDate{0xFFFF, 9, 26}, LocalTime{9, 0, 0}};
            memcpy(buf, &dev, 2);
            memcpy(buf + 2, &t, sizeof(t));
            put_block(sc, SECTOR3_WASHING_END, buf, 16);
        }
        logs_clear();
        touch(sc, 2, 4);
        const LocalDateTime gw = get_ldt(sc, SECTOR1_GATEWAY);
        tlog("  13 손상 기준: 이번검사항목보존=%u 검사일시연도=%u\n",
             (unsigned)subj_is(sc, "THISEX13"), gw.Date.Year);
        CHECK(subj_is(sc, "THISEX13") && gw.Date.Year != 0,
              "13 지난 주기 세척 종료가 손상(연도 0xFFFF)이어도 이번 검사(Status 1)는 지우지 않는다");
    }

    done();
    for (;;) {}
}
