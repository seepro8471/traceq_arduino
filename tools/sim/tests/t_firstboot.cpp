// 새 기기 첫 부팅 — 시계가 방전 표지 시각이면 기본 액교환일(현재-1개월)은 시계가 복구될 때 정한다.
#include "common.h"

static bool clear_is(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi)
{
    const LocalDateTime t = disinfectionOption.GetClearDateTime();
    return t.Date.Year == y && t.Date.Month == mo && t.Date.Day == d && t.Time.Hour == h && t.Time.Minute == mi;
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
    CHECK(!clear_is(2025, 12, 1, 0, 0), "첫 부팅(방전): 2025-12-01 을 기본값으로 쓰지 않음");

    json("{\"cmd\":\"cfg_set_date_time\",\"device_date_time\":\"2026-09-23 09:00:00\"}");
    run_loops(2);
    tlog_ldt("시계 맞춘 뒤 교환일", disinfectionOption.GetClearDateTime());
    CHECK(clear_is(2026, 8, 23, 9, 0), "첫 부팅(방전): 시계 복구 때 현재-1개월");

    // [회귀] 시계 정상인 기기에 새 버전 첫 부팅 → 즉시 현재-1개월
    EEPROM.put((int)4088, (uint32_t)0);
    g_rtcLostPower = false;
    rtc_set(DateTime(2026, 3, 31, 10, 0, 0));
    GUARDED(setup());
    run_loops(2);
    tlog_ldt("정상 첫 부팅 교환일", disinfectionOption.GetClearDateTime());
    CHECK(clear_is(2026, 2, 28, 10, 0), "회귀: 시계 정상 → 즉시 현재-1개월(3/31→2/28)");
    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
