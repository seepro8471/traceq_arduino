// HH1(13회차) — v2.2.29 `washing_start` 의 '지난 검사 잔재 소거' 한 자리 재추적.
//  ① 커밋 전 실패 뒤 사람이 다시 대는 회복 경로에서 **이번 검사**가 잔재로 오판되는가
//  ② 블록5 가 쓰기 불가(손상)면 그 스코프가 영구히 세척 불가가 되는가 · 덤프·재발급으로 회복되나
//  ③ 섹터15 의 한 블록만 손상이면 어떤 상태로 굳나
//  ④ 검사일시 == 지난 세척 시작(같은 초) 경계
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
    //    첫 시도에서 블록10(세척 시작)은 이미 **이번 접촉 시각**으로 써졌다. 재시도의 잔재 판정이
    //    그 값을 '지난 주기 세척 시작' 으로 읽으면 이번 검사(오늘 08:00)가 잔재로 오판된다.
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

    // ── ④ `exam <= prev` 경계를 **양쪽으로**: 기준은 지난 주기 세척 **종료**(블록14)다 ──
    //    같은 초면 잔재(지운다) · 1초라도 뒤면 이번 검사(남긴다). 한쪽만 물으면 `<=`→`<` 변이가 안 잡힌다.
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

    // ── ⑨ 검사일시가 **출시일보다 앞**(2026-01-01) — 잔재로 판정해 비운다 ──
    //    업그레이드 직후 태그에 남은 **진짜 잔재**도 같은 모양이라 코드가 둘을 구별할 수 없다.
    //    제품 판정은 "진짜 잔재를 지우는 쪽"(`WashingProcessor.cpp` 의 [13차 판정 · 재론 금지]).
    //    ★그 쪽을 잠근다 — '출시일 앞이면 제외' 가드를 넣으면 이 CHECK 가 빨강이 되어 판정을 다시 읽게 된다.
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

    // ⓪(게이트웨이 폴백 종단)은 **뫐다** — 이 시험 장치에서 'G' 기기 폴백을 띄우지 못했다(전제 실패).
    //  같은 사실은 기존 `t_gg1b` G1 과 `t_subj` ② 가 이미 잠금다.

    // ── ⑪ 사실 기록: **덤프 전에 같은 날 두 번째 세척**을 하면 그 검사 정보는 규칙대로 잔재가 된다 ──
    //    (CHECK 로 잠그지 않는다 — 규칙 그대로의 결과라 사장님 판정 사항이다)
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

    // ── ⑬ 기준(지난 주기 세척 **종료** 블록14)이 **손상**(연도 0xFFFF → RTClib 이 2047 로 읽는다) ──
    //    손상값은 늘 미래라 `exam <= prev` 가 참이 되어 **이번 검사**를 잔재로 몰았다 → `prev <= current` 상한으로 막는다.
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
              "13 지난 주기 기록이 손상(연도 0xFFFF)이면 이번 검사를 잔재로 보지 않는다");
    }

    done();
    for (;;) {}
}
