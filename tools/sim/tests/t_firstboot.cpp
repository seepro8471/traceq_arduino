// 새 기기 첫 부팅 — 시계가 방전 표지 시각이면 기본 액교환일(현재-1개월)은 시계가 복구될 때 정한다.
#include "common.h"

static bool clear_is(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi)
{
    const LocalDateTime t = disinfectionOption.GetClearDateTime();
    return t.Date.Year == y && t.Date.Month == mo && t.Date.Day == d && t.Time.Hour == h && t.Time.Minute == mi;
}
static bool clear_from_marker()   // 표지(2026-01-01)에서 파생된 기본값(2025-12-01)인가 — 시각과 무관
{
    const LocalDateTime t = disinfectionOption.GetClearDateTime();
    return t.Date.Year == 2025 && t.Date.Month == 12 && t.Date.Day == 1;
}
static void json(const char *s)
{
    serial_inject(s, strlen(s));
    GUARDED(serialEvent());
}

int main()
{
    // 공장 EEPROM(도장 없음) + 첫 전원(DS3231 OSF=1) → 완전 초기화
    g_rtcLostPower = true;
    GUARDED(setup());
    run_loops(2);
    tlog_ldt("첫 부팅 뒤 교환일", disinfectionOption.GetClearDateTime());
    CHECK(!clear_from_marker(), "첫 부팅(방전): 2025-12-01 을 기본값으로 쓰지 않음");

    json("{\"cmd\":\"cfg_set_date_time\",\"device_date_time\":\"2026-09-23 09:00:00\"}");
    run_loops(2);
    tlog_ldt("시계 맞춘 뒤 교환일", disinfectionOption.GetClearDateTime());
    CHECK(clear_is(2026, 8, 23, 9, 0), "첫 부팅(방전): 시계 복구 때 현재-1개월");

    // [회귀] 시계 정상인 기기에 새 버전 첫 부팅 → 즉시 현재-1개월
    EEPROM.put((int)4088, (uint32_t)0);
    g_rtcLostPower = false;
    rtc_set(DateTime(2026, 10, 31, 10, 0, 0));
    GUARDED(setup());
    run_loops(2);
    tlog_ldt("정상 첫 부팅 교환일", disinfectionOption.GetClearDateTime());
    CHECK(clear_is(2026, 9, 30, 10, 0), "회귀: 시계 정상 → 즉시 현재-1개월(10/31→9/30 말일 보정)");

    // [3차] 방전 첫 부팅 뒤 PC 접속(DTR 리셋) — 시계를 못 맞춘 채 리셋돼도 기본값을 표지로 박지 않는다
    EEPROM.put((int)4088, (uint32_t)0);
    hard_reset(true);
    hard_reset(false);                                     // 전원 유지 리셋(OSF 지워짐)
    sim_advance_ms(3UL * 1000);
    tlog_ldt("방전 첫 부팅 + DTR 리셋 뒤 교환일", disinfectionOption.GetClearDateTime());
    CHECK(!clear_from_marker() && disinfectionOption.HasPendingClear(), "첫 부팅(방전)+DTR 리셋: 여전히 미룸");
    json("{\"cmd\":\"cfg_set_date_time\",\"device_date_time\":\"2026-09-23 09:00:00\"}");
    run_loops(2);
    CHECK(clear_is(2026, 8, 23, 9, 0), "첫 부팅(방전)+DTR 리셋 뒤 시계 맞춤 → 현재-1개월");
    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
