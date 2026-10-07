// 15회차 사장님 결정 잠금 — A5(G2 없음/무효 → 게이트웨이 시계) · A7(일회성 설정 변경 시 표지 해제) · A9(afterDump 관문 유지) · C5(MaxCount 999 초과 → 0)
#include "common.h"

void handle_menu(UserInterface::MenuFunction function);   // main.cpp (전역 연결)

static SimCard mgr, sc;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', '1'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'K', 'I', 'M'};

static bool blk_is(const SimCard &c, uint8_t blk, const char *s) { return memcmp(c.data[blk], s, strlen(s)) == 0; }
static bool any_old(const SimCard &c)
{
    return blk_is(c, SECTOR15_EXAMINATION_SUBJECT, "OLDSUBJ") || blk_is(c, SECTOR15_EXAMINATION_SUBJECT2, "OLDSUBJ") ||
           blk_is(c, SECTOR15_EXAMINATION_SUBJECT3, "OLDSUBJ");
}
static void as(char type)
{
    deviceOption.SetType(type);
    hard_reset(false, 2);
    deviceOption.SetNumber(type == 'D' ? 3 : 1);
    managerOption.SetData(mk, mn);
    recordOption.SetManagerDisposability(false);
    recordOption.SetPatientCheck(false);
    disinfectionOption.SetSimultaneousDisinfectionSlot(0);
    disinfectionOption.SetMaximumCount(0);
}
static void fresh_scope(SimCard &t, uint8_t uid, int no)
{
    char id[10], ser[10];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(t, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(t, Process{0, 0, 0, 0, 0, false, 0, 0});
}

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');
    managerOption.SetData(mk, mn);
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");

    // ── A7: 일회성 담당자 ON → 담당자 태그(표지) → 메뉴에서 OFF 저장 → ON 저장 → 첫 시작은 담당자 없이 통과하면 안 된다 ──
    //  종전은 표지가 RAM 에 남아 통과했다(III-B P3-7 · 14차 이월). 메뉴 화면 버튼: R(>) S(값 전환) R R(save 칸) S(저장) = "RSRRS".
    {
        as('W');
        recordOption.SetManagerDisposability(true);
        touch(mgr);                                            // 일회성 표지 충전
        g_btnIdleLimit = 1000000UL;
        buttons_script("RSRRS");
        GUARDED(handle_menu(UserInterface::MenuFunction::ManagerDisposability));
        const bool off = !recordOption.GetManagerDisposability();
        buttons_script("RSRRS");
        GUARDED(handle_menu(UserInterface::MenuFunction::ManagerDisposability));
        const bool on = recordOption.GetManagerDisposability();
        run_loops(2);
        fresh_scope(sc, 0x31, 31);
        logs_clear(); buzz_clear();
        rtc_set(rel_date(10, 5, 0));
        touch(sc);
        const Process p = get_process(sc);
        tlog("  A7 메뉴 OFF=%d ON=%d → 시작: NoManager=%d RW=%u\n", off, on, lcd_has("No Manager Info"), p.Rewrite);
        CHECK(off && on, "A7 전제: 메뉴로 일회성 OFF 저장 → ON 저장이 됐다");
        CHECK(lcd_has("No Manager Info") && p.Rewrite == 0,
              "A7(15차) 설정을 바꾸면 앞서 세운 일회성 표지가 버려져 담당자 없이 첫 시작이 통과하지 않는다");
        // 대조: 다시 담당자를 대면 시작된다
        touch(mgr);
        logs_clear();
        touch(sc);
        CHECK(get_process(sc).Rewrite == 1, "A7 대조: 담당자를 다시 대면 시작된다");
        recordOption.SetManagerDisposability(false);
    }
    // ── A7e (17차 V-A U1 · V-H U9) 같은 값으로 저장만 하면 표지는 남는다 — "바뀌었을 때만" 을 지우면 ON 에서 메뉴만 열고 저장해도 다음 시작이 거부됐다 ──
    {
        as('W');
        recordOption.SetManagerDisposability(true);
        touch(mgr);                                            // 일회성 표지 충전
        g_btnIdleLimit = 1000000UL;
        buttons_script("RRRS");                                // 값은 그대로 · save 칸에서 저장
        GUARDED(handle_menu(UserInterface::MenuFunction::ManagerDisposability));
        const bool stillOn = recordOption.GetManagerDisposability();
        run_loops(2);
        fresh_scope(sc, 0x3E, 62);
        logs_clear(); buzz_clear();
        rtc_set(rel_date(10, 7, 0));
        touch(sc);
        tlog("  A7e 같은 값 저장: ON=%d NoManager=%d RW=%u\n", stillOn, lcd_has("No Manager Info"), get_process(sc).Rewrite);
        CHECK(stillOn && !lcd_has("No Manager Info") && get_process(sc).Rewrite == 1,
              "A7e(17차) 같은 값으로 저장만 하면 일회성 표지는 남아 첫 시작이 통과한다(표지는 설정이 바뀔 때만 버린다)");
        recordOption.SetManagerDisposability(false);
    }
    // ── A7d: 소독기 판 — 같은 배선의 형제(`disinfectionProcessor.ResetDisposability`) · 16차 IV-H: 이 줄만 지워도 초록이었다 ──
    {
        as('D');
        recordOption.SetManagerDisposability(true);
        touch(mgr);
        g_btnIdleLimit = 1000000UL;
        buttons_script("RSRRS");
        GUARDED(handle_menu(UserInterface::MenuFunction::ManagerDisposability));
        buttons_script("RSRRS");
        GUARDED(handle_menu(UserInterface::MenuFunction::ManagerDisposability));
        run_loops(2);
        make_tag(sc, 0x35, SCOPE_TYPE_TAG, 35, "SC0035", "S0035");         // 세척 끝난 스코프
        set_process(sc, Process{1, 0, 1, 0, 1, false, 0, 1});
        set_record(sc, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
        set_record(sc, SECTOR3_WASHING_END, 1, rel_date(9, 4, 0));
        logs_clear(); buzz_clear();
        rtc_set(rel_date(10, 20, 0));
        touch(sc);
        const Process p = get_process(sc);
        tlog("  A7d 소독기: ON=%d NoManager=%d DS=%u RW=%u\n", recordOption.GetManagerDisposability(), lcd_has("No Manager Info"),
             p.DisinfectionStatus, p.Rewrite);
        CHECK(recordOption.GetManagerDisposability() && lcd_has("No Manager Info") && p.DisinfectionStatus == 0,
              "A7d(16차) 소독기도 설정 변경이 일회성 표지를 버려 담당자 없이 소독 시작이 통과하지 않는다");
        recordOption.SetManagerDisposability(false);
    }

    // ── A9: 첫 세척의 섹터15 소거가 NACK 로 실패 → 세척 종료 → 소독 → **덤프 전 재세척** — 관문이 있어 검사는 남는다 ──
    //  (덤프 전은 절대 안 지운다 · 잃는 것은 찢긴 잔재 1주기 · 사장님 A9 (가)). 관문을 빼면 재세척이 지운다(III-H k2).
    {
        as('W');
        rtc_set(rel_date(11, 0, 0));
        fresh_scope(sc, 0x32, 32);                             // 덤프 뒤 상태(공정 0 · Status 0)에 검사 잔재
        set_record(sc, SECTOR1_GATEWAY, 7, rel_date(8, 0, 0));
        put_block(sc, SECTOR15_EXAMINATION_SUBJECT, "OLDSUBJ1", 8);
        put_block(sc, SECTOR15_EXAMINATION_SUBJECT2, "OLDSUBJ2", 8);
        sc.nackBlock = SECTOR15_EXAMINATION_SUBJECT2;
        touch(sc);                                             // 첫 세척 시작 — 소거 반쪽 · 블록5 남김 · 커밋
        sc.nackBlock = -1;
        const bool gwKept1 = get_ldt(sc, SECTOR1_GATEWAY).Date.Year != 0;
        const bool ws1 = get_process(sc).WashingStatus == 1;
        rtc_set(rel_date(11, 5, 0)); touch(sc);                // 세척 종료
        as('D');
        rtc_set(rel_date(11, 10, 0)); touch(sc);               // 소독 시작
        rtc_set(rel_date(11, 30, 0)); touch(sc);               // 소독 종료
        as('W');
        rtc_set(rel_date(11, 40, 0)); logs_clear(); touch(sc); // 덤프 전 재세척
        const Process p = get_process(sc);
        tlog("  A9 첫세척(NACK): 블록5남음=%d WS=%d · 재세척 뒤: 블록5년=%u 옛검사항목=%d WS=%u DS=%u\n", gwKept1, ws1,
             get_ldt(sc, SECTOR1_GATEWAY).Date.Year, any_old(sc), p.WashingStatus, p.DisinfectionStatus);
        CHECK(gwKept1 && ws1, "A9 전제: 첫 세척은 커밋됐고 섹터15 NACK 로 블록5 는 남았다");
        CHECK(get_ldt(sc, SECTOR1_GATEWAY).Date.Year != 0 && any_old(sc) && p.WashingStatus == 1,
              "A9(15차) 덤프 전 재세척은 잔재를 지우지 않는다(afterDump 관문 · 덤프 전은 절대 안 지운다)");
    }

    // ── A5: G2(검사일시) 없음/무효 → 게이트웨이 시계로 채움 · RTC 는 유효한 G2 로만 ──
    {
        deviceOption.SetType('G'); hard_reset(false, 2); deviceOption.SetNumber(7);
        rtc_set(DateTime(2026, 9, 23, 9, 0, 0));
        static const char noG2[] = "G10000;G3KEY9;NAME9;;G4SUBJ9;;;G5;";
        serial_inject(noG2, sizeof(noG2) - 1); GUARDED(serialEvent()); run_loops(1);
        fresh_scope(sc, 0x33, 33);
        logs_clear(); touch(sc);
        const LocalDateTime d1 = get_ldt(sc, SECTOR1_GATEWAY);
        const Process p1 = get_process(sc);
        tlog("  A5 G2 없음: 검사일시 %u-%02u-%02u %02u:%02u Status=%u 키=[%.5s]\n", d1.Date.Year, d1.Date.Month, d1.Date.Day,
             d1.Time.Hour, d1.Time.Minute, p1.Status, (const char *)sc.data[SECTOR2_PATIENT_KEY]);
        CHECK(p1.Status == 1 && d1.Date.Year == 2026 && d1.Date.Month == 9 && d1.Date.Day == 23 && d1.Time.Hour == 9,
              "A5(15차) G2 가 없는 환자 전문은 검사일시를 게이트웨이 시계로 채운다(0 이 아니다)");

        // 표본 RTC 를 2월 25일로 — 9월 23일이면 옛 코드도 RTC 를 안 바꿔(정규화 3월 2일이 더 이르다) "RTC 안 바꿈" 절반이 헛초록이었다(16차)
        rtc_set(DateTime(2026, 2, 25, 9, 0, 0));
        static const char badG2[] = "G10000;G22026;2;30;4;10;05;0;G3KEY8;NAME8;;G4SUBJ8;;;G5;";
        serial_inject(badG2, sizeof(badG2) - 1); GUARDED(serialEvent()); run_loops(1);
        const DateTime now = rtc.GetCurrentDateTime();
        fresh_scope(sc, 0x34, 34);
        logs_clear(); touch(sc);
        const LocalDateTime d2 = get_ldt(sc, SECTOR1_GATEWAY);
        tlog("  A5 G2 무효(2월 30일): RTC %u-%02u-%02u · 검사일시 %u-%02u-%02u\n", now.year(), now.month(), now.day(),
             d2.Date.Year, d2.Date.Month, d2.Date.Day);
        CHECK(now.month() == 2 && now.day() == 25 && d2.Date.Month == 2 && d2.Date.Day == 25,
              "A5b 무효 G2(2월 30일)는 RTC 를 안 바꾸고(종전: 3월 2일로 맞춤) 검사일시는 게이트웨이 시계(2월 25일)다");
    }

    // ── C5: MaxCount 999 초과 → 0(제한 없음) ──
    {
        disinfectionOption.SetMaximumCount(999);
        const int v999 = disinfectionOption.GetMaximumCount();
        disinfectionOption.SetMaximumCount(1000);
        const int v1000 = disinfectionOption.GetMaximumCount();
        disinfectionOption.SetMaximumCount(5);
        const int v5 = disinfectionOption.GetMaximumCount();
        tlog("  C5 999→%d 1000→%d 5→%d\n", v999, v1000, v5);
        CHECK(v999 == 999 && v1000 == 0 && v5 == 5, "C5(15차) MaxCount 는 999 까지 · 넘으면 0(제한 없음) — 세터+게터를 함께 지난 값(게터 상한이 세터를 가린다 · 17차)");
        // 게터도 같은 범위 — 구판 JSON 이 EEPROM 에 남긴 1234 가 상한으로 계속 쓰이고 제목이 4자리가 됐다(16차 IV-E/F)
        EEPROM.put((int)162, (int)1234);
        const int raw = disinfectionOption.GetMaximumCount();
        tlog("  C5b EEPROM 1234 → 게터=%d\n", raw);
        CHECK(raw == 0, "C5b(16차) 게터는 세터 범위 밖 EEPROM 값(1234)을 0(제한 없음)으로 읽는다");
        // 경계 1000(= 999+1) — 표본이 1234 뿐이면 경계를 1000 으로 민 변이가 초록(17차 V-E 안 잠김)
        {
            EEPROM.put((int)162, (int)1000);
            const int raw1000 = disinfectionOption.GetMaximumCount();
            EEPROM.put((int)162, (int)999);
            const int raw999 = disinfectionOption.GetMaximumCount();
            tlog("  C5c EEPROM 1000 → 게터=%d · 999 → %d\n", raw1000, raw999);
            CHECK(raw1000 == 0 && raw999 == 999, "C5c(17차) 게터 경계: 1000 은 0 · 999 는 999");
        }
        disinfectionOption.SetMaximumCount(0);
    }

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
