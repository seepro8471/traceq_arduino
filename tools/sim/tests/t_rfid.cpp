// 세척기(W) + RFID 폴링 — A P2-1(올려 둔 카드 재처리) · B P2-1(세척 시작 실패 알림) 재현 + 회귀.
#include "common.h"

static SimCard mgr, foreign, a, b, c, d, e;

static uint8_t count_str(const char *hay, const char *needle)
{
    uint8_t n = 0;
    const size_t len = strlen(needle);
    for (const char *p = strstr(hay, needle); p; p = strstr(p + len, needle)) ++n;
    return n;
}
static void fresh(SimCard &t, uint8_t uid, int no, uint8_t status = 1)
{
    char id[8], ser[8];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(t, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(t, Process{status, 0, 0, 0, 0, false, 0, 0});
}

int main()
{
    rtc_set(DateTime(2026, 9, 22, 9, 0, 0));
    boot('W');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");
    touch(mgr);

    // ── [A P2-1] 키가 다른 카드를 올려 두면 한 번만 처리(사유 1회) ──
    {
        card_init_foreign(foreign, 0x09);
        logs_clear();
        card_place(&foreign);
        run_loops(30);
        card_remove();
        run_loops(4);
        const uint8_t n = count_str(g_serialOut, "AuthFailed");
        tlog("  타사 카드 30루프: AuthFailed %u회\n", n);
        CHECK(n == 1, "A2-1: 타사 카드 올려 둠 → 1회만 처리");
    }
    // ── [A P2-1] HaltA 프레임이 유실돼도 올려 둔 태그를 다시 처리하지 않는다 ──
    {
        fresh(a, 0x21, 21);
        a.loseHalt = true;
        logs_clear();
        card_place(&a);
        run_loops(12);
        card_remove();
        run_loops(4);
        const Process p = get_process(a);
        const uint8_t n = count_str(g_lcdLog, "00021");
        const int32_t dur = (DefaultRtc::ToDateTime(get_ldt(a, SECTOR3_WASHING_END)) -
                             DefaultRtc::ToDateTime(get_ldt(a, SECTOR2_WASHING_START))).totalseconds();
        tlog("  HaltA 유실: 처리 %u회, RW=%u, 세척시간 %ld초\n", n, p.Rewrite, (long)dur);
        CHECK(n == 1, "A2-1: HaltA 유실 → 1회만 처리");
        CHECK(dur >= 4L * 60 && dur < 5L * 60, "A2-1: HaltA 유실 → 세척 0분 종료 없음(자동종료 4분+초 유지)");
    }
    // ── [A P2-1] 커밋 뒤 쓰기 실패(카드 IDLE)로 끝나도 올려 둔 태그를 다시 처리하지 않는다 ──
    {
        sim_advance_ms(10UL * 60 * 1000);
        fresh(b, 0x22, 22);
        b.failWriteAt = 5;                              // 시작(1) 키(2) 이름(3) Process(4) 자동종료(5)
        logs_clear();
        card_place(&b);
        run_loops(12);
        card_remove();
        run_loops(4);
        const uint8_t n = count_str(g_lcdLog, "00022");
        tlog("  커밋 뒤 실패: 처리 %u회, RW=%u\n", n, get_process(b).Rewrite);
        CHECK(n == 1, "A2-1: 커밋 뒤 실패 → 1회만 처리(즉시 '종료' 로 재처리 없음)");
    }
    // ── [회귀] 떼었다 다시 대면 다시 처리된다 ──
    {
        sim_advance_ms(10UL * 60 * 1000);
        fresh(c, 0x23, 23);
        logs_clear();
        touch(c);
        sim_advance_ms(60UL * 1000);
        touch(c);
        const uint8_t n = count_str(g_lcdLog, "00023");
        const int32_t dur = (DefaultRtc::ToDateTime(get_ldt(c, SECTOR3_WASHING_END)) -
                             DefaultRtc::ToDateTime(get_ldt(c, SECTOR2_WASHING_START))).totalseconds();
        tlog("  재접촉: 처리 %u회, 세척시간 %ld초\n", n, (long)dur);
        CHECK(n == 2, "회귀: 떼고 다시 대면 다시 처리");
        CHECK(dur >= 60 && dur <= 62, "회귀: 두 번째 접촉 = 세척 종료");
    }
    // ── [회귀] 정상 태그는 올려 둬도 1회만(HALT) ──
    {
        sim_advance_ms(10UL * 60 * 1000);
        fresh(d, 0x24, 24);
        logs_clear();
        card_place(&d);
        run_loops(20);
        card_remove();
        run_loops(4);
        CHECK(count_str(g_lcdLog, "00024") == 1, "회귀: 정상 태그 올려 둠 → 1회만");
    }
    // ── [B P2-1] 세척 시작 기록 실패를 성공으로 알리지 않는다 ──
    {
        sim_advance_ms(10UL * 60 * 1000);
        fresh(e, 0x25, 25, 1);
        e.failWriteAt = 2;                              // 시작기록(1) 다음 매니저 키(2) 실패
        rtc.ClearAlarm(1);
        logs_clear();
        touch(e, 1, 4);
        const Process p = get_process(e);
        tlog("  세척 시작 실패: RW=%u buzz50=%u alarm=%d\n", p.Rewrite, buzz_count(50), rtc.HasAlarm(1));
        CHECK(p.Rewrite == 0, "B2-1W: 실패한 시작은 커밋 안 됨");
        CHECK(buzz_count(50) == 0, "B2-1W: 실패인데 성공음 없음");
        CHECK(!rtc.HasAlarm(1), "B2-1W: 실패인데 알람 등록 없음");
        CHECK(lcd_has("Write Error"), "B2-1W: LCD 에 Write Error");
        e.failWriteAt = 0;
        e.writeCount = 0;
        logs_clear();
        touch(e);
        CHECK(get_process(e).Rewrite == 1 && rtc.HasAlarm(1), "B2-1W: 재접촉 → 정상 시작");
    }
    // ── [09-23 결정] 세척기도 2초 안에 다시 대면 종료가 아니라 시작 다시 하기(소독기와 같음) ──
    {
        static SimCard f;
        sim_advance_ms(10UL * 60 * 1000);
        fresh(f, 0x26, 26);
        touch(f, 1, 4);                                // 시작
        touch(f, 1, 4);                                // 1초 안에 떼었다 다시 댐
        const int32_t dur = (DefaultRtc::ToDateTime(get_ldt(f, SECTOR3_WASHING_END)) -
                             DefaultRtc::ToDateTime(get_ldt(f, SECTOR2_WASHING_START))).totalseconds();
        tlog("  세척기 더블터치: RW=%u, 세척시간 %ld초, alarm=%d\n", get_process(f).Rewrite, (long)dur, rtc.HasAlarm(1));
        CHECK(get_process(f).Rewrite == 1 && dur >= 4L * 60 && dur < 5L * 60, "세척기 더블터치 → 0분 종료 아님(시작 유지)");
        CHECK(rtc.HasAlarm(1), "세척기 더블터치 → 알람 유지");
        sim_advance_ms(3UL * 1000);
        touch(f);                                      // 3초 뒤 → 정상 종료
        const int32_t dur2 = (DefaultRtc::ToDateTime(get_ldt(f, SECTOR3_WASHING_END)) -
                              DefaultRtc::ToDateTime(get_ldt(f, SECTOR2_WASHING_START))).totalseconds();
        CHECK(dur2 >= 3 && dur2 < 10 && !rtc.HasAlarm(1), "회귀: 2초 지나 대면 종료");
    }
    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
