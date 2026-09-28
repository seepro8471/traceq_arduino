// 14회차 II-F(ui) 잠금 — 메뉴 화면 전환 사이의 SELECT 길게 누름 · 3행 배치 · 날짜 범위 사실 확인.
// CHECK 는 "올바른 행위" 를 묻는다 — 빨강 = 결함 재현.
//  A1~A4: 메뉴 화면 전환(handle_menu)에 SELECT 떼기 대기가 없어 0.75초 누름이 다음 화면에서 새 누름으로 먹히던 것(5차 V3 P2-1 의 형제)
//  B1·B2: 4/31 거부 · 3행 20칸 배치 — 사실 잠금.  S: 메뉴 진입음·화면 전환음(HH2 가 미확인으로 남긴 둘).
#include "common.h"

void handle_menu(UserInterface::MenuFunction function);   // main.cpp (전역 연결)

static void reboot_as(char type) { power_restore(); deviceOption.SetType(type); hard_reset(false); }

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');
    g_btnIdleLimit = 1000000UL;

    // ── A1: 메뉴 첫 항목(Number)을 0.75초 누름 → 같은 누름이 편집 화면까지 넘어가나 ──
    //  (V3 P2-1 은 홈→메뉴 진입만 막았다. 항목 선택 → 편집 화면 전환(handle_menu)은 같은 기제의 형제)
    //  사용자 다음 조작 "R S L S" 는 V3 P2-1 과 같은 '더듬는' 순서.
    {
        deviceOption.SetNumber(1);
        buttons_script("RSLS");
        unsigned long t0 = millis();
        buttons_hold('S', t0 + 200UL);                       // 짧게(0.2초)
        int r = GUARDED(handle_menu(ui.DisplayMenu(0)));
        buttons_hold(0, 0);
        const int shortNo = deviceOption.GetNumber();
        tlog("  A1 짧게 0.2s: r=%d 번호=%d 경과=%lums\n", r, shortNo, millis() - t0);
        CHECK(r == 0 && shortNo == 1, "A1 회귀: 짧게 누르면 RSLS 는 저장 없이 끝난다(번호 1 유지)");

        deviceOption.SetNumber(1);
        buttons_script("RSLS");
        t0 = millis();
        buttons_hold('S', t0 + 750UL);                       // 0.75초 누름
        r = GUARDED(handle_menu(ui.DisplayMenu(0)));
        buttons_hold(0, 0);
        const int longNo = deviceOption.GetNumber();
        tlog("  A1 길게 0.75s: r=%d 번호=%d 경과=%lums\n", r, longNo, millis() - t0);
        CHECK(r == 0 && longNo == 1, "A1 [결함] 항목을 0.75초 누르면 편집 모드로 들어가 RSLS 가 번호를 바꿔 저장한다");
    }

    // ── A2: Type 항목을 0.75초 누름 → 선택 화면에서 값이 바뀐 채 R R R S(=그대로 저장) ──
    {
        buttons_script("RRRS");
        unsigned long t0 = millis();
        buttons_hold('S', t0 + 200UL);
        int r = GUARDED(handle_menu(UserInterface::MenuFunction::Type));   // DisplayMenu 가 방금 Type 을 돌려준 순간
        buttons_hold(0, 0);
        char ty = deviceOption.GetType();
        tlog("  A2 짧게 0.2s: r=%d type=%c resets=%u\n", r, ty, g_resetCount);
        CHECK(r == 0 && ty == 'W', "A2 회귀: 짧게 누르고 RRRS 면 타입 그대로(W) · 재시작 없음");

        const uint16_t rc0 = g_resetCount;
        buttons_script("RRRS");
        t0 = millis();
        buttons_hold('S', t0 + 750UL);
        r = GUARDED(handle_menu(UserInterface::MenuFunction::Type));
        buttons_hold(0, 0);
        ty = deviceOption.GetType();
        tlog("  A2 길게 0.75s: r=%d type=%c resets+%u\n", r, ty, (unsigned)(g_resetCount - rc0));
        CHECK(r == 0 && ty == 'W', "A2 [결함] Type 을 0.75초 누르고 RRRS 면 다른 타입으로 저장·재시작된다");
        reboot_as('W');
        g_btnIdleLimit = 1000000UL;
    }

    // ── A3: home 을 0.75초 누름 → 다음 loop 이 메뉴로 다시 들어가나 ──
    {
        buttons_script("");
        unsigned long t0 = millis();
        buttons_hold('S', t0 + 750UL);
        GUARDED(handle_menu(UserInterface::MenuFunction::Home));
        logs_clear();
        const unsigned long t1 = millis();
        const int r = GUARDED(loop());
        buttons_hold(0, 0);
        const unsigned long dt = millis() - t1;
        tlog("  A3 home 길게: r=%d 다음 loop 경과=%lums 메뉴화면=%d\n", r, dt, lcd_has("home") ? 1 : 0);
        CHECK(r == 0 && dt < 5000UL && !lcd_has("home"), "A3 [결함] home 을 0.75초 누르면 다음 loop 이 메뉴로 되돌아간다(60초 대기)");
        run_loops(2);
    }

    // ── A4: loop 부터 끝까지(스크립트 모형) — 홈 S → R R(Alarm) → S 를 0.6초 넘게 → R S L S ──
    //  'SS' = 첫 S 뒤 다음 화면의 첫 읽기(0.6초 뒤)에도 눌려 있음. 'S' 하나 = 그 전에 뗌.
    {
        alarmOption.SetTimeSlot1(4);
        buttons_script("SRRS" "RSLS");
        int r = GUARDED(loop());
        const int a = alarmOption.GetTimeSlot1();
        tlog("  A4 짧게: r=%d 알람=%d\n", r, a);
        CHECK(r == 0 && a == 4, "A4 회귀: 짧게 누르면 RSLS 뒤 저장 없음(알람 4 유지)");
        run_loops(2);

        alarmOption.SetTimeSlot1(4);
        buttons_script("SRRSS" "RSLS");
        r = GUARDED(loop());
        const int b = alarmOption.GetTimeSlot1();
        tlog("  A4 길게: r=%d 알람=%d\n", r, b);
        CHECK(r == 0 && b == 4, "A4 [결함] 항목 S 가 0.6초 넘게 눌리면 같은 RSLS 가 알람 시간을 14 로 저장한다");
        run_loops(2);
    }

    // ── B1: 4/31 은 저장하지 않는다(월말 검사) — 사실 확인 ──
    {
        rtc_set(DateTime(2026, 4, 30, 10, 0, 0));
        logs_clear();
        buttons_script("RRRRR" "SRS" "RRS");   // 자리 5 → 편집 → 0→1 → 편집 끝 → exit → save
        const int r = GUARDED(ui.SetDeviceDate(rtc));
        const DateTime now = rtc_now_sim();
        tlog("  B1 4/31: r=%d rtc=%u-%u-%u Invalid=%d\n", r, now.year(), now.month(), now.day(), lcd_has("Invalid Date") ? 1 : 0);
        CHECK(r == 0 && now.month() == 4 && now.day() == 30 && lcd_has("Invalid Date"), "B1 4/31 은 경고 뒤 폐기");
        rtc_set(rel_date(10, 0, 0));
    }

    // ── B2: 3행 20칸 배치(0~4 번호 · 6~11 버전 · 12~14 R-O · 15~19 기기정보) — 자릿수가 바뀌어도 잔상 없음 ──
    {
        char want[21];
        const int nums[] = {5, 120, 7, 999, 0};
        // B1 은 SetDeviceDate 를 handle_menu 밖에서 불렀다 — 제품은 handle_menu 가 매 바퀴 지운다(main.cpp handle_menu 첫 줄들).
        ui.ClearScreen(); ui.InvalidateHome();
        for (uint8_t i = 0; i < 5; ++i)
        {
            deviceOption.SetNumber(nums[i]);
            run_loops(1);
            const char *row = lcd_row(3);
            if (nums[i] >= 100) snprintf(want, sizeof(want), "      %sR-OW:%d", TRACEQ_VERSION_STRING, nums[i]);
            else                snprintf(want, sizeof(want), "      %sR-O W:%02d", TRACEQ_VERSION_STRING, nums[i]);
            tlog("  B2 번호 %d: [%s] 기대 [%s]\n", nums[i], row, want);
            CHECK(strcmp(row, want) == 0, "B2 3행 배치 20칸 그대로(자릿수 변화 잔상 없음)");
        }
        deviceOption.SetNumber(1);
    }

    // ── S: 메뉴 진입음(홈 S)·화면 전환음(handle_menu) — 각각 50ms 1회 (II-F P3-6 · 변이로 안 잠긴 것을 확인) ──
    {
        buzz_clear();
        buttons_script("SLLS");                              // 진입 → L L(home) → S(home)
        int r = GUARDED(loop());
        const uint8_t n1 = buzz_count(50);
        tlog("  S 진입→home: r=%d buzz50=%u\n", r, n1);
        CHECK(r == 0 && n1 == 2, "S1 메뉴 진입음 1 + home 전환음 1 = 50ms 2회");
        run_loops(2);
        buzz_clear();
        buttons_script("SLS" "LLS");                         // 진입 → > → 다음 화면 → L L → home
        r = GUARDED(loop());
        const uint8_t n2 = buzz_count(50);
        tlog("  S 진입→>→home: r=%d buzz50=%u\n", r, n2);
        CHECK(r == 0 && n2 == 3, "S2 화면 전환(>)마다 50ms 1회 더");
        run_loops(2);
    }

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
