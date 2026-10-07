// 15회차 III-F 잠금 — 14차 봉합 둘의 **형제 누락**.
//  A: 3행 번호 비우기(12차 print_tag_number · 14차 회사 블록 실패)의 형제 — 번호를 안 읽고 2행 알림을 내는 자리
//     (Invalid Tag Type ×4 기기 · 소독기 Clear · 서버 Not Connected)가 앞 건 스코프 번호(00041)를 3행에 남기던 것.
//  B: SELECT 떼기 대기(14차 handle_menu · main 형제 셋)의 넷째 형제 — Type 저장(→ 소프트 리셋) 뒤 첫 loop 이
//     아직 눌린 저장 누름을 메뉴 진입으로 먹어 61초 동안 태그를 안 읽던 것.
#include "common.h"

void handle_menu(UserInterface::MenuFunction function);   // main.cpp (전역 연결)

static SimCard sc, tg;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', '1'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'K', 'I', 'M'};

static void reboot_as(char type)
{
    power_restore();
    deviceOption.SetType(type);
    hard_reset(false, 2);
    managerOption.SetData(mk, mn);
}
static bool row3_is(const char *five) { return memcmp(lcd_row(3), five, 5) == 0; }

static DateTime yday(uint8_t h, uint8_t m) { return DateTime(rel_date(h, m, 0).unixtime() - 86400UL); }
static void done_cycle(SimCard &c, uint8_t uid, int no)
{
    char id[10], ser[10];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(c, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(c, Process{1, 1, 1, 1, 3, false, 0, 2});
    set_record(c, SECTOR1_GATEWAY, 7, yday(8, 0));
    set_record(c, SECTOR2_WASHING_START, 1, yday(9, 0));
    set_record(c, SECTOR3_WASHING_END, 1, yday(9, 4));
    set_record(c, SECTOR5_DISINFECTION_START, 3, yday(9, 10));
    set_record(c, SECTOR6_DISINFECTION_END, 3, yday(9, 28));
}

// 앞 건(스코프 41)을 처리해 3행에 00041 을 띄운 뒤 다른 태그 tg 를 댄다.
static void probe(const char *label, char type, int otherTagType, const char *expectMsg, bool authed = true)
{
    reboot_as(type);
    if (type == 'S' && authed) { serial_inject("Z", 1); GUARDED(serialEvent()); run_loops(1); }
    if (type == 'S')      done_cycle(sc, 0x41, 41);
    else if (type == 'D') { make_tag(sc, 0x41, SCOPE_TYPE_TAG, 41, "SC0041", "S0041");
                            set_process(sc, Process{1, 0, 1, 0, 1, false, 0, 0});
                            set_record(sc, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
                            set_record(sc, SECTOR3_WASHING_END, 1, rel_date(9, 4, 0)); }
    else                  make_tag(sc, 0x41, SCOPE_TYPE_TAG, 41, "SC0041", "S0041");
    if (type == 'S' && authed) serial_inject("Z", 1);
    touch(sc);
    const bool shown = row3_is("00041");
    if (otherTagType == SCOPE_TYPE_TAG) { make_tag(tg, 0x77, SCOPE_TYPE_TAG, 900, "SC0900", "S0900"); }
    else                                 make_tag(tg, 0x77, otherTagType, 900, "OTHER900", "O900");
    logs_clear();
    touch(tg);
    const bool msg = lcd_has(expectMsg);
    const bool stale = row3_is("00041");
    tlog("  %s: 앞건표시=%u 알림(%s)=%u 뒤3행=[%.20s] 앞건잔존=%u\n", label, (unsigned)shown, expectMsg,
         (unsigned)msg, lcd_row(3), (unsigned)stale);
    char l1[120], l2[120];   // 자리마다 다른 라벨(16차 IV-J: 다섯 라벨이 같아 FAIL 줄로 자리를 몰랐다)
    snprintf(l1, sizeof(l1), "%s 전제: 앞 건 번호가 떴고, 다른 태그가 기대한 알림을 냈다", label);
    snprintf(l2, sizeof(l2), "%s 번호를 안 읽는 알림도 3행의 앞 건 스코프 번호를 남기지 않는다(14차 형제 누락)", label);
    CHECK(shown && msg, l1);
    CHECK(!stale, l2);
}

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');
    managerOption.SetData(mk, mn);

    // ── A: 3행 형제 여섯 ──
    probe("A1 세척기+클리어태그", 'W', CLEAR_TYPE_TAG, "Invalid Tag Type");
    probe("A2 게이트웨이+담당자태그", 'G', MANAGER_TYPE_TAG, "Invalid Tag Type");
    probe("A3 서버(인증)+담당자태그", 'S', MANAGER_TYPE_TAG, "Invalid Tag Type");
    probe("A4 소독기+종류0 태그", 'D', 0, "Invalid Tag Type");
    probe("A5 소독기+클리어태그", 'D', CLEAR_TYPE_TAG, "Clear");
    // A6: 서버 미인증 — 앞 건은 인증 상태에서 처리하고, 인증을 잃은 뒤(재부팅) 스코프를 대면 Not Connected
    {
        reboot_as('S');
        serial_inject("Z", 1); GUARDED(serialEvent()); run_loops(1);
        done_cycle(sc, 0x41, 41);
        serial_inject("Z", 1);
        touch(sc);
        const bool shown = row3_is("00041");
        hard_reset(false, 2);                                  // 인증 잃음 · LCD 는 시험 로그 기준으로 다시 본다
        managerOption.SetData(mk, mn);
        done_cycle(sc, 0x41, 41);
        touch(sc);                                             // 미인증: Not Connected — 번호는 안 읽는다
        const bool first = row3_is("00041");
        make_tag(tg, 0x78, SCOPE_TYPE_TAG, 900, "SC0900", "S0900");
        logs_clear();
        touch(tg);
        tlog("  A6 서버 미인증: 앞건표시(인증때)=%u 미인증 첫접촉 3행=[%.20s] 뒤3행=[%.20s] NotConnected=%u\n",
             (unsigned)shown, first ? "00041" : lcd_row(3), lcd_row(3), (unsigned)lcd_has("Not Connected"));
        CHECK(shown && lcd_has("Not Connected"), "A6 전제: 인증 때 번호가 떴고, 미인증 접촉은 Not Connected");
        // 사실 기록만 — 미인증은 재부팅 직후뿐이고 재부팅이 화면을 지우므로 "앞 건 번호가 남는 상태" 는 도달 불가(16차 IV-J: 늘 참인 CHECK 였다).
        //  제품의 소거 줄은 형제 일관성으로 둔다.
        tlog("  A6 사실: 미인증 Not Connected 뒤 3행=[%.20s] (앞건잔존=%u · 도달 불가 상태)\n", lcd_row(3), (unsigned)row3_is("00041"));
    }

    // ── B2: Type 저장(재시작) 뒤 아직 눌린 SELECT — 스크립트 모형(마지막 'S' 뒤의 'S' = "다음 읽기에도 아직 눌림") ──
    //  실기에서 다음 읽기는 재시작 뒤 첫 loop = LCD init 의 delay(1050) 뒤라 약 1.2초 누름에 해당.
    {
        reboot_as('W');
        g_btnIdleLimit = 1000000UL;
        buttons_script("RSRRS" "S");
        const uint16_t rc0 = g_resetCount;
        int r = GUARDED(handle_menu(UserInterface::MenuFunction::Type));
        const bool reset = (g_resetCount != rc0);
        power_restore();
        hard_reset(false, 0);
        logs_clear();
        const unsigned long t1 = millis();
        const int r2 = GUARDED(loop());
        const unsigned long dt = millis() - t1;
        const bool menu = lcd_has("home") || lcd_has("Number");
        tlog("  B2 스크립트 RSRRS+S: r=%d reset=%u type=%c · 첫 loop r=%d 경과=%lums 메뉴화면=%u\n",
             r, (unsigned)reset, deviceOption.GetType(), r2, dt, (unsigned)menu);
        CHECK(r == 1 && reset && deviceOption.GetType() == 'D', "B2 전제: Type 을 D 로 저장하면 소프트 리셋");
        CHECK(!menu && dt < 5000UL, "B2 저장 누름이 재시작 뒤까지 이어져도 메뉴로 다시 들어가지 않는다(리셋 전 떼기 대기 · 넷째 형제)");
        reboot_as('W');
    }

    // ── A7·A8: 발급 알림 자리(서버 레거시 S · 세척기 JSON cfg_new_tag)도 3행의 앞 건 번호를 지운다(16차 IV-A: 형제 5자리 누락) ──
    {
        reboot_as('S');
        serial_inject("Z", 1); GUARDED(serialEvent()); run_loops(1);
        done_cycle(sc, 0x41, 41);
        serial_inject("Z", 1);
        touch(sc);
        const bool shown7 = row3_is("00041");
        make_tag(tg, 0x79, SCOPE_TYPE_TAG, 0, "OLD", "OLDSER");
        static const char scmd[] = "S12;ABCDEFGH;";
        logs_clear();
        serial_inject(scmd, sizeof(scmd) - 1);
        card_place(&tg);
        GUARDED(serialEvent());
        card_remove(); run_loops(4);
        const bool issued7 = lcd_has("new tag") || lcd_has("timeout or error");
        tlog("  A7 서버 S 발급: 앞건표시=%u 발급알림=%u 뒤3행=[%.20s]\n", (unsigned)shown7, (unsigned)issued7, lcd_row(3));
        CHECK(shown7 && issued7, "A7 전제: 앞 건 번호가 떴고 서버 발급 알림이 났다");
        CHECK(!row3_is("00041"), "A7 서버 레거시 발급 알림도 3행의 앞 건 번호를 남기지 않는다");
        // A7b·A7c (17차 V-H U6·U7) 발급 **실패** 두 갈래(파싱 실패 · 카드 없이 시한 초과)도 3행을 지운다 — 위는 성공 갈래만 돌았다
        done_cycle(sc, 0x41, 41); serial_inject("Z", 1); touch(sc);
        const bool shown7b = row3_is("00041");
        static const char bad[] = "S;";
        logs_clear(); serial_inject(bad, sizeof(bad) - 1); GUARDED(serialEvent()); run_loops(4);
        const bool err7b = lcd_has("timeout or error");
        tlog("  A7b 파싱 실패: 앞건표시=%u 알림=%u 뒤3행=[%.20s]\n", (unsigned)shown7b, (unsigned)err7b, lcd_row(3));
        CHECK(shown7b && err7b && !row3_is("00041"), "A7b 레거시 발급 파싱 실패 알림도 3행의 앞 건 번호를 남기지 않는다");
        done_cycle(sc, 0x41, 41); serial_inject("Z", 1); touch(sc);
        const bool shown7c = row3_is("00041");
        static const char nocard[] = "S556;SER556;";
        logs_clear(); serial_inject(nocard, sizeof(nocard) - 1); GUARDED(serialEvent()); run_loops(4);   // 카드를 안 댄다 → 시한 초과
        const bool err7c = lcd_has("timeout or error");
        tlog("  A7c 시한 초과: 앞건표시=%u 알림=%u 뒤3행=[%.20s]\n", (unsigned)shown7c, (unsigned)err7c, lcd_row(3));
        CHECK(shown7c && err7c && !row3_is("00041"), "A7c 레거시 발급 시한 초과 알림도 3행의 앞 건 번호를 남기지 않는다");

        reboot_as('W');
        make_tag(sc, 0x41, SCOPE_TYPE_TAG, 41, "SC0041", "S0041");
        touch(sc);
        const bool shown8 = row3_is("00041");
        make_tag(tg, 0x7A, SCOPE_TYPE_TAG, 0, "OLD", "OLDSER");
        static const char jcmd[] = "{\"cmd\":\"cfg_new_tag\",\"type_id\":0}";
        logs_clear();
        serial_inject(jcmd, sizeof(jcmd) - 1);
        card_place(&tg);
        GUARDED(serialEvent());
        card_remove(); run_loops(4);
        const bool issued8 = lcd_has("tag created") || lcd_has("timeout or error");
        tlog("  A8 세척기 JSON 발급: 앞건표시=%u 발급알림=%u 뒤3행=[%.20s]\n", (unsigned)shown8, (unsigned)issued8, lcd_row(3));
        CHECK(shown8 && issued8, "A8 전제: 앞 건 번호가 떴고 JSON 발급 알림이 났다");
        CHECK(!row3_is("00041"), "A8 JSON 발급 알림(성공·실패)도 3행의 앞 건 번호를 남기지 않는다");
    }

    // ── B3: 저장 누름이 1.2초 이어져도(실기 LCD init 뒤 첫 loop) 메뉴 재진입 없음 · B4: SELECT 가 붙은 채여도 2초 상한 뒤 리셋된다 ──
    {
        reboot_as('W');
        g_btnIdleLimit = 1000000UL;
        buttons_script("RSRRS");
        const uint16_t rc0 = g_resetCount;
        // 스크립트 모형(B2 와 같은 방식): 저장 'S' 뒤에 'S' 60개 = 떼기 대기(20ms 읽기)로 1.2초 동안 아직 눌림. 종전의 `buttons_hold` 1.2초는
        //  저장 누름(시작 뒤 약 1.8초)보다 먼저 끝나 떼기 대기를 지워도 초록인 헛것이었다(17차 V-J R-3). 대기를 지우면 리셋 뒤 첫 loop 가 'S' 를 읽어 메뉴.
        buttons_script("RSRRS" "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS");
        GUARDED(handle_menu(UserInterface::MenuFunction::Type));
        const bool reset3 = (g_resetCount != rc0);
        power_restore();
        hard_reset(false, 0);
        logs_clear();
        const int r2 = GUARDED(loop());
        const bool menu3 = lcd_has("home") || lcd_has("Number");
        tlog("  B3 스크립트 S×60(1.2초): reset=%u type=%c 첫 loop r=%d 메뉴화면=%u\n", (unsigned)reset3, deviceOption.GetType(), r2, (unsigned)menu3);
        CHECK(reset3 && deviceOption.GetType() == 'D' && !menu3,
              "B3(16차·17차 표본 3초) 저장 누름이 리셋 시점을 넘겨 이어져도 떼기 대기(최대 2초)가 기다려 재시작 뒤 메뉴로 다시 들어가지 않는다");

        // B4: 저장 누름 뒤 SELECT 가 **붙은 채**(스크립트로 'S' 를 500회 더 읽힘 = 10초) — 떼기 대기는 100회×20ms 상한이라 2초 뒤 리셋.
        //  기준(dtBase) 은 같은 조작을 붙지 않은 채로 잰 시간. 상한이 없으면 +10초 · 대기 자체가 없으면 +0 → 둘 다 빨강.
        reboot_as('W');
        g_btnIdleLimit = 1000000UL;
        buttons_script("RSRRS");
        const unsigned long tb = millis();
        GUARDED(handle_menu(UserInterface::MenuFunction::Type));
        const unsigned long dtBase = millis() - tb;
        power_restore();
        reboot_as('W');
        g_btnIdleLimit = 1000000UL;
        static char stuck[520];
        memcpy(stuck, "RSRRS", 5); memset(stuck + 5, 'S', 500); stuck[505] = 0;
        buttons_script(stuck);
        const uint16_t rc1 = g_resetCount;
        const unsigned long t0 = millis();
        GUARDED(handle_menu(UserInterface::MenuFunction::Type));
        const unsigned long dt = millis() - t0;
        buttons_script("");
        power_restore();
        tlog("  B4 SELECT 붙은 채: reset=%u 기준=%lums 붙은채=%lums 차=%ldms\n", (unsigned)(g_resetCount != rc1), dtBase, dt, (long)(dt - dtBase));
        CHECK(g_resetCount != rc1 && (long)(dt - dtBase) >= 1500L && (long)(dt - dtBase) < 3500L,
              "B4(16차) SELECT 가 붙은 채여도 떼기 대기는 2초 상한이라 리셋이 2초 안팎 뒤에 난다(상한 없으면 +10초 · 대기 없으면 +0)");
        reboot_as('W');
    }

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
