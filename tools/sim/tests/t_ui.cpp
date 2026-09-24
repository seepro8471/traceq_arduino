// 메뉴·설정 — D P2-1(숫자 저장 절단) · D P2-2(날짜 월말) · D P2-3(재귀 깊이) · E P2-3(JSON 좁힘) 재현 + 회귀.
#include "common.h"

extern UserInterface ui;

static bool rtc_date_is(uint16_t y, uint8_t m, uint8_t d)
{
    const DateTime n = rtc_now_sim();
    return n.year() == y && n.month() == m && n.day() == d;
}
static void json(const char *s)
{
    serial_inject(s, strlen(s));
    GUARDED(serialEvent());
}

int main()
{
    rtc_set(DateTime(2026, 9, 22, 10, 0, 0));
    boot('D');

    // ── [D P2-1] 세 자리 값을 그대로 저장하면 그대로 남아야 한다 ──
    disinfectionOption.SetMaximumCount(150);
    buttons_script("LS");
    GUARDED(ui.SetDisinfectionMaximumCount(disinfectionOption));
    tlog("  MaxCount 150 저장 → %d\n", disinfectionOption.GetMaximumCount());
    CHECK(disinfectionOption.GetMaximumCount() == 150, "D2-1: MaxCount 150 유지");

    disinfectionOption.SetSimultaneousDisinfectionDelay(120);
    buttons_script("LS");
    GUARDED(ui.SetDisinfectionGroupDelay(disinfectionOption));
    tlog("  Delay 120 저장 → %u\n", disinfectionOption.GetSimultaneousDisinfectionDelay());
    CHECK(disinfectionOption.GetSimultaneousDisinfectionDelay() == 120, "D2-1: Delay 120 유지");

    alarmOption.SetTimeSlot2(120);
    buttons_script("LS");
    GUARDED(ui.SetRecordAlarmTimeSlot('D', alarmOption));
    tlog("  AlarmTime 120 저장 → %d\n", alarmOption.GetTimeSlot2());
    CHECK(alarmOption.GetTimeSlot2() == 120, "D2-1: AlarmTime 120 유지");

    deviceOption.SetNumber(300);
    buttons_script("LS");
    GUARDED(ui.SetDeviceNumber(deviceOption));
    tlog("  Number 300 저장 → %d\n", deviceOption.GetNumber());
    CHECK(deviceOption.GetNumber() == 300, "D2-1: Number 300 유지");
    deviceOption.SetNumber(1);

    // [회귀] 두 자리 편집: 30 → 45
    disinfectionOption.SetMaximumCount(30);
    buttons_script("SRS" "RSRRRRRS" "LLS");
    GUARDED(ui.SetDisinfectionMaximumCount(disinfectionOption));
    tlog("  MaxCount 30 편집 → %d\n", disinfectionOption.GetMaximumCount());
    CHECK(disinfectionOption.GetMaximumCount() == 45, "회귀: 두 자리 편집 30→45");
    // [회귀] Range 2 유지
    disinfectionOption.SetSimultaneousDisinfectionSlot(2);
    buttons_script("LS");
    GUARDED(ui.SetDisinfectionRange(disinfectionOption));
    CHECK(disinfectionOption.GetSimultaneousDisinfectionSlot() == 2, "회귀: Range 2 유지");
    disinfectionOption.SetSimultaneousDisinfectionSlot(1);

    // ── [D P2-2] 달력에 없는 날짜는 RTC 에 쓰지 않는다 ──
    rtc_set(DateTime(2026, 2, 28, 12, 0, 0));
    logs_clear();
    buttons_script("RRRRSRS" "RSRRS" "LLLLLLS");          // 260228 → 260230
    GUARDED(ui.SetDeviceDate(rtc));
    tlog("  2/30 저장 뒤 RTC %02u/%02u\n", rtc_now_sim().month(), rtc_now_sim().day());
    CHECK(rtc_date_is(2026, 2, 28) && lcd_has("Invalid Date"), "D2-2: 2026-02-30 거부");

    rtc_set(DateTime(2027, 2, 28, 12, 0, 0));
    logs_clear();
    buttons_script("RRRRRSRS" "LLLLLLS");                 // 270228 → 270229 (평년)
    GUARDED(ui.SetDeviceDate(rtc));
    CHECK(rtc_date_is(2027, 2, 28) && lcd_has("Invalid Date"), "D2-2: 평년 2027-02-29 거부");

    rtc_set(DateTime(2028, 2, 28, 12, 0, 0));
    buttons_script("RRRRRSRS" "LLLLLLS");                 // 280228 → 280229 (윤년)
    GUARDED(ui.SetDeviceDate(rtc));
    CHECK(rtc_date_is(2028, 2, 29), "회귀: 윤년 2028-02-29 저장");
    rtc_set(DateTime(2026, 9, 22, 10, 0, 0));

    // ── [E P2-3] JSON 값은 좁히기 전에 범위를 자른다 ──
    json("{\"cmd\":\"cfg_set_config\",\"device_type\":\"D\",\"device_number\":1,"
         "\"device_date_time\":\"2026-09-22 10:00:00\",\"alarm_sound\":true,\"washing_time\":200,"
         "\"df_time\":18,\"df_max_cnt\":30,\"df_sim_delay\":300,\"df_sim_slot\":1,\"df_clear_cnt\":0,"
         "\"patient_check\":true}");
    tlog("  JSON washing_time 200 → %d, df_sim_delay 300 → %u\n", alarmOption.GetTimeSlot1(),
         disinfectionOption.GetSimultaneousDisinfectionDelay());
    CHECK(alarmOption.GetTimeSlot1() == 127, "E2-3: washing_time 200 → 상한 127");
    CHECK(disinfectionOption.GetSimultaneousDisinfectionDelay() == 120, "E2-3: df_sim_delay 300 → 상한 120");
    json("{\"cmd\":\"cfg_set_config\",\"device_type\":\"D\",\"device_number\":1,"
         "\"device_date_time\":\"2026-09-22 10:00:00\",\"alarm_sound\":true,\"washing_time\":4,"
         "\"df_time\":18,\"df_max_cnt\":30,\"df_sim_delay\":3,\"df_sim_slot\":1,\"df_clear_cnt\":0,"
         "\"patient_check\":true}");
    CHECK(alarmOption.GetTimeSlot1() == 4 && disinfectionOption.GetSimultaneousDisinfectionDelay() == 3,
          "회귀: JSON 정상 범위 그대로");

    // ── [3차 D/E] str_atoi 오버플로 검사가 실제로 동작한다(부호 오버플로 UB 로 컴파일러가 지웠던 것) ──
    tlog("  str_atoi: 70000→%d 65536→%d 32767→%d 32768→%d range(70000)→%d\n",
         str_atoi("70000"), str_atoi("65536"), str_atoi("32767"), str_atoi("32768"), str_atoi_range("70000", 0, 4));
    CHECK(str_atoi("70000") == -1 && str_atoi("65536") == -1 && str_atoi("32768") == -1, "3차: str_atoi 32767 초과는 -1");
    CHECK(str_atoi("32767") == 32767 && str_atoi("0") == 0 && str_atoi("007") == 7, "회귀: str_atoi 정상 범위");
    CHECK(str_atoi_range("70000", 0, 4) == -1 && str_atoi_range("2026", 0, 3) == 2026, "3차: str_atoi_range 도 같은 검사");

    // ── [D P2-3] 메뉴 페이지를 60번 넘겨도 스택이 쌓이지 않는다 ──
    {
        static char seq[200];
        size_t k = 0;
        seq[k++] = 'S';                                   // 홈에서 메뉴 진입
        for (uint8_t i = 0; i < 60; ++i) { seq[k++] = 'L'; seq[k++] = 'S'; }   // '>' 다음 페이지
        seq[k++] = 'L'; seq[k++] = 'L'; seq[k++] = 'S';  // home
        seq[k] = 0;
        buttons_script(seq);
        g_minSP = 0xFFFF;
        g_resetArmed = true;
        const int r = setjmp(g_resetJmp);
        if (r == 0) loop();
        g_resetArmed = false;
        const uint16_t depth = SIM_STACK_TOP - g_minSP;
        tlog("  페이지 60회: 스택 %u B (r=%d)\n", depth, r);
        CHECK(r == 0, "D2-3: 메뉴 60페이지 넘김 뒤 정상 복귀");
        CHECK(depth < 1200, "D2-3: 페이지 넘김 횟수와 무관한 스택");
    }
    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
