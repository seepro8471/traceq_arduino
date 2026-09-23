// 소독기(D) 시나리오 — B P2-1·P2-2·P2-3·P2-6 재현 + 확정 사양 회귀.
#include "common.h"

static SimCard mgr, clr, s1, s2, s3, s4, s5, s6, s7, s8, s9;

// 세척을 마친 스코프 태그(WS=1, RW=1). status=1 이면 환자정보 있음(성공음 경로).
static void washed(SimCard &c, uint8_t uid, int no, const DateTime &ws, const DateTime *we, uint8_t status = 1)
{
    char id[8], ser[8];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(c, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(c, Process{status, 0, 1, 0, 1, false, 0, 1});
    set_record(c, SECTOR2_WASHING_START, 1, ws);
    if (we) set_record(c, SECTOR3_WASHING_END, 1, *we);
}
static bool rtc_is(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi)
{
    const DateTime n = rtc_now_sim();
    return n.year() == y && n.month() == mo && n.day() == d && n.hour() == h && n.minute() == mi;
}

int main()
{
    rtc_set(DateTime(2026, 9, 22, 8, 0, 0));
    boot('D');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");
    touch(mgr);
    CHECK(managerOption.HasData(), "매니저 등록");

    // ── [사양 회귀] RTC 가 뒤처지면 세척 종료+1분으로 복구 ──
    {
        const DateTime ws(2026, 9, 22, 9, 0, 0), we(2026, 9, 22, 9, 4, 0);
        washed(s1, 0x11, 11, ws, &we);
        rtc_set(DateTime(2026, 1, 1, 0, 0, 5));
        touch(s1);
        tlog_ldt("s1 소독시작", get_ldt(s1, SECTOR5_DISINFECTION_START));
        CHECK(ldt_eq(get_ldt(s1, SECTOR5_DISINFECTION_START), 2026, 9, 22, 9, 5, 0), "RTC복구: 세척종료+1분(사양)");
        CHECK(rtc_is(2026, 9, 22, 9, 5), "RTC복구: 시계도 세척종료+1분");
    }
    // ── [B P2-3] 세척 종료 기록이 0 이면 "세척 시작 + 설정 세척시간 + 1분"(2043 고착 금지) ──
    {
        sim_advance_ms(10UL * 60 * 1000);
        alarmOption.SetTimeSlot1(10);                  // 설정된 세척 시간 10분
        const DateTime ws(2026, 9, 22, 11, 0, 0);
        washed(s2, 0x12, 12, ws, nullptr);
        rtc_set(DateTime(2026, 1, 1, 0, 0, 5));
        touch(s2);
        const DateTime n = rtc_now_sim();
        tlog("  RTC 뒤 = %04u-%02u-%02u %02u:%02u\n", n.year(), n.month(), n.day(), n.hour(), n.minute());
        CHECK(n.year() == 2026, "B2-3: 종료 0 → RTC 2043 고착 없음");
        CHECK(ldt_eq(get_ldt(s2, SECTOR5_DISINFECTION_START), 2026, 9, 22, 11, 11, 0),
              "B2-3: 종료 0 → 세척시작+설정 세척시간(10분)+1분");
    }
    // ── [B P2-3 변형] 종료 기록이 지난 주기(시작보다 이름)면 같은 추정 ──
    {
        const DateTime ws(2026, 9, 22, 13, 0, 0), stale(2026, 9, 21, 13, 4, 0);
        washed(s3, 0x13, 13, ws, &stale);
        rtc_set(DateTime(2026, 1, 1, 0, 0, 5));
        touch(s3);
        tlog_ldt("s3 소독시작", get_ldt(s3, SECTOR5_DISINFECTION_START));
        CHECK(ldt_eq(get_ldt(s3, SECTOR5_DISINFECTION_START), 2026, 9, 22, 13, 11, 0),
              "B2-3: 지난 종료 → 세척시작+설정 세척시간+1분");
        alarmOption.SetTimeSlot1(4);
    }

    // 이후 시나리오는 정상 시계에서
    rtc_set(DateTime(2026, 9, 22, 14, 0, 0));

    // ── [B P2-6] 일회성 매니저 거부 뒤에도 클리어로 세운 이동 플래그가 남으면 안 된다 ──
    {
        recordOption.SetManagerDisposability(true);
        make_tag(clr, 0x02, CLEAR_TYPE_TAG, 0, "", "");
        touch(clr);                                    // mMovable = true
        const DateTime ws(2026, 9, 22, 13, 30, 0), we(2026, 9, 22, 13, 34, 0);
        washed(s4, 0x14, 14, ws, &we);                 // 시작 상태 태그 — 매니저 없이 → 거부
        logs_clear();
        touch(s4);
        CHECK(lcd_has("No Manager Info"), "B2-6: 매니저 없이 시작 → 거부");
        washed(s5, 0x15, 15, ws, &we);                 // 종료 상태 태그(RW=2)
        set_process(s5, Process{1, 1, 1, 1, 1, false, 0, 2});
        touch(s5);
        const Process p = get_process(s5);
        tlog("  s5 MV=%u RW=%u\n", p.MovementNeeded, p.Rewrite);
        CHECK(p.MovementNeeded == 0 && p.Rewrite == 2, "B2-6: 거부 뒤 종료 태그가 이동 처리되지 않음");
        recordOption.SetManagerDisposability(false);
    }

    // ── [B P2-1] 시작 기록 실패를 성공으로 알리지 않는다 ──
    {
        sim_advance_ms(60UL * 1000);
        const DateTime ws(2026, 9, 22, 13, 40, 0), we(2026, 9, 22, 13, 44, 0);
        washed(s6, 0x16, 16, ws, &we, 1);
        s6.failWriteAt = 8;                            // Clear 6회 뒤 시작기록(7) 다음, 매니저 키(8) 실패
        rtc.ClearAlarm(2);
        logs_clear();
        touch(s6, 1, 4);                               // 한 번 처리하고 바로 뗀다
        const Process p = get_process(s6);
        tlog("  s6 RW=%u buzz50=%u alarm=%d lcd=[%s]\n", p.Rewrite, buzz_count(50), rtc.HasAlarm(2), g_lcdLog);
        CHECK(p.Rewrite == 1, "B2-1: 실패한 시작은 커밋 안 됨(RW=1 유지)");
        CHECK(buzz_count(50) == 0, "B2-1: 실패인데 성공음 없음");
        CHECK(!rtc.HasAlarm(2), "B2-1: 실패인데 알람 등록 없음");
        CHECK(lcd_has("Write Error"), "B2-1: LCD 에 Write Error");
        s6.failWriteAt = 0;
        s6.writeCount = 0;
        logs_clear();
        touch(s6);                                     // 다시 대면 정상 시작
        CHECK(get_process(s6).Rewrite == 2, "B2-1: 재접촉 → 정상 시작");
        CHECK(buzz_count(50) == 1, "B2-1: 재접촉 → 성공음 1회");
    }

    // ── [B P2-2] 동시소독 guest 가 2초 안에 다시 닿아도 0분 종료가 되면 안 된다 ──
    {
        disinfectionOption.SetSimultaneousDisinfectionSlot(2);
        sim_advance_ms(30UL * 60 * 1000);              // 이전 host 시간창 밖으로
        const DateTime ws(2026, 9, 22, 14, 0, 0), we(2026, 9, 22, 14, 4, 0);
        washed(s7, 0x17, 17, ws, &we);
        washed(s8, 0x18, 18, ws, &we);
        touch(s7);                                     // host
        sim_advance_ms(20UL * 1000);
        touch(s8, 1, 4);                               // guest 시작
        touch(s8, 1, 4);                               // 1초 안 재접촉(튐) — 떼고 다시 댄 것으로 인식될 만큼 뗀다
        const LocalDateTime st = get_ldt(s8, SECTOR5_DISINFECTION_START);
        const LocalDateTime en = get_ldt(s8, SECTOR6_DISINFECTION_END);
        const int32_t dur = (DefaultRtc::ToDateTime(en) - DefaultRtc::ToDateTime(st)).totalseconds();
        tlog_ldt("s8 시작", st);
        tlog_ldt("s8 종료", en);
        tlog("  s8 소독시간 %ld초\n", (long)dur);
        // 자동 종료 = 시작 + 18분 + 시작의 초(1.4.1 사양 — 인위적으로 보이지 않게)
        CHECK(dur >= 18L * 60 && dur < 19L * 60, "B2-2: guest 튐 → 0분 종료 아님(자동종료 18분 유지)");
        CHECK(rtc.HasAlarm(2), "B2-2: guest 튐 → 공용 알람 유지");
        disinfectionOption.SetSimultaneousDisinfectionSlot(1);
    }

    // ── [사양 회귀] host 더블터치 → 재시작, 횟수 1회만 ──
    {
        sim_advance_ms(30UL * 60 * 1000);
        const DateTime ws(2026, 9, 22, 15, 0, 0), we(2026, 9, 22, 15, 4, 0);
        washed(s9, 0x19, 19, ws, &we);
        const int before = disinfectionOption.GetCount();
        touch(s9, 1, 4);
        touch(s9, 1, 4);
        tlog("  count %d -> %d, RW=%u\n", before, disinfectionOption.GetCount(), get_process(s9).Rewrite);
        CHECK(disinfectionOption.GetCount() == before + 1, "회귀: host 더블터치 → 횟수 +1");
        CHECK(get_process(s9).Rewrite == 2, "회귀: host 더블터치 → 시작 상태");
        sim_advance_ms(20UL * 60 * 1000);
        touch(s9);
        const LocalDateTime e = get_ldt(s9, SECTOR6_DISINFECTION_END);
        tlog_ldt("s9 종료", e);
        CHECK(!rtc.HasAlarm(2), "회귀: 종료 터치 → 알람 해제");
        CHECK(disinfectionOption.GetCount() == before + 1, "회귀: 종료는 횟수 불변");
    }
    // ── [3차 B] 커밋은 됐는데 확인 읽기가 실패(Write Error) → 곧바로 다시 대면 종료가 아니라 재시작 ──
    {
        static SimCard v;
        sim_advance_ms(30UL * 60 * 1000);
        const DateTime ws(2026, 9, 22, 16, 0, 0), we(2026, 9, 22, 16, 4, 0);
        washed(v, 0x1A, 26, ws, &we);
        v.readErrBlock = SECTOR1_PROCESS;
        v.readErrSkip = 1;                             // 시작 전 Process 읽기는 통과
        v.readErrTimes = 3;                            // 커밋 쓰기 뒤 확인 읽기 3회 전부 실패 → VerifyMismatch
        rtc.ClearAlarm(2);
        logs_clear();
        touch(v, 1, 4);
        tlog("  확인 실패: RW=%u lcd Write Error=%d\n", get_process(v).Rewrite, lcd_has("Write Error"));
        CHECK(get_process(v).Rewrite == 2 && lcd_has("Write Error"), "3차B: 카드엔 커밋됐지만 Write Error");
        touch(v, 1, 4);                                // 1초 안 재접촉
        const int32_t dur = (DefaultRtc::ToDateTime(get_ldt(v, SECTOR6_DISINFECTION_END)) -
                             DefaultRtc::ToDateTime(get_ldt(v, SECTOR5_DISINFECTION_START))).totalseconds();
        tlog("  재접촉 뒤 소독시간 %ld초 alarm=%d\n", (long)dur, rtc.HasAlarm(2));
        CHECK(dur >= 18L * 60 && dur < 19L * 60 && rtc.HasAlarm(2), "3차B: 재접촉 = 재시작(1초 종료 아님)");
    }
    // ── [3차 B] 재시작 도중 실패해도 커밋된 시작 기록은 남는다(지우고 시작하지 않는다) ──
    {
        static SimCard w;
        sim_advance_ms(30UL * 60 * 1000);
        const DateTime ws(2026, 9, 22, 17, 0, 0), we(2026, 9, 22, 17, 4, 0);
        washed(w, 0x1B, 27, ws, &we);
        touch(w, 1, 4);                                // 정상 시작(커밋)
        const LocalDateTime first = get_ldt(w, SECTOR5_DISINFECTION_START);
        w.readErrBlock = SECTOR14_DISINFECTION_DETAIL; // 재시작에서 상세 블록 읽기가 실패(옛 코드는 그 전에 섹터 5·6 을 지웠다)
        w.readErrTimes = 2;
        touch(w, 1, 4);                                // 1초 안 재접촉 → 재시작 → 실패
        const LocalDateTime after = get_ldt(w, SECTOR5_DISINFECTION_START);
        tlog_ldt("재시작 실패 뒤 시작", after);
        CHECK(after.Date.Year == first.Date.Year && after.Time.Hour == first.Time.Hour && after.Time.Minute == first.Time.Minute,
              "3차B: 재시작 실패 → 원래 시작 기록 보존(0 아님)");
        CHECK(get_process(w).Rewrite == 2, "3차B: 태그는 여전히 시작 상태");
    }
    // ── [3차] 시작 직후 리더기가 재부팅돼도(RAM 없음) 2초 안 재접촉은 재시작 ──
    {
        static SimCard x;
        sim_advance_ms(30UL * 60 * 1000);
        const DateTime ws(2026, 9, 22, 18, 0, 0), we(2026, 9, 22, 18, 4, 0);
        washed(x, 0x1C, 28, ws, &we);
        touch(x, 1, 2);
        hard_reset(false);                             // 전원 유지 리셋
        touch(mgr);                                    // 담당자 재등록(EEPROM 은 유지되나 절차 그대로)
        touch(x, 1, 4);
        const int32_t dur = (DefaultRtc::ToDateTime(get_ldt(x, SECTOR6_DISINFECTION_END)) -
                             DefaultRtc::ToDateTime(get_ldt(x, SECTOR5_DISINFECTION_START))).totalseconds();
        tlog("  재부팅 사이 더블터치: 소독시간 %ld초\n", (long)dur);
        CHECK(dur >= 18L * 60 && dur < 19L * 60, "3차: 재부팅 사이 더블터치도 재시작(태그가 기억)");
    }
    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
