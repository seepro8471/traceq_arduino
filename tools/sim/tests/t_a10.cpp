// 현장 확인 목록 중 시뮬로 판정할 수 있는 것을 닫는다(Z2 각도B 이관) — 「남은 것 — 현장만」(docs/AUDIT_2026-09-27.md) 중 시뮬로 판정할 수 있는 것을 닫는다.
//   ③ 첫 부팅 진행 표시 뒤 LCD 잔상   (표시 격자 20x4 를 가짜에 더해 실제 화면으로 판정)
//   ④ 출입카드+스코프 동시 인식        (X2 두 장 카드 모델)
//   ⑤ 반만 바뀐 태그 재발급
//   ⑦ 홈 SELECT 길게 누름 뒤 대기 — 2초 상한을 넘겨 눌린 채
#include "common.h"

static SimCard scope, access;

static void pump(uint32_t ms)
{
    const uint32_t end = g_ms + ms;
    while (g_ms < end)
    {
        GUARDED(loop());
        if (Serial.available() > 0) GUARDED(serialEvent());
        sim_advance_ms(20);
    }
}
static bool row_has(uint8_t r, const char *s) { return strstr(lcd_row(r), s) != nullptr; }
static void tlog_screen(const char *label)
{
    tlog("  %s\n", label);
    for (uint8_t r = 0; r < 4; ++r) tlog("    [%u]\"%s\"\n", (unsigned)r, lcd_row(r));
}

