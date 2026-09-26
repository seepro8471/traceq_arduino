// 새 기기 첫 부팅 — 시계가 방전 표지 시각이면 기본 액교환일(현재-1개월)은 시계가 복구될 때 정한다.
#include "common.h"

static bool clear_is(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi)
{
    const LocalDateTime t = disinfectionOption.GetClearDateTime();
    return t.Date.Year == y && t.Date.Month == mo && t.Date.Day == d && t.Time.Hour == h && t.Time.Minute == mi;
}

// 출시일 h:mi 의 한 달 전 — 정본 OneMonthBefore 로 유도(말일 보정 자체는 아래 10/31→9/30 검사가 명시값으로 본다).
static bool clear_is_rel_minus_month(uint8_t h, uint8_t mi)
{
    const LocalDateTime e = DisinfectionOption::OneMonthBefore(DefaultRtc::ToLocalDateTime(rel_date(h, mi, 0)));
    return clear_is(e.Date.Year, e.Date.Month, e.Date.Day, h, mi);
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
    // 공장 EEPROM(도장 없음) + 첫 전원(DS3231 OSF=1) → 묻지 않고 완전 초기화
    eeprom_factory();
    g_rtcLostPower = true;
    logs_clear();
    GUARDED(setup());
    // 전체 소거(≈14초)는 진행을 보여 준다 — 화면이 비어 있으면 고장으로 보인다(4차 D).
    CHECK(lcd_has("Initializing") && lcd_has("100%"), "첫 부팅 전체 소거: 'Initializing' 과 진행률(100%)을 보여 준다");
    run_loops(2);
    tlog_ldt("첫 부팅 뒤 교환일", disinfectionOption.GetClearDateTime());
    CHECK(!clear_from_marker(), "첫 부팅(방전): 2025-12-01 을 기본값으로 쓰지 않음");

    { char jb[96]; json(rel_json_set_time(jb, sizeof(jb), 9, 0, 0)); }
    run_loops(2);
    tlog_ldt("시계 맞춘 뒤 교환일", disinfectionOption.GetClearDateTime());
    CHECK(clear_is_rel_minus_month(9, 0), "첫 부팅(방전): 시계 복구 때 현재-1개월");

    // [회귀] 시계 정상인 공장 기기에 새 버전 첫 부팅 → 즉시 현재-1개월
    eeprom_factory();
    g_rtcLostPower = false;
    rtc_set(DateTime(2026, 10, 31, 10, 0, 0));
    GUARDED(setup());
    run_loops(2);
    tlog_ldt("정상 첫 부팅 교환일", disinfectionOption.GetClearDateTime());
    CHECK(clear_is(2026, 9, 30, 10, 0), "회귀: 시계 정상 → 즉시 현재-1개월(10/31→9/30 말일 보정)");

    // [3차] 방전 첫 부팅 뒤 PC 접속(DTR 리셋) — 시계를 못 맞춘 채 리셋돼도 기본값을 표지로 박지 않는다
    eeprom_factory();
    hard_reset(true);
    hard_reset(false);                                     // 전원 유지 리셋(OSF 지워짐)
    sim_advance_ms(3UL * 1000);
    tlog_ldt("방전 첫 부팅 + DTR 리셋 뒤 교환일", disinfectionOption.GetClearDateTime());
    CHECK(!clear_from_marker() && disinfectionOption.HasPendingClear(), "첫 부팅(방전)+DTR 리셋: 여전히 미룸");
    { char jb[96]; json(rel_json_set_time(jb, sizeof(jb), 9, 0, 0)); }
    run_loops(2);
    CHECK(clear_is_rel_minus_month(9, 0), "첫 부팅(방전)+DTR 리셋 뒤 시계 맞춤 → 현재-1개월");
    // ── [2.2.9] 쓰던 기기에 새 버전 업로드 — 묻고, 무응답이면 유지 ──
    {
        eeprom_factory();
        g_rtcLostPower = false;
        rtc_set(rel_date(10, 0, 0));
        GUARDED(setup());                              // 공장 → 묻지 않고 초기화
        run_loops(2);
        deviceOption.SetType('D');
        deviceOption.SetNumber(7);
        alarmOption.SetTimeSlot1(9);
        disinfectionOption.SetCount(33);
        disinfectionOption.SetClearDateTime(LocalDateTime{LocalDate{2026, 5, 6}, LocalTime{7, 8, 9}});
        EEPROM.update(177, 0xFF);                      // 1.4.1 이 안 쓰던 자리의 쓰레기값
        EEPROM.put((int)4088, (uint32_t)0);            // 새 펌웨어 업로드 흉내
        hard_reset(false, 0);                          // 버튼 무응답 → 유지 (loop 전 상태를 본다)
        const uint8_t pendingAfterBoot = disinfectionOption.GetClearPending();
        run_loops(2);
        tlog("  유지: type=%c no=%d wash=%d cnt=%d pending=%u\n",
             deviceOption.GetType(), deviceOption.GetNumber(),
             alarmOption.GetTimeSlot1(), disinfectionOption.GetCount(), pendingAfterBoot);
        CHECK(deviceOption.GetType() == 'D' && deviceOption.GetNumber() == 7 &&
              alarmOption.GetTimeSlot1() == 9 && disinfectionOption.GetCount() == 33,
              "2.2.9: 무응답 → 설정 유지(타입·번호·세척시간·소독 횟수)");
        CHECK(clear_is(2026, 5, 6, 7, 8), "2.2.9: 유지 → 액교환일 그대로");
        CHECK(pendingAfterBoot == 0, "2.2.9: 유지 → 부팅 중에 177번지 쓰레기값 정리(액교환일 덮어쓰기 방지)");
    }

    // ── [2.2.9] 정상 미룸(시계 못 맞춘 기기)은 유지해도 살아남는다 ──
    {
        disinfectionOption.SetClearPending(DisinfectionOption::kPendingNow);
        EEPROM.put((int)4088, (uint32_t)0);
        hard_reset(false, 0);
        const uint8_t pending = disinfectionOption.GetClearPending();
        run_loops(2);
        tlog("  유지 뒤 미룸 = %u\n", pending);
        CHECK(pending == DisinfectionOption::kPendingNow, "2.2.9: 유지 → 정상 미룸(1)은 버리지 않는다");
    }

    // ── [2.2.9] RIGHT 이 한 표본만 튀어도 지우지 않는다(되돌릴 수 없는 동작) ──
    {
        const int noBefore = deviceOption.GetNumber();
        EEPROM.put((int)4088, (uint32_t)0);
        buttons_script("rR");                          // 오른쪽으로만 옮김 — MENU 로 확정하지 않으면 안 지워진다
        hard_reset(false);
        tlog("  튐 1회 뒤: no=%d\n", deviceOption.GetNumber());
        CHECK(deviceOption.GetNumber() == noBefore, "2.2.9: > 로 옮기기만 하면 초기화되지 않음(MENU 필요)");
    }

    // ── [2.2.9] RIGHT 을 눌러 두면 완전 초기화 ──
    {
        EEPROM.put((int)4088, (uint32_t)0);
        buttons_script("rRS");                         // 뗌 → 오른쪽으로 옮김 → MENU 로 확정
        hard_reset(false);
        tlog("  초기화: type=%c no=%d wash=%d cnt=%d\n", deviceOption.GetType(),
             deviceOption.GetNumber(), alarmOption.GetTimeSlot1(), disinfectionOption.GetCount());
        CHECK(deviceOption.GetNumber() == 1 && alarmOption.GetTimeSlot1() == 4 &&
              disinfectionOption.GetCount() == 0, "2.2.9: > 로 옮겨 MENU → 완전 초기화(기본값으로)");
    }

    // ── [2.2.9] 같은 판이어도 전원 켤 때 RIGHT 을 누르고 있으면 다시 고를 수 있다(유지 뒤 되돌릴 길) ──
    {
        deviceOption.SetNumber(5);
        alarmOption.SetTimeSlot1(11);
        // 도장은 그대로(=같은 판). 누른 채 켜면 물어보고, 뗀 뒤 아무것도 안 하면 유지.
        logs_clear();
        buttons_script("RRRRRRRR");                    // 켤 때부터 계속 누르고 있음(진입 4회 + 선택창에서도 계속)
        hard_reset(false);
        tlog("  RIGHT 켜기: 물음=%d no=%d wash=%d\n", lcd_has("Keep settings"),
             deviceOption.GetNumber(), alarmOption.GetTimeSlot1());
        CHECK(lcd_has("Keep settings"), "2.2.9: 같은 판 + RIGHT 누르고 켜기 → 선택 화면이 뜬다");
        CHECK(deviceOption.GetNumber() == 5 && alarmOption.GetTimeSlot1() == 11,
              "2.2.9: 누른 채 켜도 곧바로 지워지지 않는다(뗄 때까지 무시)");
        // ★손을 떼기 전에는 커서가 움직이지 않아야 한다 — 안 그러면 뗀 뒤 MENU 만 눌러도 지워진다.
        //   ★양성대조를 같이 둔다: 화면을 아예 안 그려도 위 부정 확인만으로는 통과한다(변이 M4).
        CHECK(lcd_has("> Keep settings") && lcd_has("Erase all"), "2.2.9: 두 선택지와 커서가 화면에 그려진다");
        CHECK(!lcd_has("> Erase all"), "2.2.9: 누른 채 켜도 커서가 '초기화' 로 옮겨가지 않는다");
        CHECK(lcd_has("MENU=OK"), "2.2.9: 조작 안내와 남은 초가 화면에 나온다");

        // 뗐다가 다시 눌러야 초기화된다
        logs_clear();
        buttons_script("RRRRrRS");                     // 진입 4회 + 뗌 + 오른쪽 + MENU
        hard_reset(false);
        tlog("  RIGHT 켜고 다시 누름: no=%d wash=%d\n", deviceOption.GetNumber(), alarmOption.GetTimeSlot1());
        CHECK(deviceOption.GetNumber() == 1 && alarmOption.GetTimeSlot1() == 4,
              "2.2.9: 켠 뒤 손을 떼고 > + MENU 하면 완전 초기화");
    }

    // ── [2.2.9] `<` 로 되돌릴 수 있어야 한다 — 실수로 `>` 를 눌렀을 때의 유일한 탈출구 ──
    {
        deviceOption.SetNumber(9);
        alarmOption.SetTimeSlot1(13);
        EEPROM.put((int)4088, (uint32_t)0);
        logs_clear();
        buttons_script("rRLS");                        // 뗌 → 오른쪽(초기화) → 왼쪽으로 되돌림 → MENU
        hard_reset(false);
        tlog("  > 뒤 < 로 되돌림: no=%d wash=%d 커서=%d\n", deviceOption.GetNumber(),
             alarmOption.GetTimeSlot1(), lcd_has("> Keep settings"));
        CHECK(deviceOption.GetNumber() == 9 && alarmOption.GetTimeSlot1() == 13,
              "2.2.9: > 눌렀다가 < 로 되돌리면 유지된다");
    }

    // ── [2.2.9] SELECT(유지)를 누른 채로 있어도 설정 메뉴로 빠지지 않는다 ──
    {
        deviceOption.SetNumber(8);
        EEPROM.put((int)4088, (uint32_t)0);
        logs_clear();
        buttons_script("sSSS");                        // 뗌 → MENU(유지) 확정, 뒤에도 잠시 더 눌려 있음
        hard_reset(false);
        run_loops(4);
        tlog("  SELECT 유지: no=%d 홈=%d\n", deviceOption.GetNumber(), lcd_has("R-O"));
        CHECK(deviceOption.GetNumber() == 8, "2.2.9: SELECT → 설정 유지");
        CHECK(lcd_has("R-O") && !lcd_has("Number"), "2.2.9: SELECT 를 누른 채여도 홈으로 — 설정 메뉴에 갇히지 않는다");
    }

    // ── [회귀] 버튼을 안 누르고 켜면 묻지 않는다(같은 판) ──
    {
        deviceOption.SetNumber(6);
        logs_clear();
        hard_reset(false);
        CHECK(!lcd_has("Keep settings") && deviceOption.GetNumber() == 6,
              "회귀: 같은 판 + 버튼 안 누름 → 묻지 않고 그대로 부팅");
    }

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
