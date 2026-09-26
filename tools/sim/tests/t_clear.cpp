// 액교환일 — 시계가 방전 표지 시각(2026-01-01)인 동안 댄 클리어 태그는 시계가 복구될 때 기록한다.
#include "common.h"

static SimCard mgr, clr, sc;

static bool clear_is(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi)
{
    const LocalDateTime t = disinfectionOption.GetClearDateTime();
    return t.Date.Year == y && t.Date.Month == mo && t.Date.Day == d && t.Time.Hour == h && t.Time.Minute == mi;
}
static bool clear_on_marker_day()   // 표지 날짜(2026-01-01)에 찍혔는가 — 시각과 무관
{
    const LocalDateTime t = disinfectionOption.GetClearDateTime();
    return t.Date.Year == 2026 && t.Date.Month == 1 && t.Date.Day == 1;
}
static void reboot(bool dead) { hard_reset(dead); }   // 같은 기기 재부팅(EEPROM·RTC 칩·카드 유지, RAM 전부 새로)
static void json(const char *s)
{
    serial_inject(s, strlen(s));
    GUARDED(serialEvent());
}

int main()
{
    rtc_set(rel_date(7, 0, 0));
    boot('D');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");
    touch(mgr);
    make_tag(clr, 0x02, CLEAR_TYPE_TAG, 0, "", "");
    touch(clr);                                            // 정상 시계에서 교환(출시일 07:00)
    CHECK(clear_is(REL_YMD, 7, 0), "회귀: 시계 정상 → 교환일 즉시 기록");

    // ── 방전 부팅 → 클리어 → 첫 소독(시계 복구) ──
    reboot(true);                                          // 시계 2026-01-01 00:00
    sim_advance_ms(10UL * 60 * 1000);
    const int clearCountBefore = disinfectionOption.GetClearCount();
    touch(clr);
    tlog_ldt("방전 중 클리어 뒤 교환일", disinfectionOption.GetClearDateTime());
    CHECK(!clear_on_marker_day(), "방전: 표지 시각(2026-01-01)을 교환일로 쓰지 않음");
    CHECK(disinfectionOption.GetCount() == 0 && disinfectionOption.GetClearCount() == clearCountBefore + 1,
          "방전: 횟수 0·클리어 횟수 +1 은 즉시");

    make_tag(sc, 0x11, SCOPE_TYPE_TAG, 11, "SC0011", "S0011");
    set_process(sc, Process{1, 0, 1, 0, 1, false, 0, 1});
    set_record(sc, SECTOR2_WASHING_START, 1, rel_date(8, 0, 0));
    set_record(sc, SECTOR3_WASHING_END, 1, rel_date(8, 25, 0));
    touch(sc);                                             // 시계 → 08:26 복구
    tlog_ldt("첫 소독 뒤 교환일", disinfectionOption.GetClearDateTime());
    CHECK(clear_is(REL_YMD, 8, 26), "복구: 교환일 = 복구된 시각(세척종료+1분)");
    LocalDateTime det{};
    memcpy(&det, sc.data[SECTOR14_DISINFECTION_DETAIL], sizeof(det));
    tlog_ldt("첫 스코프 소독 상세 교환일", det);
    CHECK(ldt_eq(det, REL_YMD, 8, 26, 0), "복구: 첫 스코프 태그에도 복구된 교환일");

    // ── 방전 부팅 → 클리어 → PC 로 시계 맞춤 ──
    reboot(true);
    touch(clr);
    { char jb[96]; json(rel_json_set_time(jb, sizeof(jb), 9, 0, 0)); }
    run_loops(2);
    tlog_ldt("PC 동기 뒤 교환일", disinfectionOption.GetClearDateTime());
    CHECK(clear_is(REL_YMD, 9, 0), "복구: PC 로 맞추면 그 시각이 교환일");

    // ── [3차 C/D] 방전 부팅 → 클리어 → PC 접속(DTR 리셋, 배터리 살아 있음) → 그 뒤 시계 맞춤 ──
    //    RAM 표지였을 때: 리셋 뒤 lostPower=false 라 "맞춰짐"으로 오판 → 2026-01-01 이 교환일로 박혔다.
    reboot(true);
    touch(clr);
    reboot(false);                                         // 전원 유지 리셋 — OSF 는 이미 지워져 있다
    sim_advance_ms(5UL * 1000);
    tlog_ldt("DTR 리셋 뒤 교환일", disinfectionOption.GetClearDateTime());
    CHECK(!clear_on_marker_day() && disinfectionOption.HasPendingClear(),
          "DTR 리셋: 표지 시각으로 기록하지 않고 미룸 유지");
    { char jb[96]; json(rel_json_set_time(jb, sizeof(jb), 9, 30, 0)); }
    run_loops(2);
    CHECK(clear_is(REL_YMD, 9, 30), "DTR 리셋 뒤 시계 맞춤 → 그 시각이 교환일");

    // ── [3차 D] 메뉴에서 시간만 맞춘 것(날짜는 표지 그대로)은 동기가 아니다 ──
    reboot(true);
    touch(clr);
    rtc.SetDateTime(DateTime(2026, 1, 1, 9, 45, 0));      // SetDeviceTime 이 하는 일
    run_loops(2);
    CHECK(disinfectionOption.HasPendingClear(), "시간만 맞춤: 아직 미룸(표지 날짜)");
    rtc.SetDateTime(rel_date(9, 45, 0));     // 날짜까지 맞춤
    run_loops(2);
    CHECK(clear_is(REL_YMD, 9, 45), "날짜까지 맞춤 → 그 시각이 교환일");

    // ── [회귀] 시계 정상 재부팅 → 클리어 즉시 ──
    reboot(false);
    rtc_set(rel_date(10, 0, 0));
    touch(clr);
    CHECK(clear_is(REL_YMD, 10, 0), "회귀: 시계 정상 → 교환일 즉시");
    run_loops(3);
    CHECK(clear_is(REL_YMD, 10, 0), "회귀: 이후 루프가 교환일을 바꾸지 않음");
    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
