// II-H 제안 잠금 7 — P3 표시·부팅 봉합 셋. HEAD 9379441 초록 · 되돌림 변이 빨강이어야 한다.
//  Q) v2.2.13 알람 시간 제목 버퍼 18 — "Alarm Time (120)" 닫는 괄호. t_a7 은 "Number (300)" 만 본다(형제 반만).
//  R) v2.2.13 부팅 선택창 `lft != rgt` — `<` `>` 가 같은 표본에 읽히면 무시. 어느 시험도 동시 표본을 안 만든다.
//  S) v2.2.13 알람 줄 20자 채움 — 남은 시간이 100:00 → 99:59 로 한 자리 줄 때 20열 잔상. t_a10 ③ 은 길이가 안 주는
//     "04 Min Alarm 00:00" 한 번만 보고, t_a7 은 버퍼 크기(21)만 본다.
#include "common.h"

static SimCard mgr, sc;

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');

    // ── Q) 알람 시간 제목 3자리 ──
    {
        alarmOption.SetTimeSlot1(120);
        logs_clear();
        const int r = GUARDED(ui.SetRecordAlarmTimeSlot('W', alarmOption));   // 무조작 60초 → Exit
        tlog("  Q r=%d 제목=%d\n", r, (int)lcd_has("Alarm Time (120)"));
        CHECK(lcd_has("Alarm Time (120)"), "Q 알람 시간 제목이 3자리(120)에서도 닫는 괄호까지 보인다");
    }

    // ── S) 알람 줄: 남은 시간이 100분 → 99분으로 줄 때 20열에 잔상이 없다 ──
    {
        hard_reset(false, 2);
        make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");
        touch(mgr);
        make_tag(sc, 0x21, SCOPE_TYPE_TAG, 21, "SC0021", "S0021");
        set_process(sc, Process{0, 0, 0, 0, 0, false, 0, 0});
        touch(sc);                                        // 세척 시작 → 알람 120분
        sim_advance_ms((7200UL - 6003UL) * 1000UL);       // 남은 시간 ≈ 100:03
        run_loops(1);
        for (uint8_t i = 0; i < 6; ++i) { sim_advance_ms(1000); run_loops(1); }   // 100:0x → 99:5x
        const char *row = lcd_row(1);
        tlog("  S 1행=[%s] 남은=%ld\n", row, (long)rtc.GetAlarmRemainingSeconds(1));
        CHECK(strncmp(row, "120 Min Alarm 99:5", 18) == 0 && row[19] == ' ',
              "S 남은 시간이 한 자리 줄어도(100:00→99:59) 20열에 잔상이 남지 않는다(20자 채움)");
    }

    // ── R) 부팅 선택창: `<` `>` 가 같은 표본에 읽히면 무시 → MENU = 유지 ──
    {
        deviceOption.SetNumber(9);
        alarmOption.SetTimeSlot1(13);
        EEPROM.put((int)4088, (uint32_t)0);               // 새 판 첫 부팅(쓰던 기기) → 선택창
        logs_clear();
        buttons_script("slrLRS");                         // 무장(셋 다 뗌) → < 와 > 같은 표본 → MENU
        hard_reset(false);
        tlog("  R <>동시 뒤 MENU: no=%d wash=%d\n", deviceOption.GetNumber(), alarmOption.GetTimeSlot1());
        CHECK(lcd_has("Keep settings"), "R 전제: 선택창이 떴다");
        CHECK(deviceOption.GetNumber() == 9 && alarmOption.GetTimeSlot1() == 13,
              "R <·> 가 같은 표본에 읽히면 무시한다 — 초기화 쪽이 골라지지 않는다(설정 유지)");
    }

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
