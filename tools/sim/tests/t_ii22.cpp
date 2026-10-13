// 19회차 — 고침 둘의 잠금과, 판정 주석이 "실측" 이라 적고 시험이 없던 사실.
//  A 날짜·시간 메뉴: 안 고친 채 저장은 시계를 건드리지 않는다(자정을 걸치면 시계가 24시간 뒤로 갔다)
//  B 소독 새 시작은 그 스코프의 묵은 슬롯을 버린다(횟수 −1 · 그룹2 로 덮임 · 자기 시간창의 guest)
//  C 사실: 찢긴 재시작 뒤 창 밖 재접촉 = 종료 · 일회성 담당자 표지 잔존 · 막힘 중 같은 태그 무반응 · A8 이동 직후 튐 · 늦은 꼬리
#include "common.h"

static SimCard a, b, clr;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', 'H'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'H', 'O', 'N', 'G'};

static void to_type(char t)
{
    power_restore();
    deviceOption.SetType(t);
    hard_reset(false, 2);
    deviceOption.SetNumber(2);
    managerOption.SetData(mk, mn);
    recordOption.SetManagerDisposability(false);
    recordOption.SetPatientCheck(false);
}
static void dev_d(uint8_t slot)
{
    to_type('D');
    disinfectionOption.SetSimultaneousDisinfectionSlot(slot);
    disinfectionOption.SetSimultaneousDisinfectionDelay(30);
    disinfectionOption.SetMaximumCount(0);
    disinfectionOption.SetCount(0);
    alarmOption.SetTimeSlot2(18);
    make_tag(clr, 0x0A, CLEAR_TYPE_TAG, 0, "CLR", "CLR");
}
static void washed(SimCard &c, uint8_t uid, int no)   // 세척까지 마친 스코프
{
    make_tag(c, uid, SCOPE_TYPE_TAG, no, "SC", "SER");
    set_process(c, Process{1, 0, 1, 0, 1, false, 0, 1});
    set_record(c, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    set_record(c, SECTOR3_WASHING_END, 1, rel_date(9, 4, 0));
}
// 커밋(Process)은 닿고 확인 읽기만 끊긴 접촉 — Write Error 였나
static bool torn(SimCard &c)
{
    c.readErrBlock = SECTOR1_PROCESS; c.readErrSkip = 1; c.readErrTimes = 30;
    logs_clear(); buzz_clear(); touch(c, 1, 4);
    c.readErrBlock = -1; c.readErrTimes = 0; c.readErrSkip = 0;
    return lcd_has("Write Error");
}
static int grp(const SimCard &c, uint8_t block)
{
    DisinfectionDetail d{};
    memcpy(&d, c.data[block], sizeof(d));
    return d.GroupNumber;
}
static int32_t span_s(const SimCard &c, uint8_t from, uint8_t to)
{
    return (DefaultRtc::ToDateTime(get_ldt(c, to)) - DefaultRtc::ToDateTime(get_ldt(c, from))).totalseconds();
}
static DateTime at(const SimCard &c, uint8_t block, int32_t plus)
{
    return DateTime(DefaultRtc::ToDateTime(get_ldt(c, block)).unixtime() + plus);
}
static int32_t since(const DateTime &t0) { return (int32_t)(rtc_now_sim().unixtime() - t0.unixtime()); }

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');
    g_btnIdleLimit = 1000000UL;

    // ── A 날짜·시간 메뉴(19차) ──
    {
        const DateTime d0(TRACEQ_RELEASE_YEAR + 1, 3, 14, 23, 59, 50);
        rtc_set(d0);
        buttons_script("L");                                    // 커서를 저장으로
        buttons_at('S', g_ms + 20000UL);                        // 20초 뒤(자정 지나) 저장
        int r = GUARDED(ui.SetDeviceDate(rtc));
        const int32_t s1 = since(d0);
        CHECK(r == 0 && s1 >= 20 && s1 < 25, "A1 날짜 메뉴: 자정 전에 열어 안 고친 채 자정 뒤 저장해도 시계가 하루 뒤로 가지 않는다");

        rtc_set(d0);
        buttons_script("RRRRR" "SRRS" "RR");                    // 일의 자리 +2(14 → 16) · 커서를 저장으로
        buttons_at('S', g_ms + 20000UL);
        r = GUARDED(ui.SetDeviceDate(rtc));
        const DateTime n2 = rtc_now_sim();
        CHECK(r == 0 && n2.month() == 3 && n2.day() == 16 && n2.hour() == 0 && n2.minute() == 0,
              "A2 대조: 고친 날짜는 그대로 들어간다(시각은 지금 것)");

        const DateTime d3 = rel_date(10, 0, 0);
        rtc_set(d3);
        buttons_script("L");
        buttons_at('S', g_ms + 20000UL);
        r = GUARDED(ui.SetDeviceTime(rtc));
        const int32_t s3 = since(d3);
        CHECK(r == 0 && s3 >= 20 && s3 < 25, "A3 시간 메뉴: 안 고친 채 저장은 머문 만큼 시계를 뒤로 돌리지 않는다");

        rtc_set(d3);
        buttons_script("RRRRR" "SRRRS" "RR");                   // 초의 일의 자리 +3 → 10:00:03
        buttons_at('S', g_ms + 20000UL);
        r = GUARDED(ui.SetDeviceTime(rtc));
        const int32_t s4 = since(d3);
        tlog("  A 시계: 날짜 그대로 +%lds · 시간 그대로 +%lds · 시간 고침 +%lds\n", (long)s1, (long)s3, (long)s4);
        CHECK(r == 0 && s4 >= 3 && s4 < 8, "A4 대조: 고친 시각은 그대로 들어간다");
        rtc_set(d3);
    }

    // ── B1 묵은 host 슬롯(같은 스코프의 지난 주기 · 종료 접촉 없이 회수) → 찢긴 시작 → 창 안 재접촉: 횟수가 빠지지 않는다 ──
    {
        dev_d(0);
        washed(a, 0xA3, 113);
        rtc_set(rel_date(11, 0, 0)); touch(a);                  // 지난 주기 시작(횟수 1)
        washed(a, 0xA3, 113);                                   // 완료 처리·세척을 거쳐 다시 온 같은 스코프
        rtc_set(rel_date(12, 0, 0));
        const bool werr = torn(a);
        const bool committed = get_process(a).Rewrite == 2;
        sim_advance_ms(100); logs_clear(); buzz_clear(); touch(a);
        const bool restarted = buzz_count(50) == 1 && rtc.HasAlarm(2) && span_s(a, SECTOR5_DISINFECTION_START, SECTOR6_DISINFECTION_END) > 600;
        tlog("  B1 WE=%d 커밋=%d 재시작=%d 횟수=%d\n", werr, committed, restarted, disinfectionOption.GetCount());
        CHECK(werr && committed && restarted, "B1 전제: 시작 커밋은 닿고 Write Error · 창 안 재접촉은 재시작");
        CHECK(disinfectionOption.GetCount() == 2, "B1(19차) 지난 주기의 묵은 슬롯이 횟수 보정을 막지 않는다(1 → 2)");
    }
    // ── B2 동시소독: 묵은 guest 슬롯 → 이번 주기 host 시작이 찢김 → 재접촉이 커밋된 그룹1 을 그룹2 로 덮지 않는다 ──
    {
        dev_d(2); disinfectionOption.SetSimultaneousDisinfectionDelay(3);
        washed(a, 0xA1, 131); washed(b, 0xB1, 231);
        rtc_set(rel_date(11, 0, 0)); touch(a);                  // 지난 주기 host
        sim_advance_ms(20000); touch(b);                        // 지난 주기 guest(그룹 2 · 횟수 그대로 1)
        const bool wasGuest = grp(b, SECTOR14_DISINFECTION_DETAIL) == 2 && disinfectionOption.GetCount() == 1;
        washed(b, 0xB1, 231);
        rtc_set(rel_date(12, 0, 0));
        const bool werr = torn(b);                              // 창이 지났으니 host(그룹 1)로 커밋
        const int g1 = grp(b, SECTOR14_DISINFECTION_DETAIL);
        sim_advance_ms(100); logs_clear(); buzz_clear(); touch(b);
        tlog("  B2 guest였음=%d WE=%d 그룹 %d → %d 횟수=%d\n", wasGuest, werr, g1, grp(b, SECTOR14_DISINFECTION_DETAIL),
             disinfectionOption.GetCount());
        CHECK(wasGuest && werr && g1 == 1 && buzz_count(50) == 1, "B2 전제: 지난 주기 guest · 이번 주기 시작은 그룹1 로 커밋 뒤 Write Error");
        CHECK(grp(b, SECTOR14_DISINFECTION_DETAIL) == 1 && disinfectionOption.GetCount() == 2,
              "B2(19차) 묵은 guest 슬롯이 재시작을 guest 로 만들지 않는다(그룹 1 유지 · 횟수 +1)");
    }
    // ── B3 동시소독: 찢긴 이동을 10초 넘겨 다시 댄 2차 시작이 자기 시간창의 guest 가 되지 않는다 ──
    {
        dev_d(2);
        washed(a, 0xA6, 116);
        rtc_set(rel_date(12, 0, 0)); touch(a);                  // 1차 시작(host · 딜레이 30분)
        rtc_set(rel_date(12, 5, 0)); touch(clr);                // 액교환(횟수 0)
        sim_advance_ms(3000);
        const bool werr = torn(a);                              // 이동 커밋은 닿고 Write Error — 재확인의 뒷정리를 못 받는다
        const Process pm = get_process(a);
        sim_advance_ms(11000); logs_clear(); buzz_clear(); touch(a);   // 표지 창(10초) 밖 = 2차 시작
        const Process p2 = get_process(a);
        tlog("  B3 WE=%d MV=%u → RW=%u DC=%u 그룹2차=%d 횟수=%d\n", werr, pm.MovementNeeded, p2.Rewrite, p2.DisinfectionCount,
             grp(a, SECTOR14_DISINFECTION_DETAIL2), disinfectionOption.GetCount());
        CHECK(werr && pm.MovementNeeded && pm.Rewrite == 0 && p2.Rewrite == 2 && p2.DisinfectionCount == 2,
              "B3 전제: 찢긴 이동 뒤 11초 재접촉은 2차 시작");
        CHECK(grp(a, SECTOR14_DISINFECTION_DETAIL2) == 1 && disinfectionOption.GetCount() == 1,
              "B3(19차) 그 2차 시작은 host(그룹 1 · 횟수 +1) — 자기 묵은 슬롯의 guest 가 아니다");
    }
    // ── B4 찢긴 **재시작**은 슬롯을 버리지 않는다(버리면 다음 재접촉이 횟수를 한 번 더 올린다) + 사실: 창 밖 재접촉은 종료 ──
    {
        dev_d(0);
        washed(a, 0xA7, 117); washed(b, 0xA8, 118);
        rtc_set(rel_date(13, 0, 0)); touch(a);                  // 시작(횟수 1 · host)
        rtc_set(at(a, SECTOR5_DISINFECTION_START, 1));
        a.nackBlock = SECTOR5_DISINFECTION_START;
        logs_clear(); buzz_clear(); touch(a, 1, 4);             // 2초 안에 한 번 더 읽힘(재시작) — 첫 쓰기 거절
        const bool werrA = lcd_has("Write Error");
        a.nackBlock = -1;
        rtc_set(at(a, SECTOR5_DISINFECTION_START, 1));
        logs_clear(); buzz_clear(); touch(a);                   // 창 안 재접촉 = 재시작
        const bool okA = buzz_count(50) == 1 && rtc.HasAlarm(2);
        const int countA = disinfectionOption.GetCount();

        rtc_set(rel_date(13, 30, 0)); touch(b);                 // 다른 스코프 시작(횟수 2)
        rtc_set(at(b, SECTOR5_DISINFECTION_START, 1));
        b.nackBlock = SECTOR5_DISINFECTION_START;
        logs_clear(); buzz_clear(); touch(b, 1, 4);             // 찢긴 재시작
        const bool werrB = lcd_has("Write Error") && buzz_count(100) == 4;
        b.nackBlock = -1;
        rtc_set(at(b, SECTOR5_DISINFECTION_START, 3));
        logs_clear(); buzz_clear(); touch(b);                   // 창(2초) 밖 재접촉
        const int32_t durB = span_s(b, SECTOR5_DISINFECTION_START, SECTOR6_DISINFECTION_END);
        tlog("  B4 재시작 찢김 WE=%d → 재접촉 성공=%d 횟수=%d · 창 밖: WE=%d 성공음=%u 알람=%d 소독시간=%lds 횟수=%d\n", werrA, okA, countA,
             werrB, (unsigned)buzz_count(50), rtc.HasAlarm(2), (long)durB, disinfectionOption.GetCount());
        CHECK(werrA && okA && countA == 1, "B4(19차) 찢긴 재시작은 슬롯을 버리지 않는다 — 재접촉 재시작이 횟수를 또 올리지 않는다(1)");
        CHECK(werrB && buzz_count(50) == 1 && !rtc.HasAlarm(2) && durB >= 0 && durB <= 5 && disinfectionOption.GetCount() == 2,
              "C1b 사실: 소독기도 찢긴 재시작 뒤 창 밖 재접촉은 종료(성공음 · 3초짜리 기록 · 알람 해제 · 횟수 그대로)");
    }

    // ── B5 뒷정리(재확인 · 성공한 이동 · 종료)는 host 슬롯과 시간창을 비운다 — 그 창 안에 온 **다른** 스코프가 새 배치의 host 다 ──
    //  (자기 자신의 되넣기는 이제 새 시작이 묵은 슬롯을 버려서도 host 가 된다 — 17차 잠금 t_gg2m ⑫k 가 그것만 봐서 19차 뒤 헛것이 됐다)
    for (uint8_t k = 0; k < 3; ++k)                             // 0 = 찢긴 이동 뒤 재확인 · 1 = 성공한 이동 · 2 = 종료
    {
        dev_d(2);
        washed(a, (uint8_t)(0xC1 + k), 141 + k); washed(b, (uint8_t)(0xD1 + k), 241 + k);
        rtc_set(rel_date(12, 0, 0)); touch(a);                  // a = host(딜레이 30분 · 횟수 1)
        bool pre;
        if (k == 2)
        {
            rtc_set(rel_date(12, 2, 0)); logs_clear(); buzz_clear(); touch(a);   // 종료(재시작 창 밖)
            pre = buzz_count(50) == 1 && !rtc.HasAlarm(2);
        }
        else
        {
            rtc_set(rel_date(12, 5, 0)); touch(clr);            // 액교환(횟수 0)
            sim_advance_ms(3000);
            pre = (k == 0) ? torn(a) : true;                    // k=0: 이동 커밋 뒤 Write Error
            if (k == 0) sim_advance_ms(1500);
            logs_clear(); buzz_clear(); touch(a);               // k=0: 재확인 · k=1: 이동 성공
            pre = pre && buzz_count(50) == 1 && get_process(a).MovementNeeded && get_process(a).Rewrite == 0;
        }
        const int c0 = disinfectionOption.GetCount();
        sim_advance_ms(20000);
        logs_clear(); buzz_clear(); touch(b);                   // a 의 30분 창 안에 온 다른 스코프
        tlog("  B5[%u] 전제=%d → 다른 스코프: 그룹=%d 횟수 %d → %d\n", k, pre, grp(b, SECTOR14_DISINFECTION_DETAIL), c0, disinfectionOption.GetCount());
        CHECK(pre && grp(b, SECTOR14_DISINFECTION_DETAIL) == 1 && disinfectionOption.GetCount() == c0 + 1,
              k == 0 ? "B5 재확인이 host 슬롯·시간창을 비운다 — 그 창 안의 다른 스코프는 host(그룹 1 · 횟수 +1)"
              : k == 1 ? "B5 성공한 이동도 같다 — 그 창 안의 다른 스코프는 host"
                       : "B5 종료도 같다 — 끝난 배치의 창 안에 온 다른 스코프는 새 배치의 host");
    }

    // ── C1 사실(19차 정정): 세척 시작이 **성공한 뒤** 2초 안에 한 번 더 읽힌 접촉(재시작)이 어디서 끊기든, 창 밖 재접촉은 종료가 된다 ──
    //  18차까지 "실패음 뒤 종료가 되는 길은 첫 시작의 커밋 확인 절단 1자리뿐" 이라 적었다 — 길이 하나 더 있었다.
    to_type('W');
    {
        uint8_t cuts = 0, ends = 0;
        for (uint8_t n = 1; n < 60; ++n)
        {
            make_tag(a, (uint8_t)(0x40 + n), SCOPE_TYPE_TAG, 40, "SC", "SER");
            set_process(a, Process{1, 0, 0, 0, 0, false, 0, 0});
            rtc.ClearAlarm(1);
            rtc_set(rel_date(10, 0, 0));
            touch(a, 1, 4);                                     // 시작 성공(알람)
            rtc_set(at(a, SECTOR2_WASHING_START, 1));
            a.opCount = 0; a.removeAfterOps = n;
            logs_clear(); buzz_clear(); touch(a, 1, 4);         // 재시작 접촉이 n 동작 뒤 끊김
            const bool cut = a.opCount > n;
            const bool failBeep = buzz_count(100) == 4 && buzz_count(50) == 0;
            a.removeAfterOps = 0;
            if (!cut) break;
            ++cuts;
            rtc_set(at(a, SECTOR2_WASHING_START, 3));
            logs_clear(); buzz_clear(); touch(a);
            const int32_t dur = span_s(a, SECTOR2_WASHING_START, SECTOR3_WASHING_END);
            if (failBeep && buzz_count(50) == 1 && !rtc.HasAlarm(1) && dur >= 0 && dur <= 5) ++ends;
            else tlog("  C1 n=%u 실패음=%d 성공음=%u 알람=%d 세척시간=%lds\n", n, failBeep, (unsigned)buzz_count(50), rtc.HasAlarm(1), (long)dur);
        }
        tlog("  C1 재시작 이탈 %u자리 중 창 밖 재접촉이 종료=%u\n", cuts, ends);
        CHECK(cuts >= 20 && ends == cuts, "C1 사실: 찢긴 재시작(이탈 전 자리) 뒤 창 밖 재접촉은 종료(성공음 · 2~4초 기록 · 알람 해제)");
    }

    // ── C2 사실([18차 판정]): 일회성 담당자 — 시작 커밋은 닿고 확인만 끊긴 뒤 창 밖 재접촉(종료)은 표지를 안 쓴다 → 다음 스코프 하나가 통과 ──
    to_type('W');
    recordOption.SetManagerDisposability(true);
    {
        make_tag(clr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");
        touch(clr, 2, 2);                                       // 담당자 접촉 → 표지
        int16_t found = 0;
        for (int16_t n = 1; n <= 45 && !found; ++n)
        {
            sim_advance_ms(20000UL);
            make_tag(a, (uint8_t)(0x20 + n), SCOPE_TYPE_TAG, 20 + n, "SC", "SER");
            set_process(a, Process{0, 0, 0, 0, 0, false, 0, 0});
            a.removeAfterOps = n;
            buzz_clear(); touch(a, 2, 2);
            const Process p = get_process(a);
            if (p.WashingStatus == 1 && p.Rewrite == 1 && buzz_count(100) == 4) found = n;
            else if (buzz_count(50) >= 1 && buzz_count(100) == 0) break;      // 성공해 버렸다(표지 소모)
        }
        sim_advance_ms(3000);                                   // 재시작 창(2초)을 넘긴다
        a.removeAfterOps = 0;
        buzz_clear(); touch(a, 2, 2);                           // 종료로 처리된다
        const uint8_t endBeep = buzz_count(50);
        make_tag(b, 0x7A, SCOPE_TYPE_TAG, 90, "SC", "SER");
        set_process(b, Process{0, 0, 0, 0, 0, false, 0, 0});
        buzz_clear(); logs_clear(); touch(b, 2, 2);             // 다음 스코프 — 담당자 접촉 없이
        const uint8_t rwB = get_process(b).Rewrite;
        make_tag(b, 0x7B, SCOPE_TYPE_TAG, 91, "SC", "SER");
        set_process(b, Process{0, 0, 0, 0, 0, false, 0, 0});
        buzz_clear(); logs_clear(); touch(b, 2, 2);             // 그 다음 스코프 — 거부
        tlog("  C2 커밋 닿은 실패 자리 n=%d 종료 성공음=%u 다음 RW=%u 그 다음 RW=%u 거부=%d\n", found, endBeep, rwB, get_process(b).Rewrite,
             lcd_has("No Manager Info"));
        CHECK(found > 0 && endBeep == 1, "C2 전제: 시작 커밋은 닿고 실패음 · 3초 뒤 재접촉은 성공음(종료)");
        CHECK(rwB == 1 && get_process(b).Rewrite == 0 && lcd_has("No Manager Info"),
              "C2 사실: 표지가 남아 다음 스코프 하나가 담당자 접촉 없이 시작된다(그 다음은 No Manager Info)");
    }
    recordOption.SetManagerDisposability(false);

    // ── C3 사실(main.cpp 18차 사실): 실패음(막힘)이 끝나자마자 같은 태그를 다시 올려 들고 있으면 뗄 때까지 무반응 · 3 loop 뒤면 처리 ──
    to_type('W');
    {
        uint8_t held[2], again[2];
        for (uint8_t k = 0; k < 2; ++k)
        {
            sim_advance_ms(20000UL);
            make_tag(a, (uint8_t)(0x50 + k), SCOPE_TYPE_TAG, 50 + k, "SC", "SER");
            set_process(a, Process{0, 0, 0, 0, 0, false, 0, 0});
            a.removeAfterOps = 6;
            card_place(&a); run_loops(1);                       // 처리 중 이탈 → 실패음(이 loop 안에서 막힘)
            a.removeAfterOps = 0;
            run_loops(k ? 4 : 0);                               // 태그 없이 0 / 4 loop
            card_place(&a); run_loops(4);                       // 그대로 들고 있음
            held[k] = get_process(a).Rewrite;
            card_remove(); run_loops(4);
            card_place(&a); run_loops(2);                       // 뗐다 다시 댐
            again[k] = get_process(a).Rewrite;
            card_remove(); run_loops(4);
        }
        tlog("  C3 곧바로 다시 올림: 들고 있는 동안 RW=%u 뗐다 다시 RW=%u · 4 loop 뒤: RW=%u\n", held[0], again[0], held[1]);
        CHECK(held[0] == 0 && again[0] == 1, "C3 사실: 막힘이 끝나자마자 다시 올린 같은 태그는 들고 있는 동안 무반응 · 뗐다 다시 대면 처리");
        CHECK(held[1] == 1, "C3 대조: 리더가 떠난 것을 본 뒤(4 loop)에 올리면 바로 처리");
    }

    // ── C4 사실([18차 판정]): A8 회복 이동(남의 기기에서 시작한 1차 스코프) 직후 튐은 이 기기의 2차 시작 · 자기 기기 것은 재확인 ──
    dev_d(0);
    {
        rtc_set(rel_date(10, 20, 0));
        touch(clr, 2, 2);                                       // 액교환 → 이동 표시
        Process moved[2], bounce[2];
        for (uint8_t k = 0; k < 2; ++k)                         // k=0 남의 기기(1) · k=1 자기 기기(2)
        {
            sim_advance_ms(20000UL);
            const int machine = k ? 2 : 1;
            make_tag(a, (uint8_t)(0x61 + k), SCOPE_TYPE_TAG, 61 + k, "SC", "SER");
            set_process(a, Process{0, 1, 1, 1, machine, false, 0, 2});
            set_record(a, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
            set_record(a, SECTOR3_WASHING_END, 1, rel_date(9, 4, 0));
            set_record(a, SECTOR5_DISINFECTION_START, machine, rel_date(9, 50, 0));
            set_record(a, SECTOR6_DISINFECTION_END, machine, rel_date(10, 10, 0));
            card_place(&a); run_loops(1);
            moved[k] = get_process(a);
            card_remove(); run_loops(4);
            sim_advance_ms(300);
            card_place(&a); run_loops(1);                       // 튐
            bounce[k] = get_process(a);
            card_remove(); run_loops(4);
        }
        tlog("  C4 A8 이동 MV=%u RW=%u → 튐 DS=%u MN=%d RW=%u · 자기 기기 튐 DS=%u RW=%u\n", moved[0].MovementNeeded, moved[0].Rewrite,
             bounce[0].DisinfectionStatus, bounce[0].MachineNumber, bounce[0].Rewrite, bounce[1].DisinfectionStatus, bounce[1].Rewrite);
        CHECK(moved[0].MovementNeeded && moved[0].Rewrite == 0 && bounce[0].DisinfectionStatus == 2 && bounce[0].MachineNumber == 2 &&
              bounce[0].Rewrite == 2, "C4 사실: A8 회복 이동 직후 튐은 그 기기의 2차 시작이 된다");
        CHECK(moved[1].MovementNeeded && bounce[1].DisinfectionStatus == 1 && bounce[1].Rewrite == 0,
              "C4 대조: 자기 기기에서 시작한 스코프의 이동 직후 튐은 재확인(태그 불변)");
    }

    // ── C5 사실([18차 판정 ③]): G3~G5 만 든 늦은 꼬리 조각이 저장된 환자를 비운다(환자 빈칸 — 남의 환자가 아니다) ──
    to_type('G');
    {
        char pk[120];
        snprintf(pk, sizeof(pk), "G10002;G2%u;%u;%u;0;11;30;0;G3NEWKEY;NEWNAME;G4SUBJ1;SUBJ2;;G5",
                 (unsigned)TRACEQ_RELEASE_YEAR, (unsigned)TRACEQ_RELEASE_MONTH, (unsigned)TRACEQ_RELEASE_DAY);
        serial_inject(pk, strlen(pk));
        GUARDED(serialEvent());
        const bool had = gatewayProcessor.HasPatientInformation();
        const char *tail = "G3LATE;NAME;G4SUBJ;;;G5";
        serial_inject(tail, strlen(tail));
        GUARDED(serialEvent());
        CHECK(had && !gatewayProcessor.HasPatientInformation(), "C5 사실: 꼬리 조각(G3~G5)만 오면 저장된 환자가 비워진다");
    }
    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