int main()
{
    rtc_set(DateTime(2026, 9, 27, 9, 0, 0));

    // ══ ③ 첫 부팅 진행 표시 뒤 LCD 잔상 ══
    // 공장 초기(0xFF) → 묻지 않고 전체 소거 → "Initializing..." + 진행률. 그 뒤 홈 화면에 잔상이 남나.
    {
        eeprom_factory();
        logs_clear();
        hard_reset(false);
        CHECK(lcd_has("Initializing") && lcd_has("100%"), "③-0 전체 소거 진행 표시가 실제로 나왔다(양성대조)");
        tlog_screen("③ 공장초기 소거 뒤 홈 화면");
        // ★부분 문자열로 "없다" 만 보면 못 잡는다(변이 m1 이 통과했다) — 80칸을 **전부** 셈한다.
        CHECK(strcmp(lcd_row(0), "2026/09/27 09:00:00 ") == 0, "③ 0행 20칸이 홈 시계뿐(19자+공백) — 잔상 칸 없음");
        CHECK(strcmp(lcd_row(1), "04 Min Alarm 00:00  ") == 0, "③ 1행 20칸이 알람 줄뿐 — 진행률 잔상 없음");
        CHECK(strcmp(lcd_row(2), "                    ") == 0, "③ 2행 20칸이 전부 공백");
        CHECK(strncmp(lcd_row(3), "      ", 6) == 0 && row_has(3, "R-") && !row_has(3, "%"),
              "③ 3행 앞 6칸(태그번호 자리) 공백 + 리더 표시 + 진행률 잔상 없음");
    }

    // 쓰던 기기 + 전원 켤 때 RIGHT → "Keep settings?" 선택창 → 초기화 선택 → 그 화면의 잔상
    {
        boot('W');
        deviceOption.SetNumber(5);
        logs_clear();
        buttons_script("RRRRrRS");                 // 진입 4회 + 뗌 + 오른쪽(초기화) + MENU
        hard_reset(false);
        CHECK(lcd_has("Keep settings"), "③-1 선택창이 실제로 떴다(양성대조)");
        tlog_screen("③ 선택창 → 초기화 뒤 홈 화면");
        CHECK(strcmp(lcd_row(0), "2026/09/27 09:00:00 ") == 0 &&
              strcmp(lcd_row(1), "04 Min Alarm 00:00  ") == 0 &&
              strcmp(lcd_row(2), "                    ") == 0 &&
              strncmp(lcd_row(3), "      ", 6) == 0,
              "③ 선택창(Keep settings/Erase all/MENU=OK)·진행률 잔상이 80칸 어디에도 없음");
    }

    // ══ ④ 출입카드(타사 MIFARE 사원증) + 스코프를 함께 올림 ══
    // (교통·출입카드가 MIFARE 가 아니면 Poll 이 Invalid 로 걸러낸다 — 5차 B. 여기서는 SAK 가 같은 MIFARE.)
    {
        boot('W');
        managerOption.SetData((const unsigned char *)"MGR", (const unsigned char *)"KIM");
        recordOption.SetPatientCheck(false);

        // (i) 출입카드가 anticollision 에서 이기는 순서 — 스코프가 굶는가
        make_tag(scope, 0x81, SCOPE_TYPE_TAG, 81, "SC", "SER");
        set_process(scope, Process{0, 0, 0, 0, 0, false, 0, 0});
        card_init_foreign(access, 0x82);
        logs_clear();
        card_place(&access);                       // A = 출입카드(모델은 A 를 고른다)
        card_place2(&scope);
        run_loops(8);
        const uint8_t fail1 = buzz_count(100);
        tlog("  ④(i) 출입카드 우선: 실패음(100ms 펄스)=%u 스코프 WS=%u\n",
             (unsigned)fail1, (unsigned)get_process(scope).WashingStatus);
        CHECK(fail1 >= 4, "④(i) 출입카드는 읽기 실패로 알린다(짧게 4회)");
        CHECK(get_process(scope).WashingStatus == 1,
              "★④ 출입카드가 먼저 뽑혀도 스코프는 정상 처리된다(선택된 카드는 REQA 에 안 답한다)");
        card_remove();
        card_remove2();
        run_loops(4);

        // (ii) 스코프가 이기는 순서 — 정상 처리되고 출입카드는 방해하지 않는다
        make_tag(scope, 0x83, SCOPE_TYPE_TAG, 83, "SC", "SER");
        set_process(scope, Process{0, 0, 0, 0, 0, false, 0, 0});
        card_init_foreign(access, 0x84);
        logs_clear();
        card_place(&scope);                        // A = 스코프
        card_place2(&access);
        run_loops(8);
        tlog("  ④(ii) 스코프 우선: WS=%u 실패음=%u\n",
             (unsigned)get_process(scope).WashingStatus, (unsigned)buzz_count(100));
        CHECK(get_process(scope).WashingStatus == 1, "④(ii) 스코프가 이기면 출입카드가 있어도 정상 세척 시작");
        card_remove();
        card_remove2();
        run_loops(4);

        // (iii) 스코프를 치우고 출입카드만 얹어 두면 실패음이 **반복**되나 (알림 반복 여부 실측)
        card_init_foreign(access, 0x85);
        logs_clear();
        card_place(&access);
        run_loops(10);
        const uint8_t rep10 = buzz_count(100);
        run_loops(20);
        const uint8_t rep30 = buzz_count(100);
        run_loops(30);
        const uint8_t rep = buzz_count(100);
        tlog("  ④(iii) 출입카드만: 실패음 펄스 10루프=%u 30루프=%u 60루프=%u (경고 1회=4펄스)\n",
             (unsigned)rep10, (unsigned)rep30, (unsigned)rep);
        CHECK(rep10 >= 4, "④(iii) 출입카드만 있으면 실패음이 난다(양성대조)");
        CHECK(rep == 4, "★④(iii) 얹어 둔 출입카드의 실패음은 **1회로 끝난다**(60루프 동안 되풀이 없음)");
        card_remove();
        run_loops(4);
    }

    // ══ ⑤ 반만 바뀐 태그 재발급 ══
    // 발급 중 한 블록만 NACK(카드는 산다) → "timeout or error" · 태그는 새 번호 + 옛 공정.
    // 그 태그를 다시 발급하면 완전히 복구되는가.
    {
        boot('S');
        serial_inject("Z", 1);
        pump(600);
        CHECK(serialProcessor.IsAuthenticated(), "⑤-0 서버 인증(양성대조)");

        make_tag(scope, 0x86, SCOPE_TYPE_TAG, 86, "OLDID", "OLDSER");
        set_process(scope, Process{1, 2, 1, 0, 0, false, 0, 0});          // Status1 · 소독2회 · 세척완료
        put_block(scope, SECTOR2_PATIENT_KEY,  "OLDPT", 5);
        put_block(scope, SECTOR2_PATIENT_NAME, "OLDNAME", 7);
        set_record(scope, SECTOR1_GATEWAY, 7, DateTime(2026, 9, 1, 8, 0, 0));
        put_block(scope, SECTOR15_EXAMINATION_SUBJECT, "OLDSUBJ", 7);

        scope.nackBlock = SECTOR1_PROCESS;        // 공정 소거만 실패 — 카드는 살아 있다
        logs_clear();
        card_place(&scope);
        serial_inject("S777;SER777;", 12);
        pump(6000);
        card_remove();
        run_loops(4);
        const Process bad = get_process(scope);
        tlog("  ⑤ 1차 발급(공정 소거 NACK): 번호=%d Status=%u WS=%u DC=%u 환자=[%.8s]\n",
             *(int *)scope.data[SECTOR0_TAG], (unsigned)bad.Status, (unsigned)bad.WashingStatus,
             (unsigned)bad.DisinfectionCount, (const char *)scope.data[SECTOR2_PATIENT_KEY]);
        CHECK(lcd_has("timeout or error"), "⑤ 1차 발급 실패를 알린다(new tag 라고 하지 않는다)");
        CHECK(*(int *)scope.data[SECTOR0_TAG] == 777, "⑤ 1차 실패 태그는 **새 번호**가 이미 써져 있다(반만 바뀜)");
        CHECK(bad.WashingStatus == 1 && bad.DisinfectionCount == 2,
              "⑤ 1차 실패 태그에 옛 공정(세척완료·소독2회)이 남는다(반만 바뀜)");
        CHECK(memcmp(scope.data[SECTOR2_PATIENT_KEY], "OLDPT", 5) == 0, "⑤ 1차 실패 태그에 옛 환자도 남는다");

        scope.nackBlock = -1;                     // 고장 제거 후 재발급
        logs_clear();
        card_place(&scope);
        serial_inject("S777;SER777;", 12);
        pump(6000);
        card_remove();
        run_loops(4);
        const Process good = get_process(scope);
        tlog("  ⑤ 재발급: Status=%u WS=%u DC=%u 환자=[%.8s] 검사일시연=%u 항목=[%.8s]\n",
             (unsigned)good.Status, (unsigned)good.WashingStatus, (unsigned)good.DisinfectionCount,
             (const char *)scope.data[SECTOR2_PATIENT_KEY], get_ldt(scope, SECTOR1_GATEWAY).Date.Year,
             (const char *)scope.data[SECTOR15_EXAMINATION_SUBJECT]);
        CHECK(lcd_has("new tag : scope"), "⑤ 재발급은 성공으로 알린다");
        CHECK(good.WashingStatus == 0 && good.DisinfectionCount == 0 && good.Status == 0,
              "★⑤ 재발급이 옛 공정을 완전히 지운다(멱등)");
        CHECK(scope.data[SECTOR2_PATIENT_KEY][0] == 0 && scope.data[SECTOR2_PATIENT_NAME][0] == 0,
              "★⑤ 재발급이 옛 환자를 완전히 지운다");
        CHECK(get_ldt(scope, SECTOR1_GATEWAY).Date.Year == 0 && scope.data[SECTOR15_EXAMINATION_SUBJECT][0] == 0,
              "★⑤ 재발급이 옛 검사일시·검사항목도 지운다");
    }

    // ══ ⑦ 홈 SELECT — 2초를 넘겨 계속 눌린 채(붙은 채 고장) ══
    // [t_a8 V3 P2-1 이 1.3초 길게 누름을 이미 잠갔다] 여기서는 **2초 상한을 넘긴 채** 진입할 때
    // 기기가 멈추지 않고 메뉴 시한으로 홈에 돌아오는지, 올려 둔 태그가 어떻게 되는지만 본다.
    {
        boot('W');
        deviceOption.SetNumber(3);
        g_btnIdleLimit = 3000000UL;
        make_tag(scope, 0x87, SCOPE_TYPE_TAG, 87, "SC", "SER");
        set_process(scope, Process{0, 0, 0, 0, 0, false, 0, 0});
        card_place(&scope);                        // 태그를 올려 둔 채
        const unsigned long t0 = millis();
        buttons_hold('S', t0 + 5000UL);            // 5초 동안 눌린 채 — 0.5초 + 2초 상한을 넘긴다
        const int r = GUARDED(loop());             // 메뉴 진입 → 무조작 → 시한 → 홈 → 같은 loop 의 태그 폴링
        const unsigned long spent = millis() - t0;
        buttons_hold(0, 0);
        tlog("  ⑦ 5초 누름: r=%d 경과=%lums 번호=%d WS=%u\n", r, spent, deviceOption.GetNumber(),
             (unsigned)get_process(scope).WashingStatus);
        CHECK(r == 0, "⑦ 2초 상한을 넘겨 눌린 채여도 멈추지 않는다(soft reset·무한대기 없음)");
        CHECK(deviceOption.GetNumber() == 3, "⑦ 편집으로 들어가 값이 바뀌지 않는다");
        CHECK(spent > 50000UL && spent < 70000UL, "⑦ 메뉴 시한(60초)으로 홈에 돌아온다");
        CHECK(get_process(scope).WashingStatus == 1,
              "★⑦ 메뉴에서 나온 뒤 **같은 loop** 이 태그를 처리한다 — 올려 둔 스코프가 유실되지 않는다(시한만큼 늦어진다)");
        card_remove();
        g_btnIdleLimit = 30000UL;
        run_loops(4);
    }

    done();
    for (;;) {}
}
